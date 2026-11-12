#!/usr/bin/env python3
"""The tape watchdog: it stops the tape after a sustained flood and restarts it once the link is quiet."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

WATCHDOG = Path(__file__).resolve().parent.parent / "deploy/tape/watchdog.sh"


def run(rates_kbs, limit=600, samples=6, cooldown=300, cooldown_max=3600):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        total, values = 0, [0]
        for r in rates_kbs:
            total += r * 10240
            values.append(total)
        (tmp / "values").write_text("\n".join(map(str, values)) + "\n")
        (tmp / "read.sh").write_text('#!/bin/sh\nf="$(dirname "$0")/values"\nhead -1 "$f"\nsed -i.bak 1d "$f"\n')
        (tmp / "ctl.sh").write_text('#!/bin/sh\necho "$@" >> "$(dirname "$0")/calls"\n')
        for f in ("read.sh", "ctl.sh"):
            (tmp / f).chmod(0o755)
        env = dict(os.environ, TAPE_WATCHDOG_READ=str(tmp / "read.sh"), TAPE_WATCHDOG_SYSTEMCTL=str(tmp / "ctl.sh"),
                   TAPE_WATCHDOG_SLEEP="0", HOME=str(tmp))
        out = subprocess.run(["sh", str(WATCHDOG), str(limit), str(samples), str(cooldown), str(cooldown_max)],
                             env=env, capture_output=True, text=True, timeout=60)
        calls = (tmp / "calls").read_text().splitlines() if (tmp / "calls").exists() else []
        return calls, out.stdout


class WatchdogTests(unittest.TestCase):
    def test_normal_traffic_never_stops_the_tape(self):
        calls, _ = run([20] * 200)
        self.assertEqual(calls, [])

    def test_a_short_burst_does_not_trip(self):
        calls, _ = run([20] * 10 + [2000] * 5 + [20] * 50)
        self.assertEqual(calls, [])

    def test_a_sustained_flood_stops_the_tape(self):
        calls, out = run([20] * 10 + [2000] * 6 + [2000] * 20)
        self.assertEqual(calls, ["--user stop velocita-tape-recorder.service velocita-tape-voter.service"])
        self.assertIn("stopping the tape", out)

    def test_the_tape_restarts_after_a_quiet_cool_down(self):
        calls, out = run([20] * 5 + [2000] * 6 + [10] * 40)
        self.assertEqual(calls, ["--user stop velocita-tape-recorder.service velocita-tape-voter.service",
                                 "--user start velocita-tape-voter.service", "--user start velocita-tape-recorder.service"])
        self.assertIn("restarting the tape", out)

    def test_no_restart_while_the_link_is_still_busy(self):
        calls, _ = run([20] * 5 + [2000] * 6 + [400] * 60)
        self.assertEqual(calls, ["--user stop velocita-tape-recorder.service velocita-tape-voter.service"])

    def test_a_second_trip_within_the_hour_doubles_the_cool_down(self):
        first = [20] * 5 + [2000] * 6 + [10] * 31
        calls, out = run(first + [2000] * 6 + [10] * 40)
        self.assertEqual(calls.count("--user stop velocita-tape-recorder.service velocita-tape-voter.service"), 2)
        self.assertEqual(calls.count("--user start velocita-tape-voter.service"), 1, "the doubled cool-down has not passed")
        self.assertIn("for at least 600 s", out)


if __name__ == "__main__":
    unittest.main()
