# True Markets MCP servers: tool schemas read from the servers

## Evidence qualification — 2026-09-21

The historical capture below is preserved, not refreshed. Its collector truncated
tool descriptions, printed input schemas only, and did not follow pagination.
It is evidence of the advertised MCP inputs captured on that date, not a complete
REST contract, output contract, or proof of venue-wide capability absence.
The word "verbatim" below describes stored output, not an unmodified server body.
Missing price/cancel tools do not establish that every venue interface lacks them.
The collector now retains complete result pages; no fresh venue call was made.

Captured 2026-09-18 by `tools/mcp-tools.py`, which speaks only `initialize` and
`tools/list` and refuses `tools/call` outright. Nothing here was traded, quoted
or authenticated; `tools/list` is served to an anonymous client.

WHY THIS FILE IS THE AUTHORITY. Every earlier attempt to learn the order schema
went through prose - a quickstart, an endpoint reference, a rendered page that
did not render (D-3). This is the venue's own machine-readable contract, served
by the system that enforces it. Where it disagrees with the documentation, it
wins, and D-22 through D-26 record each place it does.

Two servers:

| Server | URL | Auth | Tools |
|---|---|---|---|
| Market data | `https://mcp.truemarkets.co/marketdata/mcp` | none | 10 |
| Trading | `https://mcp.truemarkets.co/mcp` | email OTP | 17 |

The trading server is a superset: the same 10 market-data tools plus 2 auth,
2 account, 1 quote and 2 trade tools.

## Trading server, verbatim

```
=== https://mcp.truemarkets.co/mcp
initialize -> http 200
server: {"name": "defi", "version": "1.0.0", "title": "TrueMarkets Trading", "description": "Trade crypto on the True Markets exchange and on-chain while retaining full custody of your assets. Get quotes, execute trades, and manage your portfolio with local key signing.", "websiteUrl": "https://truemarkets.co", "icons": [{"src": "https://d319hyy9pksbox.cloudfront.net/cb8139324ca6a97aada73786548561f846f559b0e581131826098da4d916e3d1.png", "mimeType": "image/png", "sizes": ["32x32"]}, {"src": "https://d319hyy9pksbox.cloudfront.net/d33409f8dbe6a8cef10db340e231ad682e932a4c51a79bf1bde547dfe978d4d0.png", "mimeType": "image/png", "sizes": ["64x64"]}, {"src": "https://d319hyy9pksbox.cloudfront.net/2426c4aaf302bbb1915c22502deb4e1e1c3672795920adb0381deaa8e94f8b7d.png", "mimeType": "image/png", "sizes": ["128x128"]}]}
tools/list -> http 200
17 tools

--- get_asset
    Get details for a specific token by chain and contract address.
    schema: {
      "properties": {
            "address": {
                  "description": "Token contract address",
                  "type": "string"
            },
            "chain": {
                  "description": "Blockchain network (solana, base)",
                  "type": "string"
            }
      },
      "required": [
            "chain",
            "address"
      ],
      "type": "object"
}
--- get_asset_summary
    Get a daily AI-generated summary for a specific asset including news analysis and sentiment.
    schema: {
      "properties": {
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_asset_summary_history
    Get historical daily summaries for a specific asset.
    schema: {
      "properties": {
            "limit": {
                  "description": "Number of historical summaries (default: 7, max: 30)",
                  "type": "string"
            },
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_balances
    Show unified balances across exchange (CeFi) accounts and on-chain wallets. Returns symbol, chain, and amount for each holding. Requires login.
    schema: {
      "properties": {},
      "required": [],
      "type": "object"
}
--- get_market_cap
    Get the market capitalization for a specific asset by symbol.
    schema: {
      "properties": {
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_market_summary
    Get a market digest with key bullets, trending assets, sentiment analysis, and sources. Returns an AI-generated summary of current market conditions.
    schema: {
      "properties": {},
      "required": [],
      "type": "object"
}
--- get_price_history
    Get historical price data for one or more tokens. Returns timestamped price points. No authentication required.
    schema: {
      "properties": {
            "resolution": {
                  "description": "Interval between points: 5s, 1m, 5m, 1h, 1d (default: 5m)",
                  "type": "string"
            },
            "symbols": {
                  "description": "Comma-separated asset symbols (e.g., SOL,BTC,ETH)",
                  "type": "string"
            },
            "window": {
                  "description": "Lookback period: 1h, 4h, 1d, 7d, 1M (default: 1d)",
                  "type": "string"
            }
      },
      "required": [
            "symbols"
      ],
      "type": "object"
}
--- get_profile
    Show account details including email and wallet addresses for each chain. Requires login.
    schema: {
      "properties": {},
      "required": [],
      "type": "object"
}
--- get_quote
    Get a price quote for a trade. Shows the amount you'll receive and any fees.
    schema: {
      "properties": {
            "base_asset": {
                  "description": "Token symbol or contract address to trade",
                  "type": "string"
            },
            "chain": {
                  "description": "Blockchain network (solana, base) for an on-chain swap. Omit for an exchange (CeFi) quote.",
                  "type": "string"
            },
            "qty": {
                  "description": "Amount to trade",
                  "type": "string"
            },
            "qty_unit": {
                  "description": "Quantity unit: 'base' (token amount) or 'quote' (USDC amount). Defaults to 'quote' for buys and 'base' for sells.",
                  "type": "string"
            },
            "side": {
                  "description": "Order side (buy, sell)",
                  "type": "string"
            }
      },
      "required": [
            "side",
            "base_asset",
            "qty"
      ],
      "type": "object"
}
--- get_surging_assets
    Get assets with the highest recent price change percentage.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            },
            "sort": {
                  "description": "Sort order: desc (default) or asc",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- get_trending_assets
    Get assets trending by trading volume.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            },
            "sort": {
                  "description": "Sort order: desc (default) or asc",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- list_assets
    List all available tradeable tokens. Returns symbol, name, chain, address, and decimals for each asset.
    schema: {
      "properties": {
            "chain": {
                  "description": "Filter by blockchain (solana, base)",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- list_market_caps
    List all assets ranked by market capitalization in descending order.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- login_request_code
    Request an email verification code for login. A one-time code will be sent to the provided email address.
    schema: {
      "properties": {
            "email": {
                  "description": "Email address to send the verification code to",
                  "type": "string"
            }
      },
      "required": [
            "email"
      ],
      "type": "object"
}
--- login_verify_code
    Verify the email code and complete login. The session is established automatically — no need to store or pass the returned tokens.
    schema: {
      "properties": {
            "code": {
                  "description": "Verification code received via email",
                  "type": "string"
            },
            "email": {
                  "description": "Email address used in the login_request_code step",
                  "type": "string"
            }
      },
      "required": [
            "email",
            "code"
      ],
      "type": "object"
}
--- trade_execute
    Execute a prepared order with client-provided signatures. Call trade_prepare first to get the order_id and payloads to sign. Only needed when trade_prepare returned payloads.
    schema: {
      "properties": {
            "order_id": {
                  "description": "Order ID from trade_prepare",
                  "type": "string"
            },
            "signatures": {
                  "description": "JSON array of Turnkey stamps, one per payload from trade_prepare",
                  "type": "string"
            }
      },
      "required": [
            "order_id",
            "signatures"
      ],
      "type": "object"
}
--- trade_prepare
    Create a market order. Returns an order_id and, when signing is required, unsigned payloads for the client to sign. If no payloads are returned the order was submitted directly. Requires login. Confirm details with the user before calling. After signing, call trade_execute with the order_id and sign
    schema: {
      "properties": {
            "base_asset": {
                  "description": "Token symbol or contract address to trade",
                  "type": "string"
            },
            "chain": {
                  "description": "Blockchain network (solana, base) for an on-chain swap. Omit for an exchange (CeFi) order.",
                  "type": "string"
            },
            "qty": {
                  "description": "Amount to trade",
                  "type": "string"
            },
            "qty_unit": {
                  "description": "Quantity unit: 'base' (token amount) or 'quote' (USDC amount). Defaults to 'quote' for buys and 'base' for sells.",
                  "type": "string"
            },
            "side": {
                  "description": "Order side (buy, sell)",
                  "type": "string"
            }
      },
      "required": [
            "side",
            "base_asset",
            "qty"
      ],
      "type": "object"
}
```

## Market data server, verbatim

```
=== https://mcp.truemarkets.co/marketdata/mcp
initialize -> http 200
server: {"name": "marketdata", "version": "1.0.0", "title": "TrueMarkets Market Data", "description": "Real-time crypto market data, trending assets, price history, and market summaries.", "websiteUrl": "https://truemarkets.co", "icons": [{"src": "https://d319hyy9pksbox.cloudfront.net/cb8139324ca6a97aada73786548561f846f559b0e581131826098da4d916e3d1.png", "mimeType": "image/png", "sizes": ["32x32"]}, {"src": "https://d319hyy9pksbox.cloudfront.net/d33409f8dbe6a8cef10db340e231ad682e932a4c51a79bf1bde547dfe978d4d0.png", "mimeType": "image/png", "sizes": ["64x64"]}, {"src": "https://d319hyy9pksbox.cloudfront.net/2426c4aaf302bbb1915c22502deb4e1e1c3672795920adb0381deaa8e94f8b7d.png", "mimeType": "image/png", "sizes": ["128x128"]}]}
tools/list -> http 200
10 tools

--- get_asset
    Get details for a specific token by chain and contract address.
    schema: {
      "properties": {
            "address": {
                  "description": "Token contract address",
                  "type": "string"
            },
            "chain": {
                  "description": "Blockchain network (solana, base)",
                  "type": "string"
            }
      },
      "required": [
            "chain",
            "address"
      ],
      "type": "object"
}
--- get_asset_summary
    Get a daily AI-generated summary for a specific asset including news analysis and sentiment.
    schema: {
      "properties": {
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_asset_summary_history
    Get historical daily summaries for a specific asset.
    schema: {
      "properties": {
            "limit": {
                  "description": "Number of historical summaries (default: 7, max: 30)",
                  "type": "string"
            },
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_market_cap
    Get the market capitalization for a specific asset by symbol.
    schema: {
      "properties": {
            "symbol": {
                  "description": "Asset symbol (e.g., BTC, ETH, SOL)",
                  "type": "string"
            }
      },
      "required": [
            "symbol"
      ],
      "type": "object"
}
--- get_market_summary
    Get a market digest with key bullets, trending assets, sentiment analysis, and sources. Returns an AI-generated summary of current market conditions.
    schema: {
      "properties": {},
      "required": [],
      "type": "object"
}
--- get_price_history
    Get historical price data for one or more tokens. Returns timestamped price points. No authentication required.
    schema: {
      "properties": {
            "resolution": {
                  "description": "Interval between points: 5s, 1m, 5m, 1h, 1d (default: 5m)",
                  "type": "string"
            },
            "symbols": {
                  "description": "Comma-separated asset symbols (e.g., SOL,BTC,ETH)",
                  "type": "string"
            },
            "window": {
                  "description": "Lookback period: 1h, 4h, 1d, 7d, 1M (default: 1d)",
                  "type": "string"
            }
      },
      "required": [
            "symbols"
      ],
      "type": "object"
}
--- get_surging_assets
    Get assets with the highest recent price change percentage.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            },
            "sort": {
                  "description": "Sort order: desc (default) or asc",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- get_trending_assets
    Get assets trending by trading volume.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            },
            "sort": {
                  "description": "Sort order: desc (default) or asc",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- list_assets
    List all available tradeable tokens. Returns symbol, name, chain, address, and decimals for each asset.
    schema: {
      "properties": {
            "chain": {
                  "description": "Filter by blockchain (solana, base)",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
--- list_market_caps
    List all assets ranked by market capitalization in descending order.
    schema: {
      "properties": {
            "limit": {
                  "description": "Max results to return (default: 10, max: 50)",
                  "type": "string"
            }
      },
      "required": [],
      "type": "object"
}
```
