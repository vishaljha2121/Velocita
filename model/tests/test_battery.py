"""Generic battery: agrees with a direct computation, picklable runs."""

from __future__ import annotations

import math
import pickle
import random
import unittest
from datetime import date, timedelta

from velocita_model.research import battery, metrics, rules
from velocita_model.research.bars import DailyBars
from velocita_model.research.demo_report import DemoRun


def walk(n: int, seed: int) -> DailyBars:
    rng = random.Random(seed)
    closes = [100.0]
    for _ in range(n - 1):
        closes.append(closes[-1] * math.exp(rng.gauss(0.0003, 0.03)))
    return DailyBars(tuple(date(2020, 1, 1) + timedelta(days=i) for i in range(n)), ("X",), (tuple(closes),), None)


class Battery(unittest.TestCase):
    def test_optimize_picks_the_best_profit_factor(self) -> None:
        b = walk(600, 3)
        run = DemoRun("trend", rules.Costs(100.0, 100.0))
        best = battery.optimize(b, [50, 100, 150], run)
        direct = {v: metrics.profit_factor(run(b, v)) for v in (50, 100, 150)}
        self.assertEqual(best.value, max(direct, key=direct.get))
        self.assertEqual(dict(best.scores), direct)

    def test_run_is_picklable_for_worker_processes(self) -> None:
        run = DemoRun("calm_trend", rules.Costs(1.0, 2.0))
        clone = pickle.loads(pickle.dumps(run))
        b = walk(400, 4)
        self.assertEqual(clone(b, 80), run(b, 80))

    def test_walk_forward_trades_only_out_of_sample_days(self) -> None:
        b = walk(900, 5)
        wf = battery.walk_forward(b, [50, 100], DemoRun("trend", rules.Costs(100.0, 100.0)),
                                  train_days=500, step_days=100)
        self.assertEqual(len(wf.returns), 900 - 500)
        self.assertEqual([d for d, _ in wf.choices], [b.dates[i].isoformat() for i in (500, 600, 700, 800)])


if __name__ == "__main__":
    unittest.main()
