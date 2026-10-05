"""The heartbeat script, run against a stand-in curl."""
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "deploy/v2/heartbeat.sh"
UNIT = ROOT / "deploy/v2/systemd/velocita-heartbeat.service"
TIMER = ROOT / "deploy/v2/systemd/velocita-heartbeat.timer"
PING = "https://monitor.example/ping/abc-123"
NTFY = "http://ntfy.example:2586"
LOCAL = ["http://127.0.0.1:9090/-/ready", "http://127.0.0.1:9093/-/ready", NTFY + "/v1/health"]

FAKE_CURL = """#!/bin/sh
for last; do :; done
echo "$last" >> "$CURL_LOG"
grep -qxF "$last" "$CURL_FAILING" && exit 22
exit 0
"""


class Heartbeat(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp())
        curl = self.dir / "bin" / "curl"
        curl.parent.mkdir()
        curl.write_text(FAKE_CURL)
        curl.chmod(curl.stat().st_mode | stat.S_IXUSR)
        (self.dir / "url").write_text(PING + "\n")
        self.log = self.dir / "curl.log"
        self.failing = self.dir / "failing"
        self.failing.write_text("")
        self.textfile = self.dir / "heartbeat.prom"

    def run_script(self, failing=()):
        self.failing.write_text("".join(url + "\n" for url in failing))
        self.log.write_text("")
        env = {"PATH": f"{self.dir / 'bin'}:{os.environ['PATH']}", "CURL_LOG": str(self.log),
               "CURL_FAILING": str(self.failing), "VELOCITA_NTFY_BASE_URL": NTFY}
        return subprocess.run(["/bin/sh", str(SCRIPT), str(self.dir / "url"), str(self.textfile)],
                              env=env, capture_output=True, text=True)

    def requested(self):
        return self.log.read_text().split()

    def metric(self, name):
        for line in self.textfile.read_text().splitlines():
            if line.startswith(name + " "):
                return int(line.split()[1])
        raise AssertionError(f"{name} is not in the textfile")

    def test_a_healthy_alert_path_pings_the_plain_url(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requested(), LOCAL + [PING])
        self.assertEqual(self.metric("velocita_heartbeat_alert_path_ok"), 1)
        self.assertEqual(self.metric("velocita_heartbeat_sent"), 1)
        self.assertGreater(self.metric("velocita_heartbeat_last_sent_timestamp_seconds"), 1_700_000_000)

    def test_each_broken_local_service_turns_the_ping_into_a_failure_report(self):
        for broken in LOCAL:
            with self.subTest(broken=broken):
                self.run_script(failing=[broken])
                self.assertEqual(self.requested()[-1], PING + "/fail")
                self.assertNotIn(PING, self.requested())
                self.assertEqual(self.metric("velocita_heartbeat_alert_path_ok"), 0)
                self.assertEqual(self.metric("velocita_heartbeat_sent"), 1)

    def test_an_unreachable_monitor_is_recorded_and_keeps_the_last_success_time(self):
        self.run_script()
        first = self.metric("velocita_heartbeat_last_sent_timestamp_seconds")
        text = self.textfile.read_text().replace(str(first), "1750000000")
        self.textfile.write_text(text)
        result = self.run_script(failing=[PING])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.metric("velocita_heartbeat_sent"), 0)
        self.assertEqual(self.metric("velocita_heartbeat_alert_path_ok"), 1)
        self.assertEqual(self.metric("velocita_heartbeat_last_sent_timestamp_seconds"), 1750000000)

    def test_the_ping_url_never_reaches_the_journal(self):
        for failing in ([], [PING], LOCAL):
            result = self.run_script(failing=failing)
            self.assertNotIn("abc-123", result.stdout + result.stderr)
            self.assertNotIn("abc-123", self.textfile.read_text())

    def test_wrong_arguments_are_a_usage_error(self):
        result = subprocess.run(["/bin/sh", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 64)


class Units(unittest.TestCase):
    def test_the_service_runs_the_script_only_once_a_url_is_configured(self):
        unit = UNIT.read_text()
        self.assertIn("ConditionPathExists=%h/.config/velocita/heartbeat-url", unit)
        self.assertIn("EnvironmentFile=%h/.config/velocita/host.env", unit)
        self.assertIn("%h/src/aeron-raft-pi/deploy/v2/heartbeat.sh %h/.config/velocita/heartbeat-url "
                      "%h/velocita-v2/textfile/velocita-heartbeat.prom", unit)
        self.assertTrue(SCRIPT.exists())

    def test_the_timer_fires_every_minute(self):
        timer = TIMER.read_text()
        self.assertIn("OnUnitActiveSec=60", timer)
        self.assertIn("WantedBy=timers.target", timer)


if __name__ == "__main__":
    unittest.main()
