#!/usr/bin/env python3
"""Book-builder conformance kit tests: the reference builder, the harness, its scenarios and the known traps."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tape_oracles import encode as E  # noqa: E402
from tape_oracles.reference import ReferenceTape  # noqa: E402
from test_tape_report import HEAD_EVERY, KEYS, LOG_APPLY, MS, OPENSSL, SEC, VERIFY, Keys, Script, write_segment  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CONFORM = ROOT / "tools/book-kit/conform.py"
REFERENCE = f"{sys.executable} -B {ROOT / 'tools/book-kit/reference_book.py'}"
TRAPS = ROOT / "tests/book_kit_traps.py"
A, B = "NVDAC-USDC", "TSLAC-USDC"
MARKER = "BOOK-KIT-MARKER-51c2"


class Stream:
    def __init__(self):
        self.sc = Script()
        self.frames = []
        self.at = 0

    def send(self, seq, kind, symbol, bids=(), asks=(), note=None):
        frame = E.depth_frame(seq, kind, symbol, bids, asks)
        if note:
            frame = frame[:-1] + b',"note":"' + note.encode() + b'"}'
        self.at += 10 * MS
        self.sc.obs(1, "DEPTH", kind, seq, frame, self.at, symbol=symbol)
        self.frames.append(frame.decode())


def stream(note=None):
    s = Stream()
    s.send(10, "SNAPSHOT", A, bids=[("100.00", "2", "1"), ("99.90", "4", "2")])
    s.send(10, "SNAPSHOT", A, bids=[("99.80", "1.5", "3")], asks=[("100.10", "3", "1"), ("100.20", "0.035", "1")])
    s.send(10, "SNAPSHOT_COMPLETE", A)
    s.send(50, "SNAPSHOT", B, bids=[("250.5", "1", "1")], asks=[("251", "2", "2")])
    s.send(50, "SNAPSHOT_COMPLETE", B)
    s.send(10, "UPDATE", A, bids=[("100.00", "2.5", "2")], note=note)
    s.send(11, "UPDATE", A, asks=[("100.10", "0", "0")])
    s.send(50, "UPDATE", B, bids=[("250.6", "0.7", "1")])
    s.send(12, "UPDATE", A, bids=[("100.05", "1.001", "1")])
    s.send(13, "UPDATE", A, asks=[("100.15", "1.000000001", "1")])
    for k in range(14, 20):
        s.send(k, "UPDATE", A, bids=[("100.00", str(3 + k), "2")])
    s.send(51, "UPDATE", B, asks=[("251", "0", "0"), ("251.2", "1.1", "1")])
    s.send(20, "SNAPSHOT", A, bids=[("100.05", "1.001", "1"), ("100.00", "22", "2")])
    s.send(20, "SNAPSHOT", A, bids=[("99.90", "4", "2"), ("99.80", "1.5", "3")], asks=[("100.15", "1.000000001", "1"), ("100.20", "0.035", "1")])
    s.send(20, "SNAPSHOT_COMPLETE", A)
    for k in range(20, 25):
        s.send(k, "UPDATE", A, asks=[("100.30", f"0.{k}", "1")])
    s.sc.tick(s.at + SEC, count=8)
    return s


def oracle_key(entries):
    ref = ReferenceTape()
    rows = []
    for i, raw in enumerate(entries, 1):
        ref.apply(i, raw)
        for symbol, book in ref.books.items():
            if book.state == "valid":
                rows.append(f"K\t{i}\t{symbol}\t{book.position}\t{ref.book_hash(symbol)}")
    return "\n".join(rows) + "\n"


class KitTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="book-kit-test-"))
        self.write(stream())

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write(self, s):
        self.frames = self.tmp / "frames.txt"
        self.key = self.tmp / "key.tsv"
        self.frames.write_text("\n".join(s.frames) + "\n")
        self.key.write_text(oracle_key(s.sc.entries))

    def conform(self, client, scenarios=("clean",), timeout=5.0, extra=()):
        cmd = [sys.executable, "-B", str(CONFORM), "--client", client, "--frames", str(self.frames), "--key", str(self.key),
               "--timeout", str(timeout), *extra]
        for s in scenarios:
            cmd += ["--scenario", s]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return out.returncode, json.loads(out.stdout) if out.stdout.strip().startswith("{") else out.stdout + out.stderr

    def trap(self, name):
        return f"{sys.executable} -B {TRAPS} {name}"

    def test_reference_builder_matches_the_oracle_everywhere(self):
        code, report = self.conform(REFERENCE)
        self.assertEqual(code, 0, report)
        c = report["scenarios"][0]["counts"]
        self.assertEqual(c["frames"], 25)
        self.assertEqual(c["unchecked"], 0)
        self.assertEqual(c["checked"], c["valid"])
        self.assertGreater(c["valid"], 15)

    def test_each_known_trap_is_caught(self):
        for name in ("add-deltas", "snapshot-is-last", "floats", "depth-field", "serve-before-complete"):
            with self.subTest(name):
                code, report = self.conform(self.trap(name))
                self.assertEqual(code, 1, report)
                failure = report["scenarios"][0]["first_failure"]
                self.assertEqual(failure["reason"], "wrong book", failure)
                self.assertEqual(failure["symbol"], A)
                self.assertIn("next_seqnum", failure)

    def test_a_wrong_book_names_the_first_differing_level(self):
        _, report = self.conform(self.trap("add-deltas"))
        failure = report["scenarios"][0]["first_failure"]
        self.assertEqual(failure["next_seqnum"], 11)
        self.assertEqual(failure["first_difference"], {"side": "bids", "level": 0, "client": ["100", "4.5", "2"],
                                                       "expected": ["100", "2.5", "2"]})

    def test_a_client_that_ignores_a_gap_fails(self):
        code, report = self.conform(self.trap("ignore-gaps"), scenarios=("drop",))
        self.assertEqual(code, 1)
        failure = report["scenarios"][0]["first_failure"]
        self.assertEqual(failure["reason"], "valid after a missing update, before a new snapshot")
        self.assertEqual((failure["symbol"], failure["missing"]), (A, 10))

    def test_reference_builder_recovers_from_a_gap(self):
        code, report = self.conform(REFERENCE, scenarios=("drop", "swap"))
        self.assertEqual(code, 0, report)
        for s in report["scenarios"]:
            self.assertEqual(s["counts"]["resubscribes"], 1, s)
            self.assertEqual(s["counts"]["unchecked"], 0)
        self.assertEqual(report["scenarios"][0]["missing"], {"symbol": A, "seqnum": 10})

    def test_duplicates_and_late_snapshots_change_nothing(self):
        code, report = self.conform(REFERENCE, scenarios=("duplicate", "late-snapshot"))
        self.assertEqual(code, 0, report)
        clean = self.conform(REFERENCE)[1]["scenarios"][0]["counts"]["checked"]
        self.assertEqual(report["scenarios"][0]["counts"]["checked"], clean + 1)

    def test_a_client_that_applies_a_late_snapshot_fails(self):
        client = f"{sys.executable} -B -c \"import sys; sys.path.insert(0, '{ROOT / 'tools/book-kit'}'); import reference_book as R; " \
                 "o = R.Book.snapshot; R.Book.snapshot = lambda self, s, d: (setattr(self, 'state', 'empty'), o(self, s, d)); R.main()\""
        code, report = self.conform(client, scenarios=("late-snapshot",))
        self.assertEqual(code, 1, report)
        self.assertEqual(report["scenarios"][0]["first_failure"]["reason"], "the book went backwards")

    def test_hanging_exiting_and_garbled_clients_fail_in_time(self):
        clients = {
            "no answer within 1.0 s": f"{sys.executable} -c \"import time; time.sleep(60)\"",
            "the client exited": f"{sys.executable} -c \"pass\"",
            "the answer is not a JSON object": f"{sys.executable} -u -c \"import sys\nfor _ in sys.stdin: print('nope', flush=True)\"",
        }
        for reason, client in clients.items():
            with self.subTest(reason):
                code, report = self.conform(client, timeout=1.0)
                self.assertEqual(code, 1)
                self.assertEqual(report["scenarios"][0]["first_failure"]["reason"], reason)

    def test_reports_are_reproducible(self):
        out = [self.tmp / "r1.json", self.tmp / "r2.json"]
        for r in out:
            self.conform(REFERENCE, scenarios=("clean", "drop"), extra=("--report", str(r)))
        self.assertEqual(out[0].read_bytes(), out[1].read_bytes())

    def test_no_frame_text_reaches_the_report(self):
        self.write(stream(note=MARKER))
        report_file = self.tmp / "r.json"
        self.conform(self.trap("add-deltas"), extra=("--report", str(report_file)))
        self.assertNotIn(MARKER, report_file.read_text())
        self.assertNotIn("bids\":[{", report_file.read_text())


@unittest.skipUnless(VERIFY and LOG_APPLY and KEYS and OPENSSL, "requires the tape binaries and an OpenSSL 3 command")
class TapeKeyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="book-kit-tape-"))
        (self.tmp / "keys").mkdir()
        self.keys = Keys(self.tmp / "keys")
        (self.tmp / "segments").mkdir()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_tape(self, seg):
        cmd = [sys.executable, "-B", str(CONFORM), "--client", REFERENCE, "--tape", str(seg), "--recorder", "1",
               "--incarnation", "1000", "--head-every", str(HEAD_EVERY), "--verify", VERIFY]
        for k in self.keys.public():
            cmd += ["--key-file", str(k)]
        return subprocess.run(cmd, capture_output=True, text=True, timeout=120)

    def test_the_answer_key_comes_only_from_a_verified_segment(self):
        s = stream()
        seg = write_segment(self.tmp / "segments", 1000, s.sc.entries, self.keys, gz=False)
        good = self.run_tape(seg)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
        counts = json.loads(good.stdout)["scenarios"][0]["counts"]
        self.assertEqual(counts["unchecked"], 0)
        self.assertEqual(counts["checked"], counts["valid"])
        log = seg / "wal.log"
        data = bytearray(log.read_bytes())
        data[len(data) // 2] ^= 0x01
        log.write_bytes(bytes(data))
        bad = self.run_tape(seg)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("did not verify", bad.stdout + bad.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
