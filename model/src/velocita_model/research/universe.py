"""Point-in-time universe: each month, the most-traded Binance USDT spot pairs of
the PREVIOUS month, chosen from every pair that ever existed (delisted ones
included). This removes the hindsight in a hand-picked shortlist: a coin enters
because it was liquid then, not because we know it went up later.

The exclusion rule is fixed in advance: stablecoins and fiat (their "momentum"
is noise around a peg), leveraged tokens, and wrapped or commodity tokens.
"""

from __future__ import annotations

from datetime import date
from typing import Iterable, Mapping, Sequence

EXCLUDED_BASES = frozenset({
    "USDC", "BUSD", "TUSD", "USDP", "PAX", "DAI", "FDUSD", "USDS", "UST", "USTC", "USDE", "USD1", "XUSD",
    "BFUSD", "RLUSD", "PYUSD", "USDSB", "SUSD", "AEUR", "EURI", "EUR", "GBP", "AUD", "TRY", "BRL", "RUB",
    "UAH", "NGN", "ZAR", "BIDR", "IDRT", "BVND", "PAXG", "XAUT", "WBTC", "WBETH", "BETH",
    "BULL", "BEAR",  # the original 3x BTC tokens, listed without a prefix
})
LEVERAGED_SUFFIXES = ("UP", "DOWN", "BULL", "BEAR")


def is_candidate(pair: str) -> bool:
    if not pair.endswith("USDT"):
        return False
    base = pair[:-4]
    if base in EXCLUDED_BASES:
        return False
    return not any(base.endswith(suffix) and len(base) > len(suffix) for suffix in LEVERAGED_SUFFIXES)


def _previous(month: str) -> str:
    y, m = int(month[:4]), int(month[5:7])
    return f"{y - 1:04d}-12" if m == 1 else f"{y:04d}-{m - 1:02d}"


def monthly_members(volumes: Mapping[str, Mapping[str, float]], months: Sequence[str], *, top_n: int,
                    always: Iterable[str] = ()) -> dict[str, set[str]]:
    """Members for each MONTH = top TOP_N pairs by quote volume in the month
    before it, plus ALWAYS. Uses nothing from the month itself."""
    out = {}
    for month in months:
        prior = _previous(month)
        ranked = sorted(((v[prior], pair) for pair, v in volumes.items() if prior in v and v[prior] > 0),
                        reverse=True)
        out[month] = {pair for _, pair in ranked[:top_n]} | set(always)
    return out


def eligibility_mask(dates: Sequence[date], symbols: Sequence[str],
                     members: Mapping[str, set[str]]) -> tuple[tuple[bool, ...], ...]:
    keys = [d.strftime("%Y-%m") for d in dates]
    return tuple(tuple(symbol in members.get(k, ()) for k in keys) for symbol in symbols)
