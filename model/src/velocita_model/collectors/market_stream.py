"""Records the True Markets market-data stream as raw evidence.

This is a recorder, not the quoting collector (`cli collect` stays blocked on
venue verification). It interprets nothing into atoms: prices stay strings in
the stored bodies, and the only derived values are unit-free monitoring figures
(spread in basis points, ages in seconds) published for Grafana.

Storage is bounded by policy, not by luck. Every message is kept byte-for-byte
except `price_candles`, which carries every listed asset about twice a second
(~30 GiB/day): only watched symbols' rows are kept, only when they change, and
`full_digest` still names the complete original.
"""

from __future__ import annotations

import hashlib
import json
import os
import time
from dataclasses import dataclass, field
from datetime import datetime
from decimal import Decimal, InvalidOperation
from typing import Callable, Iterable, Protocol, Sequence

from ..storage.sqlite_store import ResearchStore, SessionHandle

_NS = 1_000_000_000


class MarketSocket(Protocol):
    def open(self) -> None: ...
    def subscribe(self, topics: Sequence[str]) -> None: ...
    def recv_text(self) -> str | None: ...
    def close(self) -> None: ...


def _sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _rfc3339_ns(value: str) -> int | None:
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (TypeError, ValueError):
        return None
    if stamp.tzinfo is None:
        return None
    seconds = int(stamp.timestamp())
    return seconds * _NS + stamp.microsecond * 1000


def _label(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


@dataclass
class _Reference:
    mid: str
    spread_bps: Decimal | None
    source_times_ns: dict[str, int | None] = field(default_factory=dict)


class MarketStreamRecorder:
    def __init__(
        self,
        store: ResearchStore,
        session: SessionHandle,
        *,
        topics: Iterable[str],
        watch_symbols: Iterable[str],
        silent_after_s: int = 120,
    ) -> None:
        self._store, self._session = store, session
        self._topics = list(topics)
        self._watch = set(watch_symbols)
        self._silent_after_ns = silent_after_s * _NS
        self._connected_at_ns: int | None = None
        self._up = False
        self._gap_id: int | None = None
        self._last_seen_ns: dict[str, int] = {}
        self._counts: dict[str, int] = {}
        self._parse_errors = 0
        self._deduplicated = 0
        self._last_candles: str | None = None
        self._rejected: dict[tuple[str, str], int] = {}
        self._references: dict[str, _Reference] = {}
        self._latest_receive_ns = 0
        self.reconnects = 0
        self.protocol_errors = 0


    def on_connected(self, now_ns: int) -> None:
        self._up = True
        self._connected_at_ns = now_ns
        if self._gap_id is not None:
            self._store.close_quality_interval(self._gap_id)
            self._gap_id = None

    def on_disconnected(self, now_ns: int, reason: str) -> None:
        self._up = False
        if self._gap_id is None:
            self._gap_id = self._store.open_quality_interval(self._session, "disconnect", detail=reason)


    def handle(self, text: str, receive_ns: int) -> None:
        self._latest_receive_ns = max(self._latest_receive_ns, receive_ns)
        full_digest = _sha256(text)
        try:
            message = json.loads(text)
            kind = str(message["type"])
        except (ValueError, KeyError, TypeError):
            self._parse_errors += 1
            self._record("unparseable", None, None, receive_ns, text, full_digest, filtered=False)
            return
        topics = message.get("topics") if isinstance(message.get("topics"), list) else None
        self._counts[kind] = self._counts.get(kind, 0) + 1
        for topic in topics or ():
            self._last_seen_ns[str(topic)] = receive_ns
        server_ts = message.get("timestamp") if isinstance(message.get("timestamp"), int) else None

        if kind == "price_candles":
            rows = [row for row in message.get("data") or () if isinstance(row, dict) and row.get("symbol") in self._watch]
            kept = json.dumps(dict(message, data=rows), separators=(",", ":"), sort_keys=True)
            fingerprint = json.dumps(rows, sort_keys=True)
            if fingerprint == self._last_candles:
                self._deduplicated += 1
                return
            self._last_candles = fingerprint
            if rows:
                self._record(kind, topics, server_ts, receive_ns, kept, full_digest, filtered=True)
            return

        if kind == "subscribe":
            for rejected in message.get("rejected") or ():
                key = (str(rejected.get("topic")), str(rejected.get("reason")))
                self._rejected[key] = 1
        elif kind == "reference_prices":
            self._note_references(message.get("data") or ())
        self._record(kind, topics, server_ts, receive_ns, text, full_digest, filtered=False)

    def _note_references(self, rows: Iterable[object]) -> None:
        for row in rows:
            if not isinstance(row, dict) or "symbol" not in row:
                continue
            try:
                bid, ask, mid = Decimal(row["bid"]), Decimal(row["ask"]), Decimal(row["mid_price"])
                spread = (ask - bid) / mid * 10_000 if mid > 0 else None
            except (KeyError, TypeError, InvalidOperation):
                spread = None
            sources = {
                str(source.get("source")): _rfc3339_ns(source.get("timestamp"))
                for source in row.get("sources") or ()
                if isinstance(source, dict)
            }
            self._references[str(row["symbol"])] = _Reference(str(row.get("mid_price")), spread, sources)

    def _record(self, kind: str, topics: list | None, server_ts: int | None, receive_ns: int, body: str,
                full_digest: str, *, filtered: bool) -> None:
        self._store.connection.execute(
            "INSERT INTO market_message (session_id, type, topics, server_ts_s, receive_time_ns, body, "
            "body_digest, full_digest, filtered) VALUES (?,?,?,?,?,?,?,?,?)",
            (self._session.session_id, kind, None if topics is None else json.dumps(topics), server_ts,
             receive_ns, body, _sha256(body), full_digest, int(filtered)),
        )


    def metrics(self, now_ns: int) -> str:
        out = [
            "# TYPE velocita_md_up gauge", f"velocita_md_up {int(self._up)}",
            "# TYPE velocita_md_reconnects_total counter", f"velocita_md_reconnects_total {self.reconnects}",
            "# TYPE velocita_md_protocol_errors_total counter",
            f"velocita_md_protocol_errors_total {self.protocol_errors}",
            "# TYPE velocita_md_parse_errors_total counter", f"velocita_md_parse_errors_total {self._parse_errors}",
            "# TYPE velocita_md_candles_deduplicated_total counter",
            f"velocita_md_candles_deduplicated_total {self._deduplicated}",
            "# TYPE velocita_md_messages_total counter",
        ]
        out += [f'velocita_md_messages_total{{type="{_label(k)}"}} {v}' for k, v in sorted(self._counts.items())]
        out.append("# TYPE velocita_md_topic_silent gauge")
        for topic in self._topics:
            since = self._last_seen_ns.get(topic, self._connected_at_ns)
            silent = since is None or now_ns - since > self._silent_after_ns
            out.append(f'velocita_md_topic_silent{{topic="{_label(topic)}"}} {int(silent)}')
        out.append("# TYPE velocita_md_subscribe_rejected gauge")
        out += [f'velocita_md_subscribe_rejected{{topic="{_label(t)}",reason="{_label(r)}"}} {v}'
                for (t, r), v in sorted(self._rejected.items())]
        out += ["# TYPE velocita_md_reference_spread_bps gauge", "# TYPE velocita_md_reference_mid gauge",
                "# TYPE velocita_md_reference_source_age_seconds gauge"]
        for symbol, ref in sorted(self._references.items()):
            label = _label(symbol)
            if ref.spread_bps is not None:
                out.append(f'velocita_md_reference_spread_bps{{symbol="{label}"}} {ref.spread_bps:.3f}')
            out.append(f'velocita_md_reference_mid{{symbol="{label}"}} {ref.mid}')
            for source, stamp in sorted(ref.source_times_ns.items()):
                if stamp is not None:
                    age = max(0, self._latest_receive_ns - stamp) / _NS
                    out.append(f'velocita_md_reference_source_age_seconds{{symbol="{label}",source="{_label(source)}"}} '
                               f"{age:.3f}")
        return "\n".join(out) + "\n"


def publish(text: str, path: str) -> None:
    tmp = f"{path}.tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        handle.write(text)
    os.replace(tmp, path)


def run_stream(
    recorder: MarketStreamRecorder,
    socket_factory: Callable[[], MarketSocket],
    topics: Sequence[str],
    *,
    textfile: str | None,
    stop: Callable[[], bool],
    sleep: Callable[[float], None] = time.sleep,
    clock_ns: Callable[[], int] = time.time_ns,
    publish_every_s: float = 10.0,
    max_backoff_s: float = 60.0,
) -> None:
    backoff, attempts, last_publish = 1.0, 0, 0
    while not stop():
        if attempts:
            recorder.reconnects += 1
        attempts += 1
        socket = None
        try:
            socket = socket_factory()
            socket.open()
            socket.subscribe(list(topics))
            recorder.on_connected(clock_ns())
            backoff = 1.0
            while not stop():
                text = socket.recv_text()
                if text is None:
                    break
                recorder.handle(text, clock_ns())
                now = clock_ns()
                if textfile and now - last_publish >= publish_every_s * _NS:
                    publish(recorder.metrics(now), textfile)
                    last_publish = now
            reason = "stream ended"
        except OSError as error:
            reason = f"{type(error).__name__}: {error}"
        except RuntimeError as error:
            recorder.protocol_errors += 1
            reason = f"{type(error).__name__}: {error}"
        finally:
            if socket is not None:
                try:
                    socket.close()
                except OSError:
                    pass
        recorder.on_disconnected(clock_ns(), reason)
        if textfile:
            publish(recorder.metrics(clock_ns()), textfile)
        if stop():
            break
        sleep(backoff)
        backoff = min(backoff * 2, max_backoff_s)
