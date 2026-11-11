"""Verifier on a recorded three-voter run: pass, replay to a seqnum, and tampering that repairs its own checksum (VER, PROOF)."""
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

VERIFY = os.environ.get("TAPE_VERIFY")
RUN = os.environ.get("TAPE_RUN")


def verify(*args):
    out = subprocess.run([VERIFY, *map(str, args)], capture_output=True, text=True)
    return out.returncode, json.loads(out.stdout.strip().splitlines()[-1])


def records(raw):
    at = 0
    while at + 29 <= len(raw):
        (n,) = struct.unpack_from("<I", raw, at + 25)
        yield at, raw[at + 4], struct.unpack_from("<Q", raw, at + 5)[0], n
        at += 29 + n + 4


@unittest.skipUnless(VERIFY and RUN, "requires the verifier and a recorded run (tools/tape-local-run.py)")
class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.run_dir = Path(RUN)
        self.meta = json.loads((self.run_dir / "run.json").read_text())
        self.keys = sum([["--key", self.run_dir / "keys" / f"head-{v}.pub.pem"] for v in (1, 2, 3)], [])

    def full(self, voter, wal=None):
        d = self.run_dir / f"voter-{voter}"
        return verify("--wal", wal or d / "wal.log", "--heads", d / "heads", "--incarnation", self.meta["incarnation"],
                      "--head-every", self.meta["head_every"], *self.keys)

    def test_every_voter_verifies_and_agrees(self):
        roots = set()
        for v in (1, 2, 3):
            code, out = self.full(v)
            self.assertEqual(code, 0, out)
            self.assertGreater(out["heads_verified"], 0)
            roots.add((out["tree_size"], out["root"]))
        self.assertEqual(len(roots), 1)

    def test_verifier_needs_no_network(self):
        if shutil.which("sandbox-exec"):
            profile = "(version 1)(allow default)(deny network*)"
            d = self.run_dir / "voter-1"
            out = subprocess.run(["sandbox-exec", "-p", profile, VERIFY, "--wal", d / "wal.log", "--heads", d / "heads",
                                  "--incarnation", str(self.meta["incarnation"]), "--head-every", str(self.meta["head_every"]),
                                  *map(str, self.keys)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_replay_to_a_seqnum_is_identical_on_every_voter_and_proven(self):
        state = json.loads((self.run_dir / "voter-1" / "state.json").read_text())
        book = next(b for b in state["books"] if b["state"] == "valid" and b["position"] > 10)
        seq = book["position"] - 3
        answers = []
        for v in (1, 2, 3):
            d = self.run_dir / f"voter-{v}"
            code, out = verify("--wal", d / "wal.log", "--heads", d / "heads", "--incarnation", self.meta["incarnation"],
                               "--head-every", self.meta["head_every"], *self.keys, "--book", book["symbol"], "--until-seq", seq)
            self.assertEqual(code, 0, out)
            self.assertTrue(out["book"]["found"])
            answers.append((out["book"]["hash"], out["book"]["bids"], out["book"]["asks"]))
            if out["book"].get("inclusion"):
                self.assertTrue(out["book"]["inclusion"]["verified"])
        self.assertEqual(len(set(map(json.dumps, answers))), 1)

    def test_tampering_that_repairs_its_checksum_is_caught_and_located(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = self.run_dir / "voter-1" / "wal.log"
            raw = bytearray(src.read_bytes())
            target = None
            for at, kind, index, n in records(bytes(raw)):
                if kind == 1 and index > 50 and n > 300:
                    target = (at, index, n)
                    break
            at, index, n = target
            raw[at + 29 + n // 2] ^= 0x01
            struct.pack_into("<I", raw, at + 29 + n, zlib.crc32(bytes(raw[at:at + 29 + n])))
            forged = Path(tmp) / "wal.log"
            forged.write_bytes(bytes(raw))
            code, out = self.full(1, forged)
            self.assertEqual(code, 1)
            self.assertTrue(out["check"].startswith("head root"), out)
            code, out = verify("--wal", forged, "--compare-wal", self.run_dir / "voter-2" / "wal.log")
            self.assertEqual(code, 1)
            self.assertEqual(out["index"], index)


if __name__ == "__main__":
    unittest.main()
