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

# Deliberately over-broad, matching model/'s STATE_CHANGING_PATH_FRAGMENTS: a
# false refusal costs one line here, a false permit costs an order.
FORBIDDEN = ("/orders", "/order", "/trade", "/execute", "/submit", "/cancel",
             "/withdraw", "/deposit", "/transfer", "/approve", "/swap", "/sign",
             "/transaction")

CANDIDATES = [
    "/v1/market-data/instruments",
    "/v1/market-data/orderbook",
    "/v1/market-data/candles",
    "/v1/market-data/prices",
    "/v1/market-data/reference-prices",
    "/v1/defi/orderbook",
    "/v1/defi/market/orderbook",
    "/v1/conductor/orderbook",
    "/v1/conductor/quotes",
    "/v1/conductor/assets",
    "/v1/account/portfolio",
    "/v1/positions",
]


def get(path: str, token: str) -> tuple[str, bytes]:
    lowered = path.lower()
    for fragment in FORBIDDEN:
        if fragment in lowered:
            raise SystemExit(f"refusing {path}: matches {fragment!r}")
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
    token = pathlib.Path(sys.argv[1]).read_text().strip()
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
