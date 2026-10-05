#!/usr/bin/env python3
"""Read-only MCP probe. Calls ONLY initialize and tools/list.

It has no vocabulary for tools/call, so it cannot trade. The point is to get
the tool schemas from the SERVER rather than from the prose documentation,
because the prose has been wrong twenty-one times (D-1..D-21).
"""
import json
import sys
import urllib.error
import urllib.request

UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/140.0 Safari/537.36")


def rpc(url, method, params=None, session=None, rid=1):
    if method not in {"initialize", "notifications/initialized", "tools/list"}:
        raise SystemExit("refusing: this probe never calls a tool")
    message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if not method.startswith("notifications/"):
        message["id"] = rid
    body = json.dumps(message).encode()
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("content-type", "application/json")
    req.add_header("accept", "application/json, text/event-stream")
    req.add_header("user-agent", UA)
    if session:
        req.add_header("mcp-session-id", session)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()
    except Exception as e:  # noqa: BLE001
        return 0, {}, str(e).encode()


def parse(raw):
    text = raw.decode("utf-8", "replace")
    if text.lstrip().startswith("{"):
        return json.loads(text)
    for line in text.splitlines():
        if line.startswith("data:"):
            return json.loads(line[5:].strip())
    raise ValueError(text[:300])


def main(url):
    print("=== " + url)
    code, headers, raw = rpc(url, "initialize", {
        "protocolVersion": "2025-06-18",
        "capabilities": {},
        "clientInfo": {"name": "velocita-schema-probe", "version": "0"}})
    print("initialize -> http", code)
    if code != 200:
        print(raw[:400].decode("utf-8", "replace"))
        return 1
    session = headers.get("mcp-session-id") or headers.get("Mcp-Session-Id")
    doc = parse(raw)
    print("server:", json.dumps(doc.get("result", {}).get("serverInfo", {})))

    if "error" in doc or not isinstance(doc.get("result"), dict):
        return 1
    rpc(url, "notifications/initialized", session=session)
    cursor, seen = None, set()
    for page in range(100):
        code, _, raw = rpc(url, "tools/list", {"cursor": cursor} if cursor else {},
                           session=session, rid=page + 2)
        if code != 200:
            return 1
        try:
            result = parse(raw)["result"]
            if not isinstance(result["tools"], list):
                return 1
            print(json.dumps(result, indent=2))
            cursor = result.get("nextCursor")
            if cursor is None:
                return 0
            if not isinstance(cursor, str) or not cursor or cursor in seen:
                return 1
            seen.add(cursor)
        except (ValueError, KeyError, TypeError):
            return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
