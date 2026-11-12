#!/usr/bin/env python3
"""The feed-quality report (tools/tape-report.py) on scripted segments whose answers are worked out by hand."""
import gzip
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tape_oracles import encode as E  # noqa: E402
from tape_oracles import merkle as M  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "tools/tape-report.py"
VERIFY = os.environ.get("TAPE_VERIFY")
LOG_APPLY = os.environ.get("TAPE_LOG_APPLY")
KEYS = os.environ.get("TAPE_KEYS")
SYM = "NVDAC-USDC"
SEC = 1_000_000_000
MS = 1_000_000
UNKNOWN = 0xFFFFFFFF
HEAD_EVERY = 2


def find_openssl():
    for candidate in (os.environ.get("TAPE_OPENSSL"), "/opt/homebrew/bin/openssl", "/usr/local/opt/openssl@3/bin/openssl",
                      "/usr/bin/openssl", "openssl"):
        if not candidate:
            continue
        try:
            out = subprocess.run([candidate, "version"], capture_output=True, text=True)
        except OSError:
            continue
        if out.returncode == 0 and out.stdout.startswith("OpenSSL 3"):
            return candidate
    return None


OPENSSL = find_openssl()
if VERIFY and not OPENSSL:
    raise SystemExit("test_tape_report: the tape binaries are set but no OpenSSL 3 command was found to sign test heads")


def wal_bytes(entries):
    out = bytearray()
    for i, e in enumerate(entries, 1):
        header = struct.pack("<IBQQiI", 0x4C415754, 1, i, 1, 0, len(e))
        out += header + e + struct.pack("<I", zlib.crc32(header + e))
    return bytes(out)


class Keys:
    def __init__(self, d):
        self.dir = d
        for v in (1, 2, 3):
            subprocess.run([KEYS, "head", str(d / f"head-{v}.pem"), str(d / f"head-{v}.pub.pem")], check=True)

    def public(self):
        return [self.dir / f"head-{v}.pub.pem" for v in (1, 2, 3)]

    def sign(self, voter, message):
        msg = self.dir / "msg.bin"
        msg.write_bytes(message)
        out = subprocess.run([OPENSSL, "pkeyutl", "-sign", "-rawin", "-inkey", str(self.dir / f"head-{voter}.pem"), "-in", str(msg)],
                             capture_output=True, check=True)
        return out.stdout.hex()


class Script:
    """A three-recorder log with explicit receive times and ticks."""

    def __init__(self, t0=1_794_513_600 * SEC):
        self.t0 = t0
        self.entries = []
        self.req = {1: 1, 2: 1, 3: 1}
        self.ticks = 0
        self.prefix = []
        for r in (1, 2, 3):
            self.entries.append(E.event(r, r * 11, 1, "session_start", utc=t0))
            self.req[r] = 2

    def obs(self, r, channel, kind, seq, payload, at, clock=UNKNOWN, symbol=SYM):
        self.entries.append(E.observation_body(r, r * 11, self.req[r], channel, kind, symbol, seq, payload, self.t0 + at, clock=clock))
        self.req[r] += 1

    def depth(self, recorders, kind, seq, at, bids=(), asks=(), clock=UNKNOWN, extra=b""):
        for i, r in enumerate(recorders):
            when = at[i] if isinstance(at, (list, tuple)) else at
            frame = E.depth_frame(seq, kind, SYM, bids, asks)
            self.obs(r, "DEPTH", kind, seq, frame + extra, when, clock[i] if isinstance(clock, (list, tuple)) else clock)

    def snapshot(self, seq, at, bids, asks):
        self.depth((1, 2, 3), "SNAPSHOT", seq, at, bids, asks)
        self.depth((1, 2, 3), "SNAPSHOT_COMPLETE", seq, at)

    def tick(self, at, count=1):
        for k in range(count):
            self.ticks += 1
            self.entries.append(E.tick(self.ticks, utc=self.t0 + at + k * SEC))


def write_segment(base, incarnation, entries, keys, signers=(1, 2), gz=True):
    seg = base / str(incarnation)
    (seg / "heads").mkdir(parents=True)
    data = wal_bytes(entries)
    if gz:
        (seg / "wal.log.gz").write_bytes(gzip.compress(data))
    else:
        (seg / "wal.log").write_bytes(data)
    log = base.parent / f"apply-{incarnation}.bin"
    E.write_log(log, entries)
    out = subprocess.run([LOG_APPLY, str(log), "--head-every", str(HEAD_EVERY)], capture_output=True, text=True, check=True).stdout
    rows = [json.loads(l) for l in out.splitlines()]
    leaves = [bytes.fromhex(r["leaf"]) for r in rows if "leaf" in r]
    tick = 0
    for i, r in enumerate(rows):
        if "leaf" not in r:
            continue
        entry = entries[r["index"] - 1]
        if entry[1] == 4 and r["code"] == 0:
            tick = struct.unpack_from("<Q", entry, 2)[0]
        if r.get("head") and signers:
            size = r["index"]
            root = M.root(leaves[:size])
            message = b"velocita-tape/v1" + struct.pack("<QQQ", incarnation, tick, size) + root
            sigs = {str(v): keys.sign(v, message) for v in signers}
            (seg / "heads" / f"head-{tick}.json").write_text(json.dumps(
                {"incarnation": incarnation, "tick": tick, "tree_size": size, "root": root.hex(), "signatures": sigs},
                separators=(",", ":")) + "\n")
    return seg


def last_head(seg):
    heads = sorted(seg.glob("heads/head-*.json"), key=lambda p: int(p.stem[5:]))
    return json.loads(heads[-1].read_text())


def report(base, keys, out):
    cmd = [sys.executable, "-B", str(REPORT), "--segments", str(base), "--verify", VERIFY, "--head-every", str(HEAD_EVERY),
           "--out", str(out)]
    for k in keys.public():
        cmd += ["--key", str(k)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise AssertionError(r.stderr)
    return json.loads((out / "summary.json").read_text())


def basic_script():
    s = Script()
    s.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
    s.tick(1 * SEC)
    s.depth((1, 2, 3), "UPDATE", 10, [1 * SEC + 100 * MS, 1 * SEC + 130 * MS, 1 * SEC + 160 * MS],
            bids=[("100.01", "1", "1")], clock=[2000, 5000, UNKNOWN])
    s.tick(2 * SEC)
    s.depth((1, 2), "UPDATE", 11, [3 * SEC, 3 * SEC + 10 * MS], bids=[("100.02", "1", "1")])
    s.tick(3 * SEC, count=6)
    s.depth((1,), "UPDATE", 12, 9 * SEC, bids=[("100.03", "1", "1")])
    s.tick(10 * SEC, count=6)
    return s


@unittest.skipUnless(VERIFY and LOG_APPLY and KEYS and OPENSSL, "requires the tape binaries and an OpenSSL 3 command")
class ReportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tape-report-test-"))
        (self.tmp / "keys").mkdir()
        self.keys = Keys(self.tmp / "keys")
        self.base = self.tmp / "segments"
        self.base.mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_counts_are_the_verdicts_and_reconcile(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys)
        s = report(self.base, self.keys, self.tmp / "out")
        u = s["completeness"]["updates"]
        self.assertEqual((u["3/3"], u["2/3"], u["1/3"], u["divergent"]), (1, 1, 1, 0))
        self.assertEqual(u["3/3"] + u["2/3"] + u["1/3"] + u["divergent"], u["closed"])
        b = s["books"][SYM]["updates"]
        self.assertEqual(b["3/3"] + b["2/3"] + b["1/3"] + b["divergent"], b["closed"])

    def test_divergent_copies_are_counted(self):
        sc = Script()
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        sc.tick(SEC)
        sc.depth((1, 2), "UPDATE", 10, 2 * SEC, bids=[("100.01", "1", "1")])
        sc.depth((3,), "UPDATE", 10, 2 * SEC, bids=[("100.05", "1", "1")])
        sc.tick(3 * SEC, count=8)
        write_segment(self.base, 1000, sc.entries, self.keys)
        s = report(self.base, self.keys, self.tmp / "out")
        self.assertEqual(s["completeness"]["updates"]["divergent"], 1)

    def test_spread_uses_only_updates_seen_twice(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys)
        t = report(self.base, self.keys, self.tmp / "out")["timeliness"]
        self.assertEqual(t["arrival_spread_ms"]["updates"], 2)
        self.assertEqual(t["arrival_spread_ms"]["max"], 60)

    def test_clock_bound_and_unknown_hosts(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys)
        t = report(self.base, self.keys, self.tmp / "out")["timeliness"]
        self.assertEqual(t["clock_error_bound_ms"], 5.0)
        self.assertTrue(t["clock_error_unknown_on_some_hosts"])

    def test_cadence_and_silences(self):
        sc = Script()
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        updates = dict(zip((5, 1005, 2105, 35005), (10, 11, 12, 13)))
        for ms in range(0, 42000, 5):
            if ms in updates:
                seq = updates[ms]
                sc.depth((1, 2, 3), "UPDATE", seq, [ms * MS] * 3, bids=[(f"100.0{seq - 9}", "1", "1")])
            if ms % 1000 == 500:
                sc.tick(ms * MS)
        write_segment(self.base, 1000, sc.entries, self.keys)
        b = report(self.base, self.keys, self.tmp / "out")["books"][SYM]
        self.assertEqual(b["cadence_ms"]["longest"], 32900)
        self.assertEqual(b["silences_over_30s"], 1)
        self.assertEqual(b["cadence_ms"]["p50"], 1100)

    def test_a_silence_across_a_tape_outage_is_not_counted(self):
        sc = Script()
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        sc.depth((1, 2, 3), "UPDATE", 10, [SEC] * 3, bids=[("100.01", "1", "1")])
        sc.tick(SEC + 10 * MS, count=6)
        sc.depth((1, 2, 3), "UPDATE", 11, [300 * SEC] * 3, bids=[("100.02", "1", "1")])
        sc.tick(300 * SEC + 10 * MS, count=6)
        write_segment(self.base, 1000, sc.entries, self.keys)
        s = report(self.base, self.keys, self.tmp / "out")
        self.assertTrue(any(g["seconds"] > 250 for g in s["coverage"]["gaps"]))
        self.assertEqual(s["books"][SYM]["silences_over_30s"], 0)
        self.assertIsNone(s["books"][SYM]["cadence_ms"]["longest"])

    def test_gap_between_segments_is_not_counted_as_recorded(self):
        a = write_segment(self.base, 1000, basic_script().entries, self.keys)
        s2 = Script(t0=1_794_513_600 * SEC + 16 * SEC + 600 * SEC)
        s2.entries.insert(0, E.segment_start(1000, last_head(a)["tick"], last_head(a)["tree_size"], bytes.fromhex(last_head(a)["root"])))
        s2.snapshot(20, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        s2.tick(0, count=6)
        write_segment(self.base, 1001, s2.entries, self.keys)
        s = report(self.base, self.keys, self.tmp / "out")
        self.assertEqual(s["coverage"]["segments_used"], 2)
        self.assertTrue(any(g["seconds"] > 590 for g in s["coverage"]["gaps"]))
        self.assertLess(s["coverage"]["recorded_hours"] * 3600, 30)

    def test_segments_are_ordered_by_time_not_by_incarnation_number(self):
        a = write_segment(self.base, 68934737119228, basic_script().entries, self.keys)
        s2 = Script(t0=1_794_513_600 * SEC + 30 * SEC)
        h = last_head(a)
        s2.entries.insert(0, E.segment_start(68934737119228, h["tick"], h["tree_size"], bytes.fromhex(h["root"])))
        s2.snapshot(20, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        s2.tick(0, count=6)
        write_segment(self.base, 1794513600, s2.entries, self.keys)
        seg = report(self.base, self.keys, self.tmp / "out")["segments"]
        self.assertEqual([(x["incarnation"], x["status"]) for x in seg], [(68934737119228, "used"), (1794513600, "used")])
        self.assertEqual(seg[1]["linked_to"], 68934737119228)

    def test_link_to_the_wrong_head_excludes_the_segment(self):
        a = write_segment(self.base, 1000, basic_script().entries, self.keys)
        s2 = Script(t0=1_794_513_600 * SEC + 20 * SEC)
        s2.entries.insert(0, E.segment_start(1000, last_head(a)["tick"], last_head(a)["tree_size"], bytes(32)))
        s2.tick(0, count=6)
        write_segment(self.base, 1001, s2.entries, self.keys)
        seg = report(self.base, self.keys, self.tmp / "out")["segments"]
        self.assertEqual([(x["incarnation"], x["status"], x.get("reason")) for x in seg],
                         [(1000, "used", None), (1001, "excluded", "link")])

    def test_a_damaged_log_is_excluded_and_others_still_count(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys)
        bad = write_segment(self.base, 1001, basic_script().entries, self.keys)
        raw = bytearray(gzip.decompress((bad / "wal.log.gz").read_bytes()))
        raw[200] ^= 0xFF
        (bad / "wal.log.gz").write_bytes(gzip.compress(bytes(raw)))
        s = report(self.base, self.keys, self.tmp / "out")
        self.assertEqual(s["segments"][1]["status"], "excluded")
        self.assertTrue(s["segments"][1]["reason"].startswith("verify failed"))
        self.assertEqual(s["completeness"]["updates"]["closed"], 3)

    def test_a_segment_without_a_signed_head_is_excluded(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys, signers=())
        seg = report(self.base, self.keys, self.tmp / "out")["segments"]
        self.assertEqual((seg[0]["status"], seg[0]["reason"]), ("excluded", "no signed head"))

    def test_a_head_with_one_signature_is_not_enough(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys, signers=(1,))
        seg = report(self.base, self.keys, self.tmp / "out")["segments"]
        self.assertEqual(seg[0]["status"], "excluded")

    def test_updates_and_recording_are_broken_down_by_hour(self):
        boundary = 1_794_517_200 * SEC
        sc = Script(t0=boundary - 3 * SEC)
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        sc.depth((1, 2, 3), "UPDATE", 10, [SEC] * 3, bids=[("100.01", "1", "1")])
        sc.tick(SEC + 500 * MS)
        sc.depth((1, 2), "UPDATE", 11, [2 * SEC] * 2, bids=[("100.02", "1", "1")])
        sc.tick(2 * SEC + 500 * MS, count=8)
        sc.depth((1, 2, 3), "UPDATE", 12, [11 * SEC, 11 * SEC + 40 * MS, 11 * SEC + 80 * MS], bids=[("100.03", "1", "1")])
        sc.tick(11 * SEC + 500 * MS, count=2)
        write_segment(self.base, 1000, sc.entries, self.keys)
        periods = {p["hour_utc"]: p for p in report(self.base, self.keys, self.tmp / "out")["periods"]}
        before, after = periods["2026-11-12T20:00:00Z"], periods["2026-11-12T21:00:00Z"]
        self.assertEqual((before["updates"]["3/3"], before["updates"]["2/3"]), (1, 1))
        self.assertEqual((after["updates"]["3/3"], after["updates"]["closed"]), (1, 1))
        self.assertEqual(after["arrival_spread_ms"]["p50"], 80)
        self.assertAlmostEqual(before["recorded_minutes"] + after["recorded_minutes"], 10 / 60, places=1)

    def test_trades_are_checked_against_the_book(self):
        def trade(seq, side, price, qty):
            return json.dumps({"seqnum": str(seq), "channel": "TRADE", "update": "UPDATE",
                               "data": {"match_id": str(seq), "taking_side": side, "symbol": SYM,
                                        "info": {"price": price, "qty": qty, "timestamp": "1"}}}, separators=(",", ":")).encode()
        sc = Script()
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        events = {
            2000: ("trade", (1, 2, 3), 1, "BUY", "100.10", "1"),
            2500: ("depth", 10, [], [("100.10", "2", "1")]),
            4600: ("depth", 11, [("100.00", "1.5", "1")], []),
            5000: ("trade", (1,), 2, "SELL", "100.00", "0.5"),
            8000: ("trade", (1,), 3, "BUY", "100.10", "1"),
            14000: ("depth", 12, [], [("100.10", "1", "1")]),
            20000: ("trade", (1,), 4, "BUY", "100.10", "1"),
            25000: ("trade", (1,), 5, "SELL", "99.90", "0.1"),
        }
        for ms in range(0, 30000, 100):
            ev = events.get(ms)
            if ev and ev[0] == "trade":
                for r in ev[1]:
                    sc.obs(r, "TRADE", "UPDATE", ev[2], trade(ev[2], ev[3], ev[4], ev[5]), ms * MS + r * MS)
            elif ev:
                sc.depth((1, 2, 3), "UPDATE", ev[1], [ms * MS] * 3, bids=ev[2], asks=ev[3])
            if ms % 1000 == 500:
                sc.tick(ms * MS)
        write_segment(self.base, 1000, sc.entries, self.keys)
        s = report(self.base, self.keys, self.tmp / "out")
        t = s["books"][SYM]["trades"]
        self.assertEqual(t["count"], 5)
        self.assertEqual(t["position"], {"inside": 0, "touch": 4, "outside": 1, "no_book": 0})
        self.assertEqual(t["class"], {"consistent": 2, "late": 1, "unexplained": 2})
        self.assertEqual(t["accounted_delay_ms"]["p50"], 499)
        self.assertAlmostEqual(t["outside_bp"]["max"], 10.0, delta=0.02)
        self.assertEqual(s["trades"]["class"]["consistent"], 2)
        by_hour = Counter()
        for period in s["periods"]:
            by_hour.update(period["trades"])
        self.assertEqual(dict(by_hour), {"consistent": 2, "late": 1, "unexplained": 2})

    def test_the_summary_is_reproducible_and_cites_each_head(self):
        a = write_segment(self.base, 1000, basic_script().entries, self.keys)
        first = (report(self.base, self.keys, self.tmp / "out1"), (self.tmp / "out1/summary.json").read_bytes())
        report(self.base, self.keys, self.tmp / "out2")
        self.assertEqual(first[1], (self.tmp / "out2/summary.json").read_bytes())
        h = last_head(a)
        seg = first[0]["segments"][0]
        self.assertEqual((seg["last_head_tick"], seg["tree_size"], seg["root"]), (h["tick"], h["tree_size"], h["root"]))

    def test_no_payload_bytes_leave_the_report(self):
        sc = basic_script()
        sc.depth((1, 2, 3), "UPDATE", 13, [20 * SEC] * 3, bids=[("100.04", "1", "1")], extra=b" MARKER-7Q4Z")
        sc.tick(21 * SEC, count=6)
        write_segment(self.base, 1000, sc.entries, self.keys)
        report(self.base, self.keys, self.tmp / "out")
        for f in ("summary.json", "report.html"):
            self.assertNotIn("MARKER-7Q4Z", (self.tmp / "out" / f).read_text())

    def test_the_archive_is_not_modified(self):
        write_segment(self.base, 1000, basic_script().entries, self.keys)
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.base.rglob("*") if p.is_file()}
        report(self.base, self.keys, self.tmp / "out")
        after = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.base.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_memory_does_not_grow_with_segments(self):
        def peak(base):
            code = ("import resource,runpy,sys; sys.argv=sys.argv[1:]; "
                    "exec('try:\\n runpy.run_path(sys.argv[0], run_name=\"__main__\")\\nexcept SystemExit:\\n pass'); "
                    "print(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)")
            cmd = [sys.executable, "-B", "-c", code, str(REPORT), "--segments", str(base), "--verify", VERIFY,
                   "--head-every", str(HEAD_EVERY), "--out", str(self.tmp / f"out-{base.name}")]
            for k in self.keys.public():
                cmd += ["--key", str(k)]
            return int(subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines()[-1])
        one = self.tmp / "one"
        write_segment(one, 1000, basic_script().entries, self.keys)
        many = self.tmp / "many"
        prev = None
        for inc in (1000, 1001, 1002, 1003):
            sc = basic_script()
            if prev is not None:
                h = last_head(prev)
                sc.entries.insert(0, E.segment_start(inc - 1, h["tick"], h["tree_size"], bytes.fromhex(h["root"])))
            prev = write_segment(many, inc, sc.entries, self.keys)
        self.assertLess(peak(many), peak(one) * 1.3)

    def test_inconsistent_books_are_flagged(self):
        sc = Script()
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        sc.tick(SEC)
        sc.depth((1, 2, 3), "UPDATE", 10, [2 * SEC] * 3, bids=[("100.20", "1", "1")])
        sc.obs(1, "TRADE", "UPDATE", 5, E.trade_frame(5, SYM, "105.00", "1"), 3 * SEC)
        sc.tick(4 * SEC, count=8)
        write_segment(self.base, 1000, sc.entries, self.keys)
        f = report(self.base, self.keys, self.tmp / "out")["books"][SYM]["flags"]
        self.assertGreaterEqual(f["crossed"], 1)
        self.assertGreaterEqual(f["trade_outside_book"], 1)

    def test_spread_is_time_weighted_and_split_by_market_hours(self):
        market = 1_794_495_600 * SEC
        sc = Script(t0=market)
        sc.snapshot(10, 0, [("100.00", "2", "1")], [("100.10", "3", "1")])
        sc.tick(SEC)
        sc.depth((1,), "UPDATE", 10, 10 * SEC, asks=[("100.10", "0", "0"), ("100.20", "3", "1")])
        sc.depth((1,), "UPDATE", 11, 20 * SEC, bids=[("99.00", "1", "1")])
        sc.tick(21 * SEC, count=8)
        write_segment(self.base, 1000, sc.entries, self.keys)
        b = report(self.base, self.keys, self.tmp / "out")["books"][SYM]
        self.assertAlmostEqual(b["spread_bp"]["market_hours"]["mean"], 14.99, places=2)
        self.assertEqual(b["spread_bp"]["off_hours"]["seconds"], 0.0)
        self.assertEqual(b["top_size"]["bid_median"], 2.0)


if __name__ == "__main__":
    unittest.main()
