"""Nightly market-data archive and the gated prune (storage/md_archive.py).

Every expected value is written by hand from the fixture below or recomputed
here with the standard library (gzip, hashlib, sqlite3), never read back from
the module under test. The archive is opened by this file, not by md_archive.
"""

from __future__ import annotations

import calendar
import gzip
import hashlib
import json
import os
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from datetime import date

from velocita_model.storage import md_archive as mda
from velocita_model.storage.sqlite_store import ResearchStore

S = 1_000_000_000
# UTC midnights, written by hand (2026-09-28T12:53:20Z = 1790600000, minus
# 12h53m20s = 46400 s) and cross-checked against calendar.timegm in a test.
D28 = 1_790_553_600   # 2026-09-28T00:00:00Z
D29 = D28 + 86_400    # 2026-09-29
D30 = D29 + 86_400    # 2026-09-30
D01 = D30 + 86_400    # 2026-10-01
D02 = D01 + 86_400    # 2026-10-02

SESSIONS = [
    # session_id, venue, started_at_ns, ended_at_ns, config_digest, schema_digest, code_version, clock_synced, stop_reason
    (1, "truemarkets", (D28 - 3600) * S, D29 * S + 7 * 3600 * S, "c1", "s1", "0.1", 1, "interrupted"),
    (2, "truemarkets", D29 * S + 12 * 3600 * S, None, "c2", "s1", "0.1", 1, None),
]
INTERVALS = [
    # id, session_id, pair_id, kind, started_at_ns, ended_at_ns, detail
    (1, 1, None, "disconnect", D28 * S + 23 * 3600 * S, D29 * S + 3600 * S, "stream ended"),  # spans 28 -> 29
    (2, 2, None, "disconnect", D30 * S + 10 * 3600 * S, None, "OSError: reset"),             # still open
]
BODY_UNICODE = '{"type":"surging_assets","note":"café → \U0001f680","nl":"a\\nb"}\nraw tail'
MESSAGES = [
    # id, session_id, type, topics, server_ts_s, receive_time_ns, body, body_digest, full_digest, filtered
    (1, 1, "reference_prices", '["reference_prices"]', D28, D28 * S, '{"a":1}', "b1", "f1", 0),            # first ns of 28
    (2, 1, "price_candles", '["price_candles"]', D28 + 43_200, (D28 + 43_200) * S, '{"c":2}', "b2", "f2", 1),
    (3, 1, "surging_assets", None, None, D29 * S - 1, BODY_UNICODE, "b3", "f3", 0),                       # last ns of 28
    (4, 1, "reference_prices", '["reference_prices"]', D29, D29 * S, '{"a":4}', "b4", "f4", 0),            # first ns of 29
    (5, 1, "unparseable", None, None, D29 * S + S // 2, "not json", "b5", "f5", 0),
    (6, 2, "trending_assets", '["trending_assets"]', D30 + 21_600, (D30 + 21_600) * S, '{"t":6}', "b6", "f6", 0),
    (7, 2, "reference_prices", '["reference_prices"]', D01 - 1, D01 * S - 1, '{"a":7}', "b7", "f7", 0),
    (8, 2, "reference_prices", '["reference_prices"]', D01 + 1, (D01 + 1) * S, '{"a":8}', "b8", "f8", 0),  # "today"
]
DAY28, DAY29, DAY30, DAY01 = date(2026, 9, 28), date(2026, 9, 29), date(2026, 9, 30), date(2026, 10, 1)
# Hand-assigned expectations per day.
EXPECTED_IDS = {DAY28: [1, 2, 3], DAY29: [4, 5], DAY30: [6, 7]}
EXPECTED_SESSIONS = {DAY28: [1], DAY29: [1], DAY30: [2]}
EXPECTED_INTERVALS = {DAY28: [1], DAY29: [1], DAY30: [2]}


def at(seconds: int):
    return lambda: seconds * S


def build_db(path: pathlib.Path, messages=MESSAGES) -> None:
    ResearchStore(path).close()  # the production schema md-stream creates
    conn = sqlite3.connect(path)
    conn.executemany("INSERT INTO collector_session VALUES (?,?,?,?,?,?,?,?,?)", SESSIONS)
    conn.executemany("INSERT INTO data_quality_interval VALUES (?,?,?,?,?,?,?)", INTERVALS)
    conn.executemany("INSERT INTO market_message VALUES (?,?,?,?,?,?,?,?,?,?)", messages)
    conn.commit()
    conn.close()


def read_archive(path: pathlib.Path, table: str, key: str) -> list[tuple]:
    with tempfile.TemporaryDirectory() as tmp:
        plain = pathlib.Path(tmp) / "day.db"
        plain.write_bytes(gzip.decompress(path.read_bytes()))
        conn = sqlite3.connect(plain)
        rows = conn.execute(f"SELECT * FROM {table} ORDER BY {key}").fetchall()
        conn.close()
    return rows


def db_ids(path: pathlib.Path) -> list[int]:
    conn = sqlite3.connect(path)
    ids = [r[0] for r in conn.execute("SELECT id FROM market_message ORDER BY id")]
    conn.close()
    return ids


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Fixture(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._dir.name)
        self.db = self.root / "stream.db"
        self.dir = self.root / "archive"
        build_db(self.db)

    def tearDown(self) -> None:
        self._dir.cleanup()

    def archiver(self, now_s: int, **policy) -> mda.Archiver:
        return mda.Archiver(self.db, self.dir, clock_ns=at(now_s), sleep=lambda _s: None,
                            policy=mda.Policy(**policy))

    def gz(self, day: date) -> pathlib.Path:
        return self.dir / f"stream-{day.isoformat()}.sqlite.gz"

    def ack(self, day: date, sha: str | None = None) -> pathlib.Path:
        """What the Mac writes after verifying its copy: the sha256 it computed."""
        (self.dir / "acked").mkdir(exist_ok=True)
        path = self.dir / "acked" / f"stream-{day.isoformat()}.ack"
        digest = sha if sha is not None else sha256_file(self.gz(day))
        path.write_text(f"{digest}  stream-{day.isoformat()}.sqlite.gz\n")
        return path

    def ids_of_day(self, day: date) -> list[int]:
        start = calendar.timegm(day.timetuple()) * S
        return [m[0] for m in MESSAGES if start <= m[5] < start + 86_400 * S]


class DayBoundaries(Fixture):
    def test_hand_written_midnights_match_the_calendar(self):
        self.assertEqual(D28, calendar.timegm((2026, 9, 28, 0, 0, 0)))
        self.assertEqual(D01, calendar.timegm((2026, 10, 1, 0, 0, 0)))
        # The fixture's per-day expectation agrees with an independent split.
        for day, ids in EXPECTED_IDS.items():
            self.assertEqual(self.ids_of_day(day), ids)


class Archive(Fixture):
    def test_each_complete_day_round_trips_every_row_and_column(self):
        archived = self.archiver(D01 + 1800).archive()  # 00:30 UTC on 10-01
        self.assertEqual(archived, [DAY28, DAY29, DAY30])
        self.assertFalse(self.gz(DAY01).exists(), "today is never complete")
        by_id = {m[0]: m for m in MESSAGES}
        for day in (DAY28, DAY29, DAY30):
            self.assertEqual(read_archive(self.gz(day), "market_message", "id"),
                             [by_id[i] for i in EXPECTED_IDS[day]], day)
            self.assertEqual(read_archive(self.gz(day), "collector_session", "session_id"),
                             [SESSIONS[i - 1] for i in EXPECTED_SESSIONS[day]], day)
            self.assertEqual(read_archive(self.gz(day), "data_quality_interval", "id"),
                             [INTERVALS[i - 1] for i in EXPECTED_INTERVALS[day]], day)

    def test_storage_classes_survive_not_just_values(self):
        self.archiver(D01 + 1800).archive()
        with tempfile.TemporaryDirectory() as tmp:
            plain = pathlib.Path(tmp) / "d.db"
            plain.write_bytes(gzip.decompress(self.gz(DAY28).read_bytes()))
            conn = sqlite3.connect(plain)
            kinds = conn.execute("SELECT typeof(topics), typeof(server_ts_s), typeof(receive_time_ns), "
                                 "typeof(filtered), body FROM market_message WHERE id = 3").fetchone()
            conn.close()
        self.assertEqual(kinds, ("null", "null", "integer", "integer", BODY_UNICODE))

    def test_sidecar_is_sha256sum_format_and_matches_the_file(self):
        self.archiver(D01 + 1800).archive()
        for day in (DAY28, DAY29, DAY30):
            sidecar = self.dir / f"stream-{day.isoformat()}.sqlite.gz.sha256"
            self.assertEqual(sidecar.read_text(),
                             f"{sha256_file(self.gz(day))}  stream-{day.isoformat()}.sqlite.gz\n")

    def test_grace_period_holds_back_a_day_that_just_ended(self):
        archived = self.archiver(D01 + 300, grace_s=600).archive()  # 00:05 UTC
        self.assertEqual(archived, [DAY28, DAY29])
        self.assertFalse(self.gz(DAY30).exists())

    def test_rerun_is_idempotent(self):
        self.archiver(D01 + 1800).archive()
        before = {p.name: (p.stat().st_mtime_ns, sha256_file(p)) for p in self.dir.iterdir() if p.is_file()}
        self.assertEqual(self.archiver(D01 + 3600).archive(), [])
        after = {p.name: (p.stat().st_mtime_ns, sha256_file(p)) for p in self.dir.iterdir() if p.is_file()}
        self.assertEqual(before, after)

    def test_crash_before_publish_leaves_no_archive_and_the_rerun_completes(self):
        def crash(day):
            if day == DAY29:
                raise RuntimeError("power cut")
        archiver = mda.Archiver(self.db, self.dir, clock_ns=at(D01 + 1800), sleep=lambda _s: None,
                                hooks={"before_publish": crash})
        with self.assertRaises(RuntimeError):
            archiver.archive()
        self.assertTrue(self.gz(DAY28).exists())
        self.assertFalse(self.gz(DAY29).exists())
        self.assertFalse((self.dir / "stream-2026-09-29.sqlite.gz.sha256").exists())
        self.assertEqual(self.archiver(D01 + 1800).archive(), [DAY29, DAY30])
        self.assertEqual([r[0] for r in read_archive(self.gz(DAY29), "market_message", "id")], [4, 5])
        leftovers = [p.name for p in self.dir.rglob("*") if p.name.startswith(".tmp") or p.name.endswith(".tmp")]
        self.assertEqual(leftovers, [])

    def test_a_copy_that_differs_from_the_snapshot_is_never_published(self):
        def tamper(day, scratch):
            if day == DAY29:
                conn = sqlite3.connect(scratch)
                conn.execute("UPDATE market_message SET body = 'changed' WHERE id = 5")
                conn.commit()
                conn.close()
        archiver = mda.Archiver(self.db, self.dir, clock_ns=at(D01 + 1800), sleep=lambda _s: None,
                                hooks={"before_verify": tamper})
        with self.assertRaisesRegex(mda.ArchiveError, "differs"):
            archiver.archive()
        self.assertFalse(self.gz(DAY29).exists())
        self.assertFalse((self.dir / "stream-2026-09-29.sqlite.gz.sha256").exists())

    def test_archive_without_sidecar_is_redone_not_trusted(self):
        self.archiver(D01 + 1800).archive()
        (self.dir / "stream-2026-09-28.sqlite.gz.sha256").unlink()
        self.gz(DAY28).write_bytes(b"half written")
        self.assertEqual(self.archiver(D01 + 1800).archive(), [DAY28])
        self.assertEqual([r[0] for r in read_archive(self.gz(DAY28), "market_message", "id")], [1, 2, 3])

    def test_live_db_is_not_modified_by_archiving(self):
        self.archiver(D01 + 1800).archive()
        self.assertEqual(db_ids(self.db), [1, 2, 3, 4, 5, 6, 7, 8])

    def test_max_days_bounds_one_run(self):
        self.assertEqual(self.archiver(D01 + 1800, max_days=2).archive(), [DAY28, DAY29])
        self.assertEqual(self.archiver(D01 + 1800, max_days=2).archive(), [DAY30])

    def test_reads_use_the_type_time_index_never_a_table_scan_or_sort(self):
        statements: list[str] = []
        archiver = mda.Archiver(self.db, self.dir, clock_ns=at(D01 + 1800), sleep=lambda _s: None,
                                policy=mda.Policy(retention_days=0), trace=statements.append)
        archiver.archive()
        for day in (DAY28, DAY29, DAY30):
            self.ack(day)
        archiver.prune()
        checked = 0
        conn = sqlite3.connect(self.db)
        for sql in statements:
            if "market_message" not in sql or "arc." in sql or not sql.lstrip().upper().startswith(("SELECT", "DELETE")):
                continue
            plan = " | ".join(row[-1] for row in conn.execute("EXPLAIN QUERY PLAN " + sql))
            self.assertNotRegex(plan, r"SCAN market_message(?! USING COVERING INDEX)", sql)
            self.assertNotIn("TEMP B-TREE", plan, sql)
            checked += 1
        conn.close()
        self.assertGreater(checked, 5)


class Snapshot(Fixture):
    def test_a_row_written_after_the_snapshot_is_not_archived_and_blocks_the_prune(self):
        late = (9, 2, "reference_prices", None, None, D28 * S + 5 * S, '{"late":9}', "b9", "f9", 0)

        def write_late(day):
            if day == DAY28:
                other = sqlite3.connect(self.db, timeout=1)
                other.execute("INSERT INTO market_message VALUES (?,?,?,?,?,?,?,?,?,?)", late)
                other.commit()
                other.close()
        mda.Archiver(self.db, self.dir, clock_ns=at(D01 + 1800), sleep=lambda _s: None,
                     hooks={"after_snapshot": write_late}).archive()
        self.assertEqual([r[0] for r in read_archive(self.gz(DAY28), "market_message", "id")], [1, 2, 3])
        self.ack(DAY28)
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY28], "late_rows")
        self.assertEqual(db_ids(self.db), [1, 2, 3, 4, 5, 6, 7, 8, 9])


class PruneGate(Fixture):
    """Each test satisfies every condition but one; only that one may hold the rows."""

    def archived_and_acked(self) -> None:
        self.archiver(D01 + 1800).archive()
        for day in (DAY28, DAY29, DAY30):
            self.ack(day)

    def test_all_conditions_met_deletes_exactly_the_archived_rows(self):
        self.archived_and_acked()
        # 10-02 00:30 with 3 retained days: only 09-28 is older than the window.
        report = self.archiver(D02 + 1800, retention_days=3).prune()
        self.assertEqual(report[DAY28], "pruned")
        self.assertEqual(db_ids(self.db), [4, 5, 6, 7, 8])
        marker = self.dir / "pruned" / "stream-2026-09-28.pruned"
        self.assertTrue(marker.exists())

    def test_gate_a_corrupt_archive_never_prunes(self):
        self.archived_and_acked()
        data = bytearray(self.gz(DAY28).read_bytes())
        data[len(data) // 2] ^= 0x01
        self.gz(DAY28).write_bytes(bytes(data))  # ack and sidecar still name the original
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY28], "archive_unverified")
        self.assertEqual(db_ids(self.db), [1, 2, 3, 8])  # 29 and 30 pruned, 28 kept

    def test_gate_a_missing_archive_never_prunes(self):
        self.archived_and_acked()
        self.gz(DAY29).unlink()
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY29], "archive_unverified")
        self.assertEqual(db_ids(self.db), [4, 5, 8])

    def test_gate_a_manifest_for_other_bytes_never_prunes(self):
        # The manifest's id range drives the delete; it must belong to this archive.
        self.archived_and_acked()
        manifest = self.dir / "stream-2026-09-28.manifest.json"
        record = json.loads(manifest.read_text())
        record.update(sha256="2" * 64, max_id=7)
        manifest.write_text(json.dumps(record))
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY28], "archive_unverified")
        self.assertEqual(db_ids(self.db), [1, 2, 3, 8])

    def test_gate_b_missing_ack_never_prunes(self):
        self.archiver(D01 + 1800).archive()
        self.ack(DAY28)
        self.ack(DAY30)
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY29], "awaiting_ack")
        self.assertEqual(db_ids(self.db), [4, 5, 8])

    def test_gate_b_ack_for_other_bytes_never_prunes(self):
        self.archived_and_acked()
        self.ack(DAY28, sha="0" * 64)
        report = self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(report[DAY28], "ack_mismatch")
        self.assertEqual(db_ids(self.db), [1, 2, 3, 8])

    def test_gate_c_retention_window_keeps_recent_days(self):
        self.archived_and_acked()
        # 10-02: 28 is 4 days old, 29 is 3, 30 is 2. Window 3 keeps 29 and 30.
        report = self.archiver(D02 + 1800, retention_days=3).prune()
        self.assertEqual((report[DAY29], report[DAY30]), ("retention", "retention"))
        self.assertEqual(db_ids(self.db), [4, 5, 6, 7, 8])
        # Window 0 at the same instant releases both.
        self.archiver(D02 + 1800, retention_days=0).prune()
        self.assertEqual(db_ids(self.db), [8])

    def test_prune_never_touches_today(self):
        self.archived_and_acked()
        self.archiver(D01 + 1800, retention_days=0).prune()
        self.assertEqual(db_ids(self.db), [8])

    def test_a_late_row_during_the_batches_survives(self):
        self.archived_and_acked()
        late = (20, 2, "reference_prices", None, None, D28 * S + 60 * S, '{"late":20}', "b20", "f20", 0)

        def insert_late(day, batch):
            if day == DAY28 and batch == 1:
                other = sqlite3.connect(self.db, timeout=1)
                other.execute("INSERT INTO market_message VALUES (?,?,?,?,?,?,?,?,?,?)", late)
                other.commit()
                other.close()
        mda.Archiver(self.db, self.dir, clock_ns=at(D02 + 1800), sleep=lambda _s: None,
                     policy=mda.Policy(retention_days=0, batch_rows=1),
                     hooks={"after_batch": insert_late}).prune()
        self.assertIn(20, db_ids(self.db), "a row the archive never saw was deleted")
        self.assertNotIn(1, db_ids(self.db))

    def test_prune_resumes_after_a_crash_between_batches(self):
        self.archived_and_acked()

        def crash(day, batch):
            if day == DAY28 and batch == 1:
                raise RuntimeError("killed")
        with self.assertRaises(RuntimeError):
            mda.Archiver(self.db, self.dir, clock_ns=at(D02 + 1800), sleep=lambda _s: None,
                         policy=mda.Policy(retention_days=3, batch_rows=2),
                         hooks={"after_batch": crash}).prune()
        remaining = db_ids(self.db)  # the first batch of two committed, the second never ran
        self.assertEqual((len([i for i in remaining if i <= 3]), [i for i in remaining if i > 3]), (1, [4, 5, 6, 7, 8]))
        self.assertFalse((self.dir / "pruned" / "stream-2026-09-28.pruned").exists())
        report = self.archiver(D02 + 1800, retention_days=3, batch_rows=2).prune()
        self.assertEqual(report[DAY28], "pruned")
        self.assertEqual(db_ids(self.db), [4, 5, 6, 7, 8])

    def test_batches_are_bounded(self):
        self.archived_and_acked()
        batches: list[tuple[date, int]] = []
        mda.Archiver(self.db, self.dir, clock_ns=at(D02 + 1800), sleep=lambda _s: None,
                     policy=mda.Policy(retention_days=0, batch_rows=2),
                     hooks={"after_batch": lambda d, b: batches.append((d, b))}).prune()
        # 28: 3 rows -> batches of 2,1; 29: 2 -> 2; 30: 2 -> 2.
        self.assertEqual(batches, [(DAY28, 1), (DAY28, 2), (DAY29, 1), (DAY30, 1)])


class ArchiveRemoval(Fixture):
    def setUp(self) -> None:
        super().setUp()
        self.archiver(D01 + 1800).archive()

    def present(self) -> list[str]:
        return sorted(p.name for p in self.dir.glob("stream-*.sqlite.gz"))

    def test_old_archive_removed_only_after_ack_and_prune(self):
        self.ack(DAY28)
        # 10-06: 28 is 8 days old (> 7), 29 is 7, 30 is 6.
        now = D01 + 5 * 86_400 + 1800
        archiver = self.archiver(now, retention_days=3, keep_archives_days=7)
        archiver.prune()
        self.assertEqual(archiver.remove_old_archives(), [DAY28])
        self.assertEqual(self.present(), ["stream-2026-09-29.sqlite.gz", "stream-2026-09-30.sqlite.gz"])
        self.assertTrue((self.dir / "acked" / "stream-2026-09-28.ack").exists(), "the ack is the record")

    def test_unacked_old_archive_is_kept(self):
        now = D01 + 30 * 86_400
        archiver = self.archiver(now, retention_days=0, keep_archives_days=0)
        archiver.prune()
        self.assertEqual(archiver.remove_old_archives(), [])
        self.assertEqual(len(self.present()), 3)

    def test_pruned_archive_whose_ack_is_gone_is_kept(self):
        self.ack(DAY28)
        archiver = self.archiver(D01 + 30 * 86_400, retention_days=0, keep_archives_days=0)
        archiver.prune()
        self.ack_path_of(DAY28).unlink()  # lost on nuc1; the Mac re-acknowledges on its next run
        self.assertEqual(archiver.remove_old_archives(), [])
        self.ack(DAY28, sha="3" * 64)  # an ack for other bytes is no better
        self.assertEqual(archiver.remove_old_archives(), [])
        self.assertTrue(self.gz(DAY28).exists())

    def ack_path_of(self, day):
        return self.dir / "acked" / f"stream-{day.isoformat()}.ack"

    def test_acked_but_unpruned_archive_is_kept(self):
        self.ack(DAY28)
        self.assertEqual(self.archiver(D01 + 30 * 86_400, keep_archives_days=0).remove_old_archives(), [])
        self.assertTrue(self.gz(DAY28).exists())

    def test_removed_day_is_not_archived_again(self):
        for day in (DAY28, DAY29, DAY30):
            self.ack(day)
        archiver = self.archiver(D01 + 30 * 86_400, retention_days=0, keep_archives_days=0)
        archiver.prune()
        self.assertEqual(len(archiver.remove_old_archives()), 3)
        self.assertEqual(archiver.archive(), [DAY01])  # only the day that has since completed
        self.assertEqual(self.present(), ["stream-2026-10-01.sqlite.gz"])


class Metrics(Fixture):
    def test_metrics_after_a_partial_cycle(self):
        archiver = self.archiver(D01 + 1800)
        archiver.archive()
        self.ack(DAY28)
        now = D02 + 1800
        archiver = self.archiver(now, retention_days=3)
        archiver.prune()
        text = archiver.metrics(ok=True)
        conn = sqlite3.connect(self.db)
        page_size = conn.execute("PRAGMA page_size").fetchone()[0]
        free = conn.execute("PRAGMA freelist_count").fetchone()[0] * page_size
        conn.close()
        expected = {
            "velocita_md_archive_last_run_ok": "1",
            "velocita_md_archive_last_run_timestamp_seconds": str(now),
            "velocita_md_archive_last_day_timestamp_seconds": str(D30),
            'velocita_md_archive_days{state="on_host"}': "3",
            'velocita_md_archive_days{state="acked"}': "1",
            'velocita_md_archive_days{state="pruned"}': "1",
            # Oldest archive without a matching ack is 09-29, which ended at D30.
            "velocita_md_archive_oldest_unacked_day_end_timestamp_seconds": str(D30),
            "velocita_md_archive_oldest_unacked_age_seconds": str(now - D30),
            'velocita_md_archive_blocked_days{reason="archive_unverified"}': "0",
            'velocita_md_archive_blocked_days{reason="ack_mismatch"}': "0",
            'velocita_md_archive_blocked_days{reason="late_rows"}': "0",
            "velocita_md_stream_db_bytes": str(os.path.getsize(self.db)),
            "velocita_md_stream_db_free_bytes": str(free),
        }
        lines = dict(line.rsplit(" ", 1) for line in text.splitlines() if not line.startswith("#"))
        self.assertEqual(lines, expected)

    def test_nothing_unacked_reports_zero_not_absence(self):
        text = self.archiver(D01 + 1800).metrics(ok=False)
        self.assertIn("velocita_md_archive_oldest_unacked_day_end_timestamp_seconds 0\n", text)
        self.assertIn("velocita_md_archive_last_run_ok 0\n", text)
        self.assertIn("velocita_md_archive_last_day_timestamp_seconds 0\n", text)


class Restore(Fixture):
    def test_restore_merges_days_into_a_research_db(self):
        self.archiver(D01 + 1800).archive()
        target = self.root / "research.db"
        archives = [self.gz(DAY28), self.gz(DAY30), self.gz(DAY28)]  # a repeat is harmless
        mda.restore(archives, target)
        conn = sqlite3.connect(target)
        rows = conn.execute("SELECT * FROM market_message ORDER BY id").fetchall()
        sessions = conn.execute("SELECT * FROM collector_session ORDER BY session_id").fetchall()
        conn.close()
        by_id = {m[0]: m for m in MESSAGES}
        self.assertEqual(rows, [by_id[i] for i in (1, 2, 3, 6, 7)])
        self.assertEqual(sessions, SESSIONS)

    def test_restore_refuses_an_archive_that_fails_its_sidecar(self):
        self.archiver(D01 + 1800).archive()
        data = bytearray(self.gz(DAY29).read_bytes())
        data[-12] ^= 0xFF
        self.gz(DAY29).write_bytes(bytes(data))
        target = self.root / "research.db"
        with self.assertRaises(mda.ArchiveError):
            mda.restore([self.gz(DAY29)], target)


class Compact(Fixture):
    def test_refuses_while_the_recorder_is_writing(self):
        # Newest row (id 8) was received at D01+1 s; 60 s later is "recently".
        with self.assertRaises(mda.ArchiveError):
            self.archiver(D01 + 61).compact(quiet_s=120)

    def test_refuses_while_another_connection_holds_the_db(self):
        other = sqlite3.connect(self.db)  # the recorder keeps one open even when idle
        other.execute("SELECT count(*) FROM market_message").fetchone()
        try:
            with self.assertRaisesRegex(mda.ArchiveError, "holds"):
                self.archiver(D02 + 1800).compact(quiet_s=120)
            mode = other.execute("PRAGMA journal_mode").fetchone()[0]
        finally:
            other.close()
        self.assertEqual(mode, "wal")
        self.assertEqual(db_ids(self.db), [1, 2, 3, 4, 5, 6, 7, 8])

    def test_compacts_and_enables_live_incremental_reclaim(self):
        big = [(100 + i, 1, "price_candles", None, None, D28 * S + i, "x" * 3000, "b", "f", 1) for i in range(400)]
        conn = sqlite3.connect(self.db)
        conn.executemany("INSERT INTO market_message VALUES (?,?,?,?,?,?,?,?,?,?)", big)
        conn.commit()
        conn.close()
        archiver = self.archiver(D01 + 1800)
        archiver.archive()
        for day in (DAY28, DAY29, DAY30):
            self.ack(day)
        self.archiver(D02 + 1800, retention_days=0).prune()
        before = os.path.getsize(self.db)
        self.archiver(D02 + 1800).compact(quiet_s=120)
        conn = sqlite3.connect(self.db)
        self.assertEqual(conn.execute("PRAGMA auto_vacuum").fetchone()[0], 2)  # INCREMENTAL
        self.assertEqual(conn.execute("PRAGMA integrity_check").fetchone()[0], "ok")
        self.assertEqual(conn.execute("PRAGMA journal_mode").fetchone()[0], "wal")
        self.assertEqual([r[0] for r in conn.execute("SELECT id FROM market_message")], [8])
        conn.close()
        self.assertLess(os.path.getsize(self.db), before // 4)


class IncrementalReclaim(unittest.TestCase):
    def test_prune_shrinks_a_db_already_in_incremental_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            db = root / "stream.db"
            conn = sqlite3.connect(db)
            conn.execute("PRAGMA auto_vacuum = INCREMENTAL")  # must precede the first table
            conn.executescript(pathlib.Path(mda.__file__).with_name("schema.sql").read_text())
            conn.close()
            big = [(100 + i, 1, "price_candles", None, None, D28 * S + i, "y" * 3000, "b", "f", 1) for i in range(400)]
            build_db(db, MESSAGES + big)
            archiver = mda.Archiver(db, root / "archive", clock_ns=at(D01 + 1800), sleep=lambda _s: None,
                                    policy=mda.Policy(retention_days=0))
            archiver.archive()
            (root / "archive" / "acked").mkdir(exist_ok=True)
            gz = root / "archive" / "stream-2026-09-28.sqlite.gz"
            (root / "archive" / "acked" / "stream-2026-09-28.ack").write_text(sha256_file(gz) + "\n")
            pages = lambda: sqlite3.connect(db).execute("PRAGMA page_count").fetchone()[0]
            before = pages()
            archiver.prune()
            conn = sqlite3.connect(db)
            conn.execute("PRAGMA wal_checkpoint(TRUNCATE)")
            conn.close()
            self.assertLess(pages(), before // 4)
            self.assertEqual(sqlite3.connect(db).execute("PRAGMA freelist_count").fetchone()[0], 0)


class CommandLine(Fixture):
    """The `md-archive` subcommand on the real clock: the fixture's days are all
    complete by any date this test can run on."""

    def run_cli(self, *extra: str) -> tuple[int, str]:
        import contextlib
        import io

        from velocita_model import cli
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = cli.main(["md-archive", "--db", str(self.db), "--archive-dir", str(self.dir),
                             "--textfile", str(self.root / "md-archive.prom"), *extra])
        return code, out.getvalue()

    def test_archive_then_prune_after_acks(self):
        code, out = self.run_cli("--prune", "--retention-days", "0")
        self.assertEqual(code, 0, out)
        for day in (DAY28, DAY29, DAY30):
            self.assertTrue(self.gz(day).exists())
        self.assertEqual(db_ids(self.db), [1, 2, 3, 4, 5, 6, 7, 8], "nothing acknowledged yet")
        self.assertIn("velocita_md_archive_last_run_ok 1\n", (self.root / "md-archive.prom").read_text())
        for day in (DAY28, DAY29, DAY30):
            self.ack(day)
        code, out = self.run_cli("--prune", "--retention-days", "0")
        self.assertEqual(code, 0, out)
        self.assertEqual([i for i in db_ids(self.db) if i <= 7], [])

    def test_blocked_day_exits_3_and_is_counted(self):
        self.run_cli()
        self.ack(DAY28, sha="1" * 64)
        code, out = self.run_cli("--prune", "--retention-days", "0")
        self.assertEqual(code, 3, out)
        self.assertIn('velocita_md_archive_blocked_days{reason="ack_mismatch"} 1\n',
                      (self.root / "md-archive.prom").read_text())
        self.assertIn(1, db_ids(self.db))

    def test_missing_db_fails_and_says_so_in_metrics(self):
        self.db.unlink()
        code, out = self.run_cli()
        self.assertEqual(code, 1)
        self.assertIn("does not exist", out)
        self.assertFalse(self.db.exists(), "a wrong path must never create an empty database")
        self.assertIn("velocita_md_archive_last_run_ok 0\n", (self.root / "md-archive.prom").read_text())


WRITER = r"""
import json, sqlite3, sys, time, os
db, stop, start_id, base_ns = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
conn = sqlite3.connect(db, isolation_level=None, timeout=30.0)   # as md-stream opens it
worst, errors, n = 0.0, [], 0
while not os.path.exists(stop):
    t = time.monotonic()
    try:
        conn.execute("INSERT INTO market_message VALUES (?,?,?,?,?,?,?,?,?,?)",
                     (start_id + n, 2, "reference_prices", None, None, base_ns + n, "w", "b", "f", 0))
        n += 1
    except sqlite3.Error as e:
        errors.append(str(e))
    worst = max(worst, time.monotonic() - t)
    time.sleep(0.001)
print(json.dumps({"n": n, "worst": worst, "errors": errors}))
"""


class ConcurrentWriter(unittest.TestCase):
    def test_archive_and_prune_while_the_recorder_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            db = root / "stream.db"
            bulk = [(1000 + i, 1, "price_candles", None, None, D28 * S + i * 1000, f"bulk {i}", "b", "f", 1)
                    for i in range(20_000)]
            build_db(db, MESSAGES + bulk)
            stop = root / "stop"
            writer = subprocess.Popen([sys.executable, "-c", WRITER, str(db), str(stop), "100000",
                                       str(D02 * S + 600 * S)], stdout=subprocess.PIPE, text=True)
            try:
                time.sleep(0.3)
                archiver = mda.Archiver(db, root / "archive", clock_ns=at(D02 + 1800), sleep=time.sleep,
                                        policy=mda.Policy(retention_days=0, batch_rows=500))
                self.assertEqual(archiver.archive(), [DAY28, DAY29, DAY30, DAY01])
                (root / "archive" / "acked").mkdir(exist_ok=True)
                for day in (DAY28, DAY29, DAY30):
                    gz = root / "archive" / f"stream-{day.isoformat()}.sqlite.gz"
                    (root / "archive" / "acked" / f"stream-{day.isoformat()}.ack").write_text(sha256_file(gz))
                report = archiver.prune()
                time.sleep(0.2)
            finally:
                stop.write_text("")
                out = json.loads(writer.communicate(timeout=60)[0])
            self.assertEqual(out["errors"], [])
            self.assertLess(out["worst"], 2.0, "the recorder was blocked")
            self.assertGreater(out["n"], 10)
            self.assertEqual((report[DAY28], report[DAY29], report[DAY30]), ("pruned",) * 3)
            day28 = read_archive(root / "archive" / "stream-2026-09-28.sqlite.gz", "market_message", "id")
            by_id = {m[0]: m for m in MESSAGES}
            self.assertEqual(day28, [by_id[1], by_id[2], by_id[3]] + bulk)
            remaining = db_ids(db)
            # Day 01 (row 8) was archived but not acked; the writer's rows are "today" (10-02).
            self.assertEqual(remaining, [8] + list(range(100_000, 100_000 + out["n"])))


if __name__ == "__main__":
    unittest.main()
