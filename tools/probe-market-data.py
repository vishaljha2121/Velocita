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
import re

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
    "/v1/conductor/assets",          # control: a probe where all 404 proves nothing
    "/v1/prices",
    "/v1/prices/current",
    "/v1/prices/candles",
    "/v1/prices/history",
    "/v1/prices/latest",
    "/v1/prices/batch",
    "/v1/market-cap",
    "/v1/marketcap",
    "/v1/market-cap/latest",
]

TOKEN_SEGMENT = re.compile(r"^[A-Za-z0-9_-]+$")


def validate_token(token: str) -> None:
    parts = token.split(".")
    if len(parts) != 3 or any(not TOKEN_SEGMENT.fullmatch(part) for part in parts):
        raise SystemExit("token is not a safe three-segment JWT")


def validate_path(path: str) -> None:
    if path not in CANDIDATES or not path.startswith("/"):
        raise SystemExit(f"refusing unapproved probe path: {path!r}")


def get(path: str, token: str) -> tuple[str, bytes]:
    validate_path(path)
    validate_token(token)
    segments = [s for s in path.lower().split("?")[0].split("/") if s]
    for segment in segments:
        if segment in FORBIDDEN_SEGMENTS:
            raise SystemExit(f"refusing {path}: path segment {segment!r} is state-changing")
    # Keep the bearer out of argv/process listings. Curl reads this one header
    # from stdin as config; the token is restricted to JWT-safe characters above.
    config = f'header = "authorization: Bearer {token}"\n'
    result = subprocess.run(
        ["curl", "--disable", "-sS", "-m", "20", "--max-redirs", "0",
         "-w", "\n%{http_code}", "-H", f"user-agent: {UA}",
         "-H", "accept: application/json", "--config", "-", BASE + path],
        input=config.encode(), capture_output=True, check=False)
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
    try:
        validate_token(token)
    except SystemExit:
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
