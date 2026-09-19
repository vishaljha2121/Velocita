#!/usr/bin/env python3
"""Measure True Markets' markup over the raw on-chain route. READ-ONLY.

    ./tools/measure-markup.py you@example.com

Logs in by email OTP, then quotes a round trip on True Markets and the same
round trip through Jupiter, and reports the difference. NO ORDER IS PLACED.

WHY THIS NUMBER AND NOT ANOTHER. ADR 0011 section 2:

    max_volume = loss_budget x 2 / c

Everything measured so far put `c` at Jupiter's route cost, which is a LOWER
BOUND - True Markets routes through its own aggregator and may add a markup that
sits on top. Section 7 shows the markup swings the achievable volume by a factor
of 200, from $4M per $100 of budget at zero markup to $20k at 100 bps. No
further design work is worth doing until it is measured.

HOW SAFE THIS IS, STATED HONESTLY. `tools/mcp-read.py` is safe BY CONSTRUCTION:
it is pinned to the market-data server, which has no trading tool to call. This
script is not, and cannot be - `get_quote` lives only on the trading server, and
reaching it means holding a session that CAN trade. Its safety is therefore a
RULE rather than a property:

  * ALLOWED is a closed list of four tool names;
  * `trade_prepare` and `trade_execute` are refused by name as well, so a typo
    that slipped past the allowlist still cannot reach them;
  * there is no signing code here, and no path that could produce a signature.

That is weaker than mcp-read.py and the difference is the reason this notice
exists. Do not add tools to the allowlist.
"""
from __future__ import annotations

import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

TM = "https://mcp.truemarkets.co/mcp"
JUP = "https://lite-api.jup.ag/swap/v1/quote"
USDC_MINT = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"
UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/140.0"

ALLOWED = {"login_request_code", "login_verify_code", "get_quote", "get_profile"}
NEVER = {"trade_prepare", "trade_execute"}


class Session:
    def __init__(self) -> None:
        self.id = None

    def rpc(self, method, params=None, rid=1):
        body = json.dumps({"jsonrpc": "2.0", "id": rid,
                           "method": method, "params": params or {}}).encode()
        req = urllib.request.Request(TM, data=body, method="POST")
        req.add_header("content-type", "application/json")
        req.add_header("accept", "application/json, text/event-stream")
        req.add_header("user-agent", UA)
        if self.id:
            req.add_header("mcp-session-id", self.id)
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                if not self.id:
                    self.id = r.headers.get("mcp-session-id")
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def call(self, tool, **args):
        if tool in NEVER or tool not in ALLOWED:
            raise SystemExit(f"refusing: {tool} is not on this script's allowlist")
        status, raw = self.rpc("tools/call",
                               {"name": tool, "arguments": args}, rid=9)
        doc = parse(raw)
        if status != 200 or "error" in doc:
            return {"_error": doc.get("error", doc)}
        out = []
        for block in doc.get("result", {}).get("content", []):
            text = block.get("text", "")
            try:
                out.append(json.loads(text))
            except Exception:  # noqa: BLE001
                out.append(text)
        return out[0] if len(out) == 1 else out


def parse(raw):
    text = raw.decode("utf-8", "replace")
    if text.lstrip().startswith("{"):
        return json.loads(text)
    for line in text.splitlines():
        if line.startswith("data:"):
            return json.loads(line[5:].strip())
    return {"error": text[:300]}


def jupiter(input_mint, output_mint, amount):
    query = urllib.parse.urlencode({
        "inputMint": input_mint, "outputMint": output_mint,
        "amount": str(amount), "slippageBps": "50",
        "restrictIntermediateTokens": "true"})
    req = urllib.request.Request(JUP + "?" + query)
    req.add_header("user-agent", UA)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def number(doc, *names):
    """Pull the first numeric field whose name matches, at any depth."""
    if isinstance(doc, dict):
        for key, value in doc.items():
            if key in names:
                try:
                    return float(str(value).replace(",", ""))
                except ValueError:
                    pass
            found = number(value, *names)
            if found is not None:
                return found
    elif isinstance(doc, list):
        for item in doc:
            found = number(item, *names)
            if found is not None:
                return found
    return None


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    email = argv[0]
    sizes = [float(x) for x in (argv[1:] or ["25", "100", "500"])]
    session = Session()

    session.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                               "clientInfo": {"name": "velocita-markup", "version": "0"}})
    session.rpc("notifications/initialized")

    print(f"requesting a login code for {email} ...")
    print(json.dumps(session.call("login_request_code", email=email))[:300])
    code = input("paste the code from your email: ").strip()
    result = session.call("login_verify_code", code=code)
    print(json.dumps(result)[:300])
    if isinstance(result, dict) and "_error" in result:
        return 1

    # One raw quote first: the response schema is not documented, and guessing
    # which field is "amount received" is exactly the class of error D-3 was.
    print("\n=== raw get_quote, so the response shape is on the record ===")
    raw_probe = session.call("get_quote", chain="solana", side="buy",
                             base_asset="SOL", qty="25", qty_unit="quote")
    print(json.dumps(raw_probe, indent=2)[:2000])

    print("\n=== round trip: True Markets vs Jupiter ===")
    rows = []
    for usd in sizes:
        buy = session.call("get_quote", chain="solana", side="buy",
                           base_asset="SOL", qty=str(usd), qty_unit="quote")
        tokens = number(buy, "out_amount", "outAmount", "receive_amount",
                        "amount_out", "to_amount", "output_amount")
        if tokens is None:
            print(f"${usd:>7.0f}  could not locate the received amount; "
                  f"fields were {list(buy) if isinstance(buy, dict) else type(buy)}")
            rows.append({"usd": usd, "tm_raw": buy})
            continue
        time.sleep(0.8)
        sell = session.call("get_quote", chain="solana", side="sell",
                            base_asset="SOL", qty=str(tokens), qty_unit="base")
        back = number(sell, "out_amount", "outAmount", "receive_amount",
                      "amount_out", "to_amount", "output_amount")

        jup_buy = jupiter(USDC_MINT, "So11111111111111111111111111111111111111112",
                          int(usd * 1e6))
        jup_tokens = int(jup_buy["outAmount"]) / 1e9 if "outAmount" in jup_buy else None

        row = {"usd": usd, "tm_tokens": tokens, "tm_back": back,
               "jup_tokens": jup_tokens}
        if back:
            row["tm_cost_bps"] = 10000.0 * (usd - back) / usd
        if jup_tokens and tokens:
            row["markup_on_buy_bps"] = 10000.0 * (jup_tokens - tokens) / jup_tokens
        rows.append(row)

        print(f"${usd:>7.0f}  TM buy -> {tokens:.6f} SOL   "
              f"Jupiter -> {jup_tokens if jup_tokens is None else f'{jup_tokens:.6f}'} SOL   "
              f"buy markup = "
              f"{row.get('markup_on_buy_bps', float('nan')):.1f} bps   "
              f"TM round trip = {row.get('tm_cost_bps', float('nan')):.1f} bps")
        time.sleep(0.8)

    with open("markup.json", "w") as handle:
        json.dump(rows, handle, indent=2)
    print("\nwritten to markup.json")
    print("\nFeed the round-trip figure back into ADR 0011 section 7 as `c`.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
