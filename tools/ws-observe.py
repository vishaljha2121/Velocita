#!/usr/bin/env python3
"""Observe the True Markets DeFi market WebSocket. ADR 0010.

Reads a token file. Never holds key material, never signs, and sends only
subscribe frames - it has no vocabulary for anything else.

    ./tools/ws-observe.py ~/.config/velocita/tm.token out/ [seconds]

WHY THIS EXISTS. D-18 established that there is no REST market data on this
host: twenty-nine paths were probed with a valid token and none serve a book.
The book is reachable only here, and `curl` on macOS has no `wss` support - so
the client is hand-rolled over `ssl`, which is a stdlib dependency and no more.

WHAT IT IS FOR. D-12: tick and lot are published nowhere, so the only remaining
route to a price increment is to watch real resting prices and see what
granularity they land on. THAT YIELDS AN INFERRED VALUE, NOT A VERIFIED ONE, and
the must-confirm discipline says it must be recorded as `inferred` and never
promoted to `verified` without the venue saying so.
"""
from __future__ import annotations

import base64
import json
import os
import pathlib
import socket
import ssl
import struct
import sys
import time

HOST = os.environ.get("TM_WS_HOST", "api.truemarkets.co")
PATH = os.environ.get("TM_WS_PATH", "/v1/defi/market")
UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/140.0 Safari/537.36")


def connect(token: str) -> ssl.SSLSocket:
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {PATH} HTTP/1.1\r\n"
        f"Host: {HOST}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        f"User-Agent: {UA}\r\n"
        f"Origin: https://{HOST}\r\n"
        f"Authorization: Bearer {token}\r\n"
        "\r\n"
    ).encode()

    raw = socket.create_connection((HOST, 443), timeout=30)
    sock = ssl.create_default_context().wrap_socket(raw, server_hostname=HOST)
    sock.sendall(request)

    buffer = b""
    while b"\r\n\r\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise SystemExit("connection closed during handshake")
        buffer += chunk
    head, _, rest = buffer.partition(b"\r\n\r\n")
    status = head.split(b"\r\n", 1)[0].decode()
    print(f"handshake: {status}")
    if b" 101 " not in head.split(b"\r\n", 1)[0]:
        print(head.decode("utf-8", "replace")[:400])
        print(rest[:300].decode("utf-8", "replace"))
        raise SystemExit("upgrade refused")
    sock._leftover = rest  # type: ignore[attr-defined]
    return sock


def send_text(sock: ssl.SSLSocket, text: str) -> None:
    payload = text.encode()
    header = bytearray([0x81])
    mask = os.urandom(4)
    length = len(payload)
    if length < 126:
        header.append(0x80 | length)
    elif length < (1 << 16):
        header.append(0x80 | 126)
        header += struct.pack(">H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", length)
    header += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(bytes(header) + masked)


def frames(sock: ssl.SSLSocket, deadline: float):
    buffer = getattr(sock, "_leftover", b"")

    def need(count: int) -> bytes:
        nonlocal buffer
        while len(buffer) < count:
            sock.settimeout(max(0.5, deadline - time.time()))
            chunk = sock.recv(65536)
            if not chunk:
                raise StopIteration
            buffer += chunk
        head, buffer = buffer[:count], buffer[count:]
        return head

    while time.time() < deadline:
        try:
            first = need(2)
        except (StopIteration, socket.timeout, ssl.SSLError):
            return
        opcode = first[0] & 0x0F
        length = first[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", need(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", need(8))[0]
        try:
            payload = need(length) if length else b""
        except (StopIteration, socket.timeout, ssl.SSLError):
            return
        if opcode == 0x8:
            print("server closed the socket")
            return
        if opcode == 0x9:
            sock.sendall(b"\x8a\x80" + os.urandom(4))
            continue
        if opcode in (0x1, 0x2):
            yield payload


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    token = pathlib.Path(sys.argv[1]).read_text().strip()
    if token.startswith("{") or token.count(".") != 2:
        raise SystemExit("that is not a token; mint one with tools/mint-token.py first")
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 25

    sock = connect(token)

    sol = "495e07ac-fa3e-4179-85b7-b1e8cece3dc4"
    topics = [
        "zzzz:" + sol,
        "orderbook:zzzz",
        "defi:orderbook:" + sol,
        "orderbook:defi:" + sol,
        "orderbook:SOL",
        "orderbook:SOL-USDC",
        "orderbook:solana:" + sol,
        "a:b:c:d",
        "a",
        "a:b",
    ]
    attempts = [{"type": "subscribe", "topics": [topic]} for topic in topics]
    for attempt in attempts:
        send_text(sock, json.dumps(attempt))
        print("sent:", json.dumps(attempt))

    deadline = time.time() + seconds
    log = out / "ws-frames.jsonl"
    count = 0
    with log.open("w") as handle:
        for payload in frames(sock, deadline):
            count += 1
            text = payload.decode("utf-8", "replace")
            handle.write(text + "\n")
            if count <= 12:
                print(f"  <- {text[:220]}")
    print(f"\n{count} frames written to {log}")
    if count == 0:
        print("Nothing arrived. Either the subscribe envelope is wrong - which is")
        print("itself the finding - or the socket needs something we have not sent.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
