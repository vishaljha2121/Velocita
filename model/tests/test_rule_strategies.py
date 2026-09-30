"""Rule-based demo strategies: cluster-identical rule semantics, hand-computed
features and costs, no look-ahead."""

from __future__ import annotations

import math
import random
import unittest
from datetime import date, timedelta

from velocita_model.contracts.enums import Directive, PredicateOperator
from velocita_model.research import rules as rs
from velocita_model.research.bars import DailyBars
from velocita_model.simulation.rule_evaluator import NO_MATCHED_RULE_INDEX, RuleEvaluationCode, evaluate_rules

from test_rule_evaluator_parity import frame, policy, rule

GT, LE, LT, GE, EQ = (PredicateOperator.greater_than, PredicateOperator.less_than_or_equal,
                      PredicateOperator.less_than, PredicateOperator.greater_than_or_equal, PredicateOperator.equal)


def bars(closes: list[float]) -> DailyBars:
    return DailyBars(tuple(date(2024, 1, 1) + timedelta(days=i) for i in range(len(closes))), ("X",),
                     (tuple(closes),), None)


class RuleParity(unittest.TestCase):
    """The fast engine must pick exactly the rule the cluster's evaluator picks."""

    def test_first_match_equals_the_evaluator_on_random_policies(self) -> None:
        rng = random.Random(1)
        ops = list(PredicateOperator)
        for _ in range(2000):
            n_features = rng.randint(1, 4)
            features = tuple(rng.choice([-2, -1, 0, 1, 2]) * 1000 for _ in range(n_features))
            specs = [rs.Rule(rng.randrange(n_features + 1), rng.choice(ops), rng.choice([-1, 0, 1]) * 1000,
                             rng.choice(list(rs.Action))) for _ in range(rng.randint(1, 5))]
            mine = rs.first_match(specs, features)
            theirs = evaluate_rules(policy(*[rule(r.feature, r.op, r.threshold, r.action.directive) for r in specs]),
                                    frame(features))
            expected = theirs.matched_rule_index if theirs.code == RuleEvaluationCode.matched else None
            self.assertEqual(mine, expected, (specs, features))

    def test_actions_map_to_cluster_directives(self) -> None:
        self.assertEqual(rs.Action.HOLD.directive, Directive.no_action)
        self.assertEqual(rs.Action.ENTER_LONG.directive, Directive.place_limit)
        self.assertEqual(rs.Action.EXIT.directive, Directive.place_limit)


class Features(unittest.TestCase):
    def test_trend_return_and_deviation(self) -> None:
        b = bars([10, 10, 10, 10, 20])
        f = rs.features(b, 0, 4, rs.Settings(sma_days=4, vol_days=2, dev_days=2))
        # SMA4 at t=4 = (10+10+10+20)/4 = 12.5 -> 20/12.5 - 1 = 0.6 -> 600000 micros
        self.assertEqual(f[rs.TREND], 600_000)
        # 1-day return 20/10 - 1 = 1.0 -> 1000000 micros
        self.assertEqual(f[rs.RET1], 1_000_000)

    def test_volatility_is_the_sample_stdev_of_log_returns(self) -> None:
        closes = [100 * math.exp(x) for x in (0, 0.01, 0.03, 0.02, 0.05)]
        f = rs.features(bars(closes), 0, 4, rs.Settings(sma_days=2, vol_days=4, dev_days=2))
        logs = [0.01, 0.02, -0.01, 0.03]
        mean = sum(logs) / 4
        sd = math.sqrt(sum((x - mean) ** 2 for x in logs) / 3)
        self.assertEqual(f[rs.VOL], round(sd * 1_000_000))

    def test_fast_series_equals_single_day_features(self) -> None:
        rng = random.Random(5)
        closes = [100.0]
        for _ in range(300):
            closes.append(closes[-1] * math.exp(rng.gauss(0, 0.03)))
        b = bars(closes)
        settings = rs.Settings(sma_days=50, vol_days=30, dev_days=20)
        series = rs.feature_series(b, 0, settings)
        self.assertEqual(series, [rs.features(b, 0, t, settings) for t in range(len(closes))])

    def test_unavailable_history_gives_no_features(self) -> None:
        self.assertIsNone(rs.features(bars([1, 2, 3]), 0, 2, rs.Settings(sma_days=5, vol_days=2, dev_days=2)))


SMALL = rs.Settings(sma_days=2, vol_days=2, dev_days=2)


class Engine(unittest.TestCase):
    TREND_ONLY = [rs.Rule(rs.TREND, GT, 0, rs.Action.ENTER_LONG), rs.Rule(rs.TREND, LE, 0, rs.Action.EXIT)]
    COSTS = rs.Costs(buy_bps=100.0, sell_bps=200.0)

    def test_enter_fill_next_close_and_exit_with_side_costs(self) -> None:
        # Features exist from t=2 (two returns for volatility). SMA2 trend:
        # t=2: 12/11-1 > 0 -> ENTER decided at close 2, filled at close 3 (13).
        # t=3: 13/12.5-1 > 0 (already long). t=4: 9/11-1 < 0 -> EXIT decided at close 4, filled at close 5.
        closes = [10, 10, 12, 13, 9, 9, 9]
        r = rs.backtest(bars(closes), 0, self.TREND_ONLY, SMALL, self.COSTS)
        self.assertEqual(r.positions, [0, 0, 0, 1, 1, 0, 0])
        self.assertAlmostEqual(r.returns[3], -0.01)                  # buy cost 100 bp on the whole stake
        self.assertAlmostEqual(r.returns[4], 9 / 13 - 1)             # held through the fall
        self.assertAlmostEqual(r.returns[5], -0.02)                  # sell cost 200 bp, 9 -> 9 flat
        self.assertEqual((r.buys, r.sells), (1, 1))

    def test_hold_keeps_the_position(self) -> None:
        program = [rs.Rule(rs.RET1, LT, -500_000, rs.Action.ENTER_LONG), rs.Rule(rs.RET1, GT, 500_000, rs.Action.EXIT)]
        closes = [10, 10, 10, 4, 4, 4, 4, 10, 10]  # -60 % day enters; quiet days HOLD; +150 % day exits
        r = rs.backtest(bars(closes), 0, program, SMALL, rs.Costs(0, 0))
        self.assertEqual(r.positions, [0, 0, 0, 0, 1, 1, 1, 1, 0])

    def test_no_look_ahead(self) -> None:
        closes = [10, 10, 12, 13, 9, 9, 9, 9, 9]
        future = closes[:6] + [1, 50, 2]
        a = rs.backtest(bars(closes), 0, self.TREND_ONLY, SMALL, self.COSTS)
        b = rs.backtest(bars(future), 0, self.TREND_ONLY, SMALL, self.COSTS)
        self.assertEqual(a.positions[:7], b.positions[:7])


class DemoPrograms(unittest.TestCase):
    def test_three_programs_compile_to_valid_policy_rules(self) -> None:
        for name, (build, grid) in rs.DEMOS.items():
            for value in grid:
                program, settings = build(value)
                artifact = policy(*[rule(r.feature, r.op, r.threshold, r.action.directive) for r in program])
                artifact.validate()
                self.assertTrue(program and settings, name)


if __name__ == "__main__":
    unittest.main()
