"""Fill model, quoting, inventory, and markout behaviour.

The properties tested here are the ones that separate an honest shadow study
from a flattering one.
"""

from __future__ import annotations

import unittest
from decimal import Decimal

from velocita_model.contracts.enums import Side
from velocita_model.simulation.fill_model import (
    FillModel,
    FillModelSet,
    QueueAssumption,
    initial_queue_ahead,
)
from velocita_model.simulation.inventory import InventoryLimits, SimulatedInventory
from velocita_model.simulation.markouts import (
    FairValueTrack,
    compute_markouts,
    markout_bps,
    summarize,
)
from velocita_model.simulation.risk_engine import RiskEngine, StopKind
from velocita_model.simulation.shadow_quotes import (
    QuoteParameters,
    build_quote,
    cost_floor_bps,
    round_to_lot,
    round_to_tick,
)

D = Decimal


def params(**overrides: object) -> QuoteParameters:
    base = {
        "base_half_spread_bps": D("15"),
        "min_half_spread_bps": D("5"),
        "max_half_spread_bps": D("150"),
        "vol_spread_multiplier": D("1.5"),
        "inventory_skew_bps_at_limit": D("20"),
        "maker_fee_bps": D("2"),
        "gas_cost_bps": D("1"),
        "tick_size": D("0.0001"),
        "lot_size": D("0.01"),
        "min_notional_quote": D("1.00"),
    }
    base.update(overrides)
    return QuoteParameters(**base)  # type: ignore[arg-type]


def inventory(**overrides: object) -> SimulatedInventory:
    limits = InventoryLimits(
        account_equity_quote=D("20"),
        reserve_fraction=D("0.5"),
        max_inventory_quote=D("5"),
    )
    return SimulatedInventory(limits=limits, **overrides)  # type: ignore[arg-type]


class TickAndLotRounding(unittest.TestCase):
    def test_bid_rounds_down_and_ask_rounds_up(self) -> None:
        """Rounding must never move a quote toward the mid: that would make it
        more aggressive than the model asked for."""
        self.assertEqual(round_to_tick(D("1.00005"), D("0.0001"), Side.buy), D("1.0000"))
        self.assertEqual(round_to_tick(D("1.00005"), D("0.0001"), Side.sell), D("1.0001"))

    def test_exact_tick_multiple_is_unchanged_on_both_sides(self) -> None:
        for side in (Side.buy, Side.sell):
            self.assertEqual(round_to_tick(D("1.0002"), D("0.0001"), side), D("1.0002"))

    def test_size_always_rounds_down(self) -> None:
        self.assertEqual(round_to_lot(D("1.999"), D("0.01")), D("1.99"))

    def test_zero_or_negative_tick_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            round_to_tick(D("1"), D("0"), Side.buy)


class CostFloor(unittest.TestCase):
    def test_floor_covers_a_round_trip_with_margin(self) -> None:
        """2bps maker + 1bps gas, twice, times a 1.5x margin, halved across the
        two sides = 4.5bps per side."""
        self.assertEqual(cost_floor_bps(params()), D("4.5"))

    def test_no_favourable_adjustment_can_quote_inside_the_floor(self) -> None:
        quote = build_quote(
            side=Side.buy,
            fair_value=D("1.0000"),
            params=params(base_half_spread_bps=D("0"), min_half_spread_bps=D("0")),
            inventory=inventory(),
            volatility_bps=D("0"),
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("2"),
        )
        self.assertGreaterEqual(quote.half_spread_bps, cost_floor_bps(params()))


class QuoteConstruction(unittest.TestCase):
    def test_unavailable_volatility_suppresses_the_quote(self) -> None:
        """An unmeasured market is not a calm one."""
        quote = build_quote(
            side=Side.buy,
            fair_value=D("1.0000"),
            params=params(),
            inventory=inventory(),
            volatility_bps=None,
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("1"),
        )
        self.assertFalse(quote.is_quotable)
        self.assertIn("volatility unavailable", quote.suppressed_reason or "")

    def test_staleness_widens_the_quote(self) -> None:
        def half_spread(age_ns: int) -> Decimal:
            return build_quote(
                side=Side.buy,
                fair_value=D("1.0000"),
                params=params(),
                inventory=inventory(),
                volatility_bps=D("20"),
                data_age_ns=age_ns,
                recent_adverse_selection_bps=None,
                target_notional_quote=D("2"),
            ).half_spread_bps

        self.assertGreater(half_spread(1_000_000_000), half_spread(0))

    def test_recent_adverse_selection_widens_but_favourable_does_not_tighten(self) -> None:
        def half_spread(markout: Decimal | None) -> Decimal:
            return build_quote(
                side=Side.buy,
                fair_value=D("1.0000"),
                params=params(),
                inventory=inventory(),
                volatility_bps=D("5"),
                data_age_ns=0,
                recent_adverse_selection_bps=markout,
                target_notional_quote=D("2"),
            ).half_spread_bps

        neutral = half_spread(None)
        self.assertGreater(half_spread(D("-10")), neutral)
        self.assertEqual(half_spread(D("+10")), neutral)

    def test_long_inventory_backs_off_the_bid_and_leans_the_ask_in(self) -> None:
        long_book = inventory(simulated_base=D("3"))  # +3 quote notional at ~1.0
        bid = build_quote(
            side=Side.buy,
            fair_value=D("1.0000"),
            params=params(),
            inventory=long_book,
            volatility_bps=D("5"),
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("1"),
        )
        ask = build_quote(
            side=Side.sell,
            fair_value=D("1.0000"),
            params=params(),
            inventory=long_book,
            volatility_bps=D("5"),
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("1"),
        )
        self.assertGreater(bid.terms.inventory_skew_bps, 0)
        self.assertLess(ask.terms.inventory_skew_bps, 0)

    def test_notional_below_the_venue_minimum_is_suppressed(self) -> None:
        quote = build_quote(
            side=Side.buy,
            fair_value=D("1.0000"),
            params=params(min_notional_quote=D("50")),
            inventory=inventory(),
            volatility_bps=D("5"),
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("1"),
        )
        self.assertFalse(quote.is_quotable)
        self.assertIn("below venue minimum", quote.suppressed_reason or "")


class InventoryLimitBehaviour(unittest.TestCase):
    def test_reserve_cannot_be_spent(self) -> None:
        with self.assertRaises(ValueError):
            InventoryLimits(
                account_equity_quote=D("20"), reserve_fraction=D("0.5"), max_inventory_quote=D("15")
            )

    def test_limit_is_directional(self) -> None:
        """Being maximally long stops the bid, not the ask - otherwise the
        simulator could never model reducing an unwanted position."""
        book = inventory(simulated_base=D("6"))
        self.assertTrue(book.at_limit(D("1.0000"), Side.buy))
        self.assertFalse(book.at_limit(D("1.0000"), Side.sell))

    def test_quote_is_suppressed_at_the_cap(self) -> None:
        quote = build_quote(
            side=Side.buy,
            fair_value=D("1.0000"),
            params=params(),
            inventory=inventory(simulated_base=D("6")),
            volatility_bps=D("5"),
            data_age_ns=0,
            recent_adverse_selection_bps=None,
            target_notional_quote=D("1"),
        )
        self.assertFalse(quote.is_quotable)
        self.assertIn("inventory at cap", quote.suppressed_reason or "")

    def test_fees_are_charged_against_simulated_equity(self) -> None:
        book = inventory()
        before = book.simulated_equity(D("1.0000"))
        book.apply_simulated_fill(Side.buy, D("1.0000"), D("1"), fee_quote=D("0.02"))
        self.assertEqual(book.simulated_equity(D("1.0000")), before - D("0.02"))

    def test_drawdown_is_peak_to_trough(self) -> None:
        book = inventory()
        for at_ns, price in ((1, D("1.00")), (2, D("1.10")), (3, D("0.90"))):
            book.simulated_base = D("1")
            book.record_mark(at_ns, price)
        self.assertEqual(book.max_simulated_drawdown(), D("0.20"))


class QueueAssumptions(unittest.TestCase):
    def test_assumptions_order_the_queue_ahead(self) -> None:
        displayed = D("100")
        self.assertEqual(initial_queue_ahead(QueueAssumption.optimistic, displayed), D("0"))
        self.assertEqual(initial_queue_ahead(QueueAssumption.base, displayed), displayed)
        self.assertGreater(initial_queue_ahead(QueueAssumption.conservative, displayed), displayed)

    def test_touching_the_price_is_not_a_fill_under_a_real_queue(self) -> None:
        """The core discipline. A print at our price that is smaller than the
        queue ahead of us fills nobody."""
        model = FillModel(QueueAssumption.base)
        model.place(side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("100"))
        fills = model.observe_trade(at_ns=1, price=D("1.00"), size_base=D("10"), direction="sell")
        self.assertEqual(fills, [])

    def test_optimistic_assumption_fills_on_the_same_print(self) -> None:
        model = FillModel(QueueAssumption.optimistic)
        model.place(side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("100"))
        fills = model.observe_trade(at_ns=1, price=D("1.00"), size_base=D("10"), direction="sell")
        self.assertEqual(len(fills), 1)

    def test_a_swept_level_fills_regardless_of_queue_position(self) -> None:
        """Once the market trades through our price, queue position stops
        mattering."""
        model = FillModel(QueueAssumption.conservative)
        model.place(side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("100"))
        fills = model.observe_trade(at_ns=1, price=D("0.99"), size_base=D("1"), direction="sell")
        self.assertEqual(len(fills), 1)
        self.assertTrue(fills[0].swept)

    def test_partial_fills_are_tracked(self) -> None:
        model = FillModel(QueueAssumption.optimistic)
        quote = model.place(
            side=Side.buy, price=D("1.00"), size_base=D("10"), at_ns=0, displayed_size_at_level=D("0")
        )
        fills = model.observe_trade(at_ns=1, price=D("1.00"), size_base=D("4"), direction="sell")
        self.assertTrue(fills[0].partial)
        self.assertEqual(quote.remaining_base, D("6"))

    def test_direction_prevents_a_bid_filling_on_buyer_initiated_flow(self) -> None:
        model = FillModel(QueueAssumption.optimistic)
        model.place(side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("0"))
        self.assertEqual(
            model.observe_trade(at_ns=1, price=D("1.00"), size_base=D("5"), direction="buy"), []
        )

    def test_fill_counts_are_reported_as_a_range(self) -> None:
        models = FillModelSet()
        models.place(
            side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("5")
        )
        models.observe_trade(at_ns=1, price=D("1.00"), size_base=D("3"), direction="sell")
        low, high = models.fill_probability_range()
        self.assertEqual(low, 0)
        self.assertEqual(high, 1)

    def test_every_fill_is_flagged_as_a_model_output(self) -> None:
        model = FillModel(QueueAssumption.optimistic)
        model.place(side=Side.buy, price=D("1.00"), size_base=D("1"), at_ns=0, displayed_size_at_level=D("0"))
        fills = model.observe_trade(at_ns=1, price=D("1.00"), size_base=D("1"), direction="sell")
        self.assertTrue(fills[0].is_model_output)


class Markouts(unittest.TestCase):
    def test_sign_convention_positive_is_good_for_us(self) -> None:
        self.assertGreater(markout_bps(Side.buy, D("100"), D("101")), 0)
        self.assertLess(markout_bps(Side.buy, D("100"), D("99")), 0)
        self.assertGreater(markout_bps(Side.sell, D("100"), D("99")), 0)
        self.assertLess(markout_bps(Side.sell, D("100"), D("101")), 0)

    def test_a_horizon_beyond_the_data_is_omitted_not_zeroed(self) -> None:
        """Otherwise 'we stopped watching' reads as 'no adverse selection'."""
        track = FairValueTrack()
        track.observe(0, D("100"))
        track.observe(1_000_000_000, D("101"))
        result = compute_markouts(
            track=track,
            at_ns=0,
            side=Side.buy,
            fill_price=D("100"),
            fill_size_base=D("1"),
            fair_value_at_fill=D("100"),
            horizons_ms=[1_000, 300_000],
            assumption="base",
        )
        self.assertEqual([m.horizon_ms for m in result.markouts], [1_000])
        self.assertIsNone(result.at_horizon(300_000))

    def test_percentiles_report_the_unfavourable_tail(self) -> None:
        track = FairValueTrack()
        fills = []
        # Nineteen small wins and one large loss: the mean looks fine, the tail
        # does not. A strategy must be judged on the tail.
        for index, later in enumerate([D("100.1")] * 19 + [D("90")]):
            track.observe(index * 2_000_000_000, D("100"))
            track.observe(index * 2_000_000_000 + 1_000_000_000, later)
            fills.append(
                compute_markouts(
                    track=track,
                    at_ns=index * 2_000_000_000,
                    side=Side.buy,
                    fill_price=D("100"),
                    fill_size_base=D("1"),
                    fair_value_at_fill=D("100"),
                    horizons_ms=[1_000],
                    assumption="base",
                )
            )
        stats = summarize(fills, 1_000)
        self.assertEqual(stats.sample_count, 20)
        self.assertGreater(stats.median_bps, 0)
        self.assertLess(stats.worst_bps, D("-900"))
        self.assertLess(stats.p95_bps, 0)


class RiskStops(unittest.TestCase):
    def test_stale_data_pauses_the_pair(self) -> None:
        engine = RiskEngine()
        stop = engine.check_market_data(
            pair_id="X/USDC",
            at_ns=1,
            data_age_ns=10_000_000_000,
            sequence_gap=False,
            volatility_bps=None,
            near_depth_quote=None,
            reference_divergence_bps=None,
        )
        self.assertIsNotNone(stop)
        self.assertEqual(stop.kind, StopKind.stale_data)
        self.assertTrue(engine.is_paused("X/USDC"))

    def test_sequence_gap_pauses_the_pair(self) -> None:
        engine = RiskEngine()
        stop = engine.check_market_data(
            pair_id="X/USDC",
            at_ns=1,
            data_age_ns=0,
            sequence_gap=True,
            volatility_bps=None,
            near_depth_quote=None,
            reference_divergence_bps=None,
        )
        self.assertEqual(stop.kind, StopKind.sequence_gap)

    def test_unsynchronised_clock_stops_everything_globally(self) -> None:
        """Wall-clock differences underpin every latency and markout number, so
        an unsynchronised clock invalidates rather than degrades them."""
        engine = RiskEngine()
        stop = engine.check_host(
            at_ns=1, cpu_percent=1.0, disk_percent=1.0, clock_synced=False, clock_offset_ms=None
        )
        self.assertEqual(stop.kind, StopKind.clock_unsynchronised)
        self.assertIsNotNone(engine.global_stop)
        self.assertTrue(engine.is_paused("any-pair"))

    def test_resuming_a_pair_is_explicit(self) -> None:
        engine = RiskEngine()
        engine.check_market_data(
            pair_id="X/USDC",
            at_ns=1,
            data_age_ns=10_000_000_000,
            sequence_gap=False,
            volatility_bps=None,
            near_depth_quote=None,
            reference_divergence_bps=None,
        )
        engine.resume("X/USDC")
        self.assertFalse(engine.is_paused("X/USDC"))

    def test_simulated_equity_floor_records_stopped_by_risk(self) -> None:
        engine = RiskEngine()
        engine.check_simulated_equity(pair_id="X/USDC", at_ns=1, simulated_equity_quote=Decimal("20"))
        stop = engine.check_simulated_equity(
            pair_id="X/USDC", at_ns=2, simulated_equity_quote=Decimal("18.5")
        )
        self.assertEqual(stop.kind, StopKind.stopped_by_risk)


if __name__ == "__main__":
    unittest.main()
