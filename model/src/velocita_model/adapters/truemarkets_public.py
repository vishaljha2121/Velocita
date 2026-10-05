"""True Markets adapter - partially specified, deliberately not guessed.

What is recorded here was read from official sources on 2026-09-04 and is marked
`verified`. Everything else is marked `unverified` and the adapter refuses to
collect until an operator supplies it. A placeholder endpoint that happens to
return JSON is worse than no endpoint, because it produces plausible data.

VERIFIED (docs.truemarkets.co, 2026-09-04)
  * API base URL is `https://api.truemarkets.co`.
  * Authentication is an ES256 JWT minted at `POST /v1/auth/api-key/token` by
    signing `{key_id}.{timestamp}`.
  * `GET /v1/conductor/assets` returns the tradable asset catalog across CeFi
    and DeFi venues, filterable by a `venue` parameter.
  * `POST /v1/conductor/quotes` returns non-committal trade quotes.
  * `POST /v1/conductor/orders` places **market orders**. This adapter must
    never reach it; the read-only transport refuses the path structurally.
  * A "Market Data" API is advertised as providing real-time quotes, book, and
    candles, but its endpoint paths were not published at the URLs checked.

THE FINDING THAT MATTERS MOST
  The documented retail DeFi path is quote-and-market-order (RFQ-shaped), not a
  central limit order book with resting passive quotes. Separately advertised
  "Trading Firms CeFi" access mentions FIX, low-latency quoting, order
  management, and volume-tiered maker rebates - i.e. the maker-side surface
  appears to live on the KYC'd institutional path, not the permissionless DeFi
  retail path.

  If that reading is correct, a passive market-making strategy is not
  expressible on the venue surface available to this account, and no amount of
  shadow simulation changes that. `describe_capabilities()` therefore reports
  `supports_resting_limit_orders` as unverified-and-false, which makes the
  pipeline emit a venue-level NO-GO instead of a per-pair scorecard.

  Confirming or refuting this is the single highest-value thing to do before
  writing any more strategy code. See
  `docs/MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md`.
"""

from __future__ import annotations

from decimal import Decimal
from typing import Iterable, Sequence

from ..features.book_metrics import BookObservation
from ..features.flow import TradePrint
from .base import (
    MetadataField,
    RawPayload,
    ReadOnlyHttp,
    VenueAdapter,
    VenueCapabilities,
)

API_BASE_URL = "https://api.truemarkets.co"
DOCS_URL = "https://docs.truemarkets.co/"
QUICKSTART_URL = "https://docs.truemarkets.co/trading/quickstart"

ASSETS_PATH = "/v1/conductor/assets"

PROHIBITED_PATHS = ("/v1/conductor/orders", "/v1/auth/api-key/token")


class AdapterNotConfigured(RuntimeError):
    pass


class TrueMarketsPublicAdapter(VenueAdapter):
    name = "truemarkets"

    __slots__ = ("_http", "_book_path", "_trades_path", "_metadata_path", "_symbol_map")

    def __init__(
        self,
        *,
        book_path: str | None = None,
        trades_path: str | None = None,
        metadata_path: str | None = ASSETS_PATH,
        symbol_map: dict[str, str] | None = None,
        base_url: str = API_BASE_URL,
        timeout_s: float = 10.0,
    ) -> None:
        for path in (book_path, trades_path, metadata_path):
            if path is not None and path in PROHIBITED_PATHS:
                raise AdapterNotConfigured(f"{path} is a state-changing endpoint and cannot be configured here")
        self._http = ReadOnlyHttp(base_url, timeout_s=timeout_s)
        self._book_path = book_path
        self._trades_path = trades_path
        self._metadata_path = metadata_path
        self._symbol_map = dict(symbol_map or {})

    def _symbol(self, pair_id: str) -> str:
        try:
            return self._symbol_map[pair_id]
        except KeyError:
            raise AdapterNotConfigured(
                f"no venue symbol configured for {pair_id!r}; confirm it from the asset catalog"
            ) from None

    def describe_capabilities(self) -> VenueCapabilities:
        return VenueCapabilities(
            venue=self.name,
            supports_resting_limit_orders=MetadataField(
                "supports_resting_limit_orders",
                False,
                "unverified",
                QUICKSTART_URL,
            ),
            supports_post_only=MetadataField("supports_post_only", None, "unverified", DOCS_URL),
            supports_self_trade_prevention=MetadataField(
                "supports_self_trade_prevention", None, "unverified", DOCS_URL
            ),
            publishes_order_book=MetadataField(
                "publishes_order_book",
                None,
                "unverified",
                DOCS_URL,
            ),
            publishes_public_trades=MetadataField("publishes_public_trades", None, "unverified", DOCS_URL),
            publishes_sequence_numbers=MetadataField("publishes_sequence_numbers", None, "unverified", DOCS_URL),
            maker_fee_documented=MetadataField("maker_fee_documented", None, "unverified", DOCS_URL),
            accessible_without_kyc=MetadataField("accessible_without_kyc", None, "unverified", DOCS_URL),
            notes=(
                "API base https://api.truemarkets.co confirmed 2026-09-04.",
                "GET /v1/conductor/assets (catalog) and POST /v1/conductor/quotes (RFQ) confirmed.",
                "POST /v1/conductor/orders places MARKET orders; never called by this package.",
                "Documented retail DeFi path is RFQ + market order, not a resting-quote CLOB.",
                "Maker rebates and FIX order management are advertised on the CeFi firm path, "
                "which may be unavailable under this account's KYC constraints.",
                "A 'Market Data' API advertising quotes/book/candles exists but its paths were "
                "not published at the URLs checked on 2026-09-04.",
            ),
        )

    def fetch_metadata(self, pair_id: str) -> tuple[Iterable[MetadataField], RawPayload]:
        if self._metadata_path is None:
            raise AdapterNotConfigured("metadata endpoint is not configured")
        payload = self._http.get(self._metadata_path, {"venue": "defi"})
        return (
            [
                MetadataField("asset_catalog_raw_bytes", len(payload.body), "verified", payload.url),
                MetadataField("asset_catalog_parsed", False, "unverified", payload.url),
            ],
            payload,
        )

    def fetch_book(self, pair_id: str) -> tuple[BookObservation, RawPayload]:
        if self._book_path is None:
            raise AdapterNotConfigured(
                "no order-book endpoint is configured for True Markets. Confirm from official "
                "documentation whether a public DeFi order book exists at all before collecting; "
                "if the venue is RFQ-only, the correct outcome is a venue-level NO-GO."
            )
        raise AdapterNotConfigured(
            f"book endpoint {self._book_path} is configured but its response schema is unconfirmed; "
            "implement parsing against documented field names, not guessed ones"
        )

    def fetch_trades(self, pair_id: str, since_ns: int) -> tuple[Sequence[TradePrint], RawPayload]:
        if self._trades_path is None:
            raise AdapterNotConfigured("no public trade endpoint is configured for True Markets")
        raise AdapterNotConfigured(
            f"trades endpoint {self._trades_path} is configured but its response schema is unconfirmed"
        )
