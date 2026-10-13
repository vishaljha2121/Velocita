#include "velocita/v2/truemarkets_rest.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::truemarkets;

constexpr std::string_view kOrder = R"({"order_id":"order-1","asset_id":"sol-asset-1","base_asset":"SOL","quote_asset":"USDC","type":"market","side":"buy","qty":"5","qty_unit":"quote","price":"100","executed_qty":"0.04","leaves_qty":"0","executed_vwap":"125.000000000001","fee":"0.05","tx_hash":"tx-1","venue":"defi","asset_type":"spot","status":"complete"})";

void require(bool condition, const char* assertion) {
    if (!condition) throw std::runtime_error(assertion);
}

std::string change(std::string_view from, std::string_view to) {
    std::string value(kOrder);
    const auto at = value.find(from);
    require(at != std::string::npos, "fixture replacement exists");
    value.replace(at, from.size(), to);
    return value;
}

RestOrderExpectation expectation() {
    return {*tm_text("order-1"), *tm_text("sol-asset-1"), *tm_text("SOL"), *tm_text("USDC"),
            RestOrderType::market, Side::buy, QtyUnit::quote, 5'000'000};
}

RestReadDisposition assess(std::string_view body, unsigned http = 200) {
    return assess_rest_spot_order(http, body, {9, 6}, expectation());
}

void quote_buy_units_and_vwap_are_independent() {
    RestOrderDetail out;
    require(decode_rest_spot_order(kOrder, {9, 6}, out) == RestDecodeError::none, "documented fields decode");
    require(out.requested_atoms == 5'000'000, "quote request is five million USDC micros");
    require(out.executed_base_atoms == 40'000'000, "fill is forty million SOL lamports");
    require(out.fee_quote_atoms == 50'000, "fee is fifty thousand USDC micros");
    require(out.executed_vwap.view() == "125.000000000001", "VWAP retained verbatim, not reference price or rounded cash");
    require(assess(kOrder) == RestReadDisposition::terminal_needs_reconciliation, "complete is evidence needing reconciliation, not permission to book");
}

void missing_execution_is_not_zero() {
    for (const auto field : {R"("executed_qty":"0.04",)", R"("leaves_qty":"0",)",
                            R"("fee":"0.05",)", R"("executed_vwap":"125.000000000001",)",
                            R"("tx_hash":"tx-1",)"}) {
        require(assess(change(field, "")) == RestReadDisposition::incomplete, "missing execution evidence remains incomplete");
    }
    const auto body = change(R"("fee":"0.05")", R"("fee":null)");
    require(assess(body) == RestReadDisposition::incomplete, "null fee is not zero");
    require(assess(change(R"("fee":"0.05")", R"("fee":"0")")) == RestReadDisposition::terminal_needs_reconciliation, "explicit zero fee is present");
}

void complete_requires_positive_fill_and_zero_leaves() {
    require(assess(change(R"("executed_qty":"0.04")", R"("executed_qty":"0")")) == RestReadDisposition::inconsistent, "complete with zero fill is inconsistent");
    require(assess(change(R"("leaves_qty":"0")", R"("leaves_qty":"0.01")")) == RestReadDisposition::inconsistent, "complete with leaves is inconsistent");
}

void all_statuses_and_partial_cancellation() {
    for (const auto status : {"pending", "active", "cancel_pending"}) {
        const auto body = change("complete", status);
        require(assess(body) == RestReadDisposition::working, "nonterminal reads cannot finalize orders");
    }
    require(assess(change("complete", "initialized")) == RestReadDisposition::inconsistent, "unsubmitted order cannot contain executions");
    auto initialized = change(R"("executed_qty":"0.04")", R"("executed_qty":"0")");
    initialized.replace(initialized.find("complete"), 8, "initialized");
    require(assess(initialized) == RestReadDisposition::working, "unsubmitted zero execution remains working");
    for (const auto status : {"canceled", "failed"}) {
        RestOrderDetail out;
        const auto body = change("complete", status);
        require(decode_rest_spot_order(body, {9, 6}, out) == RestDecodeError::none && out.executed_base_atoms == 40'000'000,
                "terminal non-success preserves partial fill");
        require(assess(body) == RestReadDisposition::terminal_needs_reconciliation, "cancellation or failure does not erase executions");
    }
    require(assess(change("complete", "filled")) == RestReadDisposition::unknown, "old mock filled word is not REST complete");
    require(assess(change("complete", "future-status")) == RestReadDisposition::unknown, "unknown status cannot finalize");
}

void identity_and_request_must_match() {
    for (const auto& [from, to] : {
             std::pair{"order-1", "order-2"}, {"sol-asset-1", "other-asset"}, {"SOL", "BTC"},
             {"USDC", "PYUSD"}, {"\"buy\"", "\"sell\""}, {"\"market\"", "\"limit\""},
             {"\"quote\"", "\"base\""}, {"\"qty\":\"5\"", "\"qty\":\"6\""}}) {
        require(assess(change(from, to)) == RestReadDisposition::identity_conflict, "different order identity or request refused");
    }
    require(assess(change(R"("asset_id":"sol-asset-1")", R"("asset_id":null)")) == RestReadDisposition::incomplete, "null asset cannot bind catalog");
    require(assess_rest_spot_order(200, kOrder, {9, 6}, {}) == RestReadDisposition::identity_conflict, "unset expected identity refused");
}

void no_http_error_proves_absence() {
    for (const auto http : {0U, 201U, 401U, 403U, 404U, 409U, 429U, 500U, 503U}) {
        require(assess(kOrder, http) == RestReadDisposition::unknown, "only HTTP200 is order evidence");
    }
    require(assess("{}") == RestReadDisposition::unknown, "empty response cannot finalize");
}

void invalid_amounts_never_round_or_overflow() {
    for (const auto value : {"-1", "1e3", "NaN", "0.0000001", "9223372036854.775808"}) {
        const auto body = change(R"("fee":"0.05")", std::string("\"fee\":\"") + value + "\"");
        require(assess(body) == RestReadDisposition::unknown, "invalid fee refused without rounding");
    }
    require(assess(change(R"("executed_qty":"0.04")", R"("executed_qty":"0.0000000001")")) == RestReadDisposition::unknown, "base precision enforced");
    require(assess(change(R"("qty":"5")", R"("qty":"0")")) == RestReadDisposition::unknown, "zero request refused");
    require(assess(change(R"("executed_vwap":"125.000000000001")", R"("executed_vwap":"-1")")) == RestReadDisposition::unknown, "negative VWAP refused");
    require(assess(change(R"("executed_vwap":"125.000000000001")", R"("executed_vwap":"0")")) == RestReadDisposition::inconsistent, "positive execution requires positive VWAP");
}

void parsing_is_transactional_and_bounded() {
    RestOrderDetail out;
    require(decode_rest_spot_order(kOrder, {9, 6}, out) == RestDecodeError::none, "initial decode");
    require(decode_rest_spot_order(change(R"("status":"complete")", R"("status":"complete","status":"failed")"), {9, 6}, out) == RestDecodeError::malformed, "duplicate status refused");
    require(out.order_id.empty() && !out.executed_base_atoms.has_value(), "failure clears prior output");
    require(decode_rest_spot_order(kOrder, {19, 6}, out) == RestDecodeError::bad_amount, "invalid decimal profile refused");
    require(assess(std::string(kMaxTmResponseBytes + 1, ' ')) == RestReadDisposition::unknown, "oversized response refused");
    require(assess(change("order-1", std::string(97, 'a'))) == RestReadDisposition::unknown, "oversized identity refused");
    require(assess(change("order-1", "bad/id")) == RestReadDisposition::unknown, "unsafe identity refused");
    require(assess(change(R"("fee":"0.05")", R"("fee":0.05)")) == RestReadDisposition::unknown, "numeric JSON fee not silently coerced");
}

void non_defi_and_perp_are_outside_contract() {
    require(assess(change("defi", "cefi")) == RestReadDisposition::unknown, "CeFi excluded");
    require(assess(change("spot", "perp")) == RestReadDisposition::unknown, "perpetual excluded");
}

RestReadComparison compare(std::string_view before, std::string_view after, unsigned http = 200) {
    return compare_rest_spot_reads(200, before, http, after, {9, 6}, expectation());
}

void duplicate_read_is_not_a_new_fill() {
    require(compare(kOrder, kOrder) == RestReadComparison::unchanged, "duplicate evidence has no new execution");
    const auto cosmetic = change(R"("price":"100")", R"("price":"101")");
    require(compare(kOrder, cosmetic) == RestReadComparison::unchanged, "reference price change is not a fill");
}

void terminal_cannot_reopen_or_change_execution() {
    require(compare(kOrder, change("complete", "active")) == RestReadComparison::contradictory_or_stale, "terminal cannot reopen");
    for (const auto& [from, to] : {std::pair{"complete", "canceled"}, {"tx-1", "tx-2"},
             {"125.000000000001", "126"}, {"0.05", "0.06"}}) {
        require(compare(kOrder, change(from, to)) == RestReadComparison::contradictory_or_stale, "terminal evidence cannot silently change");
    }
}

void cumulative_regression_requires_reconciliation() {
    const auto previous = change("complete", "active");
    for (const auto& [from, to] : {std::pair{R"("executed_qty":"0.04")", R"("executed_qty":"0.03")"},
             {R"("fee":"0.05")", R"("fee":"0.04")"}}) {
        auto next = change(from, to);
        next.replace(next.find("complete"), 8, "active");
        require(compare(previous, next) == RestReadComparison::contradictory_or_stale, "cumulative values cannot regress");
    }
    auto next = change("125.000000000001", "126");
    next.replace(next.find("complete"), 8, "active");
    require(compare(previous, next) == RestReadComparison::contradictory_or_stale, "same cumulative quantity cannot silently revise VWAP");
}

void partial_then_complete_is_only_evidence() {
    auto previous = change(R"("executed_qty":"0.04")", R"("executed_qty":"0.02")");
    previous.replace(previous.find("complete"), 8, "active");
    require(compare(previous, kOrder) == RestReadComparison::changed_needs_reconciliation, "cumulative growth still needs cash reconciliation");
    require(compare(previous, change("complete", "canceled")) == RestReadComparison::changed_needs_reconciliation, "late fill racing cancel is retained");
}

void incomplete_followup_does_not_erase_evidence() {
    require(compare(kOrder, kOrder, 404) == RestReadComparison::unusable, "404 followup cannot erase execution");
    require(compare(kOrder, change(R"("fee":"0.05",)", "")) == RestReadComparison::unusable, "missing fee followup cannot erase fee");
    require(compare("{}", kOrder) == RestReadComparison::unusable, "unusable prior read is not a zero baseline");
    require(compare(kOrder, change("order-1", "order-2")) == RestReadComparison::unusable, "different order cannot enter comparison");
}

void base_sell_and_owned_output() {
    auto body = change(R"("qty":"5","qty_unit":"quote")", R"("qty":"0.04","qty_unit":"base")");
    body.replace(body.find("\"buy\""), 5, "\"sell\"");
    RestOrderDetail out;
    require(decode_rest_spot_order(body, {9, 6}, out) == RestDecodeError::none && out.requested_atoms == 40'000'000, "base sell uses base decimals");
    auto expected = expectation();
    expected.side = Side::sell;
    expected.qty_unit = QtyUnit::base;
    expected.requested_atoms = 40'000'000;
    require(assess_rest_spot_order(200, body, {9, 6}, expected) == RestReadDisposition::terminal_needs_reconciliation, "exact base sell matches");
    auto overfill = body;
    const auto field = overfill.find(R"("executed_qty":"0.04")");
    overfill.replace(field, std::string_view(R"("executed_qty":"0.04")").size(), R"("executed_qty":"0.05")");
    require(assess_rest_spot_order(200, overfill, {9, 6}, expected) == RestReadDisposition::inconsistent, "base order overfill refused");
    body.assign(body.size(), 'x');
    require(out.order_id.view() == "order-1" && out.executed_vwap.view() == "125.000000000001", "result owns retained fields");
}

void missing_market_class_requires_exact_catalog_binding() {
    RestSpotAsset asset{*tm_text("sol-asset-1"), *tm_text("solana"), *tm_text("SOL"), *tm_text("USDC"), {9, 6}};
    auto body = change(R"("asset_type":"spot",)", "");
    RestOrderDetail out;
    require(decode_rest_spot_order(body, {9, 6}, out) == RestDecodeError::unsupported,
            "missing asset class cannot default to spot");
    require(decode_rest_spot_order(body, {9, 6}, out, &asset) == RestDecodeError::none &&
                out.requested_atoms == 5'000'000 && out.executed_base_atoms == 40'000'000,
            "catalog-bound spot decodes omitted class with exact units");
    require(assess_rest_spot_order(200, body, {9, 6}, expectation(), &asset) ==
                RestReadDisposition::terminal_needs_reconciliation,
            "catalog binding does not prove settlement");
    require(compare_rest_spot_reads(200, body, 200, body, {9, 6}, expectation(), &asset) ==
                RestReadComparison::unchanged, "catalog-bound repeated read is not a new fill");
    asset.asset_id = *tm_text("other-asset");
    require(decode_rest_spot_order(body, {9, 6}, out, &asset) == RestDecodeError::invalid_field && out.order_id.empty(),
            "catalog asset mismatch refuses and clears output");
    asset.asset_id = *tm_text("sol-asset-1");
    asset.decimals.base = 8;
    require(decode_rest_spot_order(body, {9, 6}, out, &asset) == RestDecodeError::invalid_field,
            "catalog precision mismatch refused");
    asset.decimals.base = 9;
    asset.base_asset = *tm_text("cbBTC");
    require(decode_rest_spot_order(body, {9, 6}, out, &asset) == RestDecodeError::invalid_field,
            "catalog symbol mismatch refused");
    asset.base_asset = *tm_text("SOL");
    require(decode_rest_spot_order(change("spot", "perp"), {9, 6}, out, &asset) == RestDecodeError::unsupported,
            "catalog cannot override explicit nonspot class");
    body.insert(1, R"("chain":"base",)");
    require(decode_rest_spot_order(body, {9, 6}, out, &asset) == RestDecodeError::invalid_field,
            "catalog cannot override explicit wrong chain");
}
}

int main(int argc, char** argv) {
    const std::pair<const char*, void (*)()> tests[] = {
        {"missing_market_class_requires_exact_catalog_binding", missing_market_class_requires_exact_catalog_binding},
        {"duplicate_read_is_not_a_new_fill", duplicate_read_is_not_a_new_fill},
        {"terminal_cannot_reopen_or_change_execution", terminal_cannot_reopen_or_change_execution},
        {"cumulative_regression_requires_reconciliation", cumulative_regression_requires_reconciliation},
        {"partial_then_complete_is_only_evidence", partial_then_complete_is_only_evidence},
        {"incomplete_followup_does_not_erase_evidence", incomplete_followup_does_not_erase_evidence},
        {"quote_buy_units_and_vwap_are_independent", quote_buy_units_and_vwap_are_independent},
        {"missing_execution_is_not_zero", missing_execution_is_not_zero},
        {"complete_requires_positive_fill_and_zero_leaves", complete_requires_positive_fill_and_zero_leaves},
        {"all_statuses_and_partial_cancellation", all_statuses_and_partial_cancellation},
        {"identity_and_request_must_match", identity_and_request_must_match},
        {"no_http_error_proves_absence", no_http_error_proves_absence},
        {"invalid_amounts_never_round_or_overflow", invalid_amounts_never_round_or_overflow},
        {"parsing_is_transactional_and_bounded", parsing_is_transactional_and_bounded},
        {"non_defi_and_perp_are_outside_contract", non_defi_and_perp_are_outside_contract},
        {"base_sell_and_owned_output", base_sell_and_owned_output},
    };
    unsigned ran = 0, failed = 0;
    for (const auto& [name, test] : tests) {
        if (argc == 2 && std::string_view(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    return ran != 0 && failed == 0 ? 0 : 1;
}
