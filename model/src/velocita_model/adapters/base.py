"""Venue adapter interface and the read-only transport guard.

An adapter's job is narrow: turn venue-specific bytes into normalised
observations plus the raw payload that produced them. It never decides anything.

The transport guard is the part worth reading. Rather than trusting that no
future edit will call a trading endpoint, `ReadOnlyHttp` refuses to construct a
request whose method is not GET, or whose path matches a known state-changing
route. A venue that requires POST for a legitimate market-data query must
declare that path in `post_allowlist`, which makes the exception visible in a
diff instead of implicit in a call site.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from decimal import Decimal
from typing import Any, Iterable, Sequence

from ..contracts.digest import sha256_digest
from ..features.book_metrics import BookObservation
from ..features.flow import TradePrint


class TransportRefused(RuntimeError):
    pass


STATE_CHANGING_PATH_FRAGMENTS: tuple[str, ...] = (
    "/orders",
    "/order",
    "/trade",
    "/execute",
    "/submit",
    "/cancel",
    "/withdraw",
    "/deposit",
    "/transfer",
    "/approve",
    "/swap",
    "/sign",
    "/transaction",
)


@dataclass(frozen=True, slots=True)
class MetadataField:
    field: str
    value: Any
    confidence: str
    source_url: str | None = None

    def __post_init__(self) -> None:
        if self.confidence not in ("verified", "inferred", "unverified"):
            raise ValueError("confidence must be verified|inferred|unverified")


@dataclass(frozen=True, slots=True)
class RawPayload:
    body: bytes
    received_at_ns: int
    url: str | None = None

    @property
    def digest(self) -> bytes:
        return sha256_digest(self.body)


class ReadOnlyHttp:
    __slots__ = ("_base_url", "_timeout_s", "_post_allowlist", "_user_agent")

    def __init__(
        self,
        base_url: str,
        *,
        timeout_s: float = 10.0,
        post_allowlist: Sequence[str] = (),
        user_agent: str = "velocita-model-research/0 (passive market-data research)",
    ) -> None:
        self._base_url = base_url.rstrip("/")
        self._timeout_s = timeout_s
        self._post_allowlist = tuple(post_allowlist)
        self._user_agent = user_agent

    def _guard(self, path: str, method: str) -> None:
        lowered = path.lower()
        if method == "POST":
            if path not in self._post_allowlist:
                raise TransportRefused(
                    f"POST {path} is not in the read-only allowlist; "
                    "a market-data query that needs POST must be declared explicitly"
                )
            return
        if method != "GET":
            raise TransportRefused(f"method {method} is not permitted by the read-only transport")
        for fragment in STATE_CHANGING_PATH_FRAGMENTS:
            if fragment in lowered:
                raise TransportRefused(f"path {path} matches state-changing fragment {fragment!r}")

    def get(self, path: str, params: dict[str, Any] | None = None) -> RawPayload:
        import time

        self._guard(path, "GET")
        query = f"?{urllib.parse.urlencode(params)}" if params else ""
        url = f"{self._base_url}{path}{query}"
        request = urllib.request.Request(url, method="GET", headers={"User-Agent": self._user_agent})
        with urllib.request.urlopen(request, timeout=self._timeout_s) as response:
            body = response.read()
        return RawPayload(body=body, received_at_ns=time.time_ns(), url=url)

    @staticmethod
    def parse_json(payload: RawPayload) -> Any:
        return json.loads(payload.body.decode("utf-8"))


class VenueAdapter(ABC):
    name: str = "unnamed"

    @abstractmethod
    def describe_capabilities(self) -> "VenueCapabilities":
        pass

    @abstractmethod
    def fetch_metadata(self, pair_id: str) -> tuple[Iterable[MetadataField], RawPayload]:
        ...

    @abstractmethod
    def fetch_book(self, pair_id: str) -> tuple[BookObservation, RawPayload]:
        ...

    @abstractmethod
    def fetch_trades(self, pair_id: str, since_ns: int) -> tuple[Sequence[TradePrint], RawPayload]:
        ...


@dataclass(frozen=True, slots=True)
class VenueCapabilities:
    venue: str
    supports_resting_limit_orders: MetadataField
    supports_post_only: MetadataField
    supports_self_trade_prevention: MetadataField
    publishes_order_book: MetadataField
    publishes_public_trades: MetadataField
    publishes_sequence_numbers: MetadataField
    maker_fee_documented: MetadataField
    accessible_without_kyc: MetadataField
    notes: tuple[str, ...] = field(default_factory=tuple)

    def all_fields(self) -> tuple[MetadataField, ...]:
        return (
            self.supports_resting_limit_orders,
            self.supports_post_only,
            self.supports_self_trade_prevention,
            self.publishes_order_book,
            self.publishes_public_trades,
            self.publishes_sequence_numbers,
            self.maker_fee_documented,
            self.accessible_without_kyc,
        )

    def unverified_fields(self) -> tuple[MetadataField, ...]:
        return tuple(f for f in self.all_fields() if f.confidence != "verified")

    def passive_strategy_is_expressible(self) -> tuple[bool, str]:
        resting = self.supports_resting_limit_orders
        if resting.confidence != "verified":
            return False, f"resting limit order support is {resting.confidence}, not verified"
        if not resting.value:
            return False, "venue does not support resting limit orders; passive quoting is impossible"
        book = self.publishes_order_book
        if book.confidence != "verified" or not book.value:
            return False, f"order book availability is {book.confidence}"
        return True, "resting limit orders and an order book are both documented"
