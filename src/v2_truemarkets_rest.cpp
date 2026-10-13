#include "velocita/v2/truemarkets_rest.hpp"

#include <utility>

namespace velocita::v2::truemarkets {
namespace {

RestDecodeError read_text(std::string_view document, std::string_view key, TmText& out,
                          bool optional = false) noexcept {
    const auto raw = json_member_raw(document, key);
    if (!raw || *raw == "null") return optional ? RestDecodeError::none : RestDecodeError::missing_field;
    const auto value = json_member_string(document, key);
    if (!value) return RestDecodeError::invalid_field;
    const auto text = tm_text(*value);
    if (!text) return RestDecodeError::invalid_field;
    out = *text;
    return RestDecodeError::none;
}

RestDecodeError read_amount(std::string_view document, std::string_view key, std::uint8_t decimals,
                            std::optional<std::int64_t>& out) noexcept {
    const auto raw = json_member_raw(document, key);
    if (!raw || *raw == "null") return RestDecodeError::none;
    const auto value = json_member_string(document, key);
    if (!value) return RestDecodeError::bad_amount;
    out = parse_decimal_atoms(*value, decimals);
    return out ? RestDecodeError::none : RestDecodeError::bad_amount;
}

bool plain_decimal(std::string_view value) noexcept {
    if (value.empty()) return false;
    bool dot = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = value[i];
        if (c >= '0' && c <= '9') continue;
        if (c != '.' || dot || i == 0 || i + 1 == value.size()) return false;
        dot = true;
    }
    return true;
}

bool positive_decimal(std::string_view value) noexcept {
    for (const auto c : value) if (c >= '1' && c <= '9') return true;
    return false;
}

}

RestDecodeError decode_rest_spot_order(std::string_view document, RestDecimals decimals,
                                      RestOrderDetail& out, const RestSpotAsset* catalog_asset) noexcept {
    out = {};
    if (validate_json_object(document) != JsonError::none) return RestDecodeError::malformed;
    if (decimals.base > 18 || decimals.quote > 18) return RestDecodeError::bad_amount;
    if (json_member_string(document, "venue") != "defi") return RestDecodeError::unsupported;
    const auto market_class = json_member_raw(document, "asset_type");
    if (market_class ? json_member_string(document, "asset_type") != "spot" : catalog_asset == nullptr)
        return RestDecodeError::unsupported;
    if (catalog_asset) {
        if (catalog_asset->asset_id.empty() || catalog_asset->chain.empty() ||
            catalog_asset->base_asset.empty() || catalog_asset->quote_asset.empty() ||
            decimals.base != catalog_asset->decimals.base || decimals.quote != catalog_asset->decimals.quote)
            return RestDecodeError::invalid_field;
        if (json_member_raw(document, "chain") &&
            json_member_string(document, "chain") != catalog_asset->chain.view()) return RestDecodeError::invalid_field;
    }
    RestOrderDetail value;
    for (const auto& [key, field] : std::array{
             std::pair{"order_id", &value.order_id}, std::pair{"base_asset", &value.base_asset},
             std::pair{"quote_asset", &value.quote_asset}}) {
        const auto error = read_text(document, key, *field);
        if (error != RestDecodeError::none) return error;
    }
    for (const auto& [key, field] : std::array{
             std::pair{"asset_id", &value.asset_id}, std::pair{"tx_hash", &value.tx_hash},
             std::pair{"executed_vwap", &value.executed_vwap}}) {
        const auto error = read_text(document, key, *field, true);
        if (error != RestDecodeError::none) return error;
    }
    if (!value.executed_vwap.empty() && !plain_decimal(value.executed_vwap.view())) return RestDecodeError::bad_amount;
    const auto status = json_member_string(document, "status");
    if (status == "initialized") value.status = RestOrderStatus::initialized;
    else if (status == "pending") value.status = RestOrderStatus::pending;
    else if (status == "active") value.status = RestOrderStatus::active;
    else if (status == "cancel_pending") value.status = RestOrderStatus::cancel_pending;
    else if (status == "complete") value.status = RestOrderStatus::complete;
    else if (status == "canceled") value.status = RestOrderStatus::canceled;
    else if (status == "failed") value.status = RestOrderStatus::failed;
    else return RestDecodeError::unsupported;
    const auto type = json_member_string(document, "type");
    if (type == "market") value.type = RestOrderType::market;
    else if (type == "limit") value.type = RestOrderType::limit;
    else return RestDecodeError::unsupported;
    const auto side = json_member_string(document, "side");
    if (side == "buy") value.side = Side::buy;
    else if (side == "sell") value.side = Side::sell;
    else return RestDecodeError::unsupported;
    const auto unit = json_member_string(document, "qty_unit");
    if (unit == "base") value.qty_unit = QtyUnit::base;
    else if (unit == "quote") value.qty_unit = QtyUnit::quote;
    else return RestDecodeError::unsupported;
    std::optional<std::int64_t> requested;
    auto error = read_amount(document, "qty", value.qty_unit == QtyUnit::base ? decimals.base : decimals.quote, requested);
    if (error != RestDecodeError::none) return error;
    if (!requested) return RestDecodeError::missing_field;
    if (*requested <= 0) return RestDecodeError::bad_amount;
    value.requested_atoms = *requested;
    error = read_amount(document, "executed_qty", decimals.base, value.executed_base_atoms);
    if (error != RestDecodeError::none) return error;
    error = read_amount(document, "leaves_qty", decimals.base, value.leaves_base_atoms);
    if (error != RestDecodeError::none) return error;
    error = read_amount(document, "fee", decimals.quote, value.fee_quote_atoms);
    if (error != RestDecodeError::none) return error;
    if (catalog_asset && (value.asset_id != catalog_asset->asset_id ||
                         value.base_asset != catalog_asset->base_asset || value.quote_asset != catalog_asset->quote_asset))
        return RestDecodeError::invalid_field;
    out = value;
    return RestDecodeError::none;
}

RestReadComparison compare_rest_spot_reads(
    unsigned previous_http, std::string_view previous_document,
    unsigned current_http, std::string_view current_document,
    RestDecimals decimals, const RestOrderExpectation& expected, const RestSpotAsset* catalog_asset) noexcept {
    const auto usable = [](RestReadDisposition disposition) noexcept {
        return disposition == RestReadDisposition::working ||
               disposition == RestReadDisposition::terminal_needs_reconciliation;
    };
    if (!usable(assess_rest_spot_order(previous_http, previous_document, decimals, expected, catalog_asset)) ||
        !usable(assess_rest_spot_order(current_http, current_document, decimals, expected, catalog_asset))) return RestReadComparison::unusable;
    RestOrderDetail previous, current;
    if (decode_rest_spot_order(previous_document, decimals, previous, catalog_asset) != RestDecodeError::none ||
        decode_rest_spot_order(current_document, decimals, current, catalog_asset) != RestDecodeError::none) return RestReadComparison::unusable;
    const bool same_economics = previous.executed_base_atoms == current.executed_base_atoms &&
        previous.leaves_base_atoms == current.leaves_base_atoms && previous.fee_quote_atoms == current.fee_quote_atoms &&
        previous.executed_vwap == current.executed_vwap && previous.tx_hash == current.tx_hash;
    const bool terminal = previous.status == RestOrderStatus::complete ||
        previous.status == RestOrderStatus::canceled || previous.status == RestOrderStatus::failed;
    if (terminal && (previous.status != current.status || !same_economics)) return RestReadComparison::contradictory_or_stale;
    if (*current.executed_base_atoms < *previous.executed_base_atoms ||
        *current.fee_quote_atoms < *previous.fee_quote_atoms) return RestReadComparison::contradictory_or_stale;
    if (current.status == RestOrderStatus::initialized && previous.status != RestOrderStatus::initialized) return RestReadComparison::contradictory_or_stale;
    if (*current.executed_base_atoms == *previous.executed_base_atoms &&
        current.executed_vwap != previous.executed_vwap) return RestReadComparison::contradictory_or_stale;
    if (previous.status == current.status && same_economics) return RestReadComparison::unchanged;
    return RestReadComparison::changed_needs_reconciliation;
}

RestReadDisposition assess_rest_spot_order(unsigned http_status, std::string_view document,
                                          RestDecimals decimals, const RestOrderExpectation& expected, const RestSpotAsset* catalog_asset) noexcept {
    if (http_status != 200) return RestReadDisposition::unknown;
    RestOrderDetail value;
    if (decode_rest_spot_order(document, decimals, value, catalog_asset) != RestDecodeError::none) return RestReadDisposition::unknown;
    if (expected.order_id.empty() || expected.asset_id.empty() || expected.base_asset.empty() ||
        expected.quote_asset.empty() || value.order_id != expected.order_id ||
        value.base_asset != expected.base_asset || value.quote_asset != expected.quote_asset ||
        value.type != expected.type || value.side != expected.side || value.qty_unit != expected.qty_unit ||
        value.requested_atoms != expected.requested_atoms) return RestReadDisposition::identity_conflict;
    if (value.asset_id.empty()) return RestReadDisposition::incomplete;
    if (value.asset_id != expected.asset_id) return RestReadDisposition::identity_conflict;
    if (!value.executed_base_atoms || !value.leaves_base_atoms || !value.fee_quote_atoms) return RestReadDisposition::incomplete;
    if (value.status == RestOrderStatus::initialized && *value.executed_base_atoms != 0) return RestReadDisposition::inconsistent;
    if (value.status == RestOrderStatus::complete &&
        (*value.executed_base_atoms == 0 || *value.leaves_base_atoms != 0)) return RestReadDisposition::inconsistent;
    if (*value.executed_base_atoms > 0) {
        if (value.executed_vwap.empty() || value.tx_hash.empty()) return RestReadDisposition::incomplete;
        if (!positive_decimal(value.executed_vwap.view())) return RestReadDisposition::inconsistent;
    }
    if (value.qty_unit == QtyUnit::base && *value.executed_base_atoms > value.requested_atoms) return RestReadDisposition::inconsistent;
    if (value.status == RestOrderStatus::complete || value.status == RestOrderStatus::canceled ||
        value.status == RestOrderStatus::failed) return RestReadDisposition::terminal_needs_reconciliation;
    return RestReadDisposition::working;
}

}
