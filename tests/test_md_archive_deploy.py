"""Deploy files for the nightly market-data archive: the nuc1 unit and timer,
the Mac's launchd job, and the alerts.

The archive must act on the same database md-stream writes, publish where the
Mac pulls from, and run after the UTC day ends; the Mac's job must run after
that. Expected paths come from velocita-md-stream.service (the running
recorder) and the pull tool's defaults, not from the archive unit itself.
"""
import plistlib
import re
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
SYSTEMD = ROOT / "deploy/v2/systemd"
UNIT = SYSTEMD / "velocita-md-archive.service"
TIMER = SYSTEMD / "velocita-md-archive.timer"
STREAM = SYSTEMD / "velocita-md-stream.service"
PLIST = ROOT / "deploy/mac/com.velocita.md-backup.plist"
TOOL = ROOT / "tools/md-backup-pull"
OBS = ROOT / "deploy/v2/observability"


def words(unit):
    text = unit.read_text().replace("\\\n", " ")
    return text[text.index("ExecStart=/usr/bin/podman"):].split("\n", 1)[0].split()


def mounts(unit):
    w = words(unit)
    return {w[i + 1].split(":")[1]: w[i + 1].split(":")[0] for i, x in enumerate(w) if x == "-v"}


def arg(unit, flag):
    w = words(unit)
    return w[w.index(flag) + 1]


def host_path(unit, container_path):
    for container, host in sorted(mounts(unit).items(), key=lambda kv: -len(kv[0])):
        if container_path == container or container_path.startswith(container + "/"):
            return host + container_path[len(container):]
    return None


def env(unit):
    w = words(unit)
    return dict(w[i + 1].split("=", 1) for i, x in enumerate(w) if x == "-e")


class ArchiveUnit(unittest.TestCase):
    def test_archives_the_database_md_stream_writes(self):
        self.assertEqual(host_path(UNIT, arg(UNIT, "--db")), host_path(STREAM, arg(STREAM, "--db")))
        image = [w for w in words(STREAM) if w.startswith("docker.io/")]
        self.assertEqual([w for w in words(UNIT) if w.startswith("docker.io/")], image)
        self.assertEqual(mounts(UNIT)["/model"], mounts(STREAM)["/model"])

    def test_archive_lands_where_the_mac_pulls_from(self):
        default = re.search(r'"--remote-dir", default="([^"]+)"', TOOL.read_text()).group(1)
        self.assertEqual(host_path(UNIT, arg(UNIT, "--archive-dir")), f"%h/{default}")

    def test_its_metrics_do_not_overwrite_md_streams(self):
        ours, theirs = host_path(UNIT, arg(UNIT, "--textfile")), host_path(STREAM, arg(STREAM, "--textfile"))
        self.assertNotEqual(ours, theirs)
        self.assertEqual(Path(ours).parent, Path(theirs).parent, "node-exporter reads one textfile directory")

    def test_confined_like_md_stream_and_offline(self):
        w = words(UNIT)
        for flag in ("--read-only", "--security-opt", "no-new-privileges", "--userns=keep-id"):
            self.assertIn(flag, w)
        self.assertEqual(w[w.index("--cap-drop") + 1], "all")
        self.assertEqual(w[w.index("--network") + 1], "none")
        self.assertTrue(mounts(UNIT)["/model"] and "/model:ro" in " ".join(w))
        self.assertNotIn("velocita-secrets", UNIT.read_text())
        self.assertIn("Type=oneshot", UNIT.read_text())

    def test_sqlite_temp_files_go_to_a_writable_created_directory(self):
        tmp = env(UNIT)["SQLITE_TMPDIR"]
        host = host_path(UNIT, tmp)
        self.assertIsNotNone(host, "SQLITE_TMPDIR is not on a mounted volume")
        mount = next(m for m in words(UNIT) if m.startswith(host.split("/velocita-md")[0] + "/velocita-md:"))
        self.assertFalse(mount.endswith(":ro"), mount)
        self.assertIn(host, re.search(r"ExecStartPre=/usr/bin/mkdir -p (.*)", UNIT.read_text()).group(1).split())

    def test_prunes_with_the_documented_windows(self):
        w = words(UNIT)
        self.assertIn("--prune", w)
        self.assertEqual((arg(UNIT, "--retention-days"), arg(UNIT, "--keep-archives-days")), ("3", "7"))

    def test_every_flag_is_one_the_cli_accepts(self):
        sys.path.insert(0, str(ROOT / "model/src"))
        try:
            from velocita_model import cli
        finally:
            sys.path.pop(0)
        w = words(UNIT)
        argv = w[w.index("velocita_model.cli") + 1:]
        parsed = cli.build_parser().parse_args(argv)
        self.assertEqual(parsed.handler, cli.cmd_md_archive)
        self.assertTrue(parsed.prune)


class Timer(unittest.TestCase):
    def test_runs_after_the_utc_day_and_the_grace_period(self):
        text = TIMER.read_text()
        when = re.search(r"^OnCalendar=\*-\*-\* (\d\d):(\d\d):00 UTC$", text, re.MULTILINE)
        self.assertIsNotNone(when, "the timer must name UTC: archive days are UTC days")
        minutes = int(when.group(1)) * 60 + int(when.group(2))
        grace_s = 600
        self.assertGreater(minutes * 60, grace_s)
        self.assertIn("Persistent=true", text)
        self.assertIn("WantedBy=timers.target", text)


class MacJob(unittest.TestCase):
    def load(self):
        text = PLIST.read_text().replace("__REPO__", "/Users/owner/aeron-raft-pi").replace("__HOME__", "/Users/owner")
        return plistlib.loads(text.encode())

    def test_runs_the_pull_tool_with_the_system_python(self):
        job = self.load()
        args = job["ProgramArguments"]
        self.assertEqual(args[:2], ["/usr/bin/python3", "/Users/owner/aeron-raft-pi/tools/md-backup-pull"])
        self.assertEqual(args[args.index("--host") + 1], "velocitas@nuc1")
        self.assertNotIn("rsync", " ".join(args))
        self.assertEqual(job["Label"], PLIST.stem)
        self.assertNotIn("__", PLIST.read_text().replace("__REPO__", "").replace("__HOME__", ""))

    def test_runs_after_nuc1_has_archived_in_summer_and_winter(self):
        job = self.load()
        local = job["StartCalendarInterval"]
        when = re.search(r"OnCalendar=\*-\*-\* (\d\d):(\d\d):00 UTC", TIMER.read_text())
        archive_utc = int(when.group(1)) * 60 + int(when.group(2))
        eastern = ZoneInfo("America/New_York")
        for day in ((2026, 7, 1), (2026, 12, 1)):
            run = datetime(*day, local["Hour"], local["Minute"], tzinfo=eastern).astimezone(timezone.utc)
            pull_utc = (run.hour * 60 + run.minute) % (24 * 60)
            self.assertGreaterEqual(pull_utc - archive_utc, 30, f"{day}: the pull would race the archive")
            self.assertLess(pull_utc, 12 * 60, f"{day}: the pull should come the same night")


class Alerts(unittest.TestCase):
    NAMES = ("VelocitaMdBackupNotAcked", "VelocitaMdStreamDbLarge", "VelocitaMdArchiveFailing",
             "VelocitaMdArchiveBlocked")

    def test_each_alert_has_a_firing_and_a_quiet_rule_test(self):
        rules = (OBS / "rules/velocita-v2-alerts.yml").read_text()
        tests = (OBS / "rule-tests/velocita-v2-alerts.test.yml").read_text()
        for name in self.NAMES:
            self.assertIn(f"- alert: {name}\n", rules)
            self.assertRegex(tests, rf"alertname: {name}\n\s+exp_alerts: \[\]")
            self.assertRegex(tests, rf"alertname: {name}\n\s+exp_alerts:\n\s+- exp_labels:")

    def test_alerts_read_the_metrics_md_archive_writes(self):
        sys.path.insert(0, str(ROOT / "model/src"))
        try:
            from velocita_model.storage import md_archive
        finally:
            sys.path.pop(0)
        source = Path(md_archive.__file__).read_text()
        rules = (OBS / "rules/velocita-v2-alerts.yml").read_text()
        for metric in set(re.findall(r"\bvelocita_md_(?:archive|stream_db)_[a-z_]+", rules)):
            self.assertIn(metric, source, f"{metric} is alerted on but never written")


if __name__ == "__main__":
    unittest.main()
