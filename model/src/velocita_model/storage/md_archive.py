"""Nightly archive of the market-data recorder's stream.db, and a prune gated on
the owner's Mac holding a verified copy (docs/MD_ARCHIVE_RUNBOOK.md).

Each complete UTC day becomes `stream-YYYY-MM-DD.sqlite.gz`: a SQLite database
with the live schema holding that day's `market_message` rows plus the sessions
and data-quality intervals they need, gzip-compressed. A sidecar
`<name>.sha256` (sha256sum format) is written last and marks the archive as
complete.

A day's rows leave stream.db only when all three hold:
  (a) the archive's bytes hash to the sidecar's sha256;
  (b) `acked/stream-YYYY-MM-DD.ack`, written by the Mac after it verified its
      own copy, names that same sha256;
  (c) the day is older than the local retention window.

Concurrency with md-stream (WAL): the archive is copied inside one read
transaction, which never blocks the writer; deletes run as short IMMEDIATE
transactions of at most `batch_rows` rows each.
"""

from __future__ import annotations

import calendar
import fcntl
import gzip
import hashlib
import json
import os
import re
import shutil
import sqlite3
import time
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Iterable
from urllib.parse import quote

_NS = 1_000_000_000
_DAY_S = 86_400
_TABLES = ("collector_session", "data_quality_interval", "market_message")
_INDEX = "market_message_type_time"
_CHUNK = 1 << 20
_SHA = re.compile(r"[0-9a-f]{64}")
_SIDECAR = re.compile(r"^stream-(\d{4}-\d{2}-\d{2})\.sqlite\.gz\.sha256$")
_PRUNED = re.compile(r"^stream-(\d{4}-\d{2}-\d{2})\.pruned$")
_ACK = re.compile(r"^stream-(\d{4}-\d{2}-\d{2})\.ack$")
BLOCKED_REASONS = ("archive_unverified", "ack_mismatch", "late_rows")


class ArchiveError(RuntimeError):
    pass


@dataclass(frozen=True)
class Policy:
    retention_days: int = 3
    keep_archives_days: int = 7
    grace_s: int = 600
    max_days: int = 10
    batch_rows: int = 5000
    batch_pause_s: float = 0.05
    reclaim_pages: int = 2048


def day_start_s(day: date) -> int:
    return calendar.timegm(day.timetuple())


def utc_day(ns: int) -> date:
    return datetime.fromtimestamp(ns // _NS, tz=timezone.utc).date()


def archive_name(day: date) -> str:
    return f"stream-{day.isoformat()}.sqlite.gz"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while chunk := handle.read(_CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def parse_digest_line(text: str, name: str) -> str | None:
    words = text.split()
    if not words or not _SHA.fullmatch(words[0]) or len(words) > 2:
        return None
    if len(words) == 2 and words[1].lstrip("*") != name:
        return None
    return words[0]


def _fsync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def write_atomic(path: Path, data: bytes) -> None:
    tmp = path.with_name(f".tmp-{path.name}.tmp")
    with open(tmp, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)
    _fsync_dir(path.parent)


def _row_digest(rows: Iterable[tuple]) -> tuple[int, int]:
    count, total = 0, 0
    for row in rows:
        total = (total + int.from_bytes(hashlib.sha256(repr(tuple(row)).encode("utf-8", "surrogatepass")).digest(),
                                        "big")) % (1 << 256)
        count += 1
    return count, total


def _day_where(types: list[str], start_ns: int, end_ns: int) -> tuple[str, list]:
    marks = ",".join("?" * len(types))
    return f"type IN ({marks}) AND receive_time_ns >= ? AND receive_time_ns < ?", [*types, start_ns, end_ns]


class Archiver:
    def __init__(self, db: Path | str, archive_dir: Path | str, *, clock_ns: Callable[[], int] = time.time_ns,
                 sleep: Callable[[float], None] = time.sleep, policy: Policy = Policy(),
                 hooks: dict[str, Callable] | None = None, trace: Callable[[str], None] | None = None) -> None:
        self.db, self.dir = Path(db), Path(archive_dir)
        self._clock_ns, self._sleep, self.policy = clock_ns, sleep, policy
        self._hooks, self._trace = hooks or {}, trace
        self.last_report: dict[date, str] = {}


    def archive_path(self, day: date) -> Path:
        return self.dir / archive_name(day)

    def sidecar_path(self, day: date) -> Path:
        return self.dir / f"{archive_name(day)}.sha256"

    def manifest_path(self, day: date) -> Path:
        return self.dir / f"stream-{day.isoformat()}.manifest.json"

    def ack_path(self, day: date) -> Path:
        return self.dir / "acked" / f"stream-{day.isoformat()}.ack"

    def pruned_path(self, day: date) -> Path:
        return self.dir / "pruned" / f"stream-{day.isoformat()}.pruned"

    def _days(self, directory: Path, pattern: re.Pattern) -> list[date]:
        if not directory.is_dir():
            return []
        return sorted(date.fromisoformat(m.group(1)) for p in directory.iterdir() if (m := pattern.match(p.name)))

    def archived_days(self) -> list[date]:
        return self._days(self.dir, _SIDECAR)

    def pruned_days(self) -> list[date]:
        return self._days(self.dir / "pruned", _PRUNED)

    def sidecar_sha(self, day: date) -> str | None:
        try:
            return parse_digest_line(self.sidecar_path(day).read_text(encoding="ascii"), archive_name(day))
        except (OSError, UnicodeDecodeError):
            return None

    def ack_sha(self, day: date) -> str | None:
        try:
            return parse_digest_line(self.ack_path(day).read_text(encoding="ascii"), archive_name(day))
        except (OSError, UnicodeDecodeError):
            return None

    def manifest(self, day: date) -> dict | None:
        try:
            return json.loads(self.manifest_path(day).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None

    def _today(self) -> date:
        return utc_day(self._clock_ns())

    def _hook(self, name: str, *args: object) -> None:
        if name in self._hooks:
            self._hooks[name](*args)


    def _connect(self, timeout: float = 10.0) -> sqlite3.Connection:
        if not self.db.is_file():
            raise ArchiveError(f"{self.db} does not exist")
        conn = sqlite3.connect(f"file:{quote(str(self.db))}?mode=rw", uri=True, isolation_level=None, timeout=timeout)
        if self._trace is not None:
            conn.set_trace_callback(self._trace)
        return conn

    @staticmethod
    def _types(conn: sqlite3.Connection) -> list[str]:
        return [row[0] for row in conn.execute("SELECT DISTINCT type FROM main.market_message")]


    def archive(self) -> list[date]:
        for sub in ("", "acked", "pruned"):
            (self.dir / sub).mkdir(parents=True, exist_ok=True)
        for leftover in [*self.dir.glob(".tmp-*"), *(self.dir / "pruned").glob(".tmp-*")]:
            leftover.unlink()
        done = set(self.archived_days()) | set(self.pruned_days())
        written: list[date] = []
        conn = self._connect()
        try:
            types = self._types(conn)
            if not types:
                return written
            oldest = min(conn.execute("SELECT min(receive_time_ns) FROM main.market_message WHERE type = ?",
                                      (t,)).fetchone()[0] for t in types)
            now_s = self._clock_ns() // _NS
            day = utc_day(oldest)
            while day_start_s(day) + _DAY_S + self.policy.grace_s <= now_s and len(written) < self.policy.max_days:
                if day not in done and self._has_rows(conn, types, day):
                    self._archive_day(conn, day)
                    written.append(day)
                day += timedelta(days=1)
        finally:
            conn.close()
        return written

    @staticmethod
    def _has_rows(conn: sqlite3.Connection, types: list[str], day: date) -> bool:
        where, params = _day_where(types, day_start_s(day) * _NS, (day_start_s(day) + _DAY_S) * _NS)
        return conn.execute(f"SELECT EXISTS (SELECT 1 FROM main.market_message WHERE {where})", params).fetchone()[0] == 1

    def _create_schema(self, conn: sqlite3.Connection, path: Path) -> None:
        names = (*_TABLES, _INDEX)
        sql = dict(conn.execute(f"SELECT name, sql FROM main.sqlite_master WHERE name IN ({','.join('?' * len(names))})",
                                names).fetchall())
        missing = [n for n in names if n not in sql]
        if missing:
            raise ArchiveError(f"stream.db lacks {missing}")
        out = sqlite3.connect(path)
        try:
            for name in names:
                out.execute(sql[name])
            out.commit()
        finally:
            out.close()

    def _archive_day(self, conn: sqlite3.Connection, day: date) -> None:
        tmp_db = self.dir / f".tmp-stream-{day.isoformat()}.sqlite"
        tmp_gz = self.dir / f".tmp-{archive_name(day)}"
        try:
            self._write_archive(conn, day, tmp_db, tmp_gz)
        finally:
            tmp_db.unlink(missing_ok=True)
            tmp_gz.unlink(missing_ok=True)

    def _write_archive(self, conn: sqlite3.Connection, day: date, tmp_db: Path, tmp_gz: Path) -> None:
        start, end = day_start_s(day) * _NS, (day_start_s(day) + _DAY_S) * _NS
        name = archive_name(day)
        self._create_schema(conn, tmp_db)
        conn.execute("ATTACH DATABASE ? AS arc", (str(tmp_db),))
        try:
            conn.execute("PRAGMA arc.journal_mode = OFF")
            conn.execute("PRAGMA arc.synchronous = OFF")
            conn.execute("BEGIN")
            try:
                types = self._types(conn)
                self._hook("after_snapshot", day)
                where, params = _day_where(types, start, end)
                conn.execute(f"INSERT INTO arc.market_message SELECT * FROM main.market_message WHERE {where}", params)
                conn.execute("INSERT INTO arc.data_quality_interval SELECT * FROM main.data_quality_interval "
                             "WHERE started_at_ns < ? AND (ended_at_ns IS NULL OR ended_at_ns > ?)", (end, start))
                conn.execute("INSERT INTO arc.collector_session SELECT * FROM main.collector_session WHERE session_id IN "
                             "(SELECT session_id FROM arc.market_message UNION SELECT session_id FROM arc.data_quality_interval)")
                rows, min_id, max_id = conn.execute("SELECT count(*), min(id), max(id) FROM arc.market_message").fetchone()
                source = _row_digest(conn.execute(f"SELECT * FROM main.market_message WHERE {where}", params))
                conn.execute("COMMIT")
            except BaseException:
                conn.execute("ROLLBACK")
                raise
        finally:
            conn.execute("DETACH DATABASE arc")
        if rows == 0 or source[0] != rows:
            raise ArchiveError(f"{day}: copied {rows} rows, snapshot holds {source[0]}")

        self._hook("before_verify", day, tmp_db)
        check = sqlite3.connect(f"file:{quote(str(tmp_db))}?mode=ro", uri=True)
        try:
            integrity = check.execute("PRAGMA integrity_check").fetchone()[0]
            copied = _row_digest(check.execute("SELECT * FROM market_message"))
        finally:
            check.close()
        if integrity != "ok" or copied != source:
            raise ArchiveError(f"{day}: archive differs from stream.db ({integrity})")

        raw_sha = hashlib.sha256()
        with open(tmp_db, "rb") as src, open(tmp_gz, "wb") as out:
            with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=6, mtime=0) as gz:
                while chunk := src.read(_CHUNK):
                    raw_sha.update(chunk)
                    gz.write(chunk)
            out.flush()
            os.fsync(out.fileno())
        unpacked = hashlib.sha256()
        with gzip.open(tmp_gz, "rb") as gz:
            while chunk := gz.read(_CHUNK):
                unpacked.update(chunk)
        if unpacked.hexdigest() != raw_sha.hexdigest():
            raise ArchiveError(f"{day}: compressed archive does not decompress to the verified database")
        sha = sha256_file(tmp_gz)

        self._hook("before_publish", day)
        os.replace(tmp_gz, self.archive_path(day))
        manifest = {"day": day.isoformat(), "rows": rows, "min_id": min_id, "max_id": max_id, "sha256": sha,
                    "db_sha256": raw_sha.hexdigest(), "start_ns": start, "end_ns": end,
                    "archived_at_ns": self._clock_ns()}
        write_atomic(self.manifest_path(day), (json.dumps(manifest, sort_keys=True) + "\n").encode())
        write_atomic(self.sidecar_path(day), f"{sha}  {name}\n".encode("ascii"))


    def prune(self) -> dict[date, str]:
        report: dict[date, str] = {}
        today, pruned = self._today(), set(self.pruned_days())
        (self.dir / "pruned").mkdir(parents=True, exist_ok=True)
        conn = self._connect()
        try:
            for day in self.archived_days():
                if day in pruned:
                    continue
                report[day] = self._prune_day(conn, day, today)
            if any(outcome == "pruned" for outcome in report.values()):
                self._reclaim(conn)
        finally:
            conn.close()
        self.last_report = report
        return report

    def _prune_day(self, conn: sqlite3.Connection, day: date, today: date) -> str:
        if (today - day).days <= self.policy.retention_days:
            return "retention"
        sha = self.sidecar_sha(day)
        manifest = self.manifest(day)
        if (sha is None or manifest is None or manifest.get("sha256") != sha
                or not self.archive_path(day).is_file() or sha256_file(self.archive_path(day)) != sha):
            return "archive_unverified"
        ack = self.ack_sha(day)
        if ack is None:
            return "awaiting_ack"
        if ack != sha:
            return "ack_mismatch"

        max_id = int(manifest["max_id"])
        start, end = day_start_s(day) * _NS, (day_start_s(day) + _DAY_S) * _NS
        where, params = _day_where(self._types(conn), start, end)
        if self._late_rows(conn, where, params, max_id):
            return "late_rows"
        batch = 0
        while True:
            conn.execute("BEGIN IMMEDIATE")
            try:
                deleted = conn.execute(
                    f"DELETE FROM main.market_message WHERE id IN (SELECT id FROM main.market_message "
                    f"WHERE {where} AND id <= ? LIMIT ?)", [*params, max_id, self.policy.batch_rows]).rowcount
                conn.execute("COMMIT")
            except BaseException:
                conn.execute("ROLLBACK")
                raise
            if deleted == 0:
                break
            batch += 1
            self._hook("after_batch", day, batch)
            self._sleep(self.policy.batch_pause_s)
            if deleted < self.policy.batch_rows:
                break
        if conn.execute(f"SELECT EXISTS (SELECT 1 FROM main.market_message WHERE {where})", params).fetchone()[0]:
            return "late_rows"
        record = {"day": day.isoformat(), "sha256": sha, "max_id": max_id, "pruned_at_ns": self._clock_ns()}
        write_atomic(self.pruned_path(day), (json.dumps(record, sort_keys=True) + "\n").encode())
        return "pruned"

    @staticmethod
    def _late_rows(conn: sqlite3.Connection, where: str, params: list, max_id: int) -> bool:
        return conn.execute(f"SELECT EXISTS (SELECT 1 FROM main.market_message WHERE {where} AND id > ?)",
                            [*params, max_id]).fetchone()[0] == 1

    def _reclaim(self, conn: sqlite3.Connection) -> None:
        if conn.execute("PRAGMA main.auto_vacuum").fetchone()[0] != 2:
            return
        free = conn.execute("PRAGMA main.freelist_count").fetchone()[0]
        for _ in range(free // self.policy.reclaim_pages + 1):
            conn.execute(f"PRAGMA main.incremental_vacuum({int(self.policy.reclaim_pages)})").fetchall()
            if conn.execute("PRAGMA main.freelist_count").fetchone()[0] == 0:
                break
            self._sleep(self.policy.batch_pause_s)


    def remove_old_archives(self) -> list[date]:
        removed, today, pruned = [], self._today(), set(self.pruned_days())
        for day in self.archived_days():
            if (today - day).days <= self.policy.keep_archives_days or day not in pruned:
                continue
            sha = self.sidecar_sha(day)
            if sha is None or self.ack_sha(day) != sha:
                continue
            for path in (self.archive_path(day), self.manifest_path(day), self.sidecar_path(day)):
                path.unlink(missing_ok=True)
            removed.append(day)
        if removed:
            _fsync_dir(self.dir)
        return removed


    def compact(self, quiet_s: int = 120) -> None:
        conn = self._connect(timeout=1.0)
        try:
            types = self._types(conn)
            newest = max((conn.execute("SELECT max(receive_time_ns) FROM main.market_message WHERE type = ?",
                                       (t,)).fetchone()[0] for t in types), default=None)
            if newest is not None and self._clock_ns() - newest < quiet_s * _NS:
                raise ArchiveError(f"a row arrived {(self._clock_ns() - newest) / _NS:.0f}s ago: "
                                   "stop velocita-md-stream before compacting")
            page_size = conn.execute("PRAGMA page_size").fetchone()[0]
            used = (conn.execute("PRAGMA page_count").fetchone()[0]
                    - conn.execute("PRAGMA freelist_count").fetchone()[0]) * page_size
            free = shutil.disk_usage(self.db.parent).free
            if free < 2 * used + (256 << 20):
                raise ArchiveError(f"compaction needs {2 * used + (256 << 20)} free bytes, have {free}")
            try:
                mode = conn.execute("PRAGMA journal_mode = DELETE").fetchone()[0]
            except sqlite3.OperationalError as error:
                raise ArchiveError(f"another process holds {self.db} open ({error})") from error
            if mode != "delete":
                raise ArchiveError(f"could not take exclusive use of {self.db} (journal_mode {mode})")
            try:
                conn.execute("PRAGMA auto_vacuum = INCREMENTAL")
                conn.execute("VACUUM")
            finally:
                conn.execute("PRAGMA journal_mode = WAL")
        finally:
            conn.close()


    def metrics(self, *, ok: bool) -> str:
        now_s = self._clock_ns() // _NS
        on_host, pruned = self.archived_days(), self.pruned_days()
        acked = self._days(self.dir / "acked", _ACK)
        acked_valid = [d for d in acked if self.ack_sha(d) is not None]
        unacked = [d for d in on_host if self.ack_sha(d) is None or self.ack_sha(d) != self.sidecar_sha(d)]
        oldest_end = day_start_s(unacked[0]) + _DAY_S if unacked else 0
        last = max([*on_host, *pruned], default=None)
        blocked = {reason: sum(1 for o in self.last_report.values() if o == reason) for reason in BLOCKED_REASONS}
        db_bytes, free_bytes = 0, 0
        if self.db.is_file():
            db_bytes = os.path.getsize(self.db)
            conn = sqlite3.connect(f"file:{quote(str(self.db))}?mode=ro", uri=True, timeout=10.0)
            try:
                free_bytes = (conn.execute("PRAGMA freelist_count").fetchone()[0]
                              * conn.execute("PRAGMA page_size").fetchone()[0])
            finally:
                conn.close()
        out = [
            "# TYPE velocita_md_archive_last_run_ok gauge", f"velocita_md_archive_last_run_ok {int(ok)}",
            "# TYPE velocita_md_archive_last_run_timestamp_seconds gauge",
            f"velocita_md_archive_last_run_timestamp_seconds {now_s}",
            "# TYPE velocita_md_archive_last_day_timestamp_seconds gauge",
            f"velocita_md_archive_last_day_timestamp_seconds {day_start_s(last) if last else 0}",
            "# TYPE velocita_md_archive_days gauge",
            f'velocita_md_archive_days{{state="on_host"}} {len(on_host)}',
            f'velocita_md_archive_days{{state="acked"}} {len(acked_valid)}',
            f'velocita_md_archive_days{{state="pruned"}} {len(pruned)}',
            "# TYPE velocita_md_archive_oldest_unacked_day_end_timestamp_seconds gauge",
            f"velocita_md_archive_oldest_unacked_day_end_timestamp_seconds {oldest_end}",
            "# TYPE velocita_md_archive_oldest_unacked_age_seconds gauge",
            f"velocita_md_archive_oldest_unacked_age_seconds {now_s - oldest_end if oldest_end else 0}",
            "# TYPE velocita_md_archive_blocked_days gauge",
            *(f'velocita_md_archive_blocked_days{{reason="{r}"}} {n}' for r, n in blocked.items()),
            "# TYPE velocita_md_stream_db_bytes gauge", f"velocita_md_stream_db_bytes {db_bytes}",
            "# TYPE velocita_md_stream_db_free_bytes gauge", f"velocita_md_stream_db_free_bytes {free_bytes}",
        ]
        return "\n".join(out) + "\n"


class RunLock:
    def __init__(self, directory: Path) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        self._handle = open(directory / ".lock", "w")

    def __enter__(self) -> "RunLock":
        try:
            fcntl.flock(self._handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            self._handle.close()
            raise ArchiveError("another md-archive run holds the lock") from error
        return self

    def __exit__(self, *_exc: object) -> None:
        fcntl.flock(self._handle, fcntl.LOCK_UN)
        self._handle.close()


def _columns(conn: sqlite3.Connection, schema: str, table: str) -> list[str]:
    return [row[1] for row in conn.execute(f"PRAGMA {schema}.table_info({table})")]


def restore(archives: Iterable[Path], into: Path) -> int:
    from .sqlite_store import ResearchStore

    archives = [Path(a) for a in archives]
    for archive in archives:
        sidecar = archive.with_name(archive.name + ".sha256")
        try:
            expected = parse_digest_line(sidecar.read_text(encoding="ascii"), archive.name)
        except OSError as error:
            raise ArchiveError(f"{archive}: no sidecar {sidecar.name}") from error
        if expected is None or sha256_file(archive) != expected:
            raise ArchiveError(f"{archive}: sha256 does not match {sidecar.name}")
    added = 0
    with ResearchStore(into) as store:
        conn = store.connection
        for archive in archives:
            plain = into.with_name(f".restore-{archive.name}.db")
            with gzip.open(archive, "rb") as src, open(plain, "wb") as out:
                shutil.copyfileobj(src, out, _CHUNK)
            try:
                conn.execute("ATTACH DATABASE ? AS src", (str(plain),))
                try:
                    with store.transaction():
                        for table, verb in (("collector_session", "INSERT OR IGNORE"),
                                            ("data_quality_interval", "INSERT OR IGNORE"),
                                            ("market_message", "INSERT OR IGNORE")):
                            cols = ",".join(_columns(conn, "src", table))
                            cursor = conn.execute(f"{verb} INTO main.{table} ({cols}) SELECT {cols} FROM src.{table}")
                            if table == "market_message":
                                added += cursor.rowcount
                finally:
                    conn.execute("DETACH DATABASE src")
            finally:
                plain.unlink(missing_ok=True)
    return added
