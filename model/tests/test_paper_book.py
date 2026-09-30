"""Paper book: kernel-confirmed fills, venue fees, the ISO-week loss budget and
the buy stop.

Oracles: hand arithmetic from the catalog's unit (10 micros per tick-lot) and
the paper venue's fee rule (ceil(notional x bps / 10,000), ADR 0018); calendar
facts (2026-09-28 is a Monday, ISO week 40; 2026-10-05 starts week 41).
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from decimal import Decimal
from pathlib import Path
from tempfile import TemporaryDirectory

from velocita_model.shadow import edge
from velocita_model.shadow.paper_book import PaperBook, fee_micros, iso_week

from test_shadow_edge import history, reference

NS = 1_000_000_000
MON = 1_790_596_800 * NS - 12 * 3600 * NS   # 2026-09-28T00:00:00Z, Monday, 2026-W40
NEXT_MON = MON + 7 * 86_400 * NS            # 2026-10-05T00:00:00Z, 2026-W41


def book(**kw) -> PaperBook:
    args = dict(clip_lots=25, quote_micros_per_tick_lot=10, buy_bps=115, sell_bps=120, cash_usd=Decimal(25),
                weekly_loss_cap_usd=Decimal(12))
    args.update(kw)
    return PaperBook(**args)


def buy_then_confirm(b: PaperBook, at: int, price: int) -> None:
    b.observe(at, price, action="enter_long", authorization="applied", resync_position=None, stop=None)
    b.observe(at + 30 * NS, price, action="exit", authorization="stale_risk_state",
              resync_position=b.state.position + b.clip, stop=None)


def sell_then_confirm(b: PaperBook, at: int, price: int) -> None:
    b.observe(at, price, action="exit", authorization="applied", resync_position=None, stop=None)
    b.observe(at + 30 * NS, price, action="enter_long", authorization="stale_risk_state",
              resync_position=b.state.position - b.clip, stop=None)


class Arithmetic(unittest.TestCase):
    def test_iso_week_boundaries(self) -> None:
        self.assertEqual(iso_week(MON), "2026-W40")
        self.assertEqual(iso_week(NEXT_MON - 1), "2026-W40")  # Sunday 23:59:59.999999999
        self.assertEqual(iso_week(NEXT_MON), "2026-W41")

    def test_fees_round_against_us(self) -> None:
        self.assertEqual(fee_micros(21_000_000, 115), 241_500)   # exact
        self.assertEqual(fee_micros(840_010, 115), 9_661)        # 9,660.115 -> 9,661
        self.assertEqual(fee_micros(0, 115), 0)


class RoundTrip(unittest.TestCase):
    def test_a_confirmed_round_trip_books_volume_fees_and_pnl(self) -> None:
        b = book()
        buy_then_confirm(b, MON, 84_000)
        # 84,000 ticks x 25 lots x 10 = 21,000,000 micros; fee 241,500.
        s = b.state
        self.assertEqual((s.position, s.basis, s.cash, s.pnl), (25, 21_000_000, 25_000_000 - 21_241_500, -241_500))
        sell_then_confirm(b, MON + 60 * NS, 84_100)
        # 21,025,000 sold; fee ceil(2,523,000,000 / 10,000) = 252,300; P&L 25,000 - 252,300 = -227,300.
        self.assertEqual((s.position, s.basis), (0, 0))
        self.assertEqual(s.volume, 42_025_000)
        self.assertEqual(s.fees, 493_800)
        self.assertEqual(s.pnl, -468_800)
        self.assertEqual(s.cash, 25_000_000 - 468_800)
        self.assertEqual((s.week.key, s.week.volume, s.week.pnl, s.week.loss()), ("2026-W40", 42_025_000, -468_800,
                                                                                  468_800))

    def test_nothing_is_booked_without_the_kernels_position(self) -> None:
        b = book()
        b.observe(MON, 84_000, action="enter_long", authorization="applied", resync_position=None, stop=None)
        self.assertEqual((b.state.volume, b.state.position), (0, 0))
        # A resync that reports no move: the order was killed or is still open.
        b.observe(MON + 30 * NS, 84_000, action="enter_long", authorization="stale_risk_state", resync_position=0,
                  stop=None)
        self.assertEqual((b.state.volume, b.state.buys), (0, 0))
        self.assertIsNotNone(b.state.pending)

    def test_a_refused_authorization_is_not_an_order(self) -> None:
        b = book()
        b.observe(MON, 84_000, action="enter_long", authorization="position_limit_exceeded", resync_position=None,
                  stop=None)
        self.assertIsNone(b.state.pending)

    def test_an_unexplained_move_is_adopted_and_counted_not_priced(self) -> None:
        b = book()
        b.observe(MON, 84_000, action="exit", authorization="stale_risk_state", resync_position=25, stop=None)
        self.assertEqual((b.state.position, b.state.unattributed, b.state.volume), (25, 1, 0))
        # A pending sell does not explain a move up.
        b.observe(MON + NS, 84_000, action="exit", authorization="applied", resync_position=None, stop=None)
        b.observe(MON + 2 * NS, 84_000, action="exit", authorization="stale_risk_state", resync_position=50,
                  stop=None)
        self.assertEqual((b.state.position, b.state.unattributed, b.state.buys), (50, 2, 0))

    def test_an_adopted_position_is_marked_at_the_tick_price(self) -> None:
        # The kernel was already long 25 when this book started (switching from trend).
        b = book()
        b.observe(MON, 84_000, action="exit", authorization="stale_risk_state", resync_position=25, stop=None)
        self.assertEqual((b.state.basis, b.state.cash), (21_000_000, 25_000_000 - 21_000_000))
        sell_then_confirm(b, MON + 60 * NS, 83_000)
        # 83,000 x 25 x 10 = 20,750,000 sold against a 21,000,000 mark: -250,000 - fee 249,000.
        self.assertEqual((b.state.pnl, b.state.position, b.state.basis), (-499_000, 0, 0))
        self.assertEqual(b.state.cash, 4_000_000 + 20_750_000 - 249_000)


class LossBudget(unittest.TestCase):
    def test_buys_stop_only_once_the_weeks_loss_exceeds_the_cap(self) -> None:
        at_cap = book(weekly_loss_cap_usd=Decimal("0.4688"))       # one round trip loses exactly 468,800
        below = book(weekly_loss_cap_usd=Decimal("0.468799"))
        for b in (at_cap, below):
            self.assertIsNone(b.gate(MON, 84_000))
            buy_then_confirm(b, MON, 84_000)
            sell_then_confirm(b, MON + 60 * NS, 84_100)
        self.assertIsNone(at_cap.gate(MON + 120 * NS, 84_000))
        self.assertEqual(below.gate(MON + 120 * NS, 84_000), "loss_budget")

    def test_the_buy_fee_counts_at_the_buy(self) -> None:
        b = book(weekly_loss_cap_usd=Decimal("0.24"))  # the buy alone realizes -0.2415
        buy_then_confirm(b, MON, 84_000)
        self.assertEqual(b.gate(MON + 60 * NS, 84_000), "loss_budget")

    def test_a_new_iso_week_starts_a_new_budget(self) -> None:
        b = book(weekly_loss_cap_usd=Decimal("0.1"))
        buy_then_confirm(b, NEXT_MON - 120 * NS, 84_000)
        sell_then_confirm(b, NEXT_MON - 60 * NS, 84_000)
        self.assertEqual(b.gate(NEXT_MON - 1, 84_000), "loss_budget")
        self.assertIsNone(b.gate(NEXT_MON, 84_000))
        self.assertEqual((b.state.week.key, b.state.week.pnl), ("2026-W41", 0))
        self.assertEqual(b.state.pnl, -(241_500 + 252_000))  # the lifetime total is kept

    def test_a_flat_book_that_cannot_pay_a_clip_stops_buys(self) -> None:
        # A clip at 84,000 costs 21,000,000 + 241,500 micros.
        self.assertEqual(book(cash_usd=Decimal("21.2414")).gate(MON, 84_000), "insufficient_balance")
        self.assertIsNone(book(cash_usd=Decimal("21.2415")).gate(MON, 84_000))
        long = book(cash_usd=Decimal("21.2415"))
        buy_then_confirm(long, MON, 84_000)
        self.assertEqual(long.state.cash, 0)
        self.assertIsNone(long.gate(MON + 60 * NS, 84_000))  # long: a buy is the kernel's to refuse

    def test_withheld_buys_are_counted_by_reason_only_when_flat(self) -> None:
        b = book()
        b.observe(MON, 84_000, action="enter_long", authorization="withheld", resync_position=None,
                  stop="loss_budget")
        b.observe(MON + NS, 84_000, action="enter_long", authorization="withheld", resync_position=None,
                  stop="insufficient_balance")
        b.observe(MON + 2 * NS, 84_000, action="enter_long", authorization="withheld", resync_position=25,
                  stop="loss_budget")  # long now: the kernel would have refused it anyway
        self.assertEqual(b.state.withheld, {"loss_budget": 1, "insufficient_balance": 1})


class Metrics(unittest.TestCase):
    def test_render_after_a_round_trip(self) -> None:
        b = book(expected_week_volume_usd=(Decimal(500), Decimal(900)),
                 expected_week_loss_usd=(Decimal(6), Decimal("11.5")))
        buy_then_confirm(b, MON, 84_000)
        sell_then_confirm(b, MON + 60 * NS, 84_100)
        b.gate(MON + 120 * NS, 84_000)
        text = b.render()
        for line in ("velocita_paper_volume_usd_total 42.025000", 'velocita_paper_fills_total{side="buy"} 1',
                     'velocita_paper_fills_total{side="sell"} 1', "velocita_paper_realized_pnl_usd -0.468800",
                     "velocita_paper_cash_usd 24.531200", "velocita_paper_week_volume_usd 42.025000",
                     "velocita_paper_week_fees_usd 0.493800", "velocita_paper_week_pnl_usd -0.468800",
                     "velocita_paper_week_loss_usd 0.468800", "velocita_paper_week_fills 2",
                     "velocita_paper_weekly_loss_cap_usd 12.000000",
                     'velocita_paper_buys_stopped{reason="loss_budget"} 0',
                     'velocita_paper_buys_withheld_total{reason="insufficient_balance"} 0',
                     'velocita_paper_expected_week_volume_usd{bound="low"} 500.000000',
                     'velocita_paper_expected_week_loss_usd{bound="high"} 11.500000'):
            self.assertIn(line + "\n", text)

    def test_no_expected_series_unless_configured(self) -> None:
        self.assertNotIn("expected", book().render())


class Persistence(unittest.TestCase):
    def test_a_restarted_edge_continues_the_same_book(self) -> None:
        with TemporaryDirectory() as d:
            db = Path(d) / "ledger.db"
            b = book(db=db)
            buy_then_confirm(b, MON, 84_000)
            b.observe(MON + 60 * NS, 84_100, action="exit", authorization="applied", resync_position=None, stop=None)
            b.close()
            again = book(db=db)
            self.assertEqual((again.state.position, again.state.cash, again.state.pending),
                             (25, 25_000_000 - 21_241_500, ["sell", 84_100, 25]))
            again.observe(MON + 90 * NS, 84_100, action="enter_long", authorization="stale_risk_state",
                          resync_position=0, stop=None)
            self.assertEqual(again.state.pnl, -468_800)


PAPER_SUBMITTER = textwrap.dedent('''
    import sys
    print("READY generation=1 first_sequence=1 mode=paper caller_id=21", flush=True)
    seq = 0
    log = open(sys.argv[1], "w")
    for line in sys.stdin:
        if not line.startswith("TICK"):
            continue
        log.write(line)
        log.flush()
        seq += 1
        f = dict(p.split("=", 1) for p in line.split()[1:])
        # Rule 0 of trend (enter_long) every tick: tick 1 is authorized, tick 2 reports the fill.
        auth, extra = {1: ("applied", ""), 2: ("stale_risk_state", " resync=1 position_lots=25")}.get(seq, ("x", ""))
        if f.get("exits_only") == "1":
            auth, extra = "withheld", ""
        print(f"DECISION id={f['id']} source_seq={seq} decision={seq} code=0 decision_code=1 rule=0 "
              f"proposed=2 effective=2 from=1 resolution=1 t_read_ns={int(f['receive_ns']) + 1000} "
              f"t_source_ns={int(f['receive_ns']) + 2000000} t_decision_ns={int(f['receive_ns']) + 5000000} "
              f"risk_version=1 position_version=1 authorization={auth}{extra}", flush=True)
''')


class EdgeWithThePaperBook(unittest.TestCase):
    def run_edge(self, submitter: str, prices: list[str], paper_book: PaperBook, d: str) -> tuple[list[str], str]:
        fake, log = Path(d) / "fake.py", Path(d) / "ticks.log"
        fake.write_text(submitter)
        messages = [reference(p, "2026-07-20T12:00:%02dZ" % i) for i, p in enumerate(prices)] + [None]

        class Socket:
            def open(self): ...
            def subscribe(self, topics): ...
            def recv_text(self): return messages.pop(0)
            def close(self): ...

        textfile = Path(d) / "shadow.prom"
        source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("1"))
        edge.run_once(lambda: Socket(), [sys.executable, str(fake), str(log)], source, textfile=str(textfile),
                      shadow_recorded_code=17, clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__,
                      paper_book=paper_book)
        return log.read_text().splitlines(), textfile.read_text()

    def test_the_stop_reaches_the_tick_line_and_withheld_buys_are_counted(self) -> None:
        # Cash for exactly one clip at 300: 300 x 25 x 10 = 75,000 + fee 863 (862.5 up) = 75,863 micros.
        b = book(cash_usd=Decimal("0.075863"))
        with TemporaryDirectory() as d:
            ticks, text = self.run_edge(PAPER_SUBMITTER, ["300", "300", "300"], b, d)
        # Tick 1 buys (applied), tick 2 confirms it (cash now 0, but long: no stop), tick 3 still no stop.
        self.assertTrue(all("exits_only" not in t for t in ticks), ticks)
        self.assertIn("velocita_paper_volume_usd_total 0.075000\n", text)
        self.assertIn('velocita_paper_fills_total{side="buy"} 1\n', text)
        b2 = book(cash_usd=Decimal("0.075862"))  # one micro short: flat and cannot pay -> exits only
        with TemporaryDirectory() as d:
            ticks, text = self.run_edge(PAPER_SUBMITTER, ["300", "300"], b2, d)
        self.assertTrue(all(t.endswith(" exits_only=1") for t in ticks), ticks)
        self.assertIn('velocita_paper_buys_withheld_total{reason="insufficient_balance"} 2\n', text)
        self.assertIn('velocita_paper_buys_stopped{reason="insufficient_balance"} 1\n', text)
        self.assertNotIn('code="withheld"', text)  # never sent, so not a kernel verdict

    def test_shadow_mode_never_consults_the_book(self) -> None:
        shadow = PAPER_SUBMITTER.replace("mode=paper ", "")
        b = book(cash_usd=Decimal(0))
        with TemporaryDirectory() as d:
            ticks, text = self.run_edge(shadow, ["300"], b, d)
        self.assertTrue(all("exits_only" not in t for t in ticks))
        self.assertNotIn("velocita_paper_volume_usd_total", text)


if __name__ == "__main__":
    unittest.main()
