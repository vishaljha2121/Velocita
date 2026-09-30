"""The volume battery's execution model. Every expected value is computed by
hand from the inputs in the comment beside it, never read back from the code."""

from __future__ import annotations

import math
import random
import unittest

from velocita_model.research import volume_battery as vb
from velocita_model.research.tick_data import DataError, archive_paths, sample
from datetime import date

T0 = 1_772_409_600  # 2026-03-02 00:00 UTC, a Monday


def flat(n: int, price: float = 80_000.0, history: float | None = None) -> vb.PricePath:
    """N ticks 30 s apart at one price; 124 days of history at HISTORY (default: the price)."""
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
        # Tick 0 buys; tick 1 is the stale_risk_state re-sync; tick 2 sells; 3 lost; 4 buys ...
        r = vb.simulate(flat(9), vb.churn(), free(), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (2, "sell"), (4, "buy"), (6, "sell"), (8, "buy")])
        self.assertEqual(r.total("lost_ticks"), 4)  # ticks 1, 3, 5, 7

    def test_an_extra_lost_tick_stretches_the_cycle(self) -> None:
        # Two lost ticks per fill: fills at 0, 3, 6, 9.
        r = vb.simulate(flat(10), vb.churn(), free(resync_ticks=2), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (3, "sell"), (6, "buy"), (9, "sell")])

    def test_rotation_exits_once_held_long_enough(self) -> None:
        # R = 2 min: buy at t=0; tick 1 lost; ticks 2 (60 s) and 3 (90 s) want to enter
        # while long (refused by the one-position limit); tick 4 (120 s) exits; 5 lost; 6 buys.
        r = vb.simulate(flat(7), vb.rotate(2, gated=False), free(), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (4, "sell"), (6, "buy")])
        self.assertEqual(r.total("position_bound"), 2)

    def test_trend_gate_keeps_the_book_flat_below_the_average(self) -> None:
        # History at 100, price 90: trend < 0 on every tick, so gated churn only proposes exits.
        r = vb.simulate(flat(8, 90.0, history=100.0), vb.gated_churn(), free(clip=1), keep_fills=True)
        self.assertEqual(r.fills, [])
        self.assertEqual(r.total("exit_flat"), 8)


class PaperToolingDefinitions(unittest.TestCase):
    """The paper tooling's clock-phase, clock-rotate and minute-close dip. T0 is a
    multiple of 120 s, so floor(T0 / 60) is even: the first minute is phase 0."""

    def test_clock_churn_is_long_in_odd_minutes(self) -> None:
        # Ticks 0, 1 are in an even minute (exit while flat); tick 2 (60 s) buys; 3 lost;
        # tick 4 (120 s, even) sells; 5 lost; tick 6 (180 s, odd) buys.
        r = vb.simulate(flat(7), vb.clock_churn(60, gated=False), free(), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (4, "sell"), (6, "buy")])
        self.assertEqual(r.total("exit_flat"), 2)

    def test_clock_churn_with_two_lost_ticks_waits_for_the_next_odd_minute(self) -> None:
        # Buy at 2 (60 s); 3, 4 lost; 5 (150 s, even) sells; 6, 7 lost; 8, 9 (240, 270 s, even)
        # exit while flat; 10 (300 s, odd) buys. The flip-per-tick churn would buy at 8.
        r = vb.simulate(flat(11), vb.clock_churn(60, gated=False), free(resync_ticks=2), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (5, "sell"), (10, "buy")])

    def test_clock_rotate_exits_in_the_first_minute_of_each_period(self) -> None:
        # Trend > 0 throughout (price 110 over history 100). Ticks 0, 1 (0 s, 30 s into the
        # period) exit while flat; 2 (60 s) buys; 3 lost; 4..59 want to enter while long (56);
        # 60 (1800 s, 0 s into the next period) sells; 61 lost; 62 (60 s into it) buys.
        r = vb.simulate(flat(63, 110.0, history=100.0), vb.clock_rotate(1800, 60), free(clip=1), keep_fills=True)
        self.assertEqual(sides(r), [(2, "buy"), (60, "sell"), (62, "buy")])
        self.assertEqual(r.total("position_bound"), 56)

    def test_minute_devs_uses_completed_minute_closes(self) -> None:
        # Minute closes: 100 (ticks 0, 1), 102 (ticks 2, 3). N = 2 is first available at
        # tick 4: 98 / 101 - 1 = -0.0297030 -> -29703; tick 5: 99 / 101 - 1 -> -19802.
        # Tick 6 opens minute 3: the window rolls to closes 102 and 99 (100 drops out):
        # 103 / 100.5 - 1 = 0.0248756 -> 24876.
        times = [T0 + 30 * i for i in range(7)]
        self.assertEqual(vb.minute_devs_series(times, [100.0, 100.0, 102.0, 102.0, 98.0, 99.0, 103.0], 2),
                         [None, None, None, None, -29703, -19802, 24876])

    def test_minute_dip_buys_below_and_sells_above(self) -> None:
        # Enter below -200 bp: tick 4 at -297 bp buys; 5 lost; tick 6 (minute 3, closes 102 and 99,
        # mean 100.5): 103 / 100.5 - 1 = +248.8 bp > +100 bp sells.
        r = vb.simulate(path_of([100.0, 100.0, 102.0, 102.0, 98.0, 99.0, 103.0, 103.0]), vb.minute_dip(2, 200, 100),
                        free(clip=1), keep_fills=True)
        self.assertEqual(sides(r), [(4, "buy"), (6, "sell")])


class Costs(unittest.TestCase):
    def test_round_trip_cost_and_volume(self) -> None:
        # 25 lots = 0.00025 BTC. Buy at 80,000: notional 20.00, fee 110 bp = 0.22.
        # Sell at 81,000: notional 20.25, fee 125 bp = 0.253125.
        # Net = -0.22 + 0.25 - 0.253125 = -0.223125; volume = 40.25.
        r = vb.simulate(path_of([80_000.0, 80_000.0, 81_000.0]), vb.churn(), free(buy=110.0, sell=125.0), keep_fills=True)
        self.assertEqual(sides(r), [(0, "buy"), (2, "sell")])
        self.assertAlmostEqual(r.volume, 40.25, places=9)
        self.assertAlmostEqual(r.total("fees"), 0.473125, places=9)
        self.assertAlmostEqual(r.net, -0.223125, places=9)
        self.assertAlmostEqual(r.total("price_pnl"), 0.25, places=9)
        self.assertAlmostEqual(r.score_bp, -0.223125 / 40.25 * 10_000, places=6)

    def test_fill_lag_prices_the_fill_one_tick_later(self) -> None:
        # Decision at tick 0 (80,000) fills at tick 1's 80,400: notional 0.00025 x 80,400 = 20.10.
        r = vb.simulate(path_of([80_000.0, 80_400.0, 80_800.0]), vb.churn(), free(buy=0.0, sell=0.0, fill_lag_ticks=1),
                        keep_fills=True)
        self.assertAlmostEqual(r.fills[0].price, 80_400.0)
        self.assertAlmostEqual(r.fills[0].price * r.fills[0].qty_btc, 20.10, places=9)


class Capital(unittest.TestCase):
    def test_losses_shrink_cash_until_the_venue_kills_the_buy(self) -> None:
        # $25 cash, $20 clip, 100 bp a side: a round trip costs 0.40. Buy k needs 20.20 and
        # finds 25 - 0.40 (k - 1): buys 1..13 fill, buy 14 finds 19.80 and is killed.
        # Round trip k buys at tick 4(k-1): buy 13 at 48, sell at 50; ticks 52, 54, 56, 58 are
        # killed buys and 53, 55, 57, 59 exits with nothing to sell.
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_topup=False)
        r = vb.simulate(flat(60), vb.churn(), ex, keep_fills=True)
        self.assertEqual(r.total("buys"), 13)
        self.assertEqual(r.total("sells"), 13)
        self.assertEqual(r.total("killed_capital"), 4)
        self.assertEqual(r.total("exit_flat"), 4)
        self.assertAlmostEqual(r.weeks[0].end_equity, 19.80, places=9)
        self.assertEqual(r.weeks[0].first_kill_s, 52 * 30)

    def test_a_buy_needs_its_cost_as_well_as_its_notional(self) -> None:
        # $20.00 clip at 100 bp needs 20.20. Cash 20.10 covers the notional but not the cost: killed.
        # Cash 20.20 covers both: filled.
        short = vb.Execution(25, 100.0, 100.0, capital_usd=20.10, exposure_cap_usd=None, weekly_topup=False)
        enough = vb.Execution(25, 100.0, 100.0, capital_usd=20.20, exposure_cap_usd=None, weekly_topup=False)
        self.assertEqual((vb.simulate(flat(1), vb.churn(), short).total("killed_capital"),
                          vb.simulate(flat(1), vb.churn(), short).total("buys")), (1, 0))
        self.assertEqual(vb.simulate(flat(1), vb.churn(), enough).total("buys"), 1)

    def test_kernel_exposure_cap_matches_the_runbook_boundary(self) -> None:
        # Runbook: 250 P x 1.015 <= 25,000,000 micros. At 98,522 the hold is 24,999,958 (fits);
        # at 98,523 it is 25,000,212 (refused), whatever the cash.
        ex = vb.Execution(25, 115.0, 120.0, capital_usd=1_000.0, exposure_cap_usd=25.0, fee_hold_bps=150.0)
        fits = vb.simulate(flat(1, 98_522.0), vb.churn(), ex)
        refused = vb.simulate(flat(1, 98_523.0), vb.churn(), ex)
        self.assertEqual((fits.total("buys"), fits.total("refused_exposure")), (1, 0))
        self.assertEqual((refused.total("buys"), refused.total("refused_exposure")), (0, 1))

    def test_weekly_loss_cap_stops_buys_before_the_cap_is_spent(self) -> None:
        # $1 cap, round trip 0.40: before buy k the week has lost 0.40 (k - 1); buy k is refused
        # when that + 0.40 > 1, i.e. k = 3. First refusal at tick 8 = 240 s into the week.
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_loss_cap_usd=1.0)
        r = vb.simulate(flat(12), vb.churn(), ex)
        self.assertEqual(r.total("buys"), 2)
        self.assertAlmostEqual(r.net, -0.80, places=9)
        self.assertEqual(r.weeks[0].cap_first_s, 240)

    def test_top_up_restores_capital_at_the_week_start(self) -> None:
        # Week 1: one round trip at 100 bp a side on $20 loses 0.40. At the first tick of
        # week 2 the book is flat with 24.60 and is topped up by 0.40.
        n = vb.WEEK_S // 30 + 3
        ex = vb.Execution(25, 100.0, 100.0, capital_usd=25.0, exposure_cap_usd=None, weekly_loss_cap_usd=0.5)
        r = vb.simulate(flat(n), vb.churn(), ex)
        self.assertEqual(len(r.weeks), 2)
        self.assertAlmostEqual(r.weeks[0].net, -0.40, places=9)
        self.assertAlmostEqual(r.weeks[1].topup, 0.40, places=9)
        self.assertAlmostEqual(r.weeks[1].start_equity, 25.0, places=9)
        self.assertEqual(r.weeks[1].buys, 1)  # a fresh week, a fresh cap

    def test_drawdown_is_measured_before_top_ups(self) -> None:
        # Price 80,000 -> 72,000 while long 0.00025 BTC, no costs: equity falls 2.00.
        r = vb.simulate(path_of([80_000.0, 80_000.0, 72_000.0]), vb.buy_and_hold(), free(buy=0.0, sell=0.0))
        self.assertAlmostEqual(r.max_drawdown_usd, 2.0, places=9)
        self.assertAlmostEqual(r.weeks[0].drawdown, 2.0, places=9)

    def test_weekly_drawdown_restarts_each_week(self) -> None:
        # Long 0.00025 BTC from 80,000. Week 1 falls to 76,000 (-1.00) and ends at 76,000;
        # week 2 starts at 76,000, rises to 78,000 and falls to 77,000: its drawdown is 0.25.
        n = vb.WEEK_S // 30
        prices = [80_000.0] + [76_000.0] * (n - 1) + [76_000.0, 78_000.0, 77_000.0]
        r = vb.simulate(path_of(prices), vb.buy_and_hold(), free(buy=0.0, sell=0.0))
        self.assertAlmostEqual(r.weeks[0].drawdown, 1.0, places=9)
        self.assertAlmostEqual(r.weeks[1].drawdown, 0.25, places=9)
        self.assertAlmostEqual(r.max_drawdown_usd, 1.0, places=9)


class Features(unittest.TestCase):
    def test_trend_uses_124_closes_and_the_live_price(self) -> None:
        # SMA = (124 x 100 + 125) / 125 = 100.2; 125 / 100.2 - 1 = 0.2475049... -> 247505 micros.
        p = vb.PricePath((T0,), (125.0,), (100.0,) * 124)
        self.assertEqual(vb.trend_series(p), [247505])

    def test_trend_rolls_the_day_close_into_history(self) -> None:
        # Day 1's close (last tick) is 150. On day 2 the SMA holds 123 x 100 + 150 + price.
        # Price 100: SMA = (12300 + 150 + 100) / 125 = 100.4; 100 / 100.4 - 1 = -0.0039841 -> -3984.
        p = vb.PricePath((T0, T0 + 86_370, T0 + 86_400), (100.0, 150.0, 100.0), (100.0,) * 124)
        self.assertEqual(vb.trend_series(p)[2], -3984)

    def test_devs_is_price_over_short_average(self) -> None:
        # w = 3: [1, 2, 3, 6] -> None, None, 3/2 - 1, 6/(11/3) - 1 = 0.6363636...
        self.assertEqual(vb.devs_series([1.0, 2.0, 3.0, 6.0], 3), [None, None, 500000, 636364])

    def test_dip_buys_below_and_sells_above(self) -> None:
        # w = 2, x = 100 bp, y = 0: devs at tick 1 = 99/99.5 - 1 = -50.25 bp (hold);
        # tick 2: 97/98 - 1 = -102 bp -> buy; tick 3 lost; tick 4: 99/98.5 - 1 = +50.8 bp -> sell.
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
        # Two grid values over 4 weeks, train 2 / step 1. Value 0 scores better on weeks 0-1,
        # value 1 on weeks 1-2: out of sample week 2 comes from value 0, week 3 from value 1.
        def result(nets: list[float]) -> vb.Result:
            return vb.Result([vb.Week(start_s=k, start_equity=0.0, end_equity=x, volume=100.0) for k, x in enumerate(nets)])
        a, b = result([-1.0, -1.0, -5.0, -1.0]), result([-3.0, -0.5, -0.5, -9.0])
        wf = vb.walk_forward([a, b], train_weeks=2, step_weeks=1)
        self.assertEqual(wf.choices, [(2, 0), (3, 1)])
        self.assertAlmostEqual(wf.score_bp, (-5.0 - 9.0) / 200.0 * 10_000)


class TickSampling(unittest.TestCase):
    def test_a_tick_sees_only_bars_closed_by_its_time(self) -> None:
        # Bars open at 0..4 with closes 10..14. A bar opening at s closes at s + 1, so the
        # tick at 2 sees the bar opening at 1 (close 11), the tick at 4 the bar opening at 3.
        times, prices, worst, stale = sample([(s, 10.0 + s) for s in range(5)], 2, 6, 2)
        self.assertEqual((times, prices), ([2, 4], [11.0, 13.0]))
        self.assertEqual(worst, 0)

    def test_a_gap_is_carried_and_measured_not_filled(self) -> None:
        # Bars at 0 and 10 only: tick 6 uses the bar opening at 0, 5 s old.
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
