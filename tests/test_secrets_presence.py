"""deploy/v2/secrets-presence.sh: which secrets a voter needs, and whether each
is in the RAM-only secrets directory, as node-exporter textfile metrics.

Expected metrics are written by hand from the fixture config below. The script
must never read a secret: an unreadable key file still counts as present.
"""
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "deploy/v2/secrets-presence.sh"

CONFIG = """node_id = 1
voter.1.endpoint = 100.72.127.102:42201
voter.2.endpoint = 100.84.151.116:42202
voter.2.key_file = /run/velocita-secrets/link-1-2.key
caller.9.caller_id = 7
caller.9.key_file = /run/velocita-secrets/caller-9.key
caller.11.key_file   =   /run/velocita-secrets/caller-11.key
# caller.12.key_file = /run/velocita-secrets/commented-out.key
"""


class SecretsPresence(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.dir.name)
        self.config, self.secrets, self.out = root / "node-1.conf", root / "secrets", root / "velocita-secrets.prom"
        self.config.write_text(CONFIG)
        self.secrets.mkdir(mode=0o700)

    def tearDown(self):
        for p in self.secrets.glob("*"):
            p.chmod(0o600)
        self.dir.cleanup()

    def run_check(self, config=None):
        env = {**os.environ, "VELOCITA_NODE": "1", "VELOCITA_SSH_HOST": "nuc1"}
        subprocess.run(["bash", str(SCRIPT), str(config or self.config), str(self.secrets), str(self.out)],
                       check=True, env=env, capture_output=True, text=True)
        return self.out.read_text()

    def line(self, name, value):
        path = self.secrets / f"{name}.key"
        return (f'velocita_secret_present{{secret="{name}",path="{path}",node="1",ssh_host="nuc1"}} {value}\n')

    def test_each_needed_secret_is_reported_present_or_missing(self):
        (self.secrets / "link-1-2.key").write_bytes(b"\x01" * 32)
        text = self.run_check()
        self.assertIn(self.line("link-1-2", 1), text)
        self.assertIn(self.line("caller-9", 0), text)
        self.assertIn(self.line("caller-11", 0), text)
        self.assertNotIn("commented-out", text)
        self.assertEqual(text.count("velocita_secret_present{"), 3)
        self.assertIn("velocita_secrets_config_ok 1\n", text)
        self.assertRegex(text, r"velocita_secrets_check_timestamp_seconds \d{10}\n")

    def test_a_secret_is_never_read(self):
        key = self.secrets / "caller-9.key"
        key.write_bytes(b"\x02" * 32)
        key.chmod(0o000)  # unreadable: presence must come from the directory, not the contents
        self.assertIn(self.line("caller-9", 1), self.run_check())

    def test_an_empty_file_is_missing(self):
        (self.secrets / "caller-11.key").write_bytes(b"")  # an interrupted push
        self.assertIn(self.line("caller-11", 0), self.run_check())

    def test_a_missing_directory_reports_every_secret_missing(self):
        self.secrets.rmdir()
        text = self.run_check()
        self.assertEqual(text.count(" 0\n", 0, text.index("velocita_secrets_config_ok")), 3)

    def test_the_shipped_templates_need_the_strategy_and_gateway_keys(self):
        # Every voter authenticates every caller, so each needs caller-12 (strategy
        # tick loop) and caller-13 (paper gateway) beside its links and the others.
        root = SCRIPT.parent
        expected = {
            "three-host/node-1.conf.template": ["link-1-2", "link-1-3"],
            "three-host/node-2.conf.template": ["link-1-2", "link-2-3"],
            "three-host/node-3.conf.template": ["link-1-3", "link-2-3"],
            "two-host/node-1.conf.template": ["link-1-2"],
            "two-host/node-2.conf.template": ["link-1-2"],
        }
        callers = ["caller-9", "caller-10", "caller-11", "caller-12", "caller-13"]
        for template, links in expected.items():
            text = self.run_check(config=root / template)
            reported = re.findall(r'velocita_secret_present\{secret="([^"]+)"', text)
            self.assertEqual(sorted(reported), sorted(links + callers), template)

    def test_an_unusable_config_is_reported_not_silent(self):
        text = self.run_check(config=pathlib.Path(self.dir.name) / "absent.conf")
        self.assertIn("velocita_secrets_config_ok 0\n", text)
        self.assertNotIn("velocita_secret_present{", text)
        empty = pathlib.Path(self.dir.name) / "no-keys.conf"
        empty.write_text("node_id = 1\n")
        self.assertIn("velocita_secrets_config_ok 0\n", self.run_check(config=empty))


class UnitSecretMounts(unittest.TestCase):
    """Split authority on nuc1: each podman unit mounts exactly the one caller
    key its role owns, and only the one-shot setup sees the operator's key 11."""

    SYSTEMD = SCRIPT.parent / "systemd"

    def mounts(self, unit):
        text = (self.SYSTEMD / unit).read_text()
        return re.findall(r"-v %t/velocita-secrets([^:\s]*):", text), text

    def test_each_process_sees_only_its_own_caller_key(self):
        roles = {"velocita-v2-shadow-edge.service": ("12", "21"),
                 "velocita-v2-gateway.service": ("13", "22"),
                 "velocita-v2-shadow-setup.service": ("11", "9")}
        for unit, (key, caller) in roles.items():
            mounts, text = self.mounts(unit)
            self.assertEqual(mounts, [f"/caller-{key}.key"], unit)
            self.assertIn(f"--caller-key-id {key} --caller-id {caller} ", text, unit)

    def test_setup_installs_the_program_the_edge_evaluates(self):
        # Every argument that shapes the program text must match, or the edge's
        # frames would name a policy the operator never installed.
        shaping = ("--strategy", "--value", "--usd-per-tick", "--catalog-root", "--catalog-row", "--policy-id",
                   "--policy-version", "--feature-schema-id", "--valid-from-ns", "--valid-until-ns",
                   "--order-quantity-lots", "--max-notional-micros", "--max-age-ns", "--source-id",
                   "--time-in-force", "--post-only", "--program-out", "--period-s", "--exit-window-s",
                   "--window-min", "--enter-below-bps", "--exit-above-bps")
        # A volume strategy is chosen in shadow.env; both units must read it, after
        # their literal arguments so it can replace them (argparse keeps the last).
        chosen = "$VELOCITA_STRATEGY_ARGS"

        def program_words(unit, start, end):
            text = (self.SYSTEMD / unit).read_text().replace("\\\n", " ")
            words = text[text.index(start):].split()
            return words[1:words.index(end)] if end in words else words[1:]

        def program_args(words):
            return {w: words[i + 1] for i, w in enumerate(words) if w in shaping}

        edge = program_words("velocita-v2-shadow-edge.service", "localhost/velocita-v2-shadow:m3", "--")
        setup = program_words("velocita-v2-shadow-setup.service", "localhost/velocita-v2-shadow:m3", "--program-only")
        self.assertEqual(program_args(setup), program_args(edge))
        self.assertEqual(program_args(edge)["--order-quantity-lots"], "25")
        for words in (edge, setup):
            self.assertEqual(words.count(chosen), 1)
            self.assertTrue(all(words.index(chosen) > words.index(w) for w in shaping if w in words))

    def test_no_long_running_unit_holds_the_operator_key(self):
        for unit in sorted(self.SYSTEMD.glob("*.service")):
            text = unit.read_text()
            if "Type=oneshot" in text:
                continue
            self.assertNotIn("caller-11", text.split("[Unit]", 1)[1], unit.name)
            self.assertNotIn("caller-9.key", text, unit.name)


if __name__ == "__main__":
    unittest.main()
