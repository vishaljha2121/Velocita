// Read-only evidence from the documented personal Trading REST order endpoint.
#pragma once

#include "velocita/v2/truemarkets_wire.hpp"

namespace velocita::v2::truemarkets {

enum class RestOrderStatus : std::uint8_t {
    unset, initialized, pending, active, cancel_pending, complete, canceled, failed
};
enum class RestOrderType : std::uint8_t { unset, market, limit };
enum class RestDecodeError : std::uint8_t {
    none, malformed, missing_field, invalid_field, bad_amount, unsupported
};
enum class RestReadDisposition : std::uint8_t {
    unknown, identity_conflict, inconsistent, incomplete, working, terminal_needs_reconciliation
};
enum class RestReadComparison : std::uint8_t {
    unusable, unchanged, changed_needs_reconciliation, contradictory_or_stale
};

struct RestDecimals {
    std::uint8_t base{};
    std::uint8_t quote{};
};

struct RestSpotAsset {
    TmText asset_id{};
    TmText chain{};
    TmText base_asset{};
    TmText quote_asset{};
    RestDecimals decimals{};
};

struct RestOrderDetail {
    TmText order_id{};
    TmText asset_id{};
    TmText base_asset{};
    TmText quote_asset{};
    TmText tx_hash{};
    TmText executed_vwap{};
    RestOrderStatus status{};
    RestOrderType type{};
    Side side{};
    QtyUnit qty_unit{};
    std::int64_t requested_atoms{};
    std::optional<std::int64_t> executed_base_atoms{};
    std::optional<std::int64_t> leaves_base_atoms{};
    std::optional<std::int64_t> fee_quote_atoms{};
};

struct RestOrderExpectation {
    TmText order_id{};
    TmText asset_id{};
    TmText base_asset{};
    TmText quote_asset{};
    RestOrderType type{};
    Side side{};
    QtyUnit qty_unit{};
    std::int64_t requested_atoms{};
};

[[nodiscard]] RestDecodeError decode_rest_spot_order(
    std::string_view document, RestDecimals decimals, RestOrderDetail& out,
    const RestSpotAsset* catalog_asset = nullptr) noexcept;

[[nodiscard]] RestReadDisposition assess_rest_spot_order(
    unsigned http_status, std::string_view document, RestDecimals decimals,
    const RestOrderExpectation& expected, const RestSpotAsset* catalog_asset = nullptr) noexcept;

[[nodiscard]] RestReadComparison compare_rest_spot_reads(
    unsigned previous_http, std::string_view previous_document,
    unsigned current_http, std::string_view current_document,
    RestDecimals decimals, const RestOrderExpectation& expected,
    const RestSpotAsset* catalog_asset = nullptr) noexcept;

}
