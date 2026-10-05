"""Shadow ledger: what the committed shadow decisions would have done to a book.

Oracles are worked out by hand in dollars (100 bp per side on a $1,000 book):
    buy at 100:  1000 / (100 * 1.01)        = 9.900990099 BTC, fee 1000 - 990.10 = 9.90
    sell at 110: 9.900990099 * 110 * 0.99   = 1078.22 USDC,    fee 1089.11 - 1078.22 = 10.89
Equity is the liquidation value: what selling now would return after the sell cost.
"""

from __future__ import annotations

import sqlite3
import tempfile
import unittest
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

from velocita_model.research.rules import Costs
from velocita_model.shadow import ledger

NS = 1_000_000_000
COSTS = Costs(buy_bps=100, sell_bps=100)


def at(text: str) -> int:
    return int(datetime.fromisoformat(text).replace(tzinfo=timezone.utc).timestamp()) * NS


def cents(value: Decimal) -> Decimal:
    return value.quantize(Decimal("0.01"))


class TickBook(unittest.TestCase):
    def test_a_round_trip_in_dollars(self) -> None:
        book = ledger.Book("tick", COSTS, Decimal(1000))
        buy = book.act(at("2026-09-29T10:00:00"), Decimal(100), want_long=True)
        self.assertEqual((buy.side, cents(buy.fee_usd)), ("buy", Decimal("9.90")))
        self.assertEqual(buy.qty_btc.quantize(Decimal("0.000001")), Decimal("9.900990"))
        self.assertEqual(book.cash, Decimal(0))
        sell = book.act(at("2026-09-29T11:00:00"), Decimal(110), want_long=False)
        self.assertEqual((sell.side, cents(sell.cash_usd), cents(sell.fee_usd)),
                         ("sell", Decimal("1078.22"), Decimal("10.89")))
        self.assertEqual(cents(book.cash), Decimal("1078.22"))
        self.assertEqual(cents(book.realized_pnl), Decimal("78.22"))
        self.assertEqual(cents(book.fees), Decimal("20.79"))
        self.assertEqual((book.wins, book.losses, book.trades), (1, 0, 2))

    def test_no_order_without_a_position_change(self) -> None:
        book = ledger.Book("tick", COSTS, Decimal(1000))
        self.assertIsNone(book.act(0, Decimal(100), want_long=False))
        self.assertIsNotNone(book.act(1, Decimal(100), want_long=True))
        self.assertIsNone(book.act(2, Decimal(105), want_long=True))
        self.assertEqual(book.trades, 1)

    def test_a_losing_round_trip_and_marking_to_market(self) -> None:
        book = ledger.Book("tick", COSTS, Decimal(1000))
        book.act(0, Decimal(100), want_long=True)
        self.assertEqual(cents(book.equity(Decimal(90))), Decimal("882.18"))
        self.assertEqual(cents(book.unrealized_pnl(Decimal(90))), Decimal("-117.82"))
        book.act(1, Decimal(90), want_long=False)
        self.assertEqual(cents(book.cash), Decimal("882.18"))
        self.assertEqual((book.wins, book.losses), (0, 1))


    def test_break_even_is_not_a_win(self) -> None:
        book = ledger.Book("tick", Costs(buy_bps=0, sell_bps=0), Decimal(1000))
        book.act(0, Decimal(100), want_long=True)
        book.act(1, Decimal(100), want_long=False)
        self.assertEqual((book.wins, book.losses, book.realized_pnl), (0, 1, Decimal(0)))


class DailyBook(unittest.TestCase):
    def test_only_the_last_decision_of_each_utc_day_acts(self) -> None:
        l = ledger.Ledger(COSTS, Decimal(1000))
        l.observe(at("2026-09-29T09:00:00"), Decimal(100), "enter_long")
        l.observe(at("2026-09-29T15:00:00"), Decimal(101), "exit")
        l.observe(at("2026-09-29T23:59:00"), Decimal(102), "enter_long")
        self.assertEqual(l.books["daily"].trades, 0, "the daily book acted before the day closed")
        self.assertEqual(l.books["tick"].trades, 3, "the tick book acts on every change")
        l.observe(at("2026-09-30T00:00:30"), Decimal(103), "enter_long")
        daily = l.books["daily"]
        self.assertEqual(daily.trades, 1)
        self.assertEqual(daily.fills[-1].price, Decimal(102), "the daily fill is not at the day's close")

    def test_hold_changes_nothing(self) -> None:
        l = ledger.Ledger(COSTS, Decimal(1000))
        l.observe(0, Decimal(100), "hold")
        self.assertEqual(l.books["tick"].trades, 0)


class Benchmark(unittest.TestCase):
    def test_buy_and_hold_from_the_first_price(self) -> None:
        l = ledger.Ledger(COSTS, Decimal(1000))
        l.observe(0, Decimal(100), "exit")
        self.assertEqual(cents(l.hold_equity(Decimal(110))), Decimal("1078.22"))
        l.observe(1, Decimal(120), "enter_long")
        self.assertEqual(cents(l.hold_equity(Decimal(110))), Decimal("1078.22"))


class Persistence(unittest.TestCase):
    def test_a_restart_continues_the_same_book(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            db = Path(d) / "ledger.db"
            first = ledger.Ledger(COSTS, Decimal(1000), db=db)
            first.observe(at("2026-09-29T09:00:00"), Decimal(100), "enter_long")
            first.observe(at("2026-09-29T23:00:00"), Decimal(100), "enter_long")
            first.close()
            again = ledger.Ledger(COSTS, Decimal(1000), db=db)
            self.assertEqual(again.books["tick"].btc, first.books["tick"].btc)
            self.assertEqual(again.books["tick"].trades, 1)
            self.assertEqual(again.hold_equity(Decimal(100)), first.hold_equity(Decimal(100)))
            again.observe(at("2026-09-30T00:01:00"), Decimal(105), "exit")
            self.assertEqual(again.books["daily"].trades, 1)
            again.close()
            with sqlite3.connect(db) as c:
                self.assertEqual(c.execute("select count(*) from ledger_fills").fetchone()[0], 3)


class Metrics(unittest.TestCase):
    def test_render_reports_both_books_and_the_benchmark(self) -> None:
        l = ledger.Ledger(COSTS, Decimal(1000))
        l.observe(0, Decimal(100), "enter_long")
        text = l.render(Decimal(110))
        self.assertIn('velocita_shadow_book_equity_usd{timing="tick"} 1078.22\n', text)
        self.assertIn('velocita_shadow_book_position{timing="tick"} 1\n', text)
        self.assertIn('velocita_shadow_book_position{timing="daily"} 0\n', text)
        self.assertIn('velocita_shadow_book_fees_usd{timing="tick"} 9.90\n', text)
        self.assertIn('velocita_shadow_book_trades_total{timing="tick"} 1\n', text)
        self.assertIn("velocita_shadow_hold_equity_usd 1078.22\n", text)
        self.assertIn('velocita_shadow_book_fill{timing="tick",n="1",side="buy",price="100"} ', text)


if __name__ == "__main__":
    unittest.main()
