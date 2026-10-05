#include "velocita/v2/kernel.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

RequestReceipt plain_receipt(const std::uint64_t caller, const std::uint64_t sequence) {
    ApplyResult applied;
    applied.code = ApplyCode::applied;
    return {{CallerId{caller}, RequestSequence{sequence}, {}}, applied};
}

RequestReceipt venue_fill_receipt(const std::uint64_t caller, const std::uint64_t sequence) {
    ApplyResult fill;
    fill.code = ApplyCode::applied;
    fill.detail = ApplyResultDetail::fill(VenueId{9}, ExecutionId{20'000 + sequence}, QuantityLots::from_atoms(1));
    fill.detail_kind = ApplyResultDetailKind::fill;
    return {{CallerId{caller}, RequestSequence{sequence}, {}}, fill};
}

void append(TradingState& state, const RequestReceipt& receipt) {
    state.receipts[state.receipt_count++] = receipt;
}

void an_empty_table_pins_nothing() {
    const auto state = std::make_unique<TradingState>();
    require(pinned_receipt_count(*state) == 0, "an empty table reported pinned receipts");
}

void ordinary_results_are_never_pinned() {
    auto state = std::make_unique<TradingState>();
    for (std::uint64_t sequence = 1; sequence <= 12; ++sequence) append(*state, plain_receipt(5, sequence));
    require(state->receipt_count == 12 && pinned_receipt_count(*state) == 0,
            "a plain applied result with nothing depending on it was pinned");
}

void venue_evidence_stays_pinned_until_its_producer_acknowledges_it() {
    auto state = std::make_unique<TradingState>();
    for (std::uint64_t sequence = 1; sequence <= 10; ++sequence) append(*state, venue_fill_receipt(40, sequence));
    for (std::uint64_t sequence = 1; sequence <= 5; ++sequence) append(*state, plain_receipt(5, sequence));
    require(pinned_receipt_count(*state) == 10, "unacknowledged venue evidence was not all pinned");

    state->caller_cursors[state->caller_cursor_count++] = CallerCursor{CallerId{40}, RequestSequence{}, RequestSequence{3}};
    require(pinned_receipt_count(*state) == 7, "acknowledging through 3 must unpin exactly sequences 1 to 3");

    state->caller_cursors[0].gateway_results_acknowledged_through = RequestSequence{10};
    require(pinned_receipt_count(*state) == 0, "fully acknowledged evidence with no order left was still pinned");
}

void a_full_table_of_unacknowledged_evidence_is_all_pinned() {
    auto state = std::make_unique<TradingState>();
    for (std::uint64_t sequence = 1; sequence <= kMaxRequestReceipts; ++sequence)
        append(*state, venue_fill_receipt(40, sequence));
    require(pinned_receipt_count(*state) == kMaxRequestReceipts, "a full table of pinned receipts was undercounted");
}
}

int main() {
    an_empty_table_pins_nothing();
    ordinary_results_are_never_pinned();
    venue_evidence_stays_pinned_until_its_producer_acknowledges_it();
    a_full_table_of_unacknowledged_evidence_is_all_pinned();
    return 0;
}
