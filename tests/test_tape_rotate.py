#!/usr/bin/env python3
"""Tape segment rotation on one host: archive, relink, restart, retention."""
import gzip
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROTATE = Path(__file__).resolve().parent.parent / "deploy/tape/rotate.sh"
DAY = 43200


class Host:
    def __init__(self, incarnation=100, heads=(60, 120)):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name) / "velocita-tape"
        (self.base / "config").mkdir(parents=True)
        (self.base / "data/voter/heads").mkdir(parents=True)
        (self.base / "config/voter.conf").write_text(f"node_id=2\nincarnation={incarnation}\nhead_every_ticks=60\n")
        (self.base / "config/recorder.conf").write_text(f"recorder_id=2\nincarnation={incarnation}\n")
        (self.base / "data/voter/wal.log").write_bytes(b"x" * 5000)
        for h in heads:
            (self.base / f"data/voter/heads/head-{h}.json").write_text(f'{{"tick":{h}}}\n')
        self.ctl = Path(self.tmp.name) / "ctl.sh"
        self.ctl.write_text('#!/bin/sh\necho "$@" >> "$(dirname "$0")/calls"\n')
        self.ctl.chmod(0o755)

    def rotate(self, now, keep=2):
        env = dict(os.environ, TAPE_HOME=str(self.base), TAPE_ROTATE_SYSTEMCTL=str(self.ctl), TAPE_ROTATE_NOW=str(now),
                   TAPE_ROTATE_INTERVAL=str(DAY), TAPE_ROTATE_HEAD_WAIT="0", TAPE_ROTATE_START_WAIT="0",
                   TAPE_ROTATE_KEEP=str(keep))
        return subprocess.run(["sh", str(ROTATE)], env=env, capture_output=True, text=True, timeout=60)

    def run_segment(self):
        (self.base / "data/voter/heads").mkdir(parents=True, exist_ok=True)
        (self.base / "data/voter/wal.log").write_bytes(b"y" * 100)
        (self.base / "data/voter/heads/head-120.json").write_text('{"tick":120}\n')

    def calls(self):
        f = self.ctl.parent / "calls"
        out = f.read_text().splitlines() if f.exists() else []
        if f.exists():
            f.unlink()
        return out

    def conf(self, name):
        return dict(line.split("=", 1) for line in (self.base / "config" / name).read_text().splitlines())


class RotateTests(unittest.TestCase):
    def test_a_segment_is_archived_and_the_next_one_links_to_its_last_head(self):
        h = Host()
        r = h.rotate(3 * DAY + 5)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(h.calls(), ["--user stop velocita-tape-recorder.service", "--user stop velocita-tape-voter.service",
                                     "--user start velocita-tape-voter.service", "--user start velocita-tape-recorder.service"])
        seg = h.base / "data/segments/100"
        self.assertEqual(gzip.decompress((seg / "wal.log.gz").read_bytes()), b"x" * 5000)
        self.assertTrue((seg / "heads/head-120.json").exists())
        self.assertEqual(list((h.base / "data/voter").iterdir()), [])
        self.assertEqual(h.conf("voter.conf")["incarnation"], str(3 * DAY))
        self.assertEqual(h.conf("voter.conf")["previous_head_file"], "/var/lib/velocita-tape/segments/100/heads/head-120.json")
        self.assertEqual(h.conf("recorder.conf")["incarnation"], str(3 * DAY))
        self.assertNotIn("previous_head_file", h.conf("recorder.conf"))

    def test_running_twice_in_the_same_interval_does_nothing(self):
        h = Host()
        h.rotate(3 * DAY + 5)
        h.calls()
        r = h.rotate(3 * DAY + 600)
        self.assertEqual(r.returncode, 0)
        self.assertIn("already on segment", r.stdout)
        self.assertEqual(h.calls(), [])

    def test_a_segment_without_a_signed_head_starts_the_next_one_unlinked(self):
        h = Host(heads=())
        r = h.rotate(3 * DAY + 5)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("previous_head_file", h.conf("voter.conf"))
        self.assertIn("starts unlinked", r.stderr)

    def test_a_newer_segment_is_never_rolled_back(self):
        h = Host(incarnation=5 * DAY)
        r = h.rotate(3 * DAY + 5)
        self.assertEqual(r.returncode, 65)
        self.assertEqual(h.calls(), [])

    def test_only_the_newest_segments_are_kept_locally(self):
        h = Host(incarnation=DAY)
        for k in (2, 3, 4):
            h.rotate(k * DAY + 1)
            h.run_segment()
        kept = sorted(int(p.name) for p in (h.base / "data/segments").iterdir())
        self.assertEqual(kept, [2 * DAY, 3 * DAY])
        self.assertEqual(h.conf("voter.conf")["previous_head_file"],
                         f"/var/lib/velocita-tape/segments/{3 * DAY}/heads/head-120.json")

    def test_keep_zero_keeps_every_segment(self):
        h = Host(incarnation=DAY)
        for k in range(2, 6):
            h.rotate(k * DAY + 1, keep=0)
            h.run_segment()
        self.assertEqual(len(list((h.base / "data/segments").iterdir())), 4)


if __name__ == "__main__":
    unittest.main()
