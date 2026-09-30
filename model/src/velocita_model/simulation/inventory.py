"""Simulated inventory, capital reserve, and skew.

Everything here describes a hypothetical portfolio. The names say `simulated`
because a report that reads "position: 3.2" invites exactly one dangerous
misreading, and the cost of the longer name is zero.

The reserve is the important idea. With 20 USDC and unmeasured costs, the
default holds back half the account entirely: the simulator is not permitted to
model deploying it. That is not conservatism for its own sake - it is the only
honest starting point when the fee, gas, and slippage terms are still marked
unverified.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal

from ..contracts.enums import Side


@dataclass(frozen=True, slots=True)
class InventoryLimits:
    account_equity_quote: Decimal
    reserve_fraction: Decimal
    max_inventory_quote: Decimal

    @property
    def reserved_quote(self) -> Decimal:
        return self.account_equity_quote * self.reserve_fraction

    @property
    def deployable_quote(self) -> Decimal:
        return self.account_equity_quote - self.reserved_quote

    def __post_init__(self) -> None:
        if not (Decimal(0) <= self.reserve_fraction <= Decimal(1)):
            raise ValueError("reserve_fraction must be in [0, 1]")
        if self.max_inventory_quote > self.deployable_quote:
            raise ValueError(
                f"max_inventory_quote {self.max_inventory_quote} exceeds deployable "
                f"{self.deployable_quote}; the reserve would be spent"
            )


@dataclass(slots=True)
class SimulatedInventory:
    """Mutable simulated book. Not a position. Never a position."""

    limits: InventoryLimits
    simulated_base: Decimal = Decimal(0)
    simulated_quote: Decimal = Decimal(0)
    simulated_realized_pnl_quote: Decimal = Decimal(0)
    fills_applied: int = 0
    _history: list[tuple[int, Decimal]] = field(default_factory=list, repr=False)

    def __post_init__(self) -> None:
        if self.simulated_quote == 0:
            self.simulated_quote = self.limits.deployable_quote

    def simulated_notional(self, fair_value: Decimal) -> Decimal:
        """Signed inventory marked at fair value, in quote units."""
        return self.simulated_base * fair_value

    def simulated_equity(self, fair_value: Decimal) -> Decimal:
        """Deployable equity only. The reserve is excluded because the simulator
        is not modelling access to it."""
        return self.simulated_quote + self.simulated_notional(fair_value)

    def skew(self, fair_value: Decimal) -> Decimal:
        """Inventory as a fraction of the cap, clamped to [-1, 1]."""
        cap = self.limits.max_inventory_quote
        if cap <= 0:
            return Decimal(0)
        raw = self.simulated_notional(fair_value) / cap
        return max(Decimal(-1), min(Decimal(1), raw))

    def at_limit(self, fair_value: Decimal, side: Side) -> bool:
        """True when quoting `side` would increase inventory beyond the cap.

        The check is directional: being maximally long stops the bid, not the
        ask. Stopping both would leave the simulator unable to model reducing an
        unwanted position, which is the situation that most needs modelling.
        """
        notional = self.simulated_notional(fair_value)
        cap = self.limits.max_inventory_quote
        if side == Side.buy:
            return notional >= cap
        return notional <= -cap

    def apply_simulated_fill(
        self, side: Side, price: Decimal, size_base: Decimal, fee_quote: Decimal = Decimal(0)
    ) -> None:
        """Apply a hypothetical fill. Fees are charged as modelled, never
        omitted for a cleaner-looking curve."""
        if size_base <= 0:
            raise ValueError("simulated fill size must be positive")
        notional = price * size_base
        if side == Side.buy:
            self.simulated_base += size_base
            self.simulated_quote -= notional
        else:
            self.simulated_base -= size_base
            self.simulated_quote += notional
        self.simulated_quote -= fee_quote
        self.simulated_realized_pnl_quote -= fee_quote
        self.fills_applied += 1

    def record_mark(self, at_ns: int, fair_value: Decimal) -> None:
        self._history.append((at_ns, self.simulated_equity(fair_value)))

    def max_simulated_drawdown(self) -> Decimal:
        """Peak-to-trough of simulated deployable equity."""
        peak: Decimal | None = None
        worst = Decimal(0)
        for _, equity in self._history:
            peak = equity if peak is None else max(peak, equity)
            worst = max(worst, peak - equity)
        return worst

    def equity_history(self) -> tuple[tuple[int, Decimal], ...]:
        return tuple(self._history)
