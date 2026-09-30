"""Offline contract tests: no authentication, network, or persistent output."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import unittest
from unittest.mock import mock_open, patch

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    return runpy.run_path(str(ROOT / "tools" / name))


class MarkupTests(unittest.TestCase):
    def run_capture(self, sizes=None, reference=None):
        module = load("measure-markup.py")
        module["main"].__globals__["fetch_reference"] = lambda symbol: reference or {"candles": []}
        calls = []
        class Session:
            def rpc(self, *args, **kwargs):
                return 200, b'{"result":{}}'
            def call(self, name, **arguments):
                calls.append((name, arguments))
                if name.startswith("login_"):
                    return {"access_token": "SECRET_AUTH_TOKEN"}
                return {"route": {"outAmount": "999"}, "output_amount": "0.1"}
        module["main"].__globals__["Session"] = Session
        module["main"].__globals__["fetch_catalog"] = lambda: MarkupTests.CATALOG
        output = io.StringIO()
        with patch("builtins.input", return_value="000000"), \
             patch("builtins.open", mock_open()), \
             patch("urllib.request.urlopen", side_effect=AssertionError("network forbidden")), \
             patch("time.sleep"), contextlib.redirect_stdout(output):
            result = module["main"](["owner@example.invalid"] + (sizes or ["25"]))
        return result, calls, output.getvalue()

    def test_login_sends_both_required_fields(self):
        _, calls, _ = self.run_capture()
        arguments = next(args for name, args in calls if name == "login_verify_code")
        self.assertEqual(arguments, {"email": "owner@example.invalid", "code": "000000"})

    def test_empty_code_is_asked_again_not_sent(self):
        # A stray Enter queued before the prompt must not be spent as the code.
        module = load("measure-markup.py")
        calls = []
        class Session:
            def rpc(self, *args, **kwargs):
                return 200, b'{"result":{}}'
            def call(self, name, **arguments):
                calls.append((name, arguments))
                return {"route": {}}
        module["main"].__globals__["Session"] = Session
        module["main"].__globals__["fetch_catalog"] = lambda: MarkupTests.CATALOG
        module["main"].__globals__["fetch_reference"] = lambda symbol: {"candles": []}
        with patch("builtins.input", side_effect=["", "  ", "123456"]), patch("time.sleep"), \
             contextlib.redirect_stdout(io.StringIO()):
            module["main"](["owner@example.invalid", "25"])
        verify = [args for name, args in calls if name == "login_verify_code"]
        self.assertEqual(verify, [{"email": "owner@example.invalid", "code": "123456"}])

    def _scripted_quotes(self, quote_results):
        module = load("measure-markup.py")
        results = list(quote_results)
        class Session:
            def rpc(self, *args, **kwargs):
                return 200, b'{"result":{}}'
            def call(self, name, **arguments):
                if name.startswith("login_"):
                    return {"ok": True}
                return results.pop(0) if results else {"structuredContent": {"out": "0"}}
        module["main"].__globals__["Session"] = Session
        module["main"].__globals__["fetch_catalog"] = lambda: MarkupTests.CATALOG
        module["main"].__globals__["fetch_reference"] = lambda symbol: {"candles": []}
        output = io.StringIO()
        with patch("builtins.input", return_value="123456"), patch("time.sleep"), \
             contextlib.redirect_stdout(output):
            code = module["main"](["owner@example.invalid", "25"])
        return code, output.getvalue()

    def test_a_refused_quote_is_retried(self):
        # The MCP server reports rate limiting as a tool error; a lone retry succeeds.
        code, output = self._scripted_quotes([{"_error": "RPC failed", "detail": "invalid params"},
                                              {"_error": "RPC failed", "detail": "invalid params"},
                                              {"structuredContent": {"out": "0.2"}}])
        self.assertEqual(code, 2)
        self.assertIn('"out": "0.2"', output)

    def test_final_failure_shows_a_redacted_reason(self):
        reason = "rejected for owner@example.invalid token eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJ4In0.abc qty too small"
        code, output = self._scripted_quotes([{"_error": "tool failed", "detail": reason}] * 3)
        self.assertEqual(code, 1)
        self.assertIn("qty too small", output)
        self.assertNotIn("owner@example.invalid", output)
        self.assertNotIn("eyJhbGciOi", output)

    CATALOG = {"data": [
        {"symbol": "SOL", "chain": "solana", "address": "So11111111111111111111111111111111111111112", "tradeable": True},
        {"symbol": "SOL", "chain": "base", "address": "0xwrapped", "tradeable": True},
        {"symbol": "USDC", "chain": "solana", "address": "EPj", "tradeable": False},
        {"symbol": "CBBTC", "chain": "base", "address": "0xcbbtc", "tradeable": True}]}

    def test_base_asset_is_the_catalog_address_not_the_symbol(self):
        # 2026-09-28: get_quote answered "resolve base asset: 404" for base_asset "SOL".
        module = load("measure-markup.py")
        self.assertEqual(module["resolve_address"](self.CATALOG, "SOL", "solana"),
                         "So11111111111111111111111111111111111111112")

    def test_unknown_or_untradeable_asset_is_refused(self):
        module = load("measure-markup.py")
        for symbol, chain in (("USDC", "solana"), ("DOGE", "solana")):
            with self.assertRaises(SystemExit):
                module["resolve_address"](self.CATALOG, symbol, chain)

    def test_each_quote_carries_a_reference_price(self):
        _, _, output = self.run_capture(reference={
            "symbol": "SOL", "candles": [{"interval": "1h", "closePrice": "1"}, {"interval": "30s", "closePrice": "118.57"}]})
        self.assertIn('"reference_30s_close": "118.57"', output)

    def test_authentication_response_is_never_printed(self):
        _, _, output = self.run_capture()
        self.assertNotIn("SECRET_AUTH_TOKEN", output)

    def test_sells_are_sized_from_the_public_reference_not_a_quote(self):
        # Reference 100 USDC per unit: a $25 sell is 0.25 base units, whatever the quotes said.
        result, calls, output = self.run_capture(reference={"candles": [{"interval": "30s", "closePrice": "100"}]})
        sells = [args for name, args in calls if name == "get_quote" and args["side"] == "sell"]
        self.assertTrue(sells, "no sell quotes requested")
        for args in sells:
            self.assertEqual((args["qty"], args["qty_unit"]), ("0.250000", "base"))
        self.assertEqual(result, 2)  # capture is not a completed measurement
        self.assertIn("unverified", output.lower())

    def test_every_asset_is_quoted_on_both_sides(self):
        _, calls, _ = self.run_capture(reference={"candles": [{"interval": "30s", "closePrice": "100"}]})
        quoted = {(args["base_asset"], args["chain"], args["side"]) for name, args in calls if name == "get_quote"}
        self.assertEqual(quoted, {("So11111111111111111111111111111111111111112", "solana", "buy"),
                                  ("So11111111111111111111111111111111111111112", "solana", "sell"),
                                  ("0xcbbtc", "base", "buy"), ("0xcbbtc", "base", "sell")})

    def test_mcp_tool_error_is_failure(self):
        module = load("measure-markup.py")
        session = module["Session"]()
        session.rpc = lambda *a, **k: (200, json.dumps({"result": {
            "isError": True, "content": [{"type": "text", "text": "rate limited"}]
        }}).encode())
        self.assertIn("_error", session.call("get_quote"))

    def test_requests_name_themselves_to_cloudflare(self):
        # Cloudflare in front of mcp.truemarkets.co answers 403 to Python's
        # default "Python-urllib/x.y" agent (observed 2026-09-28).
        module = load("measure-markup.py")
        seen = []
        class Opener:
            def open(self, request, timeout):
                seen.append(request)
                raise module["urllib"].error.URLError("stop")
        with patch.object(module["urllib"].request, "build_opener", return_value=Opener()):
            module["Session"]().rpc("initialize", {})
        agent = seen[0].get_header("User-agent")
        self.assertTrue(agent and not agent.startswith("Python-urllib"), agent)

    def test_initialize_failure_reports_the_status_only(self):
        module = load("measure-markup.py")
        class Refusing:
            def open(self, request, timeout):
                raise module["urllib"].error.HTTPError(request.full_url, 403, "Forbidden", {}, io.BytesIO(b"TOKEN"))
        module["main"].__globals__["fetch_catalog"] = lambda: MarkupTests.CATALOG
        module["main"].__globals__["fetch_reference"] = lambda symbol: {"candles": []}
        output = io.StringIO()
        with patch.object(module["urllib"].request, "build_opener", return_value=Refusing()), \
             contextlib.redirect_stdout(output):
            self.assertEqual(module["main"](["owner@example.invalid", "25"]), 1)
        self.assertIn("HTTP 403", output.getvalue())
        self.assertNotIn("TOKEN", output.getvalue())

    def test_trade_tools_never_reach_transport(self):
        session = load("measure-markup.py")["Session"]()
        with patch.object(session, "rpc") as transport:
            for name in ("trade_prepare", "trade_execute", "unknown_tool"):
                with self.assertRaises(SystemExit): session.call(name)
            transport.assert_not_called()

    def test_invalid_sizes_refused_before_login(self):
        for amount in ("0", "-1", "nan", "inf", "0.0000001"):
            with self.subTest(amount=amount):
                result, calls, _ = self.run_capture([amount])
                self.assertEqual(result, 1)
                self.assertEqual(calls, [])


class SchemaCaptureTests(unittest.TestCase):
    def test_complete_paginated_schemas_are_retained(self):
        module = load("mcp-tools.py")
        calls = []
        def rpc(url, method, params=None, **kwargs):
            calls.append((method, params))
            if method == "initialize":
                return 200, {}, b'{"result":{"serverInfo":{}}}'
            if method == "notifications/initialized":
                return 202, {}, b''
            if (params or {}).get("cursor") == "second":
                return 200, {}, b'{"result":{"tools":[{"name":"second"}]}}'
            return 200, {}, json.dumps({"result": {"tools": [{"name": "first",
                "description": "x" * 400 + "FULL_DESCRIPTION_END",
                "inputSchema": {}, "outputSchema": {"required": ["OUTPUT_PROOF"]}}],
                "nextCursor": "second"}}).encode()
        module["main"].__globals__["rpc"] = rpc
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(module["main"]("https://example.invalid/mcp"), 0)
        for required in ("second", "FULL_DESCRIPTION_END", "OUTPUT_PROOF"):
            self.assertIn(required, output.getvalue())

    def test_market_data_unknown_tool_refused_before_network(self):
        module = load("mcp-read.py")
        with patch("urllib.request.urlopen") as transport, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module["main"](["trade_prepare"]), 1)
            transport.assert_not_called()


class RoundTripTests(unittest.TestCase):
    def test_invalid_size_refused_without_quotes(self):
        module = load("roundtrip-cost.py")
        called = []
        module["main"].__globals__["quote"] = lambda *a, **k: called.append(a) or {"error": "unexpected"}
        with patch("time.sleep"), patch("builtins.open", mock_open()), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module["main"](["-1"]), 1)
        self.assertEqual(called, [])


if __name__ == "__main__":
    unittest.main()
