#!/usr/bin/env python3
"""Measure the round-trip cost of a swap: USDC -> token -> USDC.

Read-only. Jupiter's quote endpoint simulates a route and returns what you
would receive; it does not build, sign or send a transaction.

WHY THIS NUMBER DECIDES THE ARCHITECTURE. On a swap venue capital recycles - a
round trip returns your money minus costs - so the cap on lifetime volume is not
capital but the LOSS BUDGET:

    idealized_two_leg_volume = 2 * loss_budget / round_trip_cost_fraction

These sequential indicative quotes are not executions or a True Markets cost
bound. They omit intervening market movement and the impact of our own buy.

Halving the cost per round trip doubles the volume the same budget buys. So the
question "which asset, at which size" is worth measuring rather than guessing.
"""
from __future__ import annotations

import json
from decimal import Decimal, InvalidOperation
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

QUOTE = "https://lite-api.jup.ag/swap/v1/quote"
USDC = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"
USDC_DECIMALS = 6
UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/140.0"


def quote(input_mint, output_mint, amount, slippage_bps=50):
    query = urllib.parse.urlencode({
        "inputMint": input_mint, "outputMint": output_mint,
        "amount": str(amount), "slippageBps": str(slippage_bps),
        "restrictIntermediateTokens": "true"})
    req = urllib.request.Request(QUOTE + "?" + query)
    req.add_header("user-agent", UA)
    req.add_header("accept", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        return {"error": e.code, "body": e.read()[:200].decode("utf-8", "replace")}
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def round_trip(symbol, mint, usd):
    """Buy `usd` of the token, then sell every unit back. Report the loss."""
    amount_in = int(Decimal(str(usd)) * 10 ** USDC_DECIMALS)
    buy = quote(USDC, mint, amount_in)
    if "outAmount" not in buy:
        return {"symbol": symbol, "usd": usd, "error": buy.get("error"), "detail": buy}
    tokens = int(buy["outAmount"])
    time.sleep(0.4)
    sell = quote(mint, USDC, tokens)
    if "outAmount" not in sell:
        return {"symbol": symbol, "usd": usd, "error": sell.get("error"), "detail": sell}
    back = int(sell["outAmount"])

    lost = amount_in - back
    return {
        "symbol": symbol,
        "usd": usd,
        "returned_usd": back / 10 ** USDC_DECIMALS,
        "cost_usd": lost / 10 ** USDC_DECIMALS,
        "cost_bps": 10000.0 * lost / amount_in,
        "buy_impact_pct": float(buy.get("priceImpactPct", 0)) * 100,
        "sell_impact_pct": float(sell.get("priceImpactPct", 0)) * 100,
        "buy_hops": len(buy.get("routePlan", [])),
        "sell_hops": len(sell.get("routePlan", [])),
    }


def main(argv):
    # symbol -> mint. Chosen for depth: the cheapest round trip wins, and depth
    # is what keeps impact from dominating at size.
    targets = {
        "SOL":   "So11111111111111111111111111111111111111112",
        "JUP":   "JUPyiwrYJFskUPiHa7hkeR8VUtAeFoSYbKedZNsDvCN",
        "JTO":   "jtojtomepa8beP8AuQc6eXt5FriJwfFMwQx2v2f9mCL",
        "DRIFT": "DriFtupJYLTosbwoN8koMbEYSx54aFAVLddWsbksjwg7",
        "W":     "85VBFQZC9TZkfaptBWjvUw7YbZjy52A6mjtPGjstQAmQ",
    }
    try:
        exact = [Decimal(x) for x in (argv or ["25", "100", "500", "2000"])]
        if any(not x.is_finite() or x <= 0 or x > 1000000000 or
               x * 1000000 != (x * 1000000).to_integral_value() for x in exact):
            raise ValueError("invalid size")
    except (InvalidOperation, ValueError):
        print("sizes must be positive finite USDC amounts with at most six decimals")
        return 1
    sizes = [float(x) for x in exact]

    rows = []
    for symbol, mint in targets.items():
        for usd in sizes:
            row = round_trip(symbol, mint, usd)
            rows.append(row)
            if "error" in row:
                print(f"{symbol:>6} ${usd:>7.0f}  ERROR {row['error']}")
            else:
                print(f"{symbol:>6} ${usd:>7.0f}  cost={row['cost_bps']:>8.1f} bps "
                      f"(${row['cost_usd']:>7.3f})  impact b/s="
                      f"{row['buy_impact_pct']:.3f}/{row['sell_impact_pct']:.3f}%  "
                      f"hops={row['buy_hops']}/{row['sell_hops']}")
            time.sleep(0.4)

    with open("roundtrip.json", "x") as handle:
        json.dump(rows, handle, indent=2)
    return int(any("error" in row for row in rows))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
