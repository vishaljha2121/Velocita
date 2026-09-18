#!/usr/bin/env python3
"""Find out which market-data paths actually exist. ADR 0010.

Reads a token file. NEVER holds key material, never signs, never sends anything
but GET, and refuses any path that looks state-changing - the same discipline
`model/`'s ReadOnlyHttp enforces, restated here because this tool lives outside
that package.

    ./tools/probe-market-data.py /run/velocita/tm.token out/

WHY A PROBE RATHER THAN A COLLECTOR. Several plausible market-data paths answer
`401 Jwt is missing` without a token, which may mean "exists, needs auth" or may
be a catch-all for anything under an authenticated router. Collecting against
guessed paths would record silence as data. The first use of a token should be
establishing which paths are real.
"""
from __future__ import annotations

import json
import pathlib
import subprocess
import sys

BASE = "https://api.truemarkets.co"
UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/140.0 Safari/537.36")

# Forbidden path SEGMENTS, compared segment by segment rather than as
# substrings.
#
# Substring matching was wrong here and the failure was instructive: `/order`
# matched `/v1/market-data/orderbook`, so the probe refused to read an ORDER
# BOOK - market data whose whole purpose is to be read, and which cannot place
# anything. Over-broad is the right instinct for this list, but "over-broad"
# should mean "more segments than strictly necessary", not "any path that starts
# with the same letters".
#
# `orders` stays on the list even though GET /orders would merely LIST orders.
# That is a standing project rule and it is kept deliberately: this tool has no
# business reading the order book of record, and a path check that reasons about
# intent is a path check that gets argued with.
FORBIDDEN_SEGMENTS = frozenset({
    "orders", "order", "trade", "trades", "execute", "submit", "cancel",
    "withdraw", "deposit", "transfer", "approve", "swap", "sign", "signature",
    "transaction", "transactions",
})

# TWO 404 SHAPES DISTINGUISH TWO DIFFERENT FACTS, and reading them apart is what
# makes this probe worth running:
#
#   {"type":"not_found",...,"request_id":...}  the prefix ROUTES to the API and
#                                              this path is not one it serves
#   Not Found (plain text)                     the edge does not route this at
#                                              all; nothing reached the API
#
# The first round established that only `/v1/conductor/*`, `/v1/account/*` and
# `/v1/defi/market*` route. `/v1/market-data/*` does not exist on this host at
# all, despite the documentation describing a market-data service - so the
# candidates below stay inside the namespaces that answer.
CANDIDATES = [
    # known-good, as a control: a probe where everything 404s proves nothing
    "/v1/conductor/assets",
    # market data inside the namespace that routes
    "/v1/conductor/candles",
    "/v1/conductor/prices",
    "/v1/conductor/price",
    "/v1/conductor/book",
    "/v1/conductor/depth",
    "/v1/conductor/instruments",
    "/v1/conductor/pairs",
    "/v1/conductor/markets",
    "/v1/conductor/tickers",
    "/v1/conductor/reference-prices",
    # the other routing namespaces
    "/v1/account",
    "/v1/account/balances",
    "/v1/account/kyc-status",
    "/v1/defi/market/candles",
    "/v1/defi/market/reference-prices",
    "/v1/cefi/market/depth",
]


def get(path: str, token: str) -> tuple[str, bytes]:
    segments = [s for s in path.lower().split("?")[0].split("/") if s]
    for segment in segments:
        if segment in FORBIDDEN_SEGMENTS:
            raise SystemExit(f"refusing {path}: path segment {segment!r} is state-changing")
    result = subprocess.run(
        ["curl", "-sS", "-m", "20", "-w", "\n%{http_code}",
         "-H", f"user-agent: {UA}", "-H", f"authorization: Bearer {token}",
         "-H", "accept: application/json", BASE + path],
        capture_output=True, check=False)
    raw = result.stdout
    body, _, status = raw.rpartition(b"\n")
    return status.decode().strip(), body


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    token_file = pathlib.Path(sys.argv[1])
    token = token_file.read_text().strip()

    # A JWT is three dot-separated base64url segments on one line. Anything else
    # is almost certainly the KEY JSON handed over by mistake - which would send
    # private key material in an Authorization header, and produce a curl
    # failure that looks like a network problem rather than an operator error.
    if token.startswith("{") or "\n" in token or token.count(".") != 2:
        raise SystemExit(
            f"{token_file} does not look like a token.\n"
            "If this is the API key JSON, mint a token from it first:\n"
            f"  ./tools/mint-token.py {token_file} <token-path>\n"
            "and pass the TOKEN path here. The key must never be sent as a "
            "bearer credential.")
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)

    findings = []
    for path in CANDIDATES:
        status, body = get(path, token)
        name = path.strip("/").replace("/", "_") + ".json"
        (out / name).write_bytes(body)
        preview = body[:120].decode("utf-8", "replace").replace("\n", " ")
        print(f"{status}  {path:<38} {preview}")
        findings.append({"path": path, "status": status, "bytes": len(body)})

    (out / "_probe.json").write_text(json.dumps(findings, indent=1))
    print(f"\nwrote {len(findings)} responses to {out}")
    print("A 200 means the path is real. A 404 means it is not. A 401 WITH a")
    print("valid token means something other than authentication refused it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
