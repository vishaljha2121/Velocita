"""Durable local store for raw evidence and derived shadow results.

SQLite in WAL mode is the right tool here for the same reason the C++ side uses
a bounded WAL: durability has to be an explicit, observable property, not an
optimistic one. WAL also survives the failure this Pi will actually experience,
which is power loss mid-write rather than a corrupted filesystem.

Two habits are inherited from the cluster design and are not negotiable here:

  * raw evidence is committed before anything derived from it;
  * nothing is ever backfilled. A gap is recorded as a gap.
"""

from __future__ import annotations

import sqlite3
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator, Sequence

_SCHEMA_PATH = Path(__file__).with_name("schema.sql")


def now_ns() -> int:
    """Wall-clock UTC nanoseconds.

    Deliberately `time.time_ns()` and not a monotonic clock: these values are
    written into `DecisionFrame.receive_time_ns`, which must be comparable
    across processes and machines. Monotonic clocks are used for measuring
    durations within a process; see `collectors/system_health.py` for the
    clock-synchronisation check that makes this value trustworthy.
    """
    return time.time_ns()


@dataclass(frozen=True, slots=True)
class SessionHandle:
    session_id: int
    venue: str
    started_at_ns: int


class ResearchStore:
    """A thin, explicit wrapper. No ORM, no lazy magic, no hidden writes."""

    def __init__(self, path: Path | str, *, read_only: bool = False) -> None:
        self._path = Path(path)
        self._read_only = read_only
        if not read_only:
            self._path.parent.mkdir(parents=True, exist_ok=True)
        uri = f"file:{self._path}?mode=ro" if read_only else f"file:{self._path}"
        self._conn = sqlite3.connect(uri, uri=True, isolation_level=None, timeout=30.0)
        self._conn.row_factory = sqlite3.Row
        if not read_only:
            self._conn.executescript(_SCHEMA_PATH.read_text(encoding="utf-8"))
        else:
            self._conn.execute("PRAGMA query_only = ON")

    @property
    def path(self) -> Path:
        return self._path

    @property
    def connection(self) -> sqlite3.Connection:
        return self._conn

    def close(self) -> None:
        self._conn.close()

    def __enter__(self) -> "ResearchStore":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    @contextmanager
    def transaction(self) -> Iterator[sqlite3.Connection]:
        """Explicit transaction. Used wherever raw evidence and its immediate
        index entry must both exist or neither."""
        self._conn.execute("BEGIN IMMEDIATE")
        try:
            yield self._conn
        except BaseException:
            self._conn.execute("ROLLBACK")
            raise
        else:
            self._conn.execute("COMMIT")

    # -- session ----------------------------------------------------------

    def open_session(
        self,
        *,
        venue: str,
        config_digest: str,
        schema_digest: str,
        code_version: str,
        clock_synced: bool,
    ) -> SessionHandle:
        started = now_ns()
        cursor = self._conn.execute(
            "INSERT INTO collector_session "
            "(venue, started_at_ns, config_digest, schema_digest, code_version, clock_synced) "
            "VALUES (?,?,?,?,?,?)",
            (venue, started, config_digest, schema_digest, code_version, int(clock_synced)),
        )
        return SessionHandle(session_id=int(cursor.lastrowid), venue=venue, started_at_ns=started)

    def close_session(self, session: SessionHandle, reason: str) -> None:
        self._conn.execute(
            "UPDATE collector_session SET ended_at_ns = ?, stop_reason = ? WHERE session_id = ?",
            (now_ns(), reason, session.session_id),
        )

    # -- raw evidence -----------------------------------------------------

    def record_metadata(
        self,
        pair_id: str,
        field: str,
        value: Any,
        *,
        confidence: str,
        source_url: str | None,
    ) -> None:
        if confidence not in ("verified", "inferred", "unverified"):
            raise ValueError(f"confidence must be verified|inferred|unverified, got {confidence!r}")
        self._conn.execute(
            "INSERT OR REPLACE INTO pair_metadata "
            "(pair_id, field, value, confidence, source_url, collected_at_ns) VALUES (?,?,?,?,?,?)",
            (pair_id, field, None if value is None else str(value), confidence, source_url, now_ns()),
        )

    def record_book_snapshot(self, session: SessionHandle, **row: Any) -> int:
        cursor = self._conn.execute(
            "INSERT INTO book_snapshot (session_id, pair_id, source_sequence, venue_sequence, "
            "event_time_ns, receive_time_ns, best_bid_atoms, best_ask_atoms, bid_size_atoms, "
            "ask_size_atoms, depth_json, status, raw_digest) "
            "VALUES (:session_id,:pair_id,:source_sequence,:venue_sequence,:event_time_ns,"
            ":receive_time_ns,:best_bid_atoms,:best_ask_atoms,:bid_size_atoms,:ask_size_atoms,"
            ":depth_json,:status,:raw_digest)",
            {"session_id": session.session_id, **row},
        )
        return int(cursor.lastrowid)

    def record_trade_print(self, session: SessionHandle, **row: Any) -> int | None:
        """Returns None when the print was a duplicate. Duplicate suppression is
        by venue trade id; a venue that supplies none forfeits deduplication and
        the collector records that limitation in metadata."""
        try:
            cursor = self._conn.execute(
                "INSERT INTO trade_print (session_id, pair_id, venue_trade_id, event_time_ns, "
                "receive_time_ns, price_atoms, size_atoms, direction, direction_inferred, raw_digest) "
                "VALUES (:session_id,:pair_id,:venue_trade_id,:event_time_ns,:receive_time_ns,"
                ":price_atoms,:size_atoms,:direction,:direction_inferred,:raw_digest)",
                {"session_id": session.session_id, **row},
            )
        except sqlite3.IntegrityError:
            return None
        return int(cursor.lastrowid)

    def record_reference_price(self, session: SessionHandle, **row: Any) -> int:
        cursor = self._conn.execute(
            "INSERT INTO reference_price (session_id, pair_id, source, event_time_ns, "
            "receive_time_ns, price_atoms, raw_digest) "
            "VALUES (:session_id,:pair_id,:source,:event_time_ns,:receive_time_ns,"
            ":price_atoms,:raw_digest)",
            {"session_id": session.session_id, **row},
        )
        return int(cursor.lastrowid)

    def open_quality_interval(
        self, session: SessionHandle, kind: str, *, pair_id: str | None = None, detail: str = ""
    ) -> int:
        cursor = self._conn.execute(
            "INSERT INTO data_quality_interval (session_id, pair_id, kind, started_at_ns, detail) "
            "VALUES (?,?,?,?,?)",
            (session.session_id, pair_id, kind, now_ns(), detail),
        )
        return int(cursor.lastrowid)

    def close_quality_interval(self, interval_id: int) -> None:
        self._conn.execute(
            "UPDATE data_quality_interval SET ended_at_ns = ? WHERE id = ? AND ended_at_ns IS NULL",
            (now_ns(), interval_id),
        )

    # -- canonical frames -------------------------------------------------

    def record_decision_frame(
        self,
        session: SessionHandle,
        *,
        pair_id: str,
        source_sequence: int,
        event_time_ns: int,
        receive_time_ns: int,
        payload: bytes,
        payload_digest: str,
    ) -> int | None:
        """Store the exact canonical bytes. Returns None for an exact duplicate,
        which is a no-op by digest rather than a second row - the same rule the
        reducer applies to a redelivered command."""
        try:
            cursor = self._conn.execute(
                "INSERT INTO decision_frame (session_id, pair_id, source_sequence, event_time_ns, "
                "receive_time_ns, payload, payload_digest) VALUES (?,?,?,?,?,?,?)",
                (
                    session.session_id,
                    pair_id,
                    source_sequence,
                    event_time_ns,
                    receive_time_ns,
                    payload,
                    payload_digest,
                ),
            )
        except sqlite3.IntegrityError:
            return None
        return int(cursor.lastrowid)

    def iter_decision_frames(self, pair_id: str, *, since_ns: int = 0) -> Iterator[sqlite3.Row]:
        yield from self._conn.execute(
            "SELECT * FROM decision_frame WHERE pair_id = ? AND event_time_ns >= ? "
            "ORDER BY event_time_ns, id",
            (pair_id, since_ns),
        )

    # -- shadow results ---------------------------------------------------

    def record_shadow_decision(self, **row: Any) -> int:
        cursor = self._conn.execute(
            "INSERT INTO shadow_decision (frame_id, policy_digest, evaluation_code, directive, "
            "effective_directive, control_outcome, matched_rule_index, rules_visited) "
            "VALUES (:frame_id,:policy_digest,:evaluation_code,:directive,:effective_directive,"
            ":control_outcome,:matched_rule_index,:rules_visited)",
            row,
        )
        return int(cursor.lastrowid)

    def record_shadow_quote(self, **row: Any) -> int:
        cursor = self._conn.execute(
            "INSERT INTO shadow_quote (decision_id, pair_id, side, price_atoms, size_atoms, "
            "fair_value_atoms, placed_at_ns, queue_ahead_atoms) "
            "VALUES (:decision_id,:pair_id,:side,:price_atoms,:size_atoms,:fair_value_atoms,"
            ":placed_at_ns,:queue_ahead_atoms)",
            row,
        )
        return int(cursor.lastrowid)

    def record_shadow_fill(self, **row: Any) -> int:
        cursor = self._conn.execute(
            "INSERT INTO shadow_fill (quote_id, assumption, filled_at_ns, filled_size_atoms, "
            "fill_probability_ppm, partial) "
            "VALUES (:quote_id,:assumption,:filled_at_ns,:filled_size_atoms,"
            ":fill_probability_ppm,:partial)",
            row,
        )
        return int(cursor.lastrowid)

    def record_markout(self, fill_id: int, horizon_ms: int, fair_value_atoms: int, markout_bps_micros: int) -> None:
        self._conn.execute(
            "INSERT OR REPLACE INTO shadow_markout "
            "(fill_id, horizon_ms, fair_value_atoms, markout_bps_micros) VALUES (?,?,?,?)",
            (fill_id, horizon_ms, fair_value_atoms, markout_bps_micros),
        )

    def record_risk_event(self, session: SessionHandle, kind: str, *, pair_id: str | None, detail: str) -> None:
        self._conn.execute(
            "INSERT INTO risk_event (session_id, pair_id, at_ns, kind, detail) VALUES (?,?,?,?,?)",
            (session.session_id, pair_id, now_ns(), kind, detail),
        )

    def record_system_health(self, session: SessionHandle, **row: Any) -> None:
        self._conn.execute(
            "INSERT INTO system_health (session_id, at_ns, cpu_percent, mem_percent, disk_percent, "
            "db_bytes, load1, clock_offset_ms, clock_synced, throttled_hex, temp_celsius) "
            "VALUES (:session_id,:at_ns,:cpu_percent,:mem_percent,:disk_percent,:db_bytes,:load1,"
            ":clock_offset_ms,:clock_synced,:throttled_hex,:temp_celsius)",
            {"session_id": session.session_id, "at_ns": now_ns(), **row},
        )

    # -- queries ----------------------------------------------------------

    def query(self, sql: str, params: Sequence[Any] = ()) -> list[sqlite3.Row]:
        return list(self._conn.execute(sql, params))

    def database_bytes(self) -> int:
        page_count = self._conn.execute("PRAGMA page_count").fetchone()[0]
        page_size = self._conn.execute("PRAGMA page_size").fetchone()[0]
        return int(page_count) * int(page_size)

    def checkpoint(self) -> None:
        """Fold the WAL back into the main database. Called on clean shutdown so
        a subsequent power loss cannot lose an unmerged tail."""
        self._conn.execute("PRAGMA wal_checkpoint(TRUNCATE)")
