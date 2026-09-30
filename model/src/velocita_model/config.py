"""Configuration loading, validation, and content hashing.

TOML via stdlib `tomllib`, so the Pi needs no dependency to read its own config.
Every resolved config is hashed and the digest is written into the collector
session row. A result that cannot be attributed to an exact configuration is not
evidence, and "I think it was running the 25bps variant" is how research becomes
folklore.

Loading is where the passive interlock meets operator input: a config carrying a
key that implies trading capability is refused before any collector starts.
"""

from __future__ import annotations

import json
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator

from .contracts.digest import sha256_digest
from .safety import assert_config_is_passive


class ConfigError(ValueError):
    pass


def _walk_keys(node: Any, prefix: str = "") -> Iterator[str]:
    if isinstance(node, dict):
        for key, value in node.items():
            path = f"{prefix}.{key}" if prefix else str(key)
            yield path
            yield from _walk_keys(value, path)
    elif isinstance(node, list):
        for item in node:
            yield from _walk_keys(item, prefix)


def _require(section: dict[str, Any], key: str, where: str) -> Any:
    if key not in section:
        raise ConfigError(f"missing required key {where}.{key}")
    return section[key]


@dataclass(frozen=True, slots=True)
class PairConfig:
    """One candidate pair. Note what is NOT defaulted: tick size, minimum size,
    and fees have no fallback value. A pair whose economics we have not
    confirmed cannot be silently studied with invented parameters."""

    pair_id: str
    base: str
    quote: str
    venue_symbol: str
    tick_size: str            # decimal string, exact
    lot_size: str
    min_notional_quote: str
    maker_fee_bps: str
    taker_fee_bps: str
    gas_cost_quote: str
    instrument_id: int
    confidence: str = "unverified"
    source_url: str | None = None
    enabled: bool = True

    def __post_init__(self) -> None:
        if self.confidence not in ("verified", "inferred", "unverified"):
            raise ConfigError(f"{self.pair_id}: confidence must be verified|inferred|unverified")
        if self.instrument_id <= 0:
            raise ConfigError(f"{self.pair_id}: instrument_id must be a positive identity")


@dataclass(frozen=True, slots=True)
class StrategyConfig:
    """Shadow-simulation parameters. Every default here is conservative, and the
    reserve fraction defaults to holding back half the notional account until
    costs are measured rather than assumed."""

    account_equity_quote: str = "20.00"
    reserve_fraction: str = "0.50"
    quote_sizes_quote: tuple[str, ...] = ("1", "2", "5", "10")
    primary_quote_size_quote: str = "1"
    max_inventory_quote: str = "5.00"
    base_half_spread_bps: str = "15"
    min_half_spread_bps: str = "5"
    max_half_spread_bps: str = "150"
    vol_spread_multiplier: str = "1.5"
    inventory_skew_bps_at_limit: str = "20"
    max_market_data_age_ms: int = 2_000
    markout_horizons_ms: tuple[int, ...] = (100, 1_000, 5_000, 30_000, 60_000, 300_000)
    queue_assumptions: tuple[str, ...] = ("optimistic", "base", "conservative")

    # Research capital floors. These bound the study, not a live account.
    daily_drawdown_limit_quote: str = "0.50"
    total_drawdown_limit_quote: str = "1.00"
    equity_floor_quote: str = "19.00"


@dataclass(frozen=True, slots=True)
class SourceConfig:
    venue: str
    chain: str
    source_id: int
    venue_id: int
    account_id: int
    book_url: str | None = None
    trades_url: str | None = None
    metadata_url: str | None = None
    reference_sources: tuple[str, ...] = ()
    poll_interval_ms: int = 500
    reconnect_backoff_ms: tuple[int, ...] = (250, 500, 1_000, 2_000, 5_000, 15_000, 30_000)
    confidence: str = "unverified"

    def __post_init__(self) -> None:
        for name in ("source_id", "venue_id", "account_id"):
            if getattr(self, name) <= 0:
                raise ConfigError(f"source.{name} must be a positive identity (zero means absent)")


@dataclass(frozen=True, slots=True)
class RuntimeConfig:
    database_path: Path
    report_directory: Path
    log_directory: Path
    retention_days: int = 30
    health_interval_s: int = 30
    max_cpu_percent: float = 85.0
    max_disk_percent: float = 90.0
    max_clock_offset_ms: float = 250.0


@dataclass(frozen=True, slots=True)
class Config:
    source: SourceConfig
    pairs: tuple[PairConfig, ...]
    strategy: StrategyConfig
    runtime: RuntimeConfig
    raw: dict[str, Any] = field(repr=False, default_factory=dict)

    @property
    def digest(self) -> bytes:
        """Content hash of the resolved config, stable across key ordering."""
        return sha256_digest(json.dumps(self.raw, sort_keys=True, separators=(",", ":")).encode())

    @property
    def digest_hex(self) -> str:
        return self.digest.hex()

    def enabled_pairs(self) -> tuple[PairConfig, ...]:
        return tuple(pair for pair in self.pairs if pair.enabled)

    def pair(self, pair_id: str) -> PairConfig:
        for candidate in self.pairs:
            if candidate.pair_id == pair_id:
                return candidate
        raise KeyError(f"unknown pair {pair_id!r}")


def load_config(path: Path | str) -> Config:
    path = Path(path)
    try:
        raw = tomllib.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ConfigError(f"config not found: {path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise ConfigError(f"config {path} is not valid TOML: {exc}") from exc

    # Fail closed before anything else inspects the file.
    assert_config_is_passive(_walk_keys(raw))

    source_raw = _require(raw, "source", "config")
    source = SourceConfig(
        venue=_require(source_raw, "venue", "source"),
        chain=_require(source_raw, "chain", "source"),
        source_id=int(_require(source_raw, "source_id", "source")),
        venue_id=int(_require(source_raw, "venue_id", "source")),
        account_id=int(_require(source_raw, "account_id", "source")),
        book_url=source_raw.get("book_url"),
        trades_url=source_raw.get("trades_url"),
        metadata_url=source_raw.get("metadata_url"),
        reference_sources=tuple(source_raw.get("reference_sources", ())),
        poll_interval_ms=int(source_raw.get("poll_interval_ms", 500)),
        reconnect_backoff_ms=tuple(source_raw.get("reconnect_backoff_ms", (250, 500, 1_000, 2_000, 5_000, 15_000, 30_000))),
        confidence=source_raw.get("confidence", "unverified"),
    )

    pairs_raw = raw.get("pair", [])
    if not pairs_raw:
        raise ConfigError("config declares no [[pair]] entries")
    pairs = tuple(
        PairConfig(
            pair_id=_require(entry, "pair_id", "pair"),
            base=_require(entry, "base", "pair"),
            quote=_require(entry, "quote", "pair"),
            venue_symbol=_require(entry, "venue_symbol", "pair"),
            tick_size=str(_require(entry, "tick_size", "pair")),
            lot_size=str(_require(entry, "lot_size", "pair")),
            min_notional_quote=str(_require(entry, "min_notional_quote", "pair")),
            maker_fee_bps=str(_require(entry, "maker_fee_bps", "pair")),
            taker_fee_bps=str(_require(entry, "taker_fee_bps", "pair")),
            gas_cost_quote=str(_require(entry, "gas_cost_quote", "pair")),
            instrument_id=int(_require(entry, "instrument_id", "pair")),
            confidence=entry.get("confidence", "unverified"),
            source_url=entry.get("source_url"),
            enabled=bool(entry.get("enabled", True)),
        )
        for entry in pairs_raw
    )
    seen = {pair.pair_id for pair in pairs}
    if len(seen) != len(pairs):
        raise ConfigError("duplicate pair_id in config")
    instruments = {pair.instrument_id for pair in pairs}
    if len(instruments) != len(pairs):
        raise ConfigError("duplicate instrument_id in config; identities must be distinct")

    strategy_raw = raw.get("strategy", {})
    defaults = StrategyConfig()
    strategy = StrategyConfig(
        **{
            name: (
                tuple(strategy_raw[name])
                if name in strategy_raw and isinstance(getattr(defaults, name), tuple)
                else strategy_raw.get(name, getattr(defaults, name))
            )
            for name in defaults.__slots__
        }
    )

    runtime_raw = raw.get("runtime", {})
    runtime = RuntimeConfig(
        database_path=Path(runtime_raw.get("database_path", "var/research.db")).expanduser(),
        report_directory=Path(runtime_raw.get("report_directory", "var/reports")).expanduser(),
        log_directory=Path(runtime_raw.get("log_directory", "var/logs")).expanduser(),
        retention_days=int(runtime_raw.get("retention_days", 30)),
        health_interval_s=int(runtime_raw.get("health_interval_s", 30)),
        max_cpu_percent=float(runtime_raw.get("max_cpu_percent", 85.0)),
        max_disk_percent=float(runtime_raw.get("max_disk_percent", 90.0)),
        max_clock_offset_ms=float(runtime_raw.get("max_clock_offset_ms", 250.0)),
    )

    return Config(source=source, pairs=pairs, strategy=strategy, runtime=runtime, raw=raw)
