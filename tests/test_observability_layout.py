"""Observability files: what Prometheus and Grafana load must be what we mean.

Rule files: Prometheus globs `rule_files` at start. A promtool unit-test file
left in the rules directory (it also ends in .yml) made Prometheus refuse to
start after a reboot; it had been running on an older directory listing. Paths
are resolved through the unit's own bind mounts, so a moved mount is checked too.

Dashboards: the single dashboard was split into five. The pre-split panels are
snapshotted in observability_pre_split_panels.json (taken from the generator's
output before the split, not from the code under test), so a panel or query
dropped by a later edit fails here.
"""
import contextlib
import glob
import importlib.util
import io
import json
import pathlib
import re
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / "deploy/v2/observability/prometheus.yml"
UNIT = ROOT / "deploy/v2/systemd/velocita-v2-prometheus.service"
OBS = ROOT / "deploy/v2/observability"
DASHBOARD_DIR = OBS / "grafana/dashboards"
GRAFANA_UNIT = ROOT / "deploy/v2/systemd/velocita-v2-grafana.service"
PROVIDER = OBS / "grafana/provisioning/dashboards/velocita.yml"
PRE_SPLIT = pathlib.Path(__file__).resolve().parent / "observability_pre_split_panels.json"
OLD_UID = "velocita-v2-cluster"


def rule_globs():
    lines, globs, inside = CONFIG.read_text().splitlines(), [], False
    for line in lines:
        if re.match(r"^rule_files:\s*$", line):
            inside = True
        elif inside and re.match(r"^\s+-\s+", line):
            globs.append(line.split("-", 1)[1].strip().strip("'\""))
        elif inside and line.strip() and not line.startswith(" "):
            inside = False
    return globs


def mounts(unit=UNIT):
    out = {}
    for host, container in re.findall(r"-v %h/src/aeron-raft-pi/(\S+?):(/\S+?)(?::ro)?\s", unit.read_text()):
        out[container] = ROOT / host
    return out


def top_level_keys(path):
    return {m.group(1) for m in re.finditer(r"^([A-Za-z_]+):", path.read_text(), re.MULTILINE)}


class RuleFilesAreRuleFiles(unittest.TestCase):
    def test_every_loaded_file_is_a_rule_group_file(self):
        globs = rule_globs()
        self.assertTrue(globs, "prometheus.yml declares no rule_files")
        loaded = []
        for pattern in globs:
            directory, name = pattern.rsplit("/", 1)
            local = mounts().get(directory)
            self.assertIsNotNone(local, f"{directory} is not mounted into the Prometheus container")
            loaded += [pathlib.Path(p) for p in glob.glob(str(local / name))]
        self.assertTrue(loaded, "the rule_files globs match nothing: no alerts would load")
        for path in loaded:
            self.assertEqual(top_level_keys(path), {"groups"},
                             f"{path.relative_to(ROOT)} is loaded as rules but is not a rule-group file")


def load_generator():
    spec = importlib.util.spec_from_file_location("generate_dashboard", OBS / "generate_dashboard.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def dashboards():
    return {p.name: json.loads(p.read_text()) for p in sorted(DASHBOARD_DIR.glob("*.json"))}


def content_panels(dashboard):
    return [p for p in dashboard["panels"] if p["type"] != "row"]


def exprs(dashboard):
    return [legacy_expression(t["expr"]) for p in content_panels(dashboard) for t in p.get("targets", [])]


def legacy_expression(expression):
    expression=expression.replace('strategy="stochastic",', '').replace('{strategy="stochastic"}', '')
    # The completed-session display preserves the original readiness query as
    # its fallback. Check that fallback for the historical no-panel-loss test;
    # the new lifecycle wrapper is asserted independently below.
    prefix='((max(velocita_indicator_run_state) == 1) * 0 + 2) or '
    return expression[len(prefix):] if expression.startswith(prefix) else expression


PAPER_METRICS = {
    "velocita_gateway_up", "velocita_gateway_new_risk_stopped", "velocita_gateway_orders_sent_total",
    "velocita_gateway_fills_total", "velocita_gateway_rejects_total", "velocita_gateway_unknown_outcomes_total",
    "velocita_gateway_authority_requests_total", "velocita_gateway_stage_ms_bucket", "velocita_gateway_stage_ms_sum",
    "velocita_gateway_stage_ms_count", "velocita_gateway_position_lots", "velocita_gateway_fees_usd_total",
    "velocita_gateway_last_fill_timestamp_seconds", "velocita_gateway_volume_usd_total",
    "velocita_gateway_reconciliation_mismatch", "velocita_gateway_reconciliation_position_lots",
    "velocita_gateway_rearms_total", "velocita_gateway_reconciliation_last_match_timestamp_seconds",
    "velocita_gateway_stops_total", "velocita_gateway_stop_cause",
}
PAPER_STAGES = {"authorized_to_proof", "proof_to_sent", "sent_to_fill", "fill_to_committed"}


class DashboardsAreGeneratedAndLoadable(unittest.TestCase):
    def test_committed_files_are_exactly_the_generator_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            with contextlib.redirect_stdout(io.StringIO()):
                load_generator().main(["generate_dashboard.py", tmp])
            generated = {p.name: p.read_text() for p in pathlib.Path(tmp).glob("*.json")}
        committed = {p.name: p.read_text() for p in DASHBOARD_DIR.glob("*.json")}
        self.assertEqual(set(committed), set(generated), "a stale or missing dashboard file in grafana/dashboards")
        for name in generated:
            self.assertEqual(committed[name], generated[name], f"{name} is stale: rerun generate_dashboard.py")

    def test_six_dashboards_with_unique_uids_titles_and_panel_ids(self):
        boards = dashboards()
        self.assertEqual(len(boards), 6)
        uids = [d["uid"] for d in boards.values()]
        titles = [d["title"] for d in boards.values()]
        self.assertEqual(len(set(uids)), 6, uids)
        self.assertEqual(len(set(titles)), 6, titles)
        for name, d in boards.items():
            self.assertTrue(re.fullmatch(r"[a-z0-9-]{1,40}", d["uid"]), f"{name}: uid {d['uid']!r}")
            ids = [p["id"] for p in d["panels"]]
            self.assertEqual(len(ids), len(set(ids)), f"{name}: duplicate panel ids")

    def test_panels_fit_the_grid_without_overlapping(self):
        for name, d in dashboards().items():
            cells = {}
            for p in d["panels"]:
                g = p["gridPos"]
                self.assertLessEqual(g["x"] + g["w"], 24, f"{name}: {p['title']} is wider than the page")
                for x in range(g["x"], g["x"] + g["w"]):
                    for y in range(g["y"], g["y"] + g["h"]):
                        self.assertNotIn((x, y), cells, f"{name}: {p['title']} overlaps {cells.get((x, y))}")
                        cells[(x, y)] = p["title"]

    def test_every_dashboard_links_to_every_dashboard_in_one_order(self):
        boards = dashboards()
        expected = None
        for name, d in boards.items():
            urls = [link["url"] for link in d.get("links", [])]
            expected = expected or urls
            self.assertEqual(urls, expected, f"{name} has a different link bar")
        self.assertEqual(sorted(expected), sorted(f"/d/{d['uid']}" for d in boards.values()))

    def test_no_pre_split_panel_or_query_was_lost(self):
        before = json.loads(PRE_SPLIT.read_text())["panels"]
        self.assertEqual(len(before), 46)
        after = {(p["type"], p["title"], tuple(legacy_expression(t["expr"]) for t in p.get("targets", [])))
                 for d in dashboards().values() for p in content_panels(d)}
        for panel in before:
            self.assertIn((panel["type"], panel["title"], tuple(panel["exprs"])), after,
                          f"pre-split panel {panel['title']!r} (row {panel['row']!r}) is gone or changed")
        before_exprs = {e for panel in before for e in panel["exprs"]}
        after_exprs = {e for d in dashboards().values() for e in exprs(d)}
        self.assertEqual(before_exprs - after_exprs, set())

    def test_submitter_completion_display_preserves_live_readiness(self):
        matched=[]
        for board in dashboards().values():
            for panel in content_panels(board):
                if panel['title']=='Submitter ready':
                    matched.append(panel)
                    self.assertEqual(panel['targets'][0]['expr'],
                        '((max(velocita_indicator_run_state{strategy="stochastic"}) == 1) * 0 + 2) or max(velocita_shadow_submitter_ready{strategy="stochastic"})')
                    self.assertEqual(panel['fieldConfig']['defaults']['mappings'][0]['options']['2']['text'],'COMPLETED')
        self.assertEqual(len(matched),2)

    def test_overview_answers_cluster_health_at_a_glance(self):
        overview = next(d for d in dashboards().values() if d["uid"] == OLD_UID)
        types = {p["type"] for p in content_panels(overview)}
        self.assertIn("alertlist", types)
        for expr in ('max(velocita_v2_leader_id)', 'max(velocita_v2_term)', 'sum(velocita_v2_up)',
                     'max(velocita_v2_log_index{kind="commit"})', 'max(velocita_shadow_submitter_ready)',
                     'min by (host) (velocita_secret_present)', 'max(velocita_gateway_new_risk_stopped)'):
            self.assertIn(expr, exprs(overview))
        self.assertTrue(any("applied" in e for e in exprs(overview)), "no commit -> applied view on the overview")

    def test_paper_dashboard_references_every_gateway_metric(self):
        paper = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-paper")
        text = "\n".join(exprs(paper))
        self.assertEqual(set(re.findall(r"\bvelocita_gateway_[a-z_]+", text)), PAPER_METRICS)
        self.assertEqual(set(re.findall(r'stage="([a-z_]+)"', text)), PAPER_STAGES)
        for label in ("side", "reason", "result"):
            self.assertRegex(text, rf"by \({label}\)")
        legends = {t.get("legendFormat", "") for p in content_panels(paper) for t in p.get("targets", [])}
        self.assertIn("{{account}} {{instrument}}", legends)
        self.assertTrue(any(p["type"] == "timeseries" and p["fieldConfig"]["defaults"]["custom"]["stacking"]["mode"] == "normal"
                            and "stage_ms_sum" in "".join(t["expr"] for t in p["targets"]) for p in content_panels(paper)),
                        "no stacked stage-latency panel")
        self.assertTrue(any("histogram_quantile(0.99" in e and "stage_ms_bucket" in e for e in exprs(paper)))
        fees = [p for p in content_panels(paper) if any("fees_usd_total" in t["expr"] for t in p.get("targets", []))]
        self.assertTrue(fees)
        for p in fees:
            self.assertEqual(p["fieldConfig"]["defaults"]["unit"], "suffix:¢", p["title"])
            self.assertTrue(all(t["expr"].startswith("100 * ") for t in p["targets"]), p["title"])

    def test_gateway_volume_is_drawn_by_side(self):
        paper = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-paper")
        self.assertIn('sum by (side) (3600 * rate(velocita_gateway_volume_usd_total[1h]))', "\n".join(exprs(paper)))
        self.assertEqual(set(re.findall(r"\bvelocita_paper_[a-z_]+", "\n".join(exprs(paper)))), set())

    def test_role_and_election_queries_cannot_come_back_empty_or_extrapolated(self):
        overview = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-cluster")
        role = next(p for p in overview["panels"] if p["title"] == "Role")["targets"][0]["expr"]
        self.assertTrue(role.startswith("max by (host, node) ("), role)
        self.assertNotIn("} * 3 + ", role)
        elections = next(p for p in overview["panels"] if p["title"] == "Elections (last hour)")["targets"][0]["expr"]
        self.assertNotIn("increase(", elections)
        self.assertIn("min_over_time(", elections)

    def test_soak_panels_go_blank_when_the_bot_is_not_reporting(self):
        raft = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-raft")
        soak = [e for e in exprs(raft) if "velocita_soak_" in e and "last_update_timestamp_seconds) <" in e]
        self.assertEqual(len(soak), 6)

    def test_strategy_dashboard_only_asks_for_metrics_the_edge_exports(self):
        strategy = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-strategy")
        text = "\n".join(exprs(strategy))
        for retired in ("velocita_shadow_book_", "velocita_shadow_hold_", 'outcome="tick"', 'decision_code="17"'):
            self.assertNotIn(retired, text)
        self.assertIn('velocita_indicator_paper_authorizations_total{code="applied"}', text)

    def test_a_paper_run_label_scopes_every_run_metric_and_the_scrape_config(self):
        generator = load_generator()
        with tempfile.TemporaryDirectory() as tmp:
            with contextlib.redirect_stdout(io.StringIO()):
                generator.main(["generate_dashboard.py", tmp, "--paper-run", "run-1"])
            generator._paper_run = None
            for path in pathlib.Path(tmp).glob("*.json"):
                board=json.loads(path.read_text())
                if board['uid']=='velocita-v2-live':
                    self.assertNotIn('strategy="stochastic"', str(board))
                    continue
                for panel in board["panels"]:
                    for target in panel.get("targets", []):
                        for selector in re.findall(r"velocita_(?:v2|shadow|gateway|indicator)_[a-zA-Z0-9_]+(\{[^}]*\})?", target["expr"]):
                            self.assertTrue(selector.startswith('{strategy="stochastic",paper_run="run-1"'), target["expr"])
            config = (pathlib.Path(tmp) / "prometheus.yml").read_text()
            self.assertEqual(config.count("paper_run: run-1}"), 5)
            self.assertNotIn("run-scoped", config)
        with self.assertRaises(SystemExit):
            generator.main(["generate_dashboard.py", "unused", "--paper-run", 'x"} or vector(1) #'])
        generator._paper_run = None

    def test_live_fill_markers_use_one_instant_row_and_persisted_observation_time(self):
        live=next(d for d in dashboards().values() if d['uid']=='velocita-v2-live')
        chart=next(p for p in live['panels'] if p['title']=='Live SOL entries / exits — effective settled price')
        self.assertEqual(len(chart['targets']),2)
        for target in chart['targets']:
            self.assertTrue(target['instant']);self.assertFalse(target['range'])
            self.assertEqual(target['format'],'table')
            self.assertIn('first_observed_ms',target['expr'])
            self.assertIn('velocita_live_observer_up == 1',target['expr'])
            self.assertNotIn('or vector(0)',target['expr'])
        converts=[t for t in chart['transformations'] if t['id']=='convertFieldType']
        self.assertTrue(all(t['id']=='organize' for t in chart['transformations'][:2]))
        self.assertEqual([t['options']['conversions'][0]['destinationType'] for t in converts],['number','time'])
        self.assertTrue(all(t['options']['conversions'][0]['targetField']=='first_observed_ms' for t in converts))
        organize=[t for t in chart['transformations'] if t['id']=='organize']
        self.assertEqual([t['filter']['options'] for t in organize],['A','B'])
        for transform in organize:
            self.assertTrue(transform['options']['excludeByName']['Time'])
            self.assertEqual(transform['options']['renameByName']['first_observed_ms'],'First confirmed observation')
            ref=transform['filter']['options']
            self.assertEqual(transform['options']['renameByName']['Value #'+ref],transform['options']['renameByName']['Value'])
        colors={o['matcher']['options']:next(p['value']['fixedColor'] for p in o['properties'] if p['id']=='color') for o in chart['fieldConfig']['overrides']}
        self.assertEqual(colors,{'Entry / buy':'green','Exit / sell':'red'})
        self.assertIn('not chain execution time',chart['description'])
        table=next(p for p in live['panels'] if p['type']=='table')
        self.assertIn('fee_usdc',table['targets'][0]['expr'])
        self.assertIn('quantity_sol',table['targets'][0]['expr'])

    def test_live_missing_settlement_metrics_never_default_to_financial_zero(self):
        live=next(d for d in dashboards().values() if d['uid']=='velocita-v2-live')
        for expr in exprs(live):
            if 'velocita_live_settled_' in expr or 'velocita_live_settlement_' in expr:
                self.assertNotIn('vector(0)',expr)
        status=next(p for p in live['panels'] if p['title']=='Confirmed-fill telemetry')
        self.assertEqual(status['fieldConfig']['defaults']['noValue'],'unknown')
        mappings=status['fieldConfig']['defaults']['mappings'][0]['options']
        self.assertEqual(mappings['0']['text'],'NOT CONNECTED')
        self.assertEqual(mappings['2']['text'],'STALE SNAPSHOT')

    def test_new_risk_stopped_is_the_first_and_widest_paper_panel(self):
        paper = next(d for d in dashboards().values() if d["uid"] == "velocita-v2-paper")
        first = content_panels(paper)[0]
        self.assertEqual(first["targets"][0]["expr"], 'max(velocita_gateway_new_risk_stopped{strategy="stochastic"})')
        self.assertEqual((first["gridPos"]["x"], first["gridPos"]["y"]), (0, 1))
        same_line = [p for p in content_panels(paper) if p["gridPos"]["y"] == first["gridPos"]["y"]]
        self.assertEqual(max(p["gridPos"]["w"] for p in same_line), first["gridPos"]["w"])
        defaults = first["fieldConfig"]["defaults"]
        self.assertEqual(defaults["mappings"][0]["options"]["1"]["text"], "NEW RISK STOPPED")
        self.assertEqual([s for s in defaults["thresholds"]["steps"] if s["value"] == 1][0]["color"], "red")


class GrafanaProvisioningLoadsEveryDashboard(unittest.TestCase):
    def provider(self):
        text = PROVIDER.read_text()
        path = re.search(r"^\s+path:\s*(\S+)\s*$", text, re.MULTILINE).group(1)
        deletion = re.search(r"^\s+disableDeletion:\s*(\S+)\s*$", text, re.MULTILINE).group(1)
        return path, deletion == "true"

    def test_provider_path_is_the_mounted_dashboard_directory(self):
        path, _ = self.provider()
        self.assertEqual(mounts(GRAFANA_UNIT).get(path), DASHBOARD_DIR,
                         f"provider path {path} is not where the unit mounts grafana/dashboards")
        self.assertEqual(len(re.findall(r"^\s+- name:", PROVIDER.read_text(), re.MULTILINE)), 1)

    def test_former_single_dashboard_uid_is_still_provisioned(self):
        _, deletion_disabled = self.provider()
        uids = {d["uid"] for d in dashboards().values()}
        if deletion_disabled:
            self.assertIn(OLD_UID, uids)

    def test_home_dashboard_is_the_overview(self):
        home = re.search(r"GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH=(\S+)", GRAFANA_UNIT.read_text()).group(1)
        directory, name = home.rsplit("/", 1)
        self.assertEqual(mounts(GRAFANA_UNIT).get(directory), DASHBOARD_DIR)
        self.assertIn(name, dashboards())
        self.assertEqual(dashboards()[name]["uid"], OLD_UID)
        self.assertIn("Overview", dashboards()[name]["title"])


class GatewayAlertsAreDefinedAndTested(unittest.TestCase):
    NAMES = ("VelocitaGatewayDown", "VelocitaGatewayNewRiskStopped", "VelocitaGatewayUnknownOutcome",
             "VelocitaGatewayAwaitingRearm", "VelocitaGatewayReconciliationMismatch")

    def test_each_gateway_alert_has_a_firing_and_a_quiet_rule_test(self):
        rules = (OBS / "rules/velocita-v2-alerts.yml").read_text()
        tests = (OBS / "rule-tests/velocita-v2-alerts.test.yml").read_text()
        for name in self.NAMES:
            self.assertIn(f"- alert: {name}\n", rules)
            self.assertRegex(tests, rf"alertname: {name}\n\s+exp_alerts: \[\]")
            self.assertRegex(tests, rf"alertname: {name}\n\s+exp_alerts:\n\s+- exp_labels:")
        stopped = rules.split("- alert: VelocitaGatewayNewRiskStopped\n", 1)[1].split("- alert:", 1)[0]
        self.assertIn("severity: critical", stopped)
        self.assertNotIn("for:", stopped)
        self.assertIn("journal", stopped)
        mismatch = rules.split("- alert: VelocitaGatewayReconciliationMismatch\n", 1)[1].split("- alert:", 1)[0]
        self.assertIn("severity: critical", mismatch)
        self.assertNotIn("for:", mismatch)


if __name__ == "__main__":
    unittest.main()
