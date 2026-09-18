#!/usr/bin/env python3
"""Call a read-only tool on the True Markets MARKET DATA MCP server.

    ./tools/mcp-read.py list_assets chain=solana

WHY THIS IS SAFE BY CONSTRUCTION, not by care. The endpoint is pinned to the
marketdata server, and that server exposes ten tools, none of which trade: there
is no `trade_prepare` on it to call. The safety property is a fact about the
server's tool list rather than a rule this script remembers to enforce - so it
cannot be lost by editing this file.

The trading server is refused outright even though it serves the same ten tools,
because reaching it means holding a session that CAN trade. See ADR 0011.
"""
from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request

ENDPOINT = "https://mcp.truemarkets.co/marketdata/mcp"
UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/140.0 Safari/537.36")


def rpc(method, params=None, session=None, rid=1):
    body = json.dumps({"jsonrpc": "2.0", "id": rid,
                       "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(ENDPOINT, data=body, method="POST")
    req.add_header("content-type", "application/json")
    req.add_header("accept", "application/json, text/event-stream")
    req.add_header("user-agent", UA)
    if session:
        req.add_header("mcp-session-id", session)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def parse(raw):
    text = raw.decode("utf-8", "replace")
    if text.lstrip().startswith("{"):
        return json.loads(text)
    for line in text.splitlines():
        if line.startswith("data:"):
            return json.loads(line[5:].strip())
    raise ValueError(text[:300])


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    tool, args = argv[0], dict(a.split("=", 1) for a in argv[1:])

    _, headers, raw = rpc("initialize", {
        "protocolVersion": "2025-06-18", "capabilities": {},
        "clientInfo": {"name": "velocita-read", "version": "0"}})
    session = headers.get("mcp-session-id") or headers.get("Mcp-Session-Id")
    rpc("notifications/initialized", session=session)

    status, _, raw = rpc("tools/call", {"name": tool, "arguments": args},
                         session=session, rid=2)
    doc = parse(raw)
    if status != 200 or "error" in doc:
        print(json.dumps(doc, indent=2)[:1200])
        return 1
    for block in doc["result"].get("content", []):
        text = block.get("text", "")
        try:
            print(json.dumps(json.loads(text), indent=2))
        except Exception:  # noqa: BLE001
            print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
