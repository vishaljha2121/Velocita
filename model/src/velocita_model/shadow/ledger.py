"""Shadow ledger: what the cluster's committed shadow decisions would have done
to a book, filled at the True Markets reference price with measured costs.

Nothing here trades. Two books run side by side:
  tick   acts on every committed decision (what the live wiring does now);
  daily  acts once per UTC day on the last decision before midnight, like the
         backtest, whose signal at a day's close sets the next day's position.
The daily book is the one that counts for promotion (docs/STRATEGY_PROMOTION_GATE.md).
Equity is the liquidation value: what selling now would return after the sell cost.
"""

from __future__ import annotations

import sqlite3
from dataclasses import dataclass, field
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

from ..research.rules import Costs

_BP = Decimal(10_000)
_FILLS_SHOWN = 10


@dataclass(frozen=True)
class Fill:
    time_ns: int
    side: str           # "buy" or "sell"
    price: Decimal      # reference price the fill is costed against
    qty_btc: Decimal
    cash_usd: Decimal   # USDC paid (buy) or received (sell)
    fee_usd: Decimal    # the measured cost against the reference price


@dataclass
class Book:
    timing: str
    costs: Costs
    cash: Decimal
    btc: Decimal = Decimal(0)
    entry_cash: Decimal = Decimal(0)
    fees: Decimal = Decimal(0)
    realized_pnl: Decimal = Decimal(0)
    wins: int = 0
    losses: int = 0
    trades: int = 0
    fills: list[Fill] = field(default_factory=list)

    def _buy_factor(self) -> Decimal:
        return 1 + Decimal(str(self.costs.buy_bps)) / _BP

    def _sell_factor(self) -> Decimal:
        return 1 - Decimal(str(self.costs.sell_bps)) / _BP

    def act(self, time_ns: int, price: Decimal, *, want_long: bool) -> Fill | None:
        """Move to the wanted position; None when already there."""
        if want_long and self.btc == 0 and self.cash > 0:
            qty = self.cash / (price * self._buy_factor())
            fill = Fill(time_ns, "buy", price, qty, self.cash, self.cash - qty * price)
            self.entry_cash, self.cash, self.btc = self.cash, Decimal(0), qty
        elif not want_long and self.btc > 0:
            gross = self.btc * price
            proceeds = gross * self._sell_factor()
            fill = Fill(time_ns, "sell", price, self.btc, proceeds, gross - proceeds)
            pnl = proceeds - self.entry_cash
            self.realized_pnl += pnl
            if pnl > 0:
                self.wins += 1
            else:
                self.losses += 1
            self.cash, self.btc, self.entry_cash = self.cash + proceeds, Decimal(0), Decimal(0)
        else:
            return None
        self.fees += fill.fee_usd
        self.trades += 1
        self.fills.append(fill)
        return fill

    def equity(self, price: Decimal) -> Decimal:
        return self.cash + self.btc * price * self._sell_factor()

    def unrealized_pnl(self, price: Decimal) -> Decimal:
        return self.equity(price) - self.cash - self.entry_cash if self.btc > 0 else Decimal(0)


_WANT = {"enter_long": True, "exit": False}


def _day(time_ns: int):
    return datetime.fromtimestamp(time_ns // 1_000_000_000, timezone.utc).date()


class Ledger:
    """Both books, a buy-and-hold benchmark, and (optionally) SQLite persistence
    so a restarted edge continues the same book."""

    def __init__(self, costs: Costs, book_usd: Decimal, *, db: Path | str | None = None) -> None:
        self.costs = costs
        self.book_usd = book_usd
        self.books = {timing: Book(timing, costs, book_usd) for timing in ("tick", "daily")}
        self.pending: tuple[int, Decimal, str] | None = None  # the daily book's latest decision today
        self.hold_btc: Decimal | None = None
        self._db = sqlite3.connect(db) if db is not None else None
        if self._db is not None:
            self._create()
            self._load()

    # -- decisions ----------------------------------------------------------------

    def observe(self, time_ns: int, price: Decimal, action: str) -> None:
        """One committed, proposed shadow decision: ACTION is the matched rule's."""
        if self.hold_btc is None:
            self.hold_btc = self.book_usd / (price * (1 + Decimal(str(self.costs.buy_bps)) / _BP))
        fills: list[tuple[str, Fill]] = []
        want = _WANT.get(action)
        if want is not None:
            fill = self.books["tick"].act(time_ns, price, want_long=want)
            if fill:
                fills.append(("tick", fill))
        if self.pending is not None and _day(time_ns) > _day(self.pending[0]):
            closing_time, closing_price, closing_action = self.pending
            closing = _WANT.get(closing_action)
            if closing is not None:
                fill = self.books["daily"].act(closing_time, closing_price, want_long=closing)
                if fill:
                    fills.append(("daily", fill))
        self.pending = (time_ns, price, action)
        self._save(fills)

    def hold_equity(self, price: Decimal) -> Decimal:
        if self.hold_btc is None:
            return self.book_usd
        return self.hold_btc * price * (1 - Decimal(str(self.costs.sell_bps)) / _BP)

    # -- metrics ------------------------------------------------------------------

    def render(self, price: Decimal) -> str:
        usd = lambda v: f"{v.quantize(Decimal('0.01'))}"
        out = []
        series = [
            ("velocita_shadow_book_equity_usd", "gauge", lambda b: usd(b.equity(price))),
            ("velocita_shadow_book_cash_usd", "gauge", lambda b: usd(b.cash)),
            ("velocita_shadow_book_btc", "gauge", lambda b: f"{b.btc.quantize(Decimal('0.00000001'))}"),
            ("velocita_shadow_book_position", "gauge", lambda b: "1" if b.btc > 0 else "0"),
            ("velocita_shadow_book_realized_pnl_usd", "gauge", lambda b: usd(b.realized_pnl)),
            ("velocita_shadow_book_unrealized_pnl_usd", "gauge", lambda b: usd(b.unrealized_pnl(price))),
            ("velocita_shadow_book_fees_usd", "gauge", lambda b: usd(b.fees)),
            ("velocita_shadow_book_trades_total", "counter", lambda b: str(b.trades)),
        ]
        for name, kind, value in series:
            out.append(f"# TYPE {name} {kind}")
            out += [f'{name}{{timing="{t}"}} {value(b)}' for t, b in self.books.items()]
        out.append("# TYPE velocita_shadow_book_round_trips_total counter")
        for t, b in self.books.items():
            out += [f'velocita_shadow_book_round_trips_total{{timing="{t}",result="win"}} {b.wins}',
                    f'velocita_shadow_book_round_trips_total{{timing="{t}",result="loss"}} {b.losses}']
        out += ["# TYPE velocita_shadow_hold_equity_usd gauge", f"velocita_shadow_hold_equity_usd {usd(self.hold_equity(price))}"]
        # The latest fills per book (n=1 newest), valued at their time in seconds:
        # a bounded order history for a Grafana table.
        out.append("# TYPE velocita_shadow_book_fill gauge")
        for t, b in self.books.items():
            for n, fill in enumerate(reversed(b.fills[-_FILLS_SHOWN:]), start=1):
                out.append(f'velocita_shadow_book_fill{{timing="{t}",n="{n}",side="{fill.side}",'
                           f'price="{format(fill.price.normalize(), "f")}"}} {fill.time_ns // 1_000_000_000}')
        return "\n".join(out) + "\n"

    # -- persistence --------------------------------------------------------------

    def _create(self) -> None:
        self._db.executescript("""
            create table if not exists ledger_books (timing text primary key, cash text, btc text,
                entry_cash text, fees text, realized text, wins integer, losses integer, trades integer);
            create table if not exists ledger_fills (id integer primary key autoincrement, timing text,
                time_ns integer, side text, price text, qty text, cash text, fee text);
            create table if not exists ledger_meta (key text primary key, value text);
        """)

    def _load(self) -> None:
        for timing, cash, btc, entry, fees, realized, wins, losses, trades in self._db.execute(
                "select timing, cash, btc, entry_cash, fees, realized, wins, losses, trades from ledger_books"):
            b = self.books[timing]
            b.cash, b.btc, b.entry_cash = Decimal(cash), Decimal(btc), Decimal(entry)
            b.fees, b.realized_pnl, b.wins, b.losses, b.trades = Decimal(fees), Decimal(realized), wins, losses, trades
            b.fills = [Fill(t, side, Decimal(p), Decimal(q), Decimal(c), Decimal(f)) for t, side, p, q, c, f in
                       reversed(self._db.execute(
                           "select time_ns, side, price, qty, cash, fee from ledger_fills where timing = ? "
                           "order by id desc limit ?", (timing, _FILLS_SHOWN)).fetchall())]
        meta = dict(self._db.execute("select key, value from ledger_meta"))
        if "hold_btc" in meta:
            self.hold_btc = Decimal(meta["hold_btc"])
        if "pending" in meta:
            t, p, action = meta["pending"].split(" ")
            self.pending = (int(t), Decimal(p), action)

    def _save(self, fills: list[tuple[str, Fill]]) -> None:
        if self._db is None:
            return
        with self._db:
            for timing, f in fills:
                self._db.execute("insert into ledger_fills (timing, time_ns, side, price, qty, cash, fee) "
                                 "values (?, ?, ?, ?, ?, ?, ?)",
                                 (timing, f.time_ns, f.side, str(f.price), str(f.qty_btc), str(f.cash_usd),
                                  str(f.fee_usd)))
            for b in self.books.values():
                self._db.execute("insert or replace into ledger_books values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                                 (b.timing, str(b.cash), str(b.btc), str(b.entry_cash), str(b.fees),
                                  str(b.realized_pnl), b.wins, b.losses, b.trades))
            self._db.execute("insert or replace into ledger_meta values ('hold_btc', ?)", (str(self.hold_btc),))
            if self.pending is not None:
                t, p, action = self.pending
                self._db.execute("insert or replace into ledger_meta values ('pending', ?)", (f"{t} {p} {action}",))

    def close(self) -> None:
        if self._db is not None:
            self._db.close()
            self._db = None
