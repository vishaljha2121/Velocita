"""The volume battery's execution model. Every expected value is computed by
hand from the inputs in the comment beside it, never read back from the code."""

from __future__ import annotations

import math
import random
import unittest

from velocita_model.research import volume_battery as vb
from velocita_model.research.tick_data import DataError, archive_paths, sample
from datetime import date

T0 = 1_772_409_600


def flat(n: int, price: float = 80_000.0, history: float | None = None) -> vb.PricePath:
    return vb.PricePath(tuple(T0 + 30 * i for i in range(n)), (price,) * n, (history or price,) * 130)


def path_of(prices: list[float]) -> vb.PricePath:
    return vb.PricePath(tuple(T0 + 30 * i for i in range(len(prices))), tuple(prices), (prices[0],) * 130)


def free(clip: int = 25, buy: float = 100.0, sell: float = 100.0, **kw) -> vb.Execution:
    kw.setdefault("capital_usd", None)
    kw.setdefault("exposure_cap_usd", None)
    return vb.Execution(clip, buy, sell, **kw)


def sides(result: vb.Result) -> list[tuple[int, str]]:
    return [(f.tick, f.side) for f in result.fills]


class FillTiming(unittest.TestCase):
    def test_churn_loses_the_tick_after_every_fill(self) -> None:
        r = vb.simulate(flat(9), vb.churn(), free(), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (2, "sell"), (4, "buy"), (6, "sell"), (8, "buy")])
        self.assertEqual(r.total("lost_ticks"), 4)

    def test_an_extra_lost_tick_stretches_the_cycle(self) -> None:
        r = vb.simulate(flat(10), vb.churn(), free(resync_ticks=2), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (3, "sell"), (6, "buy"), (9, "sell")])

    def test_rotation_exits_once_held_long_enough(self) -> None:
        r = vb.simulate(flat(7), vb.rotate(2, gated=False), free(), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (4, "sell"), (6, "buy")])
        self.assertEqual(r.total("position_bound"), 2)

    def test_trend_gate_keeps_the_book_flat_below_the_average(self) -> None:
        r = vb.simulate(flat(8, 90.0, history=100.0), vb.gated_churn(), free(clip=1), keep_fills=True)
        self.assertEqual(r.fills, [])
        self.assertEqual(r.total("exit_flat"), 8)


class PaperToolingDefinitions(unittest.TestCase):
    def test_clock_churn_is_long_in_odd_minutes(self) -> None:
        r = vb.simulate(flat(7), vb.clock_churn(60, gated=False), free(), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (4, "sell"), (6, "buy")])
        self.assertEqual(r.total("exit_flat"), 2)

    def test_clock_churn_with_two_lost_ticks_waits_for_the_next_odd_minute(self) -> None:
        r = vb.simulate(flat(11), vb.clock_churn(60, gated=False), free(resync_ticks=2), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (5, "sell"), (10, "buy")])

    def test_clock_rotate_exits_in_the_first_minute_of_each_period(self) -> None:
        r = vb.simulate(flat(63, 110.0, history=100.0), vb.clock_rotate(1800, 60), free(clip=1), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (60, "sell"), (62, "buy")])
        self.assertEqual(r.total("position_bound"), 56)

    def test_minute_devs_uses_completed_minute_closes(self) -> None:
        times = [T0 + 30 * i for i in range(7)]
        self.assertEqual(vb.minute_devs_series(times, [100.0, 100.0, 102.0, 102.0, 98.0, 99.0, 103.0], 2),
                         [None, None, None, None, -29703, -19802, 24876])

    def test_minute_dip_buys_below_and_sells_above(self) -> None:
        r = vb.simulate(path_of([100.0, 100.0, 102.0, 102.0, 98.0, 99.0, 103.0, 103.0]), vb.minute_dip(2, 200, 100),
                        free(clip=1), keep_fills=True)
        self.assertEqual(sides(r), [(4, "buy"), (6, "sell")])


class Costs(unittest.TestCase):
    def test_round_trip_cost_and_volume(self) -> None:
        r = vb.simulate(path_of([80_000.0, 80_000.0, 81_000.0]), vb.churn(), free(buy=110.0, sell=125.0), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (2, "sell")])
        self.assertAlmostEqual(r.volume, 40.25, places=9)
        self.assertAlmostEqual(r.total("fees"), 0.473125, places=9)
        self.assertAlmostEqual(r.net, -0.223125, places=9)
        self.assertAlmostEqual(r.total("price_pnl"), 0.25, places=9)
        self.assertAlmostEqual(r.score_bp, -0.223125 / 40.25 * 10_000, places=6)

    def test_fill_lag_prices_the_fill_one_tick_later(self) -> None:
        r = vb.simulate(path_of([80_000.0, 80_400.0, 80_800.0]), vb.churn(), free(buy=0.0, sell=0.0, fill_lag_ticks=1),
                        keep_fills=True)
        self.assertAlmostEqual(r.fills[0].price, 80_400.0)
        self.assertAlmostEqual(r.fills[0].price * r.fills[0].qty_btc, 20.10, places=9)


class Capital(unittest.TestCase):
    def test_losses_shrink_cash_until_the_venue_kills_the_buy(self) -> None:
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_topup=False)
        r = vb.simulate(flat(60), vb.churn(), ex, keep_fills=True)
        self.assertEqual(r.total("buys"), 13)
        self.assertEqual(r.total("sells"), 13)
        self.assertEqual(r.total("killed_capital"), 4)
        self.assertEqual(r.total("exit_flat"), 4)
        self.assertAlmostEqual(r.weeks[0].end_equity, 19.80, places=9)
        self.assertEqual(r.weeks[0].first_kill_s, 52 * 30)

    def test_a_buy_needs_its_cost_as_well_as_its_notional(self) -> None:
        short = vb.Execution(25, 100.0, 100.0, capital_usd=20.10, exposure_cap_usd=None, weekly_topup=False)
        enough = vb.Execution(25, 100.0, 100.0, capital_usd=20.20, exposure_cap_usd=None, weekly_topup=False)
        self.assertEqual((vb.simulate(flat(1), vb.churn(), short).total("killed_capital"),
                          vb.simulate(flat(1), vb.churn(), short).total("buys")), (1, 0))
        self.assertEqual(vb.simulate(flat(1), vb.churn(), enough).total("buys"), 1)

    def test_kernel_exposure_cap_matches_the_runbook_boundary(self) -> None:
        ex = vb.Execution(25, 115.0, 120.0, capital_usd=1_000.0, exposure_cap_usd=25.0, fee_hold_bps=150.0)
        fits = vb.simulate(flat(1, 98_522.0), vb.churn(), ex)
        refused = vb.simulate(flat(1, 98_523.0), vb.churn(), ex)
        self.assertEqual((fits.total("buys"), fits.total("refused_exposure")), (1, 0))
        self.assertEqual((refused.total("buys"), refused.total("refused_exposure")), (0, 1))

    def test_weekly_loss_cap_stops_buys_before_the_cap_is_spent(self) -> None:
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_loss_cap_usd=1.0)
        r = vb.simulate(flat(12), vb.churn(), ex)
        self.assertEqual(r.total("buys"), 2)
        self.assertAlmostEqual(r.net, -0.80, places=9)
        self.assertEqual(r.weeks[0].cap_first_s, 240)

    def test_top_up_restores_capital_at_the_week_start(self) -> None:
        n = vb.WEEK_S // 30 + 3
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_loss_cap_usd=0.5)
        r = vb.simulate(flat(n), vb.churn(), ex)
        self.assertEqual(len(r.weeks), 2)
        self.assertAlmostEqual(r.weeks[0].net, -0.40, places=9)
        self.assertAlmostEqual(r.weeks[1].topup, 0.40, places=9)
        self.assertAlmostEqual(r.weeks[1].start_equity, 25.0, places=9)
        self.assertEqual(r.weeks[1].buys, 1)

    def test_drawdown_is_measured_before_top_ups(self) -> None:
        r = vb.simulate(path_of([80_000.0, 80_000.0, 72_000.0]), vb.buy_and_hold(), free(buy=0.0, sell=0.0))
        self.assertAlmostEqual(r.max_drawdown_usd, 2.0, places=9)
        self.assertAlmostEqual(r.weeks[0].drawdown, 2.0, places=9)

    def test_weekly_drawdown_restarts_each_week(self) -> None:
        n = vb.WEEK_S // 30
        prices = [80_000.0] + [76_000.0] * (n - 1) + [76_000.0, 78_000.0, 77_000.0]
        r = vb.simulate(path_of(prices), vb.buy_and_hold(), free(buy=0.0, sell=0.0))
        self.assertAlmostEqual(r.weeks[0].drawdown, 1.0, places=9)
        self.assertAlmostEqual(r.weeks[1].drawdown, 0.25, places=9)
        self.assertAlmostEqual(r.max_drawdown_usd, 1.0, places=9)


class Features(unittest.TestCase):
    def test_trend_uses_124_closes_and_the_live_price(self) -> None:
        p = vb.PricePath((T0,), (125.0,), (100.0,) * 124)
        self.assertEqual(vb.trend_series(p), [247505])

    def test_trend_rolls_the_day_close_into_history(self) -> None:
        p = vb.PricePath((T0, T0 + 86_370, T0 + 86_400), (100.0, 150.0, 100.0), (100.0,) * 124)
        self.assertEqual(vb.trend_series(p)[2], -3984)

    def test_devs_is_price_over_short_average(self) -> None:
        self.assertEqual(vb.devs_series([1.0, 2.0, 3.0, 6.0], 3), [None, None, 500000, 636364])

    def test_dip_buys_below_and_sells_above(self) -> None:
        r = vb.simulate(path_of([100.0, 99.0, 97.0, 98.0, 99.0]), vb.dip(2, 100, 0), free(clip=1), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (4, "sell")])


class Nulls(unittest.TestCase):
    def test_permutation_keeps_moves_and_endpoints(self) -> None:
        rng = random.Random(1)
        prices = [100.0 * math.exp(rng.gauss(0, 0.01)) for _ in range(50)]
        p = path_of(prices)
        q = vb.permute(p, 10, random.Random(7))
        self.assertEqual(q.prices[:10], p.prices[:10])
        self.assertAlmostEqual(q.prices[-1], p.prices[-1], places=6)
        moves = lambda s: sorted(round(s[i] / s[i - 1], 12) for i in range(10, len(s)))
        self.assertEqual(moves(q.prices), moves(p.prices))
        self.assertNotEqual(q.prices, p.prices)


class WalkForward(unittest.TestCase):
    def test_choice_uses_only_the_training_weeks(self) -> None:
        def result(nets: list[float]) -> vb.Result:
            return vb.Result([vb.Week(start_s=k, start_equity=0.0, end_equity=x, volume=100.0) for k, x in enumerate(nets)])
        a, b = result([-1.0, -1.0, -5.0, -1.0]), result([-3.0, -0.5, -0.5, -9.0])
        wf = vb.walk_forward([a, b], train_weeks=2, step_weeks=1)
        self.assertEqual(wf.choices, [(2, 0), (3, 1)])
        self.assertAlmostEqual(wf.score_bp, (-5.0 - 9.0) / 200.0 * 10_000)


class TickSampling(unittest.TestCase):
    def test_a_tick_sees_only_bars_closed_by_its_time(self) -> None:
        times, prices, worst, stale = sample([(s, 10.0 + s) for s in range(5)], 2, 6, 2)
        self.assertEqual((times, prices), ([2, 4], [11.0, 13.0]))
        self.assertEqual(worst, 0)

    def test_a_gap_is_carried_and_measured_not_filled(self) -> None:
        times, prices, worst, stale = sample([(0, 1.0), (10, 2.0)], 1, 12, 5)
        self.assertEqual(prices, [1.0, 1.0, 2.0])
        self.assertEqual((worst, stale), (5, 0))

    def test_no_bar_before_the_first_tick_is_refused(self) -> None:
        with self.assertRaises(DataError):
            sample([(5, 1.0)], 3, 10, 1)

    def test_archive_plan_uses_months_only_when_whole(self) -> None:
        paths = archive_paths("BTCUSDT", date(2026, 3, 1), date(2026, 4, 3))
        self.assertEqual(paths, ["/data/spot/monthly/klines/BTCUSDT/1s/BTCUSDT-1s-2026-03.zip",
                                 "/data/spot/daily/klines/BTCUSDT/1s/BTCUSDT-1s-2026-04-01.zip",
                                 "/data/spot/daily/klines/BTCUSDT/1s/BTCUSDT-1s-2026-04-02.zip"])


if __name__ == "__main__":
    unittest.main()
