#!/usr/bin/env python3
"""Owner-operated read-only quote capture, not a verified markup measurement.

python3 tools/measure-markup.py EMAIL [USDC amounts]

Exit 2: capture only; response schema and units remain unverified.
Exit 1: failure. No files written, no signing, no order submission.
Review quote output for personal information before sharing.
"""
from __future__ import annotations

from datetime import datetime, timezone
from decimal import ROUND_DOWN, Decimal, InvalidOperation
import json
import re
import sys
import time
import urllib.error
import urllib.request

TM = "https://mcp.truemarkets.co/mcp"
CATALOG = "https://api.truemarkets.co/v1/conductor/assets?venue=defi"
REFERENCE = "https://api.truemarkets.co/v1/defi/market/prices?symbol="
ASSETS = (("SOL", "solana"), ("CBBTC", "base"))
ALLOWED = frozenset({"login_request_code", "login_verify_code", "get_quote"})
MAX_RESPONSE_BYTES = 2 * 1024 * 1024


def parse(raw):
    text = raw.decode("utf-8", "strict")
    if text.lstrip().startswith("{"):
        return json.loads(text)
    for event in text.replace("\r\n", "\n").split("\n\n"):
        data = "\n".join(line[5:].lstrip() for line in event.splitlines()
                         if line.startswith("data:"))
        if not data:
            continue
        doc = json.loads(data)
        if isinstance(doc, dict) and ("result" in doc or "error" in doc):
            return doc
    raise ValueError("missing JSON-RPC response")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise urllib.error.HTTPError(TM, 403, "redirect refused", {}, None)


class Session:
    def __init__(self):
        self.id = None
        self.sequence = 0

    def rpc(self, method, params=None, rid=None):
        if method not in {"initialize", "notifications/initialized", "tools/call"}:
            raise SystemExit("refusing unsupported RPC method")
        params = params or {}
        if method == "tools/call" and params.get("name") not in ALLOWED:
            raise SystemExit("refusing tool outside read-only allowlist")
        self.sequence += 1
        body = {"jsonrpc": "2.0", "method": method, "params": params}
        if not method.startswith("notifications/"):
            body["id"] = self.sequence
        request = urllib.request.Request(TM, data=json.dumps(body).encode(), method="POST")
        request.add_header("content-type", "application/json")
        request.add_header("accept", "application/json, text/event-stream")
        request.add_header("MCP-Protocol-Version", "2025-06-18")
        request.add_header("User-Agent", "velocita-quote-capture/1")
        if self.id:
            request.add_header("mcp-session-id", self.id)
        try:
            with urllib.request.build_opener(NoRedirect()).open(request, timeout=60) as response:
                self.id = self.id or response.headers.get("mcp-session-id")
                raw = response.read(MAX_RESPONSE_BYTES + 1)
                if len(raw) > MAX_RESPONSE_BYTES:
                    return 413, b'{}'
                if "id" in body and parse(raw).get("id") != body["id"]:
                    return 502, b'{}'
                return response.status, raw
        except urllib.error.HTTPError as error:
            return error.code, b'{}'
        except (urllib.error.URLError, ValueError, OSError, AttributeError):
            return 502, b'{}'

    def call(self, tool, **args):
        if tool not in ALLOWED:
            raise SystemExit("refusing tool outside read-only allowlist")
        status, raw = self.rpc("tools/call", {"name": tool, "arguments": args})
        try:
            doc = parse(raw)
            result = doc.get("result")
            if status != 200:
                return {"_error": "RPC failed", "detail": f"HTTP {status}"}
            if "error" in doc or not isinstance(result, dict):
                error = doc.get("error") if isinstance(doc.get("error"), dict) else {}
                return {"_error": "RPC failed", "detail": f"{error.get('code')} {error.get('message')}"}
            if result.get("isError"):
                text = " ".join(str(c.get("text", "")) for c in result.get("content") or [] if isinstance(c, dict))
                return {"_error": "tool failed", "detail": text}
            return result
        except (ValueError, AttributeError):
            return {"_error": "invalid response", "detail": ""}


def redact(value):
    if isinstance(value, dict):
        return {key: ("[REDACTED]" if any(word in key.lower() for word in
                ("token", "secret", "password", "signature", "private", "authorization", "email"))
                else redact(item)) for key, item in value.items()}
    if isinstance(value, list):
        return [redact(item) for item in value]
    if isinstance(value, str):
        try:
            parsed = json.loads(value)
        except ValueError:
            return value
        if isinstance(parsed, (dict, list)):
            return json.dumps(redact(parsed))
    return value


def fetch_catalog():
    request = urllib.request.Request(CATALOG, headers={"User-Agent": "velocita-quote-capture/1"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read(MAX_RESPONSE_BYTES))


def fetch_reference(symbol):
    request = urllib.request.Request(REFERENCE + symbol, headers={"User-Agent": "velocita-quote-capture/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read(MAX_RESPONSE_BYTES))


def reference_close(document):
    for candle in (document or {}).get("candles") or ():
        if isinstance(candle, dict) and candle.get("interval") == "30s":
            return candle.get("closePrice")
    return None


def resolve_address(catalog, symbol, chain):
    matches = [a for a in catalog.get("data") or () if a.get("symbol") == symbol
               and a.get("chain") == chain and a.get("tradeable") and a.get("address")]
    if len(matches) != 1:
        raise SystemExit(f"{symbol} on {chain}: expected one tradeable catalog entry, found {len(matches)}")
    return matches[0]["address"]


def scrub(text, limit=300):
    text = re.sub(r"[\w.+-]+@[\w-]+(\.[\w-]+)+", "[EMAIL]", str(text))
    text = re.sub(r"eyJ[\w-]+\.[\w-]+(\.[\w-]*)?", "[TOKEN]", text)
    text = re.sub(r"[A-Za-z0-9+/_=-]{32,}", "[REDACTED]", text)
    return text[:limit]


def read_code(attempts=3):
    try:
        import termios
        if sys.stdin.isatty():
            termios.tcflush(sys.stdin, termios.TCIFLUSH)
    except (ImportError, OSError):
        pass
    for _ in range(attempts):
        code = input("Paste the code from your email (never share it): ").strip()
        if code:
            return code
    return ""


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    try:
        sizes = [Decimal(x) for x in (argv[1:] or ["25", "100", "500"])]
        for value in sizes:
            if (not value.is_finite() or value <= 0 or value > Decimal("1000000000")
                    or value * 1000000 != (value * 1000000).to_integral_value()):
                raise ValueError("invalid amount")
    except (InvalidOperation, ValueError):
        print("Amounts must be positive, finite USDC values with at most six decimals.")
        return 1
    catalog = fetch_catalog()
    assets = [(symbol, chain, resolve_address(catalog, symbol, chain)) for symbol, chain in ASSETS]
    session = Session()
    status, raw = session.rpc("initialize", {"protocolVersion": "2025-06-18",
        "capabilities": {}, "clientInfo": {"name": "velocita-quote-capture", "version": "1"}})
    try:
        initialized = parse(raw)
        if status != 200 or "error" in initialized or "result" not in initialized:
            raise ValueError("initialize failed")
    except (ValueError, TypeError):
        print(f"Session initialization failed (HTTP {status}).")
        return 1
    status, _ = session.rpc("notifications/initialized")
    if status not in (200, 202, 204):
        print("Session notification failed.")
        return 1
    if "_error" in session.call("login_request_code", email=argv[0]):
        print("Login code request failed.")
        return 1
    code = read_code()
    if not code:
        print("No code entered; no quotes collected.")
        return 1
    if "_error" in session.call("login_verify_code", email=argv[0], code=code):
        print("Login verification failed; no quotes collected.")
        return 1
    print("Quote schema/units unverified. Capture only; no markup or round-trip estimate.")
    first = True
    for symbol, chain, address in assets:
        for amount in sizes:
            for side in ("buy", "sell"):
                if not first:
                    time.sleep(1)
                first = False
                try:
                    reference_before = reference_close(fetch_reference(symbol))
                except (urllib.error.URLError, ValueError, OSError):
                    reference_before = None
                if side == "buy":
                    qty, unit = format(amount, "f"), "quote"
                elif reference_before is None:
                    print(f"No reference price for {symbol}; sell quote for ${amount} skipped.")
                    continue
                else:
                    qty = format((amount / Decimal(reference_before)).quantize(Decimal("0.000001"), ROUND_DOWN), "f")
                    unit = "base"
                arguments = dict(chain=chain, side=side, base_asset=address, qty=qty, qty_unit=unit)
                requested_at = datetime.now(timezone.utc).isoformat()
                for attempt in range(3):
                    result = session.call("get_quote", **arguments)
                    if "_error" not in result:
                        break
                    time.sleep(2 * (attempt + 1))
                else:
                    print(f"Quote request failed after 3 attempts: {result['_error']}: "
                          f"{scrub(result.get('detail', ''))}")
                    return 1
                received_at = datetime.now(timezone.utc).isoformat()
                try:
                    reference_after = reference_close(fetch_reference(symbol))
                except (urllib.error.URLError, ValueError, OSError):
                    reference_after = None
                print(json.dumps({"status": "schema_unverified", "symbol": symbol, "usd_size": format(amount, "f"),
                    "requested_at": requested_at, "received_at": received_at, "request": arguments,
                    "reference_30s_close": reference_before, "reference_30s_close_after": reference_after,
                    "quote_evidence": redact(result)}, indent=2))
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
