"""Volume strategies: features from event time and the minute window, the
programs they install, and the trend program staying byte-identical.

Oracles: hand arithmetic on the definitions in shadow/volume.py; hand-written
program text; `rules.features` (tested elsewhere) for the four daily features.
"""

from __future__ import annotations

import hashlib
import unittest
from datetime import date, timedelta
from decimal import Decimal
from pathlib import Path
from tempfile import TemporaryDirectory

from velocita_model.research import rules
from velocita_model.research.bars import DailyBars
from velocita_model.shadow import edge, volume

from test_shadow_edge import history, reference

NS = 1_000_000_000
NOON_0928 = 1_790_596_800 * NS
CATALOG = edge.CatalogRow(root="ab" * 32, account=1, venue=20, instrument=1, instrument_spec_version=1,
                          venue_capabilities_version=1)
UNIT = dict(valid_from_ns=1, valid_until_ns=4_102_444_800_000_000_000, order_quantity_lots=25,
            max_notional_micros=25_000_000, max_age_ns=5 * NS, source_id=41, time_in_force="fok",
            post_only="prohibited")


class Phase(unittest.TestCase):
    def test_parity_of_whole_periods_since_the_epoch(self) -> None:
        self.assertEqual(volume.phase(0, 60), 0)
        self.assertEqual(volume.phase(60 * NS - 1, 60), 0)
        self.assertEqual(volume.phase(60 * NS, 60), 1_000_000)
        self.assertEqual(volume.phase(120 * NS - 1, 60), 1_000_000)
        self.assertEqual(volume.phase(120 * NS, 60), 0)
        self.assertEqual(volume.phase(NOON_0928, 60), 0)
        self.assertEqual(volume.phase(NOON_0928 + 60 * NS, 60), 1_000_000)
        self.assertEqual(volume.phase(NOON_0928 + 44_999_000_000, 45), 0)
        self.assertEqual(volume.phase(NOON_0928 + 45 * NS, 45), 1_000_000)


class Rotation(unittest.TestCase):
    def test_microseconds_into_the_period(self) -> None:
        self.assertEqual(volume.rotation(NOON_0928, 1800), 0)
        self.assertEqual(volume.rotation(NOON_0928 + 59_500_000_000, 1800), 59_500_000)
        self.assertEqual(volume.rotation(NOON_0928 + 60 * NS, 1800), 60_000_000)
        self.assertEqual(volume.rotation(NOON_0928 + 1799 * NS + 999, 1800), 1_799_000_000)
        self.assertEqual(volume.rotation(NOON_0928 + 1800 * NS, 1800), 0)


class MinuteWindow(unittest.TestCase):
    M = 60 * NS

    def test_last_price_per_minute_with_carry_forward(self) -> None:
        w = volume.MinuteWindow(3)
        w.observe(10 * NS, Decimal("100"))
        w.observe(50 * NS, Decimal("102"))
        w.observe(2 * self.M + 10 * NS, Decimal("99"))
        self.assertEqual(w.deviation_micros(), -19_802)

    def test_not_available_until_the_window_is_covered(self) -> None:
        w = volume.MinuteWindow(3)
        w.observe(5 * self.M, Decimal("100"))
        self.assertIsNone(w.deviation_micros())
        w.observe(6 * self.M, Decimal("100"))
        self.assertIsNone(w.deviation_micros())
        w.observe(7 * self.M, Decimal("103"))
        self.assertEqual(w.deviation_micros(), 19_802)

    def test_bounded_and_the_newest_pruned_close_carries_in(self) -> None:
        w = volume.MinuteWindow(2)
        for minute, price in ((0, "100"), (1, "110"), (5, "120")):
            w.observe(minute * self.M, Decimal(price))
        self.assertLessEqual(len(w.closes), 2)
        self.assertEqual(w.carry, Decimal("110"))
        self.assertEqual(w.deviation_micros(), 43_478)
        for minute in range(6, 5000):
            w.observe(minute * self.M, Decimal(minute))
        self.assertLessEqual(len(w.closes), 2)

    def test_an_older_or_equal_stamp_changes_nothing(self) -> None:
        w = volume.MinuteWindow(1)
        w.observe(10 * NS, Decimal("100"))
        w.observe(10 * NS, Decimal("150"))
        w.observe(9 * NS, Decimal("150"))
        self.assertEqual(list(w.closes), [(0, Decimal("100"))])

    def test_rounding_is_half_even(self) -> None:
        w = volume.MinuteWindow(2)
        w.observe(0, Decimal("999999.5"))
        w.observe(self.M, Decimal("1000000.5"))
        self.assertEqual(w.deviation_micros(), 0)
        w = volume.MinuteWindow(2)
        w.observe(0, Decimal("999998.5"))
        w.observe(self.M, Decimal("1000001.5"))
        self.assertEqual(w.deviation_micros(), 2)

    def test_a_window_is_1_to_1440_minutes(self) -> None:
        for bad in (0, 1441):
            with self.assertRaises(ValueError):
                volume.MinuteWindow(bad)


class Programs(unittest.TestCase):
    def text(self, strategy: str, params: volume.Params | None = None, value: int = 125) -> str:
        policy, schema = volume.identity(strategy)
        return edge.program_text(strategy, value, CATALOG, policy_id=policy, policy_version=1,
                                 feature_schema_id=schema, params=params, **UNIT)

    def test_trend_program_is_byte_identical_to_the_one_running(self) -> None:
        expected = (
            "# generated from research.rules.trend(125)\n"
            f"catalog.root = {'ab' * 32}\ncatalog.account = 1\ncatalog.venue = 20\ncatalog.instrument = 1\n"
            "catalog.instrument_spec_version = 1\ncatalog.venue_capabilities_version = 1\n"
            "policy.id = 21\npolicy.version = 1\npolicy.feature_schema_id = 31\npolicy.risk_version = 1\n"
            f"policy.model_digest = {hashlib.sha256(b'velocita.rules.v1 trend 125').hexdigest()}\n"
            "policy.valid_from_ns = 1\npolicy.valid_until_ns = 4102444800000000000\nfeatures.generic = 4\n"
            "rule.1 = 0 gt 0 enter_long\nrule.2 = 0 le 0 exit\n"
            "order.time_in_force = fok\norder.post_only = prohibited\norder.quantity_lots = 25\n"
            "order.max_notional_micros = 25000000\norder.max_market_data_age_ns = 5000000000\nsource.id = 41\n")
        self.assertEqual(self.text("trend"), expected)

    def test_each_volume_program_has_its_own_identity_rules_and_digest(self) -> None:
        cases = {
            "churn": (22, 32, "velocita.volume.v1 churn sma_days=125 period_s=60",
                      ["rule.1 = 4 gt 0 enter_long", "rule.2 = 4 le 0 exit"]),
            "trend_churn": (23, 33, "velocita.volume.v1 trend_churn sma_days=125 period_s=60",
                            ["rule.1 = 0 le 0 exit", "rule.2 = 4 gt 0 enter_long", "rule.3 = 4 le 0 exit"]),
            "rotate": (24, 34, "velocita.volume.v1 rotate sma_days=125 period_s=1800 exit_window_s=60",
                       ["rule.1 = 0 le 0 exit", "rule.2 = 4 lt 60000000 exit", "rule.3 = 0 gt 0 enter_long"]),
            "dip": (25, 35, "velocita.volume.v1 dip sma_days=125 window_min=30 enter_below_bps=20 exit_above_bps=10",
                    ["rule.1 = 4 lt -2000 enter_long", "rule.2 = 4 gt 1000 exit"]),
        }
        for strategy, (policy, schema, model, rule_lines) in cases.items():
            text = self.text(strategy)
            lines = text.splitlines()
            self.assertEqual(lines[0], f"# generated from {model}", strategy)
            self.assertIn(f"policy.id = {policy}", lines, strategy)
            self.assertIn(f"policy.feature_schema_id = {schema}", lines, strategy)
            self.assertIn("features.generic = 5", lines, strategy)
            self.assertIn(f"policy.model_digest = {hashlib.sha256(model.encode()).hexdigest()}", lines, strategy)
            self.assertEqual([l for l in lines if l.startswith("rule.")], rule_lines, strategy)

    def test_parameters_reach_the_rules_and_the_digest(self) -> None:
        text = self.text("rotate", volume.Params(period_s=900, exit_window_s=90))
        self.assertIn("rule.2 = 4 lt 90000000 exit\n", text)
        digest = hashlib.sha256(b"velocita.volume.v1 rotate sma_days=125 period_s=900 exit_window_s=90").hexdigest()
        self.assertIn(f"policy.model_digest = {digest}\n", text)
        dip = self.text("dip", volume.Params(window_min=15, enter_below_bps=35, exit_above_bps=5))
        self.assertIn("rule.1 = 4 lt -3500 enter_long\nrule.2 = 4 gt 500 exit\n", dip)
        churn = self.text("churn", volume.Params(period_s=90))
        self.assertIn(hashlib.sha256(b"velocita.volume.v1 churn sma_days=125 period_s=90").hexdigest(), churn)

    def test_impossible_parameters_are_refused(self) -> None:
        for strategy, params in (("rotate", volume.Params(period_s=60, exit_window_s=60)),
                                 ("rotate", volume.Params(exit_window_s=0)),
                                 ("churn", volume.Params(period_s=0)),
                                 ("dip", volume.Params(window_min=0)),
                                 ("dip", volume.Params(enter_below_bps=-1))):
            with self.assertRaises(ValueError, msg=f"{strategy} {params}"):
                self.text(strategy, params)


def trend_features(closes: list[float], price: float) -> tuple[int, ...]:
    days = len(closes) + 1
    bars = DailyBars(tuple(date(2026, 1, 1) + timedelta(days=i) for i in range(days)), ("BTC",),
                     (tuple(closes) + (price,),), None)
    return rules.features(bars, 0, days - 1, rules.Settings(sma_days=125))


NOON_0720 = 1_784_548_800 * NS


class VolumeTicks(unittest.TestCase):
    def test_churn_tick_is_the_daily_features_then_phase_then_price(self) -> None:
        f = edge.FeatureSource("churn", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        daily = trend_features([c for _, c in history(200)], 300.0)
        even = f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=NOON_0720, tick_id=1)
        self.assertEqual(even.features, list(daily) + [0, 30_000])
        self.assertEqual(even.expected_rule, 1)
        odd = f.tick(reference("300", "2026-07-20T12:01:00Z"), receive_ns=NOON_0720 + 60 * NS, tick_id=2)
        self.assertEqual(odd.features[4:], [1_000_000, 30_000])
        self.assertEqual(odd.expected_rule, 0)
        self.assertEqual(f.feature_names, ("trend", "ret1", "vol", "dev", "phase"))

    def test_rotate_tick_carries_seconds_into_the_period(self) -> None:
        f = edge.FeatureSource("rotate", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        tick = f.tick(reference("300", "2026-07-20T12:00:30Z"), receive_ns=NOON_0720 + 30 * NS, tick_id=1)
        self.assertEqual(tick.features[4], 30_000_000)
        self.assertGreater(tick.features[0], 0)
        self.assertEqual(tick.expected_rule, 1)
        later = f.tick(reference("300", "2026-07-20T12:01:00Z"), receive_ns=NOON_0720 + 60 * NS, tick_id=2)
        self.assertEqual((later.features[4], later.expected_rule), (60_000_000, 2))

    def test_dip_warms_up_on_every_observation_even_throttled_ones(self) -> None:
        f = edge.FeatureSource("dip", 125, closes=history(200), usd_per_tick=Decimal("0.01"),
                               min_interval_ns=600 * NS, params=volume.Params(window_min=2))
        self.assertIsNone(f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=NOON_0720, tick_id=1))
        self.assertEqual(f.last_outcome, "warming_up")
        first = f.tick(reference("306", "2026-07-20T12:01:00Z"), receive_ns=NOON_0720 + 60 * NS, tick_id=2)
        self.assertEqual((first.features[4:], first.expected_rule), ([9_901, 30_600], 1))
        self.assertIsNone(f.tick(reference("294", "2026-07-20T12:02:00Z"), receive_ns=NOON_0720 + 120 * NS, tick_id=3))
        self.assertEqual(f.last_outcome, "throttled")
        later = f.tick(reference("300", "2026-07-20T12:11:00Z"), receive_ns=NOON_0720 + 660 * NS, tick_id=4)
        self.assertEqual(later.features[4:], [10_101, 30_000])

    def test_metrics_name_the_strategy_feature_and_keep_the_price_last(self) -> None:
        f = edge.FeatureSource("churn", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        tick = f.tick(reference("300", "2026-07-20T12:01:00Z"), receive_ns=NOON_0720 + 60 * NS, tick_id=1)
        m = edge.EdgeMetrics(shadow_recorded_code=17, feature_names=f.feature_names)
        m.observe_message("tick", tick, Decimal("0.01"))
        text = m.render(now_ns=0)
        self.assertIn('velocita_shadow_feature_micros{name="phase"} 1000000\n', text)
        self.assertIn("velocita_shadow_price_usd 300.00\n", text)

    def test_the_stop_token_is_appended_only_while_on(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        tick = f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=NOON_0720, tick_id=7)
        plain = tick.line()
        self.assertTrue(plain.endswith(f"evidence={tick.evidence}"))
        tick.exits_only = True
        self.assertEqual(tick.line(), plain + " exits_only=1")


class CommandLine(unittest.TestCase):
    ARGS = ["shadow-edge", "--usd-per-tick", "1", "--catalog-root", "ab" * 32, "--catalog-row", "1,20,1,1,1",
            "--max-notional-micros", "25000000", "--order-quantity-lots", "12", "--program-only"]

    def run_cli(self, *extra: str) -> tuple[int, str]:
        from velocita_model import cli
        with TemporaryDirectory() as d:
            out = Path(d) / "p"
            args = cli.build_parser().parse_args(self.ARGS + list(extra) + ["--program-out", str(out)])
            code = args.handler(args)
            return code, out.read_text() if out.exists() else ""

    def test_strategy_defaults_pick_its_policy_and_schema(self) -> None:
        code, text = self.run_cli("--strategy", "trend_churn", "--period-s", "90")
        self.assertEqual(code, 0)
        self.assertIn("policy.id = 23\npolicy.version = 1\npolicy.feature_schema_id = 33\n", text)
        self.assertIn("velocita.volume.v1 trend_churn sma_days=125 period_s=90", text)
        self.assertIn("order.quantity_lots = 12\n", text)
        code, text = self.run_cli("--strategy", "dip", "--policy-version", "3")
        self.assertIn("policy.id = 25\npolicy.version = 3\npolicy.feature_schema_id = 35\n", text)

    def test_trend_keeps_21_and_31(self) -> None:
        code, text = self.run_cli()
        self.assertIn("policy.id = 21\npolicy.version = 1\npolicy.feature_schema_id = 31\n", text)

    def test_a_bad_parameter_is_a_configuration_error(self) -> None:
        code, text = self.run_cli("--strategy", "rotate", "--period-s", "60", "--exit-window-s", "60")
        self.assertEqual((code, text), (64, ""))


if __name__ == "__main__":
    unittest.main()
