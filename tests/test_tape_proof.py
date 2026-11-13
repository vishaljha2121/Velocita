#!/usr/bin/env python3
"""Proof bundle and browser check tests: the verifier's --bundle output and tools/tape-proof/check.js under Node."""
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tape_oracles import encode as E  # noqa: E402
from tape_oracles import merkle as M  # noqa: E402
from tape_oracles.reference import ReferenceTape  # noqa: E402
from test_tape_report import (HEAD_EVERY, KEYS, LOG_APPLY, MS, OPENSSL, SEC, SYM, VERIFY, Keys, Script,  # noqa: E402
                              wal_bytes, write_segment)

ROOT = Path(__file__).resolve().parent.parent
CHECK = ROOT / "tools/tape-proof/check.js"
BUILD = ROOT / "tools/tape-proof/build.py"
NODE = shutil.which("node")
INCARNATION = 1000
MARKER = b"PROOF-MARKER-7f3a"
if VERIFY and not NODE:
    raise SystemExit("test_tape_proof: the tape binaries are set but Node was not found to run check.js")

RUNNER = r"""
const fs = require("fs");
const P = require(process.argv[1]);
const b = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const opt = JSON.parse(process.argv[3]);
(async () => {
  const r = await P.verifyBundle(b, opt);
  const out = { ok: r.ok, steps: r.steps, signed_by: r.signed_by, fingerprints: r.fingerprints };
  try {
    const s = globalThis.crypto.subtle;
    const book = await P.bookHash(s, b);
    out.book = P.bytesHex(book);
    out.leaf = P.bytesHex(await P.leafHash(s, b, book));
    out.message = P.bytesHex(P.headMessage(b.head));
  } catch (e) {}
  console.log(JSON.stringify(out));
})();
"""


def script():
    s = Script()
    s.snapshot(10, 0, [("100.00", "2", "1"), ("99.90", "4", "2")], [("100.10", "3", "1")])
    s.tick(1 * SEC)
    frame = E.depth_frame(10, "UPDATE", SYM, [("100.01", "1", "1")], [])
    frame = frame[:-1] + b',"note":"' + MARKER + b'"}'
    for r, at in ((1, 100), (2, 130), (3, 160)):
        s.obs(r, "DEPTH", "UPDATE", 10, frame, 1 * SEC + at * MS)
    s.tick(2 * SEC, count=8)
    return s


def py_book_hash(b):
    out = b"velocita-tape/v1/book" + bytes([len(b["symbol"])]) + b["symbol"].encode() + struct.pack("<Q", int(b["book"]["position"]))
    for side in (b["book"]["bids"], b["book"]["asks"]):
        out += struct.pack("<H", len(side))
        for price, qty, count in side:
            out += struct.pack("<qqI", int(price), int(qty), count)
    return hashlib.sha256(out).digest()


def py_leaf_hash(b, book):
    f = b["leaf"]
    out = b"\x00velocita-tape/v1/leaf" + struct.pack("<Q", int(f["index"])) + bytes.fromhex(f["entry_digest"])
    out += bytes([0xE0, f["code"], f["type"], f["stream"], f["book_state"]]) + book + struct.pack("<HH", f["flags"], f["closes"])
    return hashlib.sha256(out).digest()


def py_head_message(h):
    return b"velocita-tape/v1" + struct.pack("<QQQ", int(h["incarnation"]), int(h["tick"]), int(h["tree_size"])) + bytes.fromhex(h["root"])


@unittest.skipUnless(VERIFY and LOG_APPLY and KEYS and OPENSSL and NODE, "requires the tape binaries, OpenSSL 3 and Node")
class ProofTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tape-proof-test-"))
        (self.tmp / "keys").mkdir()
        self.keys = Keys(self.tmp / "keys")
        self.base = self.tmp / "segments"
        self.base.mkdir()
        self.pinned = {}
        for v, pub in enumerate(self.keys.public(), 1):
            der = subprocess.run([OPENSSL, "pkey", "-pubin", "-in", str(pub), "-outform", "DER"], capture_output=True, check=True).stdout
            self.pinned[str(v)] = hashlib.sha256(der).hexdigest()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def segment(self, entries, signers=(1, 2)):
        seg = write_segment(self.base, INCARNATION, entries, self.keys, signers=signers, gz=False)
        return seg

    def bundle(self, seg, until=10, wal=None):
        out = self.tmp / "bundle.json"
        cmd = [VERIFY, "--wal", str(wal or seg / "wal.log"), "--heads", str(seg / "heads"), "--incarnation", str(INCARNATION),
               "--head-every", str(HEAD_EVERY), "--book", SYM, "--until-seq", str(until), "--bundle", str(out)]
        for k in self.keys.public():
            cmd += ["--key", str(k)]
        run = subprocess.run(cmd, capture_output=True, text=True)
        return run, out

    def good(self, signers=(1, 2)):
        sc = script()
        seg = self.segment(sc.entries, signers)
        run, out = self.bundle(seg)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return sc, json.loads(out.read_text())

    def check(self, bundle, pinned=None, module=CHECK):
        f = self.tmp / "check-input.json"
        f.write_text(json.dumps(bundle))
        out = subprocess.run([NODE, "-e", RUNNER, str(module), str(f), json.dumps({"pinned": pinned})],
                             capture_output=True, text=True, check=True)
        return json.loads(out.stdout)

    def failed_at(self, r):
        return next((s["name"] for s in r["steps"] if not s["ok"]), None)

    def test_hashes_match_the_reference_and_the_cluster(self):
        sc, b = self.good()
        index = int(b["leaf"]["index"])
        ref = ReferenceTape()
        for i, raw in enumerate(sc.entries[:index], 1):
            result = ref.apply(i, raw)
        self.assertEqual(result["state"], "valid")
        self.assertEqual(b["leaf"]["book_state"], 2)
        self.assertEqual(result["stream"], b["leaf"]["stream"])
        self.assertEqual(result["code"], b["leaf"]["code"])
        self.assertEqual(ref.book_hash(SYM), py_book_hash(b).hex())
        self.assertEqual(hashlib.sha256(sc.entries[index - 1]).hexdigest(), b["leaf"]["entry_digest"])
        log = self.tmp / "apply.bin"
        E.write_log(log, sc.entries)
        rows = [json.loads(line) for line in subprocess.run([LOG_APPLY, str(log), "--head-every", str(HEAD_EVERY)],
                                                            capture_output=True, text=True, check=True).stdout.splitlines()]
        leaves = [bytes.fromhex(r["leaf"]) for r in rows if "leaf" in r]
        self.assertEqual(leaves[index - 1], py_leaf_hash(b, py_book_hash(b)))
        self.assertEqual(M.root(leaves[:int(b["head"]["tree_size"])]).hex(), b["head"]["root"])
        r = self.check(b, self.pinned)
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["book"], py_book_hash(b).hex())
        self.assertEqual(r["leaf"], py_leaf_hash(b, py_book_hash(b)).hex())
        self.assertEqual(r["message"], py_head_message(b["head"]).hex())

    def test_stored_hashes_are_never_trusted(self):
        _, b = self.good()
        b["book_hash"] = "00" * 32
        b["leaf"]["leaf_hash"] = "11" * 32
        self.assertTrue(self.check(b, self.pinned)["ok"])

    def test_two_of_three_signatures_pass(self):
        _, b = self.good(signers=(1, 2))
        r = self.check(b, self.pinned)
        self.assertTrue(r["ok"])
        self.assertEqual(r["signed_by"], [1, 2])
        self.assertEqual(r["fingerprints"], self.pinned)

    def test_one_signature_or_a_repeated_one_fails(self):
        _, b = self.good(signers=(1, 2))
        one = json.loads(json.dumps(b))
        del one["head"]["signatures"]["2"]
        self.assertEqual(self.failed_at(self.check(one, self.pinned)), "signatures")
        moved = json.loads(json.dumps(b))
        moved["head"]["signatures"]["2"] = moved["head"]["signatures"]["1"]
        self.assertEqual(self.failed_at(self.check(moved, self.pinned)), "signatures")
        doubled = json.loads(json.dumps(b))
        doubled["keys"]["2"] = doubled["keys"]["1"]
        doubled["head"]["signatures"]["2"] = doubled["head"]["signatures"]["1"]
        self.assertEqual(self.failed_at(self.check(doubled)), "signatures")
        self.assertEqual(self.failed_at(self.check(doubled, self.pinned)), "keys")

    def test_every_field_change_fails_at_the_right_step(self):
        _, b = self.good()

        def bump(s):
            return str(int(s) + 1)

        changes = {
            "bid size": (lambda x: x["book"]["bids"][0].__setitem__(1, bump(x["book"]["bids"][0][1])), "inclusion"),
            "ask price": (lambda x: x["book"]["asks"][0].__setitem__(0, bump(x["book"]["asks"][0][0])), "inclusion"),
            "order count": (lambda x: x["book"]["bids"][0].__setitem__(2, x["book"]["bids"][0][2] + 1), "inclusion"),
            "position": (lambda x: x["book"].__setitem__("position", bump(x["book"]["position"])), "inclusion"),
            "index": (lambda x: x["leaf"].__setitem__("index", bump(x["leaf"]["index"])), "inclusion"),
            "entry digest": (lambda x: x["leaf"].__setitem__("entry_digest", "0" * 64), "inclusion"),
            "flags": (lambda x: x["leaf"].__setitem__("flags", x["leaf"]["flags"] ^ 1), "inclusion"),
            "proof hash": (lambda x: x["proof"].__setitem__(0, "0" * 64), "inclusion"),
            "tree size": (lambda x: x["head"].__setitem__("tree_size", bump(x["head"]["tree_size"])), ("inclusion", "signatures")),
            "root": (lambda x: x["head"].__setitem__("root", "0" * 64), "inclusion"),
            "incarnation": (lambda x: x["head"].__setitem__("incarnation", bump(x["head"]["incarnation"])), "signatures"),
            "tick": (lambda x: x["head"].__setitem__("tick", bump(x["head"]["tick"])), "signatures"),
            "signature": (lambda x: x["head"]["signatures"].__setitem__("1", "0" * 128), "signatures"),
            "malformed": (lambda x: x["book"].__setitem__("position", "ten"), "bundle"),
        }
        for name, (change, step) in changes.items():
            with self.subTest(name):
                x = json.loads(json.dumps(b))
                change(x)
                r = self.check(x, self.pinned)
                self.assertFalse(r["ok"])
                self.assertIn(self.failed_at(r), step if isinstance(step, tuple) else (step,))

    def test_a_proof_with_a_hash_dropped_or_added_fails(self):
        _, b = self.good()
        self.assertGreater(len(b["proof"]), 0)
        dropped = json.loads(json.dumps(b))
        dropped["proof"].pop()
        added = json.loads(json.dumps(b))
        added["proof"].append("ab" * 32)
        for x in (dropped, added):
            self.assertEqual(self.failed_at(self.check(x, self.pinned)), "inclusion")

    def test_the_page_makes_no_network_requests(self):
        page = self.tmp / "proof.html"
        subprocess.run([sys.executable, str(BUILD), "--out", str(page)], check=True, capture_output=True)
        text = page.read_text()
        for needle in ("src=", "href=\"http", "fetch(", "XMLHttpRequest", "import(", "WebSocket", "sendBeacon", "@import", "url("):
            self.assertNotIn(needle, text, needle)
        self.assertIn("verifyBundle", text)
        self.assertTrue(text.startswith("<!doctype html>"))
        self.assertIn('<meta charset="utf-8">', text[:200])
        self.assertTrue(text.isascii(), "non-ASCII characters break when a browser guesses the encoding")

    def test_no_payload_bytes_reach_the_bundle(self):
        _, b = self.good()
        text = (self.tmp / "bundle.json").read_text()
        self.assertNotIn(MARKER.decode(), text)
        self.assertEqual(set(b), {"version", "symbol", "book", "leaf", "proof", "head", "keys"})

    def test_no_bundle_without_a_verified_log_and_head(self):
        sc = script()
        seg = self.segment(sc.entries)
        data = bytearray((seg / "wal.log").read_bytes())
        data[len(data) // 2] ^= 0x01
        damaged = self.tmp / "damaged.log"
        damaged.write_bytes(bytes(data))
        run, out = self.bundle(seg, wal=damaged)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(out.exists())
        self.assertFalse(Path(str(out) + ".tmp").exists())
        shutil.rmtree(self.base)
        self.base.mkdir()
        seg = self.segment(sc.entries, signers=(1,))
        run, out = self.bundle(seg)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(out.exists())
        run, out = self.bundle(self.segment_again(sc), until=99)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(out.exists())

    def segment_again(self, sc):
        shutil.rmtree(self.base)
        self.base.mkdir()
        return self.segment(sc.entries)

    def test_mutants_of_the_check_are_caught(self):
        _, b = self.good(signers=(1, 2))
        one = json.loads(json.dumps(b))
        del one["head"]["signatures"]["2"]
        tampered = json.loads(json.dumps(b))
        tampered["book"]["bids"][0][1] = str(int(tampered["book"]["bids"][0][1]) + 1)
        cases = [(b, True), (one, False), (tampered, False)]
        source = CHECK.read_text()
        mutants = {
            "byte order": ("b[i] = Number(v & 0xffn)", "b[size - 1 - i] = Number(v & 0xffn)"),
            "one signature is enough": ("result.signed_by.length >= 2", "result.signed_by.length >= 1"),
            "proof sides swapped": ("r = await nodeHash(subtle, p, r);", "r = await nodeHash(subtle, r, p);"),
            "root not compared": ("root2 !== null && equal(root2, hexBytes(bundle.head.root))", "root2 !== null"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(name):
                self.assertEqual(source.count(old), 1, name)
                module = self.tmp / f"mutant-{name.replace(' ', '-')}.js"
                module.write_text(source.replace(old, new))
                verdicts = [self.check(x, self.pinned, module)["ok"] == expected for x, expected in cases]
                self.assertFalse(all(verdicts), f"mutant survived: {name}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
