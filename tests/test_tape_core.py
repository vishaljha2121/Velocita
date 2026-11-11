"""Tape reducer against the independent reference: attestation (ATT, TICK), books (BOOK) and Merkle hashing (PROOF)."""
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tape_oracles import encode as E  # noqa: E402
from tape_oracles.reference import ReferenceTape, APPLIED, DUPLICATE, OUT_OF_ORDER, TICK_REFUSED  # noqa: E402
from tape_oracles import merkle as M  # noqa: E402

BINARY = os.environ.get("TAPE_LOG_APPLY")
SYM = "NVDAC-USDC"


def run(entries, *args):
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "log.bin"
        E.write_log(path, entries)
        out = subprocess.run([BINARY, str(path), *args], capture_output=True, text=True, check=True).stdout
    rows, closes, final = [], {}, None
    for line in out.splitlines():
        m = json.loads(line)
        if m.get("close"):
            closes.setdefault(m["close_index"], []).append(m)
        elif m.get("final"):
            final = m
        else:
            rows.append(m)
    return rows, closes, final


def expected(entries, **kw):
    ref = ReferenceTape(**kw)
    return [ref.apply(i + 1, e) for i, e in enumerate(entries)]


def key(c):
    return (c["stream"], c["kind"], c["seqnum"], c["owner"], c["outcome"], c["reason"], c["mask"], c["first_index"])


class Recorders:
    """Builds a three-recorder log, each recorder with its own session and contiguous request numbers."""

    def __init__(self):
        self.entries = []
        self.next = {1: 1, 2: 1, 3: 1}
        self.session = {1: 11, 2: 22, 3: 33}
        self.ticks = 0
        for r in (1, 2, 3):
            self.entries.append(E.event(r, self.session[r], 1, "session_start"))
            self.next[r] = 2

    def obs(self, r, channel, kind, seq, payload, symbol=SYM, utc=0):
        self.entries.append(E.observation_body(r, self.session[r], self.next[r], channel, kind, symbol, seq, payload,
                                               utc or 1_000_000_000 + len(self.entries)))
        self.next[r] += 1

    def depth(self, r, kind, seq, bids=(), asks=(), symbol=SYM):
        self.obs(r, "DEPTH", kind, seq, E.depth_frame(seq, kind, symbol, bids, asks), symbol)

    def snapshot(self, r, seq, bids, asks, symbol=SYM):
        self.depth(r, "SNAPSHOT", seq, bids, asks, symbol)
        self.depth(r, "SNAPSHOT_COMPLETE", seq, symbol=symbol)

    def tick(self):
        self.ticks += 1
        self.entries.append(E.tick(self.ticks))


@unittest.skipUnless(BINARY, "requires the tape-log-apply test executable")
class ReducerAgainstReference(unittest.TestCase):
    def compare(self, entries, **kw):
        rows, closes, _ = run(entries, *(["--verdict-ticks", str(kw["verdict_ticks"])] if "verdict_ticks" in kw else []))
        want = expected(entries, **kw)
        self.assertEqual(len(rows), len(want))
        for row, w in zip(rows, want):
            i = row["index"]
            self.assertEqual(row["code"], w["code"], f"index {i}")
            self.assertEqual(row["stream"], w["stream"], f"index {i}")
            self.assertEqual(row["state"], w["state"], f"index {i}")
            self.assertEqual(row["hash"], w["hash"], f"index {i}")
            self.assertEqual(sorted(map(key, closes.get(i, []))), sorted(map(key, w["closes"])), f"index {i}")
        return rows, closes

    def test_random_arrivals_match_reference(self):
        for seed in range(25):
            rng = random.Random(seed)
            rec = Recorders()
            for r in (1, 2, 3):
                rec.snapshot(r, 100, [("10.000", "1.0", "1")], [("11.000", "1.0", "1")])
            pending = []
            for seq in range(100, 160):
                bid = f"{10 + rng.randint(0, 5)}.{rng.randint(0, 999):03d}"
                frame = E.depth_frame(seq, "UPDATE", SYM, [(bid, rng.choice(["0", "0.5", "2"]), "1")], [])
                alt = E.depth_frame(seq, "UPDATE", SYM, [(bid, "9.9", "3")], [])
                for r in (1, 2, 3):
                    if rng.random() < 0.15:
                        continue
                    payload = alt if rng.random() < 0.02 else frame
                    pending.append((r, seq, payload))
            rng.shuffle(pending)
            pending.sort(key=lambda x: (x[1] + rng.randint(-2, 2)))
            sent = {1: set(), 2: set(), 3: set()}
            for r, seq, payload in pending:
                if seq in sent[r]:
                    continue
                sent[r].add(seq)
                rec.obs(r, "DEPTH", "UPDATE", seq, payload)
                if rng.random() < 0.2:
                    rec.tick()
            for _ in range(8):
                rec.tick()
            with self.subTest(seed=seed):
                self.compare(rec.entries)

    def test_all_three_close_at_the_third_copy(self):
        rec = Recorders()
        for r in (1, 2, 3):
            rec.snapshot(r, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        for r in (1, 2, 3):
            rec.depth(r, "UPDATE", 5, [("1.5", "1", "1")])
        rows, closes = self.compare(rec.entries)
        third = rows[-1]["index"]
        self.assertTrue(any(c["kind"] == "UPDATE" and c["outcome"] == "3/3" and c["reason"] == "all_reported"
                            for c in closes[third]))

    def test_silent_recorder_closes_at_exactly_n_ticks(self):
        rec = Recorders()
        for r in (1, 2, 3):
            rec.snapshot(r, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 5, [("1.5", "1", "1")])
        rec.depth(2, "UPDATE", 5, [("1.5", "1", "1")])
        for _ in range(5):
            rec.tick()
        rows, closes = self.compare(rec.entries)
        fifth_tick = rows[-1]["index"]
        fourth_tick = rows[-2]["index"]
        self.assertFalse(any(c["kind"] == "UPDATE" for c in closes.get(fourth_tick, [])))
        self.assertTrue(any(c["kind"] == "UPDATE" and c["outcome"] == "2/3" and c["reason"] == "n_ticks"
                            for c in closes[fifth_tick]))

    def test_third_copy_on_either_side_of_the_close_tick(self):
        for before in (True, False):
            rec = Recorders()
            for r in (1, 2, 3):
                rec.snapshot(r, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
            rec.depth(1, "UPDATE", 5, [("1.5", "1", "1")])
            rec.depth(2, "UPDATE", 5, [("1.5", "1", "1")])
            for _ in range(4):
                rec.tick()
            if before:
                rec.depth(3, "UPDATE", 5, [("1.5", "1", "1")])
                rec.tick()
            else:
                rec.tick()
                rec.depth(3, "UPDATE", 5, [("1.5", "1", "1")])
            with self.subTest(before=before):
                rows, closes = self.compare(rec.entries)
                updates = [c for cs in closes.values() for c in cs if c["kind"] == "UPDATE"]
                self.assertEqual([c["outcome"] for c in updates], ["3/3"] if before else ["2/3"])

    def test_late_divergent_copy_makes_the_book_unknown_without_reopening(self):
        rec = Recorders()
        for r in (1, 2, 3):
            rec.snapshot(r, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 5, [("1.5", "1", "1")])
        for _ in range(5):
            rec.tick()
        rec.depth(2, "UPDATE", 5, [("1.5", "7", "1")])
        rows, closes = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "unknown")
        self.assertEqual(rows[-1]["hash"], "")
        self.assertEqual(sum(1 for cs in closes.values() for c in cs if c["kind"] == "UPDATE"), 1)

    def test_three_versions_are_divergent_not_a_majority(self):
        rec = Recorders()
        for r in (1, 2, 3):
            rec.snapshot(r, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 5, [("1.5", "1", "1")])
        rec.depth(2, "UPDATE", 5, [("1.5", "1", "1")])
        rec.depth(3, "UPDATE", 5, [("1.5", "2", "1")])
        rows, closes = self.compare(rec.entries)
        update = [c for c in closes[rows[-1]["index"]] if c["kind"] == "UPDATE"][0]
        self.assertEqual((update["outcome"], update["distinct"]), ("divergent", 2))
        self.assertEqual(rows[-1]["state"], "unknown")

    def test_snapshots_from_different_subscriptions_are_not_copies(self):
        rec = Recorders()
        rec.snapshot(1, 50, [("10.0", "1", "1"), ("9.0", "1", "1")], [("11.0", "1", "1")])
        rec.snapshot(2, 50, [("10.0", "1", "1"), ("9.5", "2", "1")], [("11.0", "1", "1")])
        rec.depth(1, "UPDATE", 50, [("9.0", "0", "0"), ("9.5", "2", "1")])
        for _ in range(6):
            rec.tick()
        rows, closes = self.compare(rec.entries)
        self.assertEqual([r for r in rows if r["stream"] != 255][-1]["state"], "valid")
        snapshot_closes = [c for cs in closes.values() for c in cs if c["kind"] == "SNAPSHOT"]
        self.assertEqual([c["outcome"] for c in snapshot_closes], ["1/3", "1/3"])
        self.assertEqual(len({c["owner"] for c in snapshot_closes}), 2)
        self.assertFalse(any(r["flags"] & 0x20 for r in rows), "a mid-batch snapshot is not a disagreement")

    def test_a_recorders_own_resubscription_snapshot_is_not_a_copy(self):
        rec = Recorders()
        rec.snapshot(1, 50, [("10.0", "1", "1"), ("9.0", "1", "1")], [("11.0", "1", "1")])
        rec.snapshot(1, 50, [("10.0", "1", "1"), ("9.5", "2", "1")], [("11.0", "1", "1")])
        rec.depth(1, "UPDATE", 50, [("9.0", "0", "0"), ("9.5", "2", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual([r for r in rows if r["stream"] != 255][-1]["state"], "valid")

    def test_snapshot_still_wrong_after_the_next_update_is_flagged(self):
        rec = Recorders()
        rec.snapshot(1, 50, [("10.0", "1", "1")], [("11.0", "1", "1")])
        rec.snapshot(2, 50, [("10.0", "1", "1"), ("8.0", "4", "1")], [("11.0", "1", "1")])
        rec.depth(1, "UPDATE", 50, [("10.0", "2", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertTrue(rows[-1]["flags"] & 0x20)

    def test_capacity_closes_the_oldest(self):
        rec = Recorders()
        rec.snapshot(1, 0, [("1.0", "1", "1")], [("2.0", "1", "1")])
        for seq in range(0, 300):
            rec.depth(1, "UPDATE", seq, [("1.5", str(seq + 1), "1")])
        rows, closes = self.compare(rec.entries)
        capacity = [c for cs in closes.values() for c in cs if c["reason"] == "capacity"]
        self.assertGreater(len(capacity), 0)

    def test_duplicate_and_out_of_order_requests_are_refused(self):
        rec = Recorders()
        rec.entries.append(E.event(1, 11, 2, "gap"))
        rec.entries.append(E.event(1, 11, 2, "gap"))
        rec.entries.append(E.event(1, 11, 4, "gap"))
        rows, _ = self.compare(rec.entries)
        self.assertEqual([r["code"] for r in rows[-3:]], [APPLIED, DUPLICATE, OUT_OF_ORDER])

    def test_ticks_must_be_consecutive(self):
        entries = [E.tick(1), E.tick(1), E.tick(3), E.tick(2)]
        rows, _ = self.compare(entries)
        self.assertEqual([r["code"] for r in rows], [APPLIED, TICK_REFUSED, TICK_REFUSED, APPLIED])

    def test_tick_time_is_never_used(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        for n in range(1, 8):
            rec.entries.append(E.tick(n, utc=n))
        a, _, fa = run(rec.entries)
        b, _, fb = run([e if e[1] != 4 else e[:-8] + (123456789).to_bytes(8, "little") for e in rec.entries])
        self.assertEqual([(r["state"], r["hash"]) for r in a], [(r["state"], r["hash"]) for r in b])

    def test_held_message_applies_in_order_when_it_arrives_in_time(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 6, [("1.6", "1", "1")])
        rec.tick()
        rec.depth(1, "UPDATE", 5, [("1.5", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "valid")

    def test_missing_message_makes_the_book_unknown_after_n_ticks(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 6, [("1.6", "1", "1")])
        for _ in range(5):
            rec.tick()
        rec.depth(1, "UPDATE", 7, [("1.7", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "unknown")

    def test_reorder_buffer_full_is_unknown_at_once(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        for seq in range(7, 16):
            rec.depth(1, "UPDATE", seq, [("1.5", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "unknown")

    def test_stale_snapshot_is_ignored_and_interleaved_snapshot_applies_at_complete(self):
        rec = Recorders()
        rec.snapshot(1, 10, [("1.0", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 10, [("1.1", "1", "1")])
        rec.snapshot(2, 3, [("9.0", "1", "1")], [("9.5", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "valid")
        rec.depth(3, "SNAPSHOT", 20, [("1.2", "1", "1")], [("2.0", "1", "1")])
        rec.depth(1, "UPDATE", 20, [("1.3", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "building")
        rec.depth(3, "SNAPSHOT_COMPLETE", 20)
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "valid")

    def test_decimal_boundaries(self):
        cases = [("182.4000", "valid"), ("182.40001", "valid"), ("1.0000000001", "unknown"),
                 ("9223372036.854775807", "valid"), ("9223372036.854775808", "unknown"),
                 ("-1", "unknown"), ("1e3", "unknown"), ("", "unknown"), ("NaN", "unknown")]
        for price, want in cases:
            rec = Recorders()
            rec.snapshot(1, 5, [(price, "1", "1")], [("9999999999.0" if want == "x" else "9223372036.854775807", "1", "1")])
            with self.subTest(price=price):
                rows, _ = self.compare(rec.entries)
                self.assertEqual(rows[-1]["state"], want)

    def test_level_capacity_makes_the_book_unknown(self):
        rec = Recorders()
        rec.snapshot(1, 5, [(f"{i}.0", "1", "1") for i in range(1, 70)], [("100.0", "1", "1")])
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "unknown")

    def test_split_observation_matches_a_whole_one(self):
        rec = Recorders()
        payload = E.depth_frame(5, "SNAPSHOT", SYM, [(f"{i}.0", "1", "1") for i in range(1, 60)], [("100.0", "1", "1")])
        whole = E.observation_body(1, 11, 2, "DEPTH", "SNAPSHOT", SYM, 5, payload, 99)
        rec.entries.extend(E.parts(1, 11, 2, whole, 1500))
        n = len(E.parts(1, 11, 2, whole, 1500))
        rec.next[1] = 2 + n
        rec.depth(1, "SNAPSHOT_COMPLETE", 5)
        rows, _ = self.compare(rec.entries)
        self.assertEqual(rows[-1]["state"], "valid")
        dropped = rec.entries[:4] + rec.entries[5:]
        self.compare(dropped)

    def test_integrity_checks_never_change_books(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("2.0", "1", "1")], [("1.0", "1", "1")])
        rec.obs(1, "EBBO", "UPDATE", 9, E.ebbo_frame(9, "UPDATE", SYM, ("3.0", "1", "1"), ("4.0", "1", "1")))
        rec.obs(1, "TRADE", "UPDATE", 3, E.trade_frame(3, SYM, "50.0", "1"))
        for _ in range(4):
            rec.tick()
        rec.depth(1, "UPDATE", 5, [("2.1", "1", "1")])
        on, _, _ = run(rec.entries)
        off, _, _ = run(rec.entries, "--no-integrity")
        self.assertEqual([(r["state"], r["hash"]) for r in on], [(r["state"], r["hash"]) for r in off])
        self.assertTrue(any(r["flags"] for r in on))
        self.assertFalse(any(r["flags"] & 0x3F for r in off))


@unittest.skipUnless(BINARY, "requires the tape-log-apply test executable")
class MerkleAgainstReference(unittest.TestCase):
    def test_rfc_vectors(self):
        leaves = [b"", b"\x00", b"\x10", b"\x20\x21", b"\x30\x31", b"\x40\x41\x42\x43",
                  b"\x50\x51\x52\x53\x54\x55\x56\x57", bytes(range(0x60, 0x70))]
        roots = M.VECTOR_ROOTS
        for n in range(1, 9):
            self.assertEqual(M.root([M.leaf(x) for x in leaves[:n]]).hex(), roots[n - 1], n)

    def test_cluster_root_equals_reference_root_and_proofs_verify(self):
        rec = Recorders()
        rec.snapshot(1, 5, [("1.0", "1", "1")], [("2.0", "1", "1")])
        for n in range(1, 40):
            rec.tick()
        rows, _, final = run(rec.entries)
        leaves = [bytes.fromhex(r["leaf"]) for r in rows]
        self.assertEqual(M.root(leaves).hex(), final["root"])
        for i in (0, 7, len(leaves) - 1):
            proof = M.inclusion(leaves, i)
            self.assertTrue(M.verify_inclusion(leaves[i], i, len(leaves), proof, M.root(leaves)))
            bad = bytearray(leaves[i])
            bad[0] ^= 1
            self.assertFalse(M.verify_inclusion(bytes(bad), i, len(leaves), proof, M.root(leaves)))
        for m in (1, 5, 16, 31):
            proof = M.consistency(leaves, m)
            self.assertTrue(M.verify_consistency(m, len(leaves), M.root(leaves[:m]), M.root(leaves), proof))

    def test_proof_sizes_by_hand(self):
        for n, size in ((1000, 320), (1_000_000, 640), (10_000_000, 768)):
            self.assertEqual(M.max_inclusion_hashes(n) * 32, size)


@unittest.skipUnless(BINARY and os.environ.get("TAPE_CAPTURE"), "requires a day-one capture")
class CapturedFeed(unittest.TestCase):
    def test_two_real_connections_replay_identically_to_reference(self):
        capture = Path(os.environ["TAPE_CAPTURE"])
        rec = Recorders()
        streams = []
        for r, name in ((1, "frames.jsonl"), (2, "frames-second.jsonl")):
            for line in (capture / name).read_text().splitlines():
                row = json.loads(line)
                streams.append((row["utc_ns"], r, row["raw"].encode()))
        streams.sort()
        last_tick_ns = streams[0][0]
        for utc, r, raw in streams:
            m = json.loads(raw)
            if m.get("channel") not in ("DEPTH", "EBBO", "TRADE", "INSTRUMENT"):
                continue
            d = m.get("data") or {}
            sym = m.get("item") or d.get("symbol") or (d.get("info") or {}).get("symbol")
            rec.obs(r, m["channel"], m["update"], int(m["seqnum"]), raw, sym, utc)
            while utc - last_tick_ns >= 1_000_000_000:
                rec.tick()
                last_tick_ns += 1_000_000_000
        rows, _ = ReducerAgainstReference.compare(self, rec.entries)
        states = [r["state"] for r in rows if r["stream"] != 255]
        self.assertIn("valid", states)


CapturedFeed.compare = ReducerAgainstReference.compare


if __name__ == "__main__":
    unittest.main()
