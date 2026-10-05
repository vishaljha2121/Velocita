"""Paper book: the edge's record of the paper fills the kernel confirmed, the
weekly loss budget, and the buy stop that enforces it.

* A fill is booked only when the kernel's position confirms it: an
  authorization the kernel `applied` becomes the pending order; the next
  stale_risk_state refusal reports the account's position, and a move of the
  pending order's size and direction books it at the pending order's price
  (the paper venue fills fill-or-kill at the limit, ADR 0018).
* Fees are the paper venue's: ceil(notional * bps / 10,000) micros, charged at
  the fill and taken from cash at once (book ch52). Integer micros throughout.
* Realized P&L is booked at fill events: a buy realizes -fee, a sell realizes
  (sell notional - cost basis of the lots sold) - fee. A round trip's sum is
  proceeds minus everything paid.
* Weekly: the ISO week (UTC) of the tick that confirms the fill. The loss is
  max(0, -realized P&L of the week). While it exceeds the cap, or while the
  book is flat and cash cannot pay a clip plus its fee, buys are stopped
  (the TICK line's exits_only=1); exits always go.
"""

from __future__ import annotations

import json
import sqlite3
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

MICROS = 1_000_000
NS = 1_000_000_000
REASONS = ("loss_budget", "insufficient_balance")


def iso_week(event_ns: int) -> str:
    year, week, _ = datetime.fromtimestamp(event_ns // NS, timezone.utc).isocalendar()
    return f"{year}-W{week:02d}"


def fee_micros(notional: int, bps: int) -> int:
    return -(-notional * bps // 10_000)


def usd(micros: int) -> str:
    return f"{Decimal(micros) / MICROS:.6f}"


@dataclass
class Week:
    key: str = ""
    volume: int = 0
    fees: int = 0
    pnl: int = 0
    buys: int = 0
    sells: int = 0

    def loss(self) -> int:
        return max(0, -self.pnl)


@dataclass
class State:
    position: int = 0
    basis: int = 0
    cash: int = 0
    pending: list | None = None
    volume: int = 0
    fees: int = 0
    pnl: int = 0
    buys: int = 0
    sells: int = 0
    unattributed: int = 0
    withheld: dict = field(default_factory=lambda: {r: 0 for r in REASONS})
    week: Week = field(default_factory=Week)


class PaperBook:
    def __init__(self, *, clip_lots: int, quote_micros_per_tick_lot: int, buy_bps: int, sell_bps: int,
                 cash_usd: Decimal, weekly_loss_cap_usd: Decimal, db: Path | str | None = None,
                 expected_week_volume_usd: tuple[Decimal, Decimal] | None = None,
                 expected_week_loss_usd: tuple[Decimal, Decimal] | None = None) -> None:
        if clip_lots <= 0 or quote_micros_per_tick_lot <= 0:
            raise ValueError("clip and quote micros per tick-lot must be positive")
        self.clip = clip_lots
        self.unit = quote_micros_per_tick_lot
        self.buy_bps, self.sell_bps = buy_bps, sell_bps
        self.cap = int(weekly_loss_cap_usd * MICROS)
        self.expected = {"volume": expected_week_volume_usd, "loss": expected_week_loss_usd}
        self.state = State(cash=int(cash_usd * MICROS))
        self.stop: str | None = None
        self._db = sqlite3.connect(db) if db is not None else None
        if self._db is not None:
            self._db.execute("create table if not exists paper_book (id integer primary key check (id = 1), state text)")
            row = self._db.execute("select state from paper_book where id = 1").fetchone()
            if row:
                loaded = json.loads(row[0])
                loaded["week"] = Week(**loaded["week"])
                self.state = State(**loaded)


    def _roll(self, event_ns: int) -> None:
        key = iso_week(event_ns)
        if self.state.week.key != key:
            self.state.week = Week(key)

    def gate(self, event_ns: int, price_ticks: int) -> str | None:
        self._roll(event_ns)
        s = self.state
        notional = price_ticks * self.clip * self.unit
        if s.week.loss() > self.cap:
            self.stop = "loss_budget"
        elif s.position == 0 and s.cash < notional + fee_micros(notional, self.buy_bps):
            self.stop = "insufficient_balance"
        else:
            self.stop = None
        return self.stop


    def observe(self, event_ns: int, price_ticks: int, *, action: str | None, authorization: str,
                resync_position: int | None, stop: str | None) -> None:
        self._roll(event_ns)
        s = self.state
        if resync_position is not None:
            self._confirm(resync_position, price_ticks)
        if authorization == "applied" and action in ("enter_long", "exit"):
            s.pending = ["buy" if action == "enter_long" else "sell", price_ticks, self.clip]
        if authorization == "withheld" and stop in REASONS and s.position == 0:
            s.withheld[stop] += 1
        self._save()

    def _confirm(self, position: int, price_ticks: int) -> None:
        s = self.state
        delta = position - s.position
        if delta == 0:
            return
        side = "buy" if delta > 0 else "sell"
        if s.pending and s.pending[0] == side and abs(delta) == s.pending[2]:
            self._fill(side, s.pending[1], abs(delta))
            s.pending = None
        else:
            s.unattributed += 1
            s.cash -= delta * price_ticks * self.unit
            s.position = position
            s.basis = position * price_ticks * self.unit

    def _fill(self, side: str, price_ticks: int, lots: int) -> None:
        s, w = self.state, self.state.week
        notional = price_ticks * lots * self.unit
        fee = fee_micros(notional, self.buy_bps if side == "buy" else self.sell_bps)
        if side == "buy":
            pnl = -fee
            s.cash -= notional + fee
            s.basis += notional
            s.position += lots
            s.buys += 1
            w.buys += 1
        else:
            cost = s.basis if lots == s.position else s.basis * lots // s.position
            pnl = notional - cost - fee
            s.cash += notional - fee
            s.basis -= cost
            s.position -= lots
            s.sells += 1
            w.sells += 1
        s.volume += notional
        s.fees += fee
        s.pnl += pnl
        w.volume += notional
        w.fees += fee
        w.pnl += pnl

    def _save(self) -> None:
        if self._db is None:
            return
        with self._db:
            self._db.execute("insert or replace into paper_book values (1, ?)", (json.dumps(asdict(self.state)),))

    def close(self) -> None:
        if self._db is not None:
            self._db.close()
            self._db = None


    def render(self) -> str:
        s, w = self.state, self.state.week
        out = ["# TYPE velocita_paper_volume_usd_total counter", f"velocita_paper_volume_usd_total {usd(s.volume)}",
               "# TYPE velocita_paper_fills_total counter",
               f'velocita_paper_fills_total{{side="buy"}} {s.buys}', f'velocita_paper_fills_total{{side="sell"}} {s.sells}',
               "# TYPE velocita_paper_realized_pnl_usd gauge", f"velocita_paper_realized_pnl_usd {usd(s.pnl)}",
               "# TYPE velocita_paper_cash_usd gauge", f"velocita_paper_cash_usd {usd(s.cash)}",
               "# TYPE velocita_paper_book_position_lots gauge", f"velocita_paper_book_position_lots {s.position}",
               "# TYPE velocita_paper_week_volume_usd gauge", f"velocita_paper_week_volume_usd {usd(w.volume)}",
               "# TYPE velocita_paper_week_fees_usd gauge", f"velocita_paper_week_fees_usd {usd(w.fees)}",
               "# TYPE velocita_paper_week_pnl_usd gauge", f"velocita_paper_week_pnl_usd {usd(w.pnl)}",
               "# TYPE velocita_paper_week_loss_usd gauge", f"velocita_paper_week_loss_usd {usd(w.loss())}",
               "# TYPE velocita_paper_week_fills gauge", f"velocita_paper_week_fills {w.buys + w.sells}",
               "# TYPE velocita_paper_weekly_loss_cap_usd gauge", f"velocita_paper_weekly_loss_cap_usd {usd(self.cap)}",
               "# TYPE velocita_paper_buys_stopped gauge"]
        out += [f'velocita_paper_buys_stopped{{reason="{r}"}} {int(self.stop == r)}' for r in REASONS]
        out.append("# TYPE velocita_paper_buys_withheld_total counter")
        out += [f'velocita_paper_buys_withheld_total{{reason="{r}"}} {s.withheld[r]}' for r in REASONS]
        out += ["# TYPE velocita_paper_unattributed_position_changes_total counter",
                f"velocita_paper_unattributed_position_changes_total {s.unattributed}"]
        for name, interval in self.expected.items():
            if interval is not None:
                out.append(f"# TYPE velocita_paper_expected_week_{name}_usd gauge")
                out += [f'velocita_paper_expected_week_{name}_usd{{bound="{b}"}} {usd(int(v * MICROS))}'
                        for b, v in zip(("low", "high"), interval)]
        return "\n".join(out) + "\n"
