"""Live swap orders from the paper stochastic one-second rule; pure, with no network, keys or cluster access."""
from dataclasses import dataclass
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR

from .stochastic_second import Indicators, Signals, candle

CLIP_LAMPORTS = 120_000_000
WARMUP_BARS = 1200
REENTRY_SECONDS = 60
MAX_ORDERS = 240
DRAWDOWN_ATOMS = 10_000_000
SIGN_LIFETIME_MS = 20_000
BUY_FLOOR = Decimal('0.92')
SELL_FLOOR = Decimal('0.94')
SELL_FEE_MARGIN = Decimal('1.06')


@dataclass(frozen=True)
class Account:
    base_atoms: int
    quote_atoms: int
    realized_pnl_atoms: int
    orders: int
    pending: bool


@dataclass(frozen=True)
class Order:
    side: str
    amount: int
    min_receive: int
    fee_cap: int
    source_ms: int
    sign_by_ms: int


@dataclass(frozen=True)
class Stop:
    reason: str


def buy_order(price, now_ms, source_ms=None):
    source_ms = now_ms if source_ms is None else source_ms
    principal = CLIP_LAMPORTS * Decimal(str(price)) / 1000
    total = int((principal / Decimal('0.998') / 10_000).to_integral_value(ROUND_CEILING)) * 10_000
    floor = int((CLIP_LAMPORTS * BUY_FLOOR).to_integral_value(ROUND_FLOOR))
    return Order('buy', total, floor, total // 10_000 * 20, source_ms, source_ms + SIGN_LIFETIME_MS)


def sell_order(base_atoms, price, now_ms, source_ms=None):
    source_ms = now_ms if source_ms is None else source_ms
    expected = base_atoms * Decimal(str(price)) / 1000
    floor = int((expected * SELL_FLOOR).to_integral_value(ROUND_FLOOR))
    fee_cap = int((expected * SELL_FEE_MARGIN / 499).to_integral_value(ROUND_CEILING))
    return Order('sell', base_atoms, floor, fee_cap, source_ms, source_ms + SIGN_LIFETIME_MS)


class LiveRule:
    def __init__(self, max_orders=MAX_ORDERS):
        if type(max_orders) is not int or not 1 <= max_orders <= MAX_ORDERS:
            raise ValueError('order limit must be 1 to 240')
        self.max_orders = max_orders
        self.reset()

    def reset(self):
        self.indicators = Indicators()
        self.signals = Signals()
        self.previous = None

    def bar(self, start_ms, end_ms, o, h, l, c, account, now_ms, last_exit_s, draining=False):
        bar = candle(start_ms, end_ms, o, h, l, c)
        if self.indicators.last is not None and bar[0] != self.indicators.last + 1:
            self.reset()
        flags = self.previous
        self.previous = self.signals.push(self.indicators.push(bar))
        if account.pending:
            return None
        held = account.base_atoms > 0
        if account.orders >= self.max_orders:
            return Stop('capacity_exposure' if held else 'capacity')
        stop = ('drawdown' if account.realized_pnl_atoms <= -DRAWDOWN_ATOMS else
                'capacity' if account.orders + 1 >= self.max_orders else
                'review_drained' if draining else None)
        if flags is None or self.indicators.count < WARMUP_BARS:
            if not (stop and held):
                return None
            flags = (0, 0)
        exit_flag, entry_flag = flags
        if last_exit_s is not None and bar[0] - last_exit_s < REENTRY_SECONDS:
            entry_flag = 0
        if stop:
            exit_flag, entry_flag = 1, 0
        if held:
            return sell_order(account.base_atoms, c, now_ms, end_ms + 1) if exit_flag else None
        if stop:
            return Stop(stop)
        order = buy_order(c, now_ms, end_ms + 1)
        if order.amount > account.quote_atoms:
            return Stop('insufficient_balance')
        return order if entry_flag else None
