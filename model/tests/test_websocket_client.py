"""Read-only WebSocket client.

Expected bytes come from RFC 6455 itself (section 1.3 handshake, section 5.7
frame examples), never from the encoder under test.
"""

from __future__ import annotations

import json
import socket
import threading
import unittest

from velocita_model.adapters import websocket as ws


class Rfc6455Examples(unittest.TestCase):
    def test_accept_key_matches_rfc_example(self) -> None:
        self.assertEqual(ws.accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_masked_hello_matches_rfc_bytes(self) -> None:
        frame = ws.encode_client_frame(ws.OP_TEXT, b"Hello", mask_key=bytes.fromhex("37fa213d"))
        self.assertEqual(frame, bytes.fromhex("818537fa213d7f9f4d5158"))

    def test_decoder_reads_unmasked_hello(self) -> None:
        decoder = ws.FrameDecoder(max_message_bytes=1024)
        self.assertEqual(decoder.feed(bytes.fromhex("810548656c6c6f")), [(ws.OP_TEXT, b"Hello")])

    def test_decoder_joins_fragments_split_anywhere(self) -> None:
        decoder = ws.FrameDecoder(max_message_bytes=1024)
        out = []
        for byte in bytes.fromhex("010348656c80026c6f"):
            out += decoder.feed(bytes([byte]))
        self.assertEqual(out, [(ws.OP_TEXT, b"Hello")])

    def test_decoder_reads_16_and_64_bit_lengths(self) -> None:
        decoder = ws.FrameDecoder(max_message_bytes=70_000)
        self.assertEqual(decoder.feed(bytes.fromhex("827e0100") + b"\x00" * 256), [(ws.OP_BINARY, b"\x00" * 256)])
        big = bytes.fromhex("827f0000000000010000") + b"\x01" * 65536
        self.assertEqual(decoder.feed(big), [(ws.OP_BINARY, b"\x01" * 65536)])

    def test_control_frame_interleaved_in_a_fragmented_message(self) -> None:
        decoder = ws.FrameDecoder(max_message_bytes=1024)
        ping = bytes.fromhex("890548656c6c6f")
        out = decoder.feed(bytes.fromhex("010348656c") + ping + bytes.fromhex("80026c6f"))
        self.assertEqual(out, [(ws.OP_PING, b"Hello"), (ws.OP_TEXT, b"Hello")])


class DecoderRefusals(unittest.TestCase):
    def _refuses(self, data: bytes, max_message_bytes: int = 1024) -> None:
        with self.assertRaises(ws.ProtocolError):
            ws.FrameDecoder(max_message_bytes=max_message_bytes).feed(data)

    def test_masked_server_frame(self) -> None:
        self._refuses(bytes.fromhex("818537fa213d7f9f4d5158"))

    def test_reserved_bits(self) -> None:
        self._refuses(bytes.fromhex("c10548656c6c6f"))

    def test_oversized_message_is_refused_before_buffering(self) -> None:
        self._refuses(bytes.fromhex("827f0000000100000000"), max_message_bytes=1 << 20)

    def test_fragments_that_together_exceed_the_bound(self) -> None:
        self._refuses(bytes.fromhex("0103414141") + bytes.fromhex("800341414141"), max_message_bytes=5)

    def test_fragmented_control_frame(self) -> None:
        self._refuses(bytes.fromhex("0900"))

    def test_continuation_without_a_start(self) -> None:
        self._refuses(bytes.fromhex("80026c6f"))


class ReadOnlyGuards(unittest.TestCase):
    def test_only_the_market_data_endpoint_is_reachable(self) -> None:
        for host, path in (("api.truemarkets.co", "/v1/cefi/market"), ("evil.example", "/v1/defi/market"),
                           ("api.truemarkets.co", "/v1/defi/orders")):
            with self.assertRaises(ws.WebSocketRefused):
                ws.ReadOnlyMarketSocket(host, path)

    def test_subscribe_topics_are_validated(self) -> None:
        for bad in ("orders", "orderbook", "orderbook.hyperliquid-spot", "price_candles;drop", ""):
            with self.assertRaises(ws.WebSocketRefused):
                ws.subscribe_message([bad])
        message = json.loads(ws.subscribe_message(["reference_prices", "orderbook.hyperliquid-spot.HYPE-USDC"]))
        self.assertEqual(message, {"type": "subscribe",
                                   "topics": ["reference_prices", "orderbook.hyperliquid-spot.HYPE-USDC"]})

    def test_the_client_exposes_no_general_send(self) -> None:
        for name in ("send", "send_text", "send_message", "write"):
            self.assertFalse(hasattr(ws.ReadOnlyMarketSocket, name), f"client must not expose .{name}")


class HandshakeRefusals(unittest.TestCase):
    def _open_against(self, reply: bytes, reason: str) -> None:
        client_side, server_side = socket.socketpair()

        def server() -> None:
            request = b""
            while b"\r\n\r\n" not in request:
                request += server_side.recv(4096)
            server_side.sendall(reply)

        thread = threading.Thread(target=server)
        thread.start()
        try:
            with self.assertRaisesRegex(ws.ProtocolError, reason):
                ws.ReadOnlyMarketSocket("api.truemarkets.co", "/v1/defi/market",
                                        connect=lambda: client_side, timeout_s=5).open()
        finally:
            thread.join(timeout=10)
            client_side.close()
            server_side.close()

    def test_wrong_accept_key(self) -> None:
        self._open_against(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           b"Connection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
                           "accept key mismatch")

    def test_non_upgrade_response(self) -> None:
        self._open_against(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "handshake refused")

    def test_a_503_is_refused_with_its_status_and_the_socket_is_closed(self) -> None:
        client_side, server_side = socket.socketpair()

        def server() -> None:
            request = b""
            while b"\r\n\r\n" not in request:
                request += server_side.recv(4096)
            server_side.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")

        thread = threading.Thread(target=server)
        thread.start()
        try:
            with self.assertRaises(ws.HandshakeRefused) as refused:
                ws.ReadOnlyMarketSocket("api.truemarkets.co", "/v1/defi/market",
                                        connect=lambda: client_side, timeout_s=5).open()
            self.assertEqual(refused.exception.status, 503)
            self.assertIsInstance(refused.exception, ws.ProtocolError)
            self.assertEqual(client_side.fileno(), -1, "a refused handshake leaked its socket")
        finally:
            thread.join(timeout=10)
            client_side.close()
            server_side.close()


class LoopbackSession(unittest.TestCase):
    def test_session(self) -> None:
        client_side, server_side = socket.socketpair()
        received: list[bytes] = []

        def server() -> None:
            request = b""
            while b"\r\n\r\n" not in request:
                request += server_side.recv(4096)
            key = [line.split(b": ", 1)[1] for line in request.split(b"\r\n")
                   if line.lower().startswith(b"sec-websocket-key:")][0].decode()
            server_side.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                 "Connection: Upgrade\r\nSec-WebSocket-Accept: " + ws.accept_key(key) +
                                 "\r\n\r\n").encode())
            server_side.sendall(bytes.fromhex("890548656c6c6f"))
            server_side.sendall(bytes.fromhex("010348656c") + bytes.fromhex("80026c6f"))
            server_side.sendall(bytes.fromhex("880203e8"))
            server_side.settimeout(5)
            try:
                while True:
                    chunk = server_side.recv(4096)
                    if not chunk:
                        break
                    received.append(chunk)
            except OSError:
                pass

        thread = threading.Thread(target=server)
        thread.start()
        client = ws.ReadOnlyMarketSocket("api.truemarkets.co", "/v1/defi/market",
                                         connect=lambda: client_side, timeout_s=5)
        client.open()
        client.subscribe(["reference_prices"])
        self.assertEqual(client.recv_text(), "Hello")
        self.assertIsNone(client.recv_text())
        client.close()
        thread.join(timeout=10)
        server_side.close()

        frames = ws.FrameDecoder(max_message_bytes=4096, expect_masked=True)
        opcodes = [op for op, _ in frames.feed(b"".join(received))]
        self.assertEqual(opcodes, [ws.OP_TEXT, ws.OP_PONG, ws.OP_CLOSE])
        payloads = ws.FrameDecoder(max_message_bytes=4096, expect_masked=True).feed(b"".join(received))
        self.assertEqual(json.loads(payloads[0][1]), {"type": "subscribe", "topics": ["reference_prices"]})
        self.assertEqual(payloads[1][1], b"Hello")


if __name__ == "__main__":
    unittest.main()
