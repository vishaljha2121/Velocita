"""Backtest lab: every expectation is computed by hand in the comment beside it
or is a structural property (no look-ahead, invariants of a permutation) that
holds regardless of what the code under test returns."""

from __future__ import annotations

import io
import math
import random
import unittest
import zipfile
from datetime import date, timedelta

from velocita_model.research import bars as rb
from velocita_model.research import metrics, permutation, strategy, universe, validation


def make_bars(closes: dict[str, list[float | None]], start: date = date(2024, 1, 1),
              volume: float | None = 1e9) -> rb.DailyBars:
    n = len(next(iter(closes.values())))
    symbols = tuple(closes)
    return rb.DailyBars(
        dates=tuple(start + timedelta(days=i) for i in range(n)),
        symbols=symbols,
        close=tuple(tuple(closes[s]) for s in symbols),
        quote_volume=None if volume is None else tuple(
            tuple(None if c is None else volume for c in closes[s]) for s in symbols),
    )


class Metrics(unittest.TestCase):
    def test_profit_factor(self) -> None:
        # log(1.1) + log(1.2) over |log(0.9)|: 0.09531 + 0.18232 = 0.27763 / 0.10536 = 2.635
        self.assertAlmostEqual(metrics.profit_factor([0.1, -0.1, 0.2, 0.0]), 2.635, places=3)
        self.assertEqual(metrics.profit_factor([0.1, 0.0]), math.inf)
        self.assertEqual(metrics.profit_factor([0.0, 0.0]), 0.0)

    def test_max_drawdown(self) -> None:
        # equity 1 -> 1.5 -> 0.75 -> 0.9: peak 1.5 to trough 0.75 = 50%
        self.assertAlmostEqual(metrics.max_drawdown([0.5, -0.5, 0.2]), 0.5)

    def test_total_return(self) -> None:
        self.assertAlmostEqual(metrics.total_return([0.1, -0.1]), 1.1 * 0.9 - 1)

    def test_p_value(self) -> None:
        # real 2.0; nulls 1, 2, 3 -> two at least as good -> (1 + 2) / (3 + 1)
        self.assertEqual(metrics.p_value(2.0, [1.0, 2.0, 3.0]), 0.75)


class BinanceArchive(unittest.TestCase):
    def _zip(self, rows: list[str]) -> bytes:
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w") as archive:
            archive.writestr("X-1d-2025-01.csv", "\n".join(rows) + "\n")
        return buffer.getvalue()

    def test_parses_millisecond_and_microsecond_open_times(self) -> None:
        rows = [
            # 2024-12-31 in ms (Binance used ms before 2025)
            "1735603200000,1,1,1,93000.5,10,1735689599999,930005.0,1,1,1,0",
            # 2025-01-01 in us (spot archives switched to microseconds in 2025)
            "1735689600000000,1,1,1,94000.0,11,1735775999999999,1034000.0,1,1,1,0",
        ]
        parsed = rb.parse_binance_kline_zip(self._zip(rows))
        self.assertEqual(parsed, [(date(2024, 12, 31), 93000.5, 930005.0), (date(2025, 1, 1), 94000.0, 1034000.0)])

    def test_checksum_mismatch_is_refused(self) -> None:
        data = self._zip(["1735603200000,1,1,1,1,1,1,1,1,1,1,0"])
        with self.assertRaises(rb.DataError):
            rb.verify_checksum(data, "0" * 64 + "  X-1d-2025-01.zip")

    def test_internal_gap_is_refused(self) -> None:
        with self.assertRaises(rb.DataError):
            rb.assemble({"BTC": [(date(2024, 1, 1), 1.0, 1.0), (date(2024, 1, 3), 1.0, 1.0)]})


BTC_UP = [100, 101, 102, 103, 104, 105, 106]  # above its MAs, 2-day momentum ~2%: never the strongest


class TrueMarketsHistory(unittest.TestCase):
    def test_midnight_point_is_the_previous_days_close(self) -> None:
        # Inferred 2026-09-28: a 1d point stamped 00:00Z on day d correlates 0.74-0.90 with Binance's
        # close of day d-1 and 0.09-0.25 with day d's close.
        rows = rb.truemarkets_daily_rows([{"t": "2026-09-28T00:00:00Z", "price": "118.5"},
                                          {"t": "2026-09-29T00:00:00Z", "price": "119.0"}])
        self.assertEqual(rows, [(date(2026, 9, 27), 118.5, None), (date(2026, 9, 28), 119.0, None)])

    def test_a_point_off_midnight_is_refused(self) -> None:
        with self.assertRaises(rb.DataError):
            rb.truemarkets_daily_rows([{"t": "2026-09-28T11:51:00Z", "price": "118.5"}])


class Strategy(unittest.TestCase):
    """Two assets plus BTC as regime asset. Parameters shrunk so the hand
    arithmetic stays small: momentum 2 days, history 3, MAs 3 and 4."""

    params = strategy.Params(momentum_days=2, min_history=3, ma_short=3, ma_long=4, btc_momentum_days=2,
                             min_quote_volume=0.0, leverage_cap=0.5, cost_bps=0.0, lag_days=1,
                             rebalance_weekday=None)

    def test_holds_the_strongest_asset_at_the_capped_fraction(self) -> None:
        # Index 3 is the first day with 4 BTC closes for the long MA; BTC above it -> full exposure.
        # 2-day momentum at index 3: BTC 103/101-1 = 2%, A 11/10-1 = 10%, B 10.5/10-1 = 5% -> A at 0.5.
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 12, 24], "B": [10, 10, 10, 10.5, 11, 11, 11]})
        result = strategy.backtest(b, self.params)
        # Chosen at the close of day 3, filled at the close of day 4 (lag 1).
        self.assertEqual(result.holdings[3], (None, 0.0))
        self.assertEqual(result.holdings[4], ("A", 0.5))
        # Day 5: A flat. Day 6: A doubles (12 -> 24) with half the equity in A -> +50%.
        self.assertEqual(result.returns[:6], [0.0] * 6)
        self.assertAlmostEqual(result.returns[6], 0.5)

    def test_no_look_ahead(self) -> None:
        base = {"BTC": BTC_UP + [107], "A": [10, 10, 10, 11, 12, 12, 13, 14],
                "B": [10, 10, 10, 10.5, 11, 11, 11, 11]}
        future = dict(base, B=base["B"][:6] + [1000, 5000])  # only days 6-7 change
        r1, r2 = strategy.backtest(make_bars(base), self.params), strategy.backtest(make_bars(future), self.params)
        self.assertEqual(r1.holdings[:7], r2.holdings[:7])

    def test_btc_regime_scales_exposure(self) -> None:
        # Index 4: BTC 8 < MA4 (10+10+9+8)/4 = 9.25 and 8/10-1 < 0 -> 0.15 of the 0.5 cap = 0.075.
        b = make_bars({"BTC": [10, 10, 10, 9, 8, 7, 6], "A": [10, 10, 10, 11, 12, 13, 14],
                       "B": [10, 10, 10, 10, 10, 10, 10]})
        self.assertEqual(strategy.backtest(b, self.params).holdings[5], ("A", 0.075))

    def test_costs_are_charged_on_traded_value(self) -> None:
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 12, 12], "B": [10, 10, 10, 10.5, 11, 11, 11]})
        costly = strategy.Params(**{**self.params.__dict__, "cost_bps": 100.0})
        # Buying 0.5 of equity at 1% cost on the fill day (4), A flat that day -> -0.5%.
        self.assertAlmostEqual(strategy.backtest(b, costly).returns[4], -0.005)

    def test_new_listing_waits_for_its_history(self) -> None:
        # C lists at index 1 and triples, but with min_history 4 it has only 3 closes at index 3,
        # so A (11/10-1 = 10%) must be chosen over C (30/10-1 = 200%).
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 12, 12], "C": [None, 10, 20, 30, 30, 30, 30]})
        young = strategy.Params(**{**self.params.__dict__, "min_history": 4})
        self.assertEqual(strategy.backtest(b, young).holdings[4], ("A", 0.5))

    def test_illiquid_asset_is_ineligible(self) -> None:
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 12, 12], "B": [10, 10, 10, 10.5, 11, 11, 11]},
                      volume=5.0)
        strict = strategy.Params(**{**self.params.__dict__, "min_quote_volume": 10.0})
        self.assertEqual(strategy.backtest(b, strict).holdings[5], (None, 0.0))


class PointInTimeUniverse(unittest.TestCase):
    def test_candidate_filter(self) -> None:
        for kept in ("BTCUSDT", "SOLUSDT", "LUNAUSDT", "1INCHUSDT"):
            self.assertTrue(universe.is_candidate(kept), kept)
        for dropped in ("USDCUSDT", "FDUSDUSDT", "EURUSDT", "PAXGUSDT", "BTCUPUSDT", "ETHDOWNUSDT", "XRPBULLUSDT",
                        "WBTCUSDT", "BTCUSDC", "BEARUSDT", "BULLUSDT", "XAUTUSDT"):
            self.assertFalse(universe.is_candidate(dropped), dropped)

    def test_membership_uses_only_the_previous_month(self) -> None:
        volumes = {"BTCUSDT": {"2024-01": 1.0, "2024-02": 1.0},
                   "AUSDT": {"2024-01": 9.0, "2024-02": 1.0},
                   "BUSDT": {"2024-01": 5.0, "2024-02": 9.0},
                   "CUSDT": {"2024-01": 7.0, "2024-02": 8.0}}
        members = universe.monthly_members(volumes, ["2024-02", "2024-03"], top_n=2, always={"BTCUSDT"})
        # February is chosen from January volumes (A 9, C 7), March from February (B 9, C 8); BTC always.
        self.assertEqual(members["2024-02"], {"AUSDT", "CUSDT", "BTCUSDT"})
        self.assertEqual(members["2024-03"], {"BUSDT", "CUSDT", "BTCUSDT"})

    def test_mask_follows_calendar_months(self) -> None:
        dates = (date(2024, 2, 28), date(2024, 2, 29), date(2024, 3, 1))
        mask = universe.eligibility_mask(dates, ("A", "B"), {"2024-02": {"A"}, "2024-03": {"B"}})
        self.assertEqual(mask, ((True, True, False), (False, False, True)))


class EligibilityAndDelisting(unittest.TestCase):
    params = Strategy.params

    def test_non_member_is_never_chosen(self) -> None:
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 12, 12], "B": [10, 10, 10, 10.5, 11, 11, 11]})
        mask = ((True,) * 7, (False,) * 7, (True,) * 7)  # A (strongest) is outside the universe
        gated = rb.DailyBars(b.dates, b.symbols, b.close, b.quote_volume, eligible=mask)
        self.assertEqual(strategy.backtest(gated, self.params).holdings[4], ("B", 0.5))

    def test_held_asset_that_stops_trading_is_sold_at_its_last_close(self) -> None:
        # A chosen at close 3, filled at close 4 (price 12); last close 13 on day 5, then no data.
        b = make_bars({"BTC": BTC_UP, "A": [10, 10, 10, 11, 12, 13, None], "B": [10, 10, 10, 10.5, 11, 11, 11]})
        result = strategy.backtest(b, self.params)
        # Day 5: half the equity in A gains 13/12 - 1 -> +0.5 * 1/12 = +4.1667%.
        self.assertAlmostEqual(result.returns[5], 0.5 / 12)
        # Day 6: A has no close -> liquidated at 13: flat, and no longer held.
        self.assertEqual(result.returns[6], 0.0)
        self.assertEqual(result.holdings[6][0], None)

    def test_gaps_are_allowed_only_when_asked(self) -> None:
        rows = {"X": [(date(2024, 1, 1), 1.0, 1.0), (date(2024, 1, 3), 1.0, 1.0)],
                "BTC": [(date(2024, 1, 1), 1.0, 1.0), (date(2024, 1, 2), 1.0, 1.0), (date(2024, 1, 3), 1.0, 1.0)]}
        self.assertEqual(rb.assemble(rows, allow_gaps=True).close[0], (1.0, None, 1.0))


class Permutation(unittest.TestCase):
    def setUp(self) -> None:
        rng = random.Random(7)
        n = 60
        btc, eth = [100.0], [50.0]
        for _ in range(n - 1):
            btc.append(btc[-1] * math.exp(rng.gauss(0, 0.03)))
            eth.append(eth[-1] * math.exp(rng.gauss(0, 0.04)))
        late = [None] * 20 + [5.0]
        for _ in range(n - 21):
            late.append(late[-1] * math.exp(rng.gauss(0, 0.05)))
        self.bars = make_bars({"BTC": btc, "ETH": eth, "NEW": late})

    def _log_returns(self, series, start):
        return sorted(round(math.log(series[i] / series[i - 1]), 12) for i in range(max(1, start), len(series))
                      if series[i] is not None and series[i - 1] is not None)

    def test_invariants(self) -> None:
        p = permutation.permute(self.bars, start_index=1, rng=random.Random(1))
        for a, series in enumerate(self.bars.close):
            new = p.close[a]
            self.assertEqual(self._log_returns(series, 1), self._log_returns(new, 1), "return multiset changed")
            first = next(i for i, c in enumerate(series) if c is not None)
            self.assertEqual(new[:first + 1], series[:first + 1], "history before listing or first price changed")
            self.assertAlmostEqual(new[-1], series[-1], places=6)
        self.assertNotEqual(p.close[0], self.bars.close[0], "nothing was permuted")

    def test_same_day_moves_stay_together(self) -> None:
        # BTC and ETH trade every day, so each permuted day must carry one real day's pair of moves.
        p = permutation.permute(self.bars, start_index=1, rng=random.Random(3))
        real_pairs = {(round(math.log(self.bars.close[0][i] / self.bars.close[0][i - 1]), 9),
                       round(math.log(self.bars.close[1][i] / self.bars.close[1][i - 1]), 9)) for i in range(1, 60)}
        for i in range(1, 60):
            pair = (round(math.log(p.close[0][i] / p.close[0][i - 1]), 9),
                    round(math.log(p.close[1][i] / p.close[1][i - 1]), 9))
            self.assertIn(pair, real_pairs)

    def test_volume_travels_with_its_day(self) -> None:
        # Give every real day a distinct volume; each permuted day must carry a real (move, volume) pair.
        volumes = tuple(tuple(None if c is None else float(1000 * a + i) for i, c in enumerate(series))
                        for a, series in enumerate(self.bars.close))
        b = rb.DailyBars(self.bars.dates, self.bars.symbols, self.bars.close, volumes)
        p = permutation.permute(b, start_index=1, rng=random.Random(4))
        real = {(round(math.log(b.close[0][i] / b.close[0][i - 1]), 9), b.quote_volume[0][i]) for i in range(1, 60)}
        for i in range(1, 60):
            self.assertIn((round(math.log(p.close[0][i] / p.close[0][i - 1]), 9), p.quote_volume[0][i]), real)

    def test_prefix_before_start_is_untouched(self) -> None:
        p = permutation.permute(self.bars, start_index=30, rng=random.Random(5))
        for a in range(3):
            self.assertEqual(p.close[a][:30], self.bars.close[a][:30])

    def test_seeded_and_deterministic(self) -> None:
        a = permutation.permute(self.bars, start_index=1, rng=random.Random(9))
        b = permutation.permute(self.bars, start_index=1, rng=random.Random(9))
        self.assertEqual(a, b)

    def test_random_walk_keeps_shape_and_listing(self) -> None:
        w = permutation.random_walk(self.bars, start_index=1, rng=random.Random(2))
        self.assertEqual(w.close[2][:21], self.bars.close[2][:21])
        self.assertTrue(all(c is not None and c > 0 for c in w.close[0]))


class Validation(unittest.TestCase):
    def test_in_sample_picks_the_best_grid_value(self) -> None:
        rng = random.Random(11)
        closes = {"BTC": [100 * math.exp(0.001 * i) for i in range(400)]}
        for s in ("A", "B", "C"):
            p = [10.0]
            for _ in range(399):
                p.append(p[-1] * math.exp(rng.gauss(0.0005, 0.03)))
            closes[s] = p
        b = make_bars(closes)
        base = strategy.Params(min_quote_volume=0.0, cost_bps=0.0)
        best = validation.optimize(b, base, grid=[20, 60, 120])
        scores = {m: metrics.profit_factor(strategy.backtest(b, strategy.Params(**{**base.__dict__, "momentum_days": m})).returns)
                  for m in (20, 60, 120)}
        self.assertEqual(best.momentum_days, max(scores, key=scores.get))
        self.assertEqual(best.score, max(scores.values()))


if __name__ == "__main__":
    unittest.main()
