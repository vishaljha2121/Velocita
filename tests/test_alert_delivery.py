"""Alert delivery wiring: Prometheus -> Alertmanager -> ntfy -> phone.

Four files and two units must agree on addresses, ports, paths and names, and
nothing checks that at start: a wrong port or template name is a silent "no
alert ever arrives". These tests read the files as deployed (through the units'
own bind mounts) and compare them with each other.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROMETHEUS = ROOT / "deploy/v2/observability/prometheus.yml"
RULES = ROOT / "deploy/v2/observability/rules/velocita-v2-alerts.yml"
AM_UNIT = ROOT / "deploy/v2/systemd/velocita-v2-alertmanager.service"
NTFY_UNIT = ROOT / "deploy/v2/systemd/velocita-ntfy.service"
HOST_ENV = ROOT / "deploy/v2/systemd/host.env.example"


def repo_mounts(unit):
    return {c: ROOT / h for h, c in re.findall(r"-v %h/src/aeron-raft-pi/(\S+?):(/\S+?):ro\s", unit.read_text())}


def one(pattern, text, what):
    found = re.findall(pattern, text, re.MULTILINE)
    if len(found) != 1:
        raise AssertionError(f"expected exactly one {what}, found {found}")
    return found[0]


AM_CONFIG = repo_mounts(AM_UNIT)["/etc/alertmanager/alertmanager.yml"]
NTFY_CONFIG = repo_mounts(NTFY_UNIT)["/etc/ntfy/server.yml"]
TEMPLATES = repo_mounts(NTFY_UNIT)["/etc/ntfy/templates"]
WEBHOOK = one(r'^\s+- url: "(\S+)"$', AM_CONFIG.read_text(), "webhook url")


class MountedFilesExist(unittest.TestCase):
    def test_every_repo_file_a_unit_mounts_is_in_the_repo(self):
        for unit in (AM_UNIT, NTFY_UNIT):
            mounts = repo_mounts(unit)
            self.assertTrue(mounts, unit.name)
            for container, local in mounts.items():
                self.assertTrue(local.exists(), f"{unit.name} mounts {local.relative_to(ROOT)}, which does not exist")


class AddressesAgree(unittest.TestCase):
    def test_prometheus_sends_alerts_where_alertmanager_listens(self):
        listen = one(r"--web\.listen-address=(\S+)", AM_UNIT.read_text(), "alertmanager listen address")
        block = PROMETHEUS.read_text().split("alerting:", 1)[1].split("scrape_configs:", 1)[0]
        self.assertEqual(re.findall(r'targets: \["(\S+)"\]', block), [listen])
        scrape = PROMETHEUS.read_text().split("job_name: alertmanager", 1)[1].split("job_name:", 1)[0]
        self.assertIn(f'targets: ["{listen}"]', scrape)
        self.assertTrue(listen.startswith("127.0.0.1:"), "Alertmanager's API must not be reachable off the host")

    def test_the_webhook_reaches_ntfy_on_nuc1s_tailscale_address_and_port(self):
        host, port = re.fullmatch(r"http://([\d.]+):(\d+)/.*", WEBHOOK).groups()
        addresses = set(re.findall(r'targets: \["([\d.]+):\d+"\]\n\s+labels: \{host: nuc1[,}]', PROMETHEUS.read_text()))
        addresses.discard("127.0.0.1")
        self.assertEqual(len(addresses), 1, addresses)
        nuc1 = addresses.pop()
        self.assertEqual(host, nuc1)
        self.assertEqual(one(r"NTFY_LISTEN_HTTP=\$\{VELOCITA_TAILSCALE_IP\}:(\d+)", NTFY_UNIT.read_text(), "ntfy port"), port)
        self.assertEqual(one(r"^VELOCITA_NTFY_BASE_URL=(\S+)$", HOST_ENV.read_text(), "base url"), f"http://{host}:{port}")
        self.assertIn("-e NTFY_BASE_URL=${VELOCITA_NTFY_BASE_URL}", NTFY_UNIT.read_text())

    def test_the_webhook_names_a_template_that_exists_and_one_topic_under_the_acl_prefix(self):
        topic, template = re.fullmatch(r"http://[^/]+/([a-z-]+)\?template=([a-z]+)", WEBHOOK).groups()
        self.assertTrue((TEMPLATES / f"{template}.yml").is_file(), f"ntfy has no template {template}.yml")
        self.assertTrue(topic.startswith("velocita-"), "the runbook grants access to velocita-* only")
        self.assertEqual(one(r'^template-dir: "(\S+)"$', NTFY_CONFIG.read_text(), "template-dir"), "/etc/ntfy/templates")


class TheTokenStaysOutOfTheRepository(unittest.TestCase):
    def test_alertmanager_reads_the_token_from_a_file_mounted_from_outside_the_repo(self):
        credentials = one(r"credentials_file: (\S+)", AM_CONFIG.read_text(), "credentials_file")
        host_path = one(rf"-v (\S+):{re.escape(credentials)}:ro", AM_UNIT.read_text(), "token mount")
        self.assertTrue(host_path.startswith("%h/.config/velocita/"), host_path)
        self.assertNotIn("credentials:", AM_CONFIG.read_text(), "an inline token would be committed")

    def test_no_ntfy_token_or_password_hash_is_committed(self):
        for path in [AM_CONFIG, NTFY_CONFIG, AM_UNIT, NTFY_UNIT, HOST_ENV, ROOT / "docs/ALERT_DELIVERY_RUNBOOK.md"]:
            text = path.read_text()
            self.assertIsNone(re.search(r"\btk_[a-z0-9]{29}\b", text), f"{path.name} holds an ntfy token")
            self.assertIsNone(re.search(r"\$2[aby]\$\d\d\$", text), f"{path.name} holds a password hash")


class NtfyIsClosedByDefault(unittest.TestCase):
    def test_no_anonymous_access_and_no_signup(self):
        text = NTFY_CONFIG.read_text()
        self.assertEqual(one(r'^auth-default-access: "(\S+)"$', text, "auth-default-access"), "deny-all")
        self.assertEqual(one(r"^enable-signup: (\S+)$", text, "enable-signup"), "false")
        for key in ("auth-users", "auth-tokens"):
            self.assertNotIn(key, text, "accounts are created with the CLI, never written in the config")


class SeveritiesAreTheOnesTheRouteAndTemplateKnow(unittest.TestCase):
    def test_every_rule_is_critical_or_warning(self):
        severities = re.findall(r"labels: \{severity: (\w+)\}", RULES.read_text())
        self.assertEqual(len(severities), len(re.findall(r"^\s+- alert:", RULES.read_text(), re.MULTILINE)))
        self.assertEqual(set(severities), {"critical", "warning"})
        template = (TEMPLATES / "velocita.yml").read_text()
        self.assertIn('labels.severity "critical"', template)
        self.assertIn("severity=\"critical\"", AM_CONFIG.read_text())


if __name__ == "__main__":
    unittest.main()
