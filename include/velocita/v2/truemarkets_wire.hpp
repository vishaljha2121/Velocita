// True Markets request and response encoding.
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

inline constexpr std::size_t kMaxTmTextBytes = 96;
inline constexpr std::size_t kMaxTmPayloads = kMaxSignPayloads;
inline constexpr std::size_t kMaxTmRequestBytes = 4096;
inline constexpr std::size_t kMaxTmResponseBytes = 32768;
inline constexpr std::size_t kMaxJsonDepth = 16;
inline constexpr std::size_t kMaxTmIssues = 8;

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

[[nodiscard]] std::optional<std::int64_t> parse_decimal_atoms(std::string_view text,
                                                              std::uint8_t decimals) noexcept;

enum class JsonError : std::uint8_t {
    none = 0,
    malformed,
    too_deep,
    duplicate_key,
    trailing_bytes,
    not_object,
    too_large,
};

[[nodiscard]] JsonError validate_json_object(std::string_view document) noexcept;

[[nodiscard]] std::optional<std::string_view> json_member_raw(std::string_view object,
                                                              std::string_view key) noexcept;

[[nodiscard]] std::optional<std::string_view> json_member_string(std::string_view object,
                                                                 std::string_view key) noexcept;

[[nodiscard]] std::optional<std::size_t> json_array_elements(
    std::string_view array, std::span<std::string_view> out) noexcept;

enum class EncodeError : std::uint8_t {
    none = 0,
    output_too_small,
    unsafe_text,
    unrepresentable_value,
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

[[nodiscard]] EncodeResult encode_trade_call(std::uint64_t rpc_id, TradeTool tool,
                                             const TradeArguments& arguments,
                                             std::span<char> out) noexcept;

[[nodiscard]] EncodeResult encode_trade_execute(std::uint64_t rpc_id, const TmText& order_id,
                                                std::span<const Signature> signatures,
                                                std::span<char> out) noexcept;

[[nodiscard]] EncodeResult encode_order_status_path(const TmText& order_id,
                                                    std::span<char> out) noexcept;

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
    TmText handle{};
    Side side{};
    TmText base_asset{};
    TmText quote_asset{};
    TmText fee_asset{};
    std::int64_t qty_atoms{};
    std::int64_t qty_out_atoms{};
    std::int64_t fee_atoms{};
    std::size_t issue_count{};
    std::size_t payload_count{};
    std::array<std::string_view, kMaxTmPayloads> payloads{};
};

[[nodiscard]] DecodeError decode_trade_reply(std::string_view document, ReplyShape shape,
                                             const AmountDecimals& decimals,
                                             TradeReply& out) noexcept;

struct ExecuteReply {
    TmText order_id{};
    TmText status{};
};

[[nodiscard]] DecodeError decode_execute_reply(std::string_view document,
                                               ExecuteReply& out) noexcept;

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

}
