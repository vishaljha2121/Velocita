"""The interlock must fail closed, and must fail on realistic mistakes."""

from __future__ import annotations

import textwrap
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from velocita_model import safety
from velocita_model.adapters.base import ReadOnlyHttp, TransportRefused
from velocita_model.contracts import codec


class LiveTradingFlag(unittest.TestCase):
    def test_flag_is_false(self) -> None:
        self.assertIs(safety.LIVE_TRADING, False)


class StructuralGuarantees(unittest.TestCase):
    def test_codec_cannot_author_an_external_effect(self) -> None:
        safety.assert_no_effect_encoder()
        for forbidden in ("OrderEffect", "encode_order_effect", "VenueObservation"):
            self.assertFalse(hasattr(codec, forbidden), f"codec must not expose {forbidden}")

    def test_the_live_package_passes_its_own_audit(self) -> None:
        self.assertEqual(safety.audit_tree(), [])


class StaticAudit(unittest.TestCase):
    def _audit(self, source: str) -> list[safety.Finding]:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.py"
            path.write_text(textwrap.dedent(source), encoding="utf-8")
            return safety.audit_tree(Path(directory))

    def test_signing_import_is_caught(self) -> None:
        findings = self._audit("import eth_account\n")
        self.assertTrue(findings)
        self.assertIn("eth_account", str(findings[0]))

    def test_submodule_import_is_caught(self) -> None:
        self.assertTrue(self._audit("from web3.middleware import geth_poa\n"))

    def test_order_management_definition_is_caught(self) -> None:
        self.assertTrue(self._audit("def submit_order(x):\n    return x\n"))

    def test_signing_attribute_access_is_caught(self) -> None:
        self.assertTrue(self._audit("def f(c):\n    return c.send_raw_transaction(b'')\n"))

    def test_ordinary_research_code_passes(self) -> None:
        self.assertEqual(
            self._audit(
                """
                from decimal import Decimal

                def spread_bps(bid: Decimal, ask: Decimal) -> Decimal:
                    mid = (bid + ask) / 2
                    return (ask - bid) / mid * Decimal(10_000)
                """
            ),
            [],
        )

    def test_a_docstring_naming_the_vocabulary_does_not_trip_the_audit(self) -> None:
        self.assertEqual(
            self._audit('"""This module must never sign_transaction or submit_order."""\n'),
            [],
        )

    def test_an_empty_tree_is_a_failure_not_a_pass(self) -> None:
        with TemporaryDirectory() as directory:
            with self.assertRaises(safety.SafetyViolation):
                safety.audit_tree(Path(directory))

    def test_unparseable_source_is_a_finding(self) -> None:
        self.assertTrue(self._audit("def broken(:\n"))


class ConfigInterlock(unittest.TestCase):
    def test_credential_shaped_keys_are_refused(self) -> None:
        for key in ("wallet.private_key", "venue.api_secret", "runtime.live_trading", "signer"):
            with self.subTest(key=key):
                with self.assertRaises(safety.SafetyViolation):
                    safety.assert_config_is_passive([key])

    def test_ordinary_keys_are_accepted(self) -> None:
        safety.assert_config_is_passive(["source.venue", "pair.tick_size", "strategy.max_inventory_quote"])


class TransportGuard(unittest.TestCase):
    def setUp(self) -> None:
        self.http = ReadOnlyHttp("https://example.invalid")

    def test_order_path_is_refused_before_any_network_call(self) -> None:
        with self.assertRaises(TransportRefused):
            self.http.get("/v1/conductor/orders")

    def test_every_state_changing_fragment_is_refused(self) -> None:
        for fragment in ("/orders", "/cancel", "/swap", "/approve", "/transaction", "/withdraw"):
            with self.subTest(fragment=fragment):
                with self.assertRaises(TransportRefused):
                    self.http.get(f"/v1{fragment}")

    def test_post_is_refused_unless_explicitly_allowlisted(self) -> None:
        with self.assertRaises(TransportRefused):
            self.http._guard("/v1/market/book", "POST")

    def test_allowlisted_post_is_permitted(self) -> None:
        client = ReadOnlyHttp("https://example.invalid", post_allowlist=("/v1/market/book",))
        client._guard("/v1/market/book", "POST")

    def test_non_get_non_post_methods_are_refused(self) -> None:
        for method in ("PUT", "DELETE", "PATCH"):
            with self.subTest(method=method):
                with self.assertRaises(TransportRefused):
                    self.http._guard("/v1/market/book", method)


if __name__ == "__main__":
    unittest.main()
