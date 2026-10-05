"""Minimal read-only RFC 6455 client for the True Markets market-data stream.

Stdlib only, like the rest of this package. The guard is structural, the same
idea as `ReadOnlyHttp`: the client can reach one allowlisted endpoint, and the
only data frame it can emit is a subscribe message it builds itself from
validated topic names. Pong and close are protocol replies. There is no general
send method, so no future call site can push arbitrary bytes to the venue.

Every inbound message is bounded (`max_message_bytes`) and the bound is checked
against the header before any payload is buffered.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import socket
import ssl
import struct
from typing import Callable, Sequence

OP_CONTINUATION = 0x0
OP_TEXT = 0x1
OP_BINARY = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA
_CONTROL = (OP_CLOSE, OP_PING, OP_PONG)
_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

ALLOWED_ENDPOINTS: frozenset[tuple[str, str]] = frozenset(
    {("api.truemarkets.co", "/v1/defi/market"), ("api.uat.truemarkets.co", "/v1/defi/market")}
)

_TOPIC = re.compile(
    r"^(price_candles|trending_assets|surging_assets|reference_prices|all"
    r"|orderbook\.[a-z0-9-]{1,40}\.[A-Za-z0-9]{1,20}-[A-Za-z0-9]{1,20})$"
)


class ProtocolError(RuntimeError):
    pass


class HandshakeRefused(ProtocolError):
    def __init__(self, status: int | None, message: str) -> None:
        super().__init__(message)
        self.status = status


class WebSocketRefused(RuntimeError):
    pass


def accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + _GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def encode_client_frame(opcode: int, payload: bytes, *, mask_key: bytes | None = None) -> bytes:
    mask_key = os.urandom(4) if mask_key is None else mask_key
    if len(mask_key) != 4:
        raise ValueError("mask key must be 4 bytes")
    length = len(payload)
    if opcode in _CONTROL and length > 125:
        raise ValueError("control frame payload exceeds 125 bytes")
    header = bytes([0x80 | opcode])
    if length < 126:
        header += bytes([0x80 | length])
    elif length < 1 << 16:
        header += bytes([0x80 | 126]) + struct.pack("!H", length)
    else:
        header += bytes([0x80 | 127]) + struct.pack("!Q", length)
    masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
    return header + mask_key + masked


class FrameDecoder:
    def __init__(self, *, max_message_bytes: int, expect_masked: bool = False) -> None:
        self._max = max_message_bytes
        self._expect_masked = expect_masked
        self._buffer = bytearray()
        self._message_opcode: int | None = None
        self._message = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, bytes]]:
        self._buffer += data
        out: list[tuple[int, bytes]] = []
        while True:
            frame = self._take_frame()
            if frame is None:
                return out
            fin, opcode, payload = frame
            if opcode in _CONTROL:
                if not fin or len(payload) > 125:
                    raise ProtocolError("fragmented or oversized control frame")
                out.append((opcode, payload))
                continue
            if opcode == OP_CONTINUATION:
                if self._message_opcode is None:
                    raise ProtocolError("continuation frame without a message start")
            elif opcode in (OP_TEXT, OP_BINARY):
                if self._message_opcode is not None:
                    raise ProtocolError("new data message before the previous one finished")
                self._message_opcode = opcode
            else:
                raise ProtocolError(f"reserved opcode {opcode:#x}")
            if len(self._message) + len(payload) > self._max:
                raise ProtocolError("message exceeds the configured bound")
            self._message += payload
            if fin:
                out.append((self._message_opcode, bytes(self._message)))
                self._message_opcode = None
                self._message = bytearray()

    def _take_frame(self) -> tuple[bool, int, bytes] | None:
        buf = self._buffer
        if len(buf) < 2:
            return None
        if buf[0] & 0x70:
            raise ProtocolError("reserved bits set without a negotiated extension")
        fin, opcode = bool(buf[0] & 0x80), buf[0] & 0x0F
        masked, length, offset = bool(buf[1] & 0x80), buf[1] & 0x7F, 2
        if masked != self._expect_masked:
            raise ProtocolError("unexpected frame masking")
        if length == 126:
            if len(buf) < 4:
                return None
            length, offset = struct.unpack("!H", buf[2:4])[0], 4
        elif length == 127:
            if len(buf) < 10:
                return None
            length, offset = struct.unpack("!Q", buf[2:10])[0], 10
        if length > self._max or (opcode not in _CONTROL and len(self._message) + length > self._max):
            raise ProtocolError("frame exceeds the configured bound")
        mask = b""
        if masked:
            if len(buf) < offset + 4:
                return None
            mask, offset = bytes(buf[offset:offset + 4]), offset + 4
        if len(buf) < offset + length:
            return None
        payload = bytes(buf[offset:offset + length])
        del buf[:offset + length]
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return fin, opcode, payload


def subscribe_message(topics: Sequence[str]) -> str:
    if not topics:
        raise WebSocketRefused("no topics")
    for topic in topics:
        if not isinstance(topic, str) or not _TOPIC.match(topic):
            raise WebSocketRefused(f"topic {topic!r} is not a known market-data topic")
    return json.dumps({"type": "subscribe", "topics": list(topics)}, separators=(",", ":"))


class ReadOnlyMarketSocket:
    __slots__ = ("_host", "_path", "_timeout_s", "_max", "_connect", "_sock", "_decoder", "_pending", "_closed")

    def __init__(
        self,
        host: str,
        path: str,
        *,
        timeout_s: float = 30.0,
        max_message_bytes: int = 4 << 20,
        connect: Callable[[], socket.socket] | None = None,
    ) -> None:
        if (host, path) not in ALLOWED_ENDPOINTS:
            raise WebSocketRefused(f"wss://{host}{path} is not an allowlisted market-data endpoint")
        self._host, self._path, self._timeout_s, self._max = host, path, timeout_s, max_message_bytes
        self._connect = connect or self._tls_connect
        self._sock: socket.socket | None = None
        self._decoder = FrameDecoder(max_message_bytes=max_message_bytes)
        self._pending: list[tuple[int, bytes]] = []
        self._closed = False

    def _tls_connect(self) -> socket.socket:
        raw = socket.create_connection((self._host, 443), timeout=self._timeout_s)
        return ssl.create_default_context().wrap_socket(raw, server_hostname=self._host)

    def open(self) -> None:
        sock = self._connect()
        try:
            self._handshake(sock)
        except BaseException:
            sock.close()
            raise

    def _handshake(self, sock: socket.socket) -> None:
        sock.settimeout(self._timeout_s)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        sock.sendall((f"GET {self._path} HTTP/1.1\r\nHost: {self._host}\r\nUpgrade: websocket\r\n"
                      f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
                      "User-Agent: velocita-model/readonly\r\n\r\n").encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                raise ProtocolError("connection closed during the handshake")
            response += chunk
            if len(response) > 16384:
                raise ProtocolError("handshake response too large")
        head, _, rest = response.partition(b"\r\n\r\n")
        lines = head.decode("iso-8859-1").split("\r\n")
        if not lines[0].startswith("HTTP/1.1 101"):
            status = lines[0].split(" ", 2)[1] if lines[0].count(" ") >= 1 else ""
            raise HandshakeRefused(int(status) if status.isdigit() else None, f"handshake refused: {lines[0]!r}")
        headers = {k.strip().lower(): v.strip() for k, _, v in (line.partition(":") for line in lines[1:])}
        if headers.get("sec-websocket-accept") != accept_key(key):
            raise ProtocolError("handshake accept key mismatch")
        self._sock = sock
        if rest:
            self._pending += self._decoder.feed(rest)

    def subscribe(self, topics: Sequence[str]) -> None:
        self._write(encode_client_frame(OP_TEXT, subscribe_message(topics).encode("utf-8")))

    def recv_text(self) -> str | None:
        while not self._closed:
            while self._pending:
                opcode, payload = self._pending.pop(0)
                if opcode == OP_PING:
                    self._write(encode_client_frame(OP_PONG, payload))
                elif opcode == OP_CLOSE:
                    self._reply_close(payload[:2])
                    return None
                elif opcode == OP_TEXT:
                    return payload.decode("utf-8")
                elif opcode == OP_BINARY:
                    raise ProtocolError("unexpected binary message on a JSON stream")
            assert self._sock is not None, "open() first"
            chunk = self._sock.recv(65536)
            if not chunk:
                self._closed = True
                return None
            self._pending += self._decoder.feed(chunk)
        return None

    def _reply_close(self, code: bytes) -> None:
        if not self._closed:
            self._closed = True
            try:
                self._write(encode_client_frame(OP_CLOSE, code))
            except OSError:
                pass

    def _write(self, frame: bytes) -> None:
        assert self._sock is not None, "open() first"
        self._sock.sendall(frame)

    def close(self) -> None:
        if self._sock is None:
            return
        self._reply_close(struct.pack("!H", 1000))
        try:
            self._sock.close()
        finally:
            self._sock = None
