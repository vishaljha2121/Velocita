"""Structural safety interlock.

A boolean named LIVE_TRADING is a weak guarantee: it can be flipped. The real
guarantee in this system is structural, and it comes from ADR 0001 rather than
from this file:

  1. The only path from an intent to a venue is a committed outbox effect
     consumed by the C++ execution gateway. That gateway consumes `OrderEffect`
     records produced by the Raft reducer. It does not accept input from this
     process, on any transport.
  2. This package cannot author an `OrderEffect`. `contracts/codec.py`
     deliberately implements no encoder for it. There is no environment
     variable, config key, or flag that adds one.
  3. Everything this package emits - `DecisionFrame`, `PolicyArtifact` - is
     advisory by contract. A frame is *proposed*; the cluster performs the final
     authoritative evaluation and may reject it. A published policy is not an
     activated policy.
  4. Phase 3C retains an evaluation mode on every decision, and Phase 4 must
     refuse to build a risk-increasing effect from a shadow decision. Shadow is
     an authority property, not a logging preference.

So the checks below are defence in depth, not the primary control. They exist to
catch the realistic failure - somebody pastes a wallet snippet into this repo
during a late-night leaderboard push - rather than to be the thing standing
between the research process and the chain.

The audit fails closed: an unreadable source tree is a failure, not a pass.
"""

from __future__ import annotations

import ast
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Final, Iterable

LIVE_TRADING: Final[bool] = False

#: Import names that would give this process the ability to sign or broadcast.
BANNED_IMPORTS: Final[frozenset[str]] = frozenset(
    {
        "web3",
        "eth_account",
        "eth_keys",
        "eth_keyfile",
        "solana",
        "solders",
        "anchorpy",
        "bip_utils",
        "mnemonic",
        "hdwallet",
        "ecdsa",
        "coincurve",
        "ccxt",
        "ccxt.pro",
        "bitcoinlib",
        "nacl.signing",
    }
)

#: Identifier substrings that indicate order-management or signing capability.
#: Matched against attribute access and function definitions, not raw text, so a
#: docstring explaining what is forbidden does not trip the audit.
BANNED_IDENTIFIER_FRAGMENTS: Final[tuple[str, ...]] = (
    "private_key",
    "privatekey",
    "seed_phrase",
    "seedphrase",
    "mnemonic",
    "keystore",
    "sign_transaction",
    "sign_typed_data",
    "signtransaction",
    "send_raw_transaction",
    "send_transaction",
    "sendtransaction",
    "broadcast_tx",
    "submit_order",
    "place_order",
    "create_order",
    "cancel_order",
    "amend_order",
    "replace_order",
    "approve_token",
    "set_allowance",
    "swap_exact",
)

#: Config keys that imply the operator intends trading capability. Presence of
#: any of these is a fail-closed condition even when the value is empty.
BANNED_CONFIG_KEYS: Final[tuple[str, ...]] = (
    "private_key",
    "secret_key",
    "seed_phrase",
    "mnemonic",
    "wallet",
    "keystore",
    "api_secret",
    "trading_key",
    "signer",
    "live_trading",
)


class SafetyViolation(RuntimeError):
    """Raised when the process cannot prove it is incapable of trading."""


@dataclass(frozen=True, slots=True)
class Finding:
    path: Path
    line: int
    detail: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.detail}"


def _module_root(name: str) -> str:
    return name.split(".", 1)[0]


def _audit_source(path: Path, source: str) -> list[Finding]:
    findings: list[Finding] = []
    try:
        tree = ast.parse(source, filename=str(path))
    except SyntaxError as exc:
        return [Finding(path, exc.lineno or 0, f"source does not parse: {exc.msg}")]

    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                if _module_root(alias.name) in BANNED_IMPORTS or alias.name in BANNED_IMPORTS:
                    findings.append(Finding(path, node.lineno, f"banned import {alias.name!r}"))
        elif isinstance(node, ast.ImportFrom):
            module = node.module or ""
            if _module_root(module) in BANNED_IMPORTS or module in BANNED_IMPORTS:
                findings.append(Finding(path, node.lineno, f"banned import from {module!r}"))
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            lowered = node.name.lower()
            for fragment in BANNED_IDENTIFIER_FRAGMENTS:
                if fragment in lowered:
                    findings.append(Finding(path, node.lineno, f"banned definition {node.name!r}"))
        elif isinstance(node, ast.Attribute):
            lowered = node.attr.lower()
            for fragment in BANNED_IDENTIFIER_FRAGMENTS:
                if fragment in lowered:
                    findings.append(Finding(path, node.lineno, f"banned attribute access .{node.attr}"))
    return findings


def package_root() -> Path:
    return Path(__file__).resolve().parent


def audit_tree(root: Path | None = None) -> list[Finding]:
    """Audit every Python source file under `root` for trading capability."""
    root = root or package_root()
    sources = sorted(root.rglob("*.py"))
    if not sources:
        raise SafetyViolation(f"safety audit found no sources under {root}; refusing to pass vacuously")

    findings: list[Finding] = []
    for path in sources:
        if path.name == "safety.py" and path.parent == package_root():
            continue  # this module names the forbidden vocabulary by design
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise SafetyViolation(f"safety audit could not read {path}: {exc}") from exc
        findings.extend(_audit_source(path, source))
    return findings


def assert_no_effect_encoder() -> None:
    """Prove the codec cannot author an external effect.

    This is the check that actually matters. `OrderEffect` is the only value the
    execution gateway acts on, and the model has no vocabulary for it.
    """
    from . import contracts

    codec = sys.modules.get("velocita_model.contracts.codec")
    if codec is None:
        from .contracts import codec  # noqa: F401
        codec = sys.modules["velocita_model.contracts.codec"]

    for forbidden in ("OrderEffect", "encode_order_effect", "VenueObservation"):
        if hasattr(codec, forbidden):
            raise SafetyViolation(
                f"contracts.codec exposes {forbidden!r}; the model must not be able to "
                "author values the execution gateway acts on"
            )
    del contracts


def assert_config_is_passive(keys: Iterable[str]) -> None:
    """Fail closed on any configuration key that implies trading capability."""
    offending = sorted({key for key in keys if any(banned in key.lower() for banned in BANNED_CONFIG_KEYS)})
    if offending:
        raise SafetyViolation(
            "configuration implies trading capability and was refused: " + ", ".join(offending)
        )


def enforce(root: Path | None = None) -> None:
    """Run every interlock. Call once at process start, before any collector."""
    if LIVE_TRADING:
        raise SafetyViolation("LIVE_TRADING is true; this package has no live path and must not claim one")
    assert_no_effect_encoder()
    findings = audit_tree(root)
    if findings:
        raise SafetyViolation(
            "static safety audit failed:\n  " + "\n  ".join(str(f) for f in findings)
        )
