#pragma once

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/venue_signer.hpp"
#include "velocita/v2/venue_wire.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace velocita::v2::truemarkets {

// True Markets request and response encoding. Integer-only, allocation-free,
// and refusing rather than guessing: a truncated or rounded amount is a real
// order at the wrong size.
//
// PROVENANCE OF EVERY SHAPE BELOW (ADR 0019 lists the open questions):
//   * get_quote / trade_prepare / trade_execute INPUT schemas: recorded from the
//     trading MCP server, docs/evidence/V2_TRUEMARKETS_MCP_SCHEMA_2026-09-18.md.
//   * get_quote OUTPUT fields (quote_id, order_side, base_asset, quote_asset,
//     qty, qty_out, fee, fee_asset, issues): recorded 2026-09-28 in
//     docs/evidence/truemarkets_markup_2026-09-28/. The `payloads` member was
//     present but omitted from the evidence, so its inner shape is unknown and
//     is carried as opaque bytes for the signer.
//   * trade_prepare OUTPUT: only its description ("an order_id and, when signing
//     is required, unsigned payloads"). The quote-shaped economic fields are a
//     mock assumption.
//   * trade_execute OUTPUT and order status: NOT RECORDED. The decoders below
//     define a fixture shape and must be replaced from a recorded response
//     before any live use.
//   * The MCP result envelope (structuredContent versus content[].text) is the
//     transport's business; these decoders take the tool result object.

inline constexpr std::size_t kMaxTmTextBytes = 96;
inline constexpr std::size_t kMaxTmPayloads = kMaxSignPayloads;
inline constexpr std::size_t kMaxTmRequestBytes = 4096;
inline constexpr std::size_t kMaxTmResponseBytes = 32768;
inline constexpr std::size_t kMaxJsonDepth = 16;
inline constexpr std::size_t kMaxTmIssues = 8;

// An identifier, address or status word. Only [A-Za-z0-9._:-] is accepted, so
// it can be placed in JSON or a URL path without escaping; anything else means
// the metadata or the reply is not what this adapter was written against.
struct TmText {
    std::array<char, kMaxTmTextBytes> bytes{};
    std::uint8_t length{};

    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), length}; }
    [[nodiscard]] bool empty() const noexcept { return length == 0; }
    constexpr bool operator==(const TmText&) const noexcept = default;
};

[[nodiscard]] std::optional<TmText> tm_text(std::string_view text) noexcept;

enum class Chain : std::uint8_t { unset = 0, solana, base };
enum class QtyUnit : std::uint8_t { unset = 0, base, quote };

// `atoms / 10^decimals` from a plain decimal string. No sign, exponent, spaces
// or leading point; more fractional digits than `decimals` is refused, never
// rounded. No value on overflow.
[[nodiscard]] std::optional<std::int64_t> parse_decimal_atoms(std::string_view text,
                                                              std::uint8_t decimals) noexcept;

// ---------------------------------------------------------------------------
// A bounded JSON reader: validation plus top-level member lookup. It never
// allocates and never unescapes; a string that needs unescaping is refused
// where its value is required.

enum class JsonError : std::uint8_t {
    none = 0,
    malformed,
    too_deep,
    duplicate_key,
    trailing_bytes,
    not_object,
    too_large,
};

// Whole-document validation. An object whose top level repeats a key is
// refused: two answers to one question is not something to pick between.
[[nodiscard]] JsonError validate_json_object(std::string_view document) noexcept;

// The raw text of a top-level member's value. Requires a validated object.
[[nodiscard]] std::optional<std::string_view> json_member_raw(std::string_view object,
                                                              std::string_view key) noexcept;

// A top-level string member's content. No value if absent, not a string, or
// containing an escape.
[[nodiscard]] std::optional<std::string_view> json_member_string(std::string_view object,
                                                                 std::string_view key) noexcept;

// The raw elements of a JSON array value. No value if it is not an array or has
// more than `out.size()` elements.
[[nodiscard]] std::optional<std::size_t> json_array_elements(
    std::string_view array, std::span<std::string_view> out) noexcept;

// ---------------------------------------------------------------------------
// Requests.

enum class EncodeError : std::uint8_t {
    none = 0,
    // Never a partial write.
    output_too_small,
    unsafe_text,
    unrepresentable_value,
    // chain and qty_unit must always be explicit: omitting chain routes to a
    // different venue (D-25) and qty_unit's default depends on side (D-24).
    unset_field,
};

struct EncodeResult {
    EncodeError error{EncodeError::none};
    std::size_t bytes_written{};
};

enum class TradeTool : std::uint8_t { get_quote = 1, trade_prepare };

struct TradeArguments {
    Side side{};
    TmText base_asset{};
    TmText qty{};
    QtyUnit qty_unit{};
    Chain chain{};
};

// JSON-RPC 2.0 `tools/call` body for get_quote or trade_prepare.
[[nodiscard]] EncodeResult encode_trade_call(std::uint64_t rpc_id, TradeTool tool,
                                             const TradeArguments& arguments,
                                             std::span<char> out) noexcept;

// JSON-RPC 2.0 `tools/call` body for trade_execute. The recorded schema types
// `signatures` as a STRING holding a JSON array, so the array is embedded as an
// escaped string, one element per payload.
[[nodiscard]] EncodeResult encode_trade_execute(std::uint64_t rpc_id, const TmText& order_id,
                                                std::span<const Signature> signatures,
                                                std::span<char> out) noexcept;

// `/v1/conductor/orders/{order_id}/status` (path confirmed in
// MUST_CONFIRM_BEFORE_ADAPTER_CONNECT.md; response schema unrecorded).
[[nodiscard]] EncodeResult encode_order_status_path(const TmText& order_id,
                                                    std::span<char> out) noexcept;

// ---------------------------------------------------------------------------
// Replies. Amount fields are atoms at the stated decimals.

enum class DecodeError : std::uint8_t {
    none = 0,
    malformed,
    missing_field,
    unsafe_text,
    bad_amount,
    bad_side,
    too_many_payloads,
    too_many_issues,
};

enum class ReplyShape : std::uint8_t { quote = 1, prepare };

struct AmountDecimals {
    std::uint8_t qty{};
    std::uint8_t qty_out{};
    std::uint8_t fee{};
};

struct TradeReply {
    // quote_id for a quote, order_id for a prepare.
    TmText handle{};
    Side side{};
    TmText base_asset{};
    TmText quote_asset{};
    TmText fee_asset{};
    std::int64_t qty_atoms{};
    std::int64_t qty_out_atoms{};
    std::int64_t fee_atoms{};
    std::size_t issue_count{};
    // Views into the decoded document; valid only while it is.
    std::size_t payload_count{};
    std::array<std::string_view, kMaxTmPayloads> payloads{};
};

[[nodiscard]] DecodeError decode_trade_reply(std::string_view document, ReplyShape shape,
                                             const AmountDecimals& decimals,
                                             TradeReply& out) noexcept;

// FIXTURE SHAPE, UNVERIFIED: {"order_id": ..., "status": ...}.
struct ExecuteReply {
    TmText order_id{};
    TmText status{};
};

[[nodiscard]] DecodeError decode_execute_reply(std::string_view document,
                                               ExecuteReply& out) noexcept;

// FIXTURE SHAPE, UNVERIFIED: {"order_id", "status", and when present "qty",
// "qty_out", "fee", "tx_signature"}. Amounts are required together or not at
// all.
struct StatusReply {
    TmText order_id{};
    TmText status{};
    bool has_execution{};
    std::int64_t qty_atoms{};
    std::int64_t qty_out_atoms{};
    std::int64_t fee_atoms{};
    TmText tx_signature{};
};

[[nodiscard]] DecodeError decode_status_reply(std::string_view document,
                                              const AmountDecimals& decimals,
                                              StatusReply& out) noexcept;

}  // namespace velocita::v2::truemarkets
