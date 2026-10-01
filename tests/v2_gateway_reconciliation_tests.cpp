// compare_venue_book: the venue's own book against committed state at one cut
// (book ch50). Every expectation is derived from the fixture's inputs in the
// test, never from the function under test.
#include "velocita/v2/gateway_loop.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr AccountId kAccount{5};
constexpr InstrumentId kInstrument{4};

// A committed state holding POSITION lots on the served row, one on another
// row, and the given cluster orders.
std::unique_ptr<TradingState> state_with(std::int64_t position, std::vector<std::uint64_t> orders = {}) {
    auto state = std::make_unique<TradingState>();
    // Same instrument on both rows, the served one first and another account
    // after it, so neither "first match" nor "last match" passes by accident.
    state->risk_account_count = 3;
    state->risk_accounts[0].account_id = AccountId{4};
    state->risk_accounts[0].instrument_id = kInstrument;
    state->risk_accounts[0].position_quantity = QuantityLots::from_atoms(777);
    state->risk_accounts[1].account_id = kAccount;
    state->risk_accounts[1].instrument_id = kInstrument;
    state->risk_accounts[1].position_quantity = QuantityLots::from_atoms(position);
    state->risk_accounts[2].account_id = AccountId{6};
    state->risk_accounts[2].instrument_id = kInstrument;
    state->risk_accounts[2].position_quantity = QuantityLots::from_atoms(999);
    for (const auto id : orders) {
        state->orders[state->order_count].cluster_order_id = ClusterOrderId{id};
        state->orders[state->order_count].state = ClusterOrderState::filled;
        ++state->order_count;
    }
    return state;
}

sim::VenueBook book_with(std::int64_t base) {
    sim::VenueBook book;
    book.available = true;
    book.base = QuantityLots::from_atoms(base);
    return book;
}

constexpr BookCut kClean{100, 90, 0, 0};

void equal_position_at_a_clean_cut_matches() {
    const auto state = state_with(25);
    const auto result = compare_venue_book(*state, kAccount, kInstrument, book_with(25), kClean);
    require(result.verdict == BookVerdict::match, "25 kernel lots and 25 venue lots at a clean cut did not match");
    require(result.kernel_lots == 25 && result.venue_lots == 25, "the compared values are not the inputs");
}

void different_position_at_a_clean_cut_is_a_mismatch_with_both_values() {
    // The live break: the kernel holds 25 lots, a reset venue holds 0.
    const auto state = state_with(25);
    const auto result = compare_venue_book(*state, kAccount, kInstrument, book_with(0), kClean);
    require(result.verdict == BookVerdict::mismatch && result.kind == BookBreak::position,
            "a 25-lot kernel position beside a flat venue was not a position mismatch");
    require(result.kernel_lots == 25 && result.venue_lots == 0, "the mismatch does not carry both values");
    // Zero tolerance: one lot is a break.
    const auto one = compare_venue_book(*state, kAccount, kInstrument, book_with(24), kClean);
    require(one.verdict == BookVerdict::mismatch, "a one-lot difference was tolerated");
}

void absent_risk_account_is_a_flat_kernel_position() {
    auto state = std::make_unique<TradingState>();
    const auto flat = compare_venue_book(*state, kAccount, kInstrument, book_with(0), kClean);
    require(flat.verdict == BookVerdict::match, "no risk account and a flat venue did not match");
    const auto held = compare_venue_book(*state, kAccount, kInstrument, book_with(3), kClean);
    require(held.verdict == BookVerdict::mismatch && held.kernel_lots == 0 && held.venue_lots == 3,
            "venue base without any kernel account was not a mismatch");
}

void only_the_served_row_is_compared() {
    // Account 6 holds 999 lots; the served row (account 5) holds 25.
    const auto state = state_with(25);
    const auto result = compare_venue_book(*state, kAccount, kInstrument, book_with(25), kClean);
    require(result.verdict == BookVerdict::match && result.kernel_lots == 25,
            "another row's position leaked into the comparison");
}

void unavailable_venue_concludes_nothing() {
    const auto state = state_with(25);
    auto book = book_with(0);
    book.available = false;
    require(compare_venue_book(*state, kAccount, kInstrument, book, kClean).verdict == BookVerdict::unavailable,
            "an unavailable venue book produced a verdict");
}

void a_read_older_than_the_gateway_has_seen_is_not_comparable() {
    const auto state = state_with(25);
    const BookCut stale{89, 90, 0, 0};
    require(compare_venue_book(*state, kAccount, kInstrument, book_with(0), stale).verdict ==
                BookVerdict::not_comparable,
            "a read applied below the gateway's own high-water index produced a break");
    const BookCut equal{90, 90, 0, 0};
    require(compare_venue_book(*state, kAccount, kInstrument, book_with(25), equal).verdict == BookVerdict::match,
            "a read at exactly the seen index was not comparable");
}

void a_read_older_than_the_venue_committed_cut_is_not_comparable() {
    const auto state = state_with(0);
    auto book = book_with(25);
    book.committed_through = 101;  // the venue knows evidence committed at 101
    require(compare_venue_book(*state, kAccount, kInstrument, book, kClean).verdict == BookVerdict::not_comparable,
            "a read applied below the venue's committed cut produced a break");
    book.committed_through = 100;
    require(compare_venue_book(*state, kAccount, kInstrument, book, kClean).verdict == BookVerdict::mismatch,
            "a read at the venue's committed cut was not compared");
}

void venue_owned_reports_make_the_cut_not_comparable() {
    const auto state = state_with(0, {7});
    auto book = book_with(25);
    book.unacknowledged_reports = 2;
    book.pending_order_count = 1;
    book.pending_orders[0] = ClusterOrderId{7};
    require(compare_venue_book(*state, kAccount, kInstrument, book, kClean).verdict == BookVerdict::not_comparable,
            "a venue still owning reports for a held order produced a break");
}

void pending_gateway_evidence_makes_the_cut_not_comparable() {
    const auto state = state_with(0);
    const BookCut pending{100, 90, 1, 0};
    require(compare_venue_book(*state, kAccount, kInstrument, book_with(25), pending).verdict ==
                BookVerdict::not_comparable,
            "uncommitted gateway evidence produced a break");
}

void an_open_order_makes_the_cut_not_comparable() {
    const auto state = state_with(0);
    const BookCut open{100, 90, 0, 1};
    require(compare_venue_book(*state, kAccount, kInstrument, book_with(25), open).verdict ==
                BookVerdict::not_comparable,
            "an open served-row order produced a break");
}

void a_venue_report_for_an_order_the_kernel_does_not_hold_is_a_mismatch() {
    const auto state = state_with(0, {7});
    auto book = book_with(0);
    book.unacknowledged_reports = 1;
    book.pending_order_count = 2;
    book.pending_orders[0] = ClusterOrderId{7};
    book.pending_orders[1] = ClusterOrderId{8};
    const auto result = compare_venue_book(*state, kAccount, kInstrument, book, kClean);
    require(result.verdict == BookVerdict::mismatch && result.kind == BookBreak::unknown_order &&
                result.order == ClusterOrderId{8},
            "a venue report naming order 8, which the kernel never held, was not an order mismatch");
}

void a_truncated_pending_list_is_not_comparable() {
    const auto state = state_with(0);
    auto book = book_with(0);
    book.pending_orders_truncated = true;
    require(compare_venue_book(*state, kAccount, kInstrument, book, kClean).verdict == BookVerdict::not_comparable,
            "a venue that could not list every pending order was compared");
}

void only_a_fence_is_rearmable() {
    // The owner's rule: a fence (term, leader, read gap) re-arms; everything
    // else needs an operator.
    const std::vector<std::pair<GatewayStopCause, bool>> expected{
        {GatewayStopCause::fence, true},
        {GatewayStopCause::unknown_outcome, false},
        {GatewayStopCause::conflict, false},
        {GatewayStopCause::refused_evidence, false},
        {GatewayStopCause::control_change, false},
        {GatewayStopCause::reconciliation_mismatch, false},
        {GatewayStopCause::reconciliation_exhausted, false},
        {GatewayStopCause::capacity, false},
        {GatewayStopCause::corrupt_state, false},
        {GatewayStopCause::wrong_cluster, false},
        {GatewayStopCause::authority, false},
    };
    require(expected.size() == static_cast<std::size_t>(GatewayStopCause::count), "a stop cause is not classified");
    for (const auto& [cause, rearm] : expected)
        require(rearmable(cause) == rearm, std::string("cause ") + describe(cause) + " has the wrong re-arm class");
}

void every_cause_and_verdict_renders_from_the_first_scrape() {
    GatewayMetrics metrics;
    metrics.stops[static_cast<std::size_t>(GatewayStopCause::fence)] = 3;
    metrics.rearms = 2;
    metrics.reconciliation_mismatch = true;
    metrics.reconciliation_kernel_lots = 25;
    metrics.reconciliation_venue_lots = 0;
    metrics.stop_active[static_cast<std::size_t>(GatewayStopCause::reconciliation_mismatch)] = true;
    const auto text = render_gateway_metrics(metrics);
    const std::vector<std::string> lines{
        "velocita_gateway_stops_total{cause=\"fence\"} 3",
        "velocita_gateway_stops_total{cause=\"unknown_outcome\"} 0",
        "velocita_gateway_stops_total{cause=\"reconciliation_mismatch\"} 0",
        "velocita_gateway_stops_total{cause=\"control_change\"} 0",
        "velocita_gateway_rearms_total 2",
        "velocita_gateway_reconciliation_mismatch 1",
        "velocita_gateway_reconciliation_position_lots{source=\"kernel\"} 25",
        "velocita_gateway_reconciliation_position_lots{source=\"venue\"} 0",
        "velocita_gateway_stop_cause{cause=\"reconciliation_mismatch\"} 1",
        "velocita_gateway_stop_cause{cause=\"fence\"} 0",
        "velocita_gateway_stop_cause{cause=\"awaiting_arm\"} 0",
        "velocita_gateway_reconciliation_verdict{verdict=\"unavailable\"} 1",
        "velocita_gateway_reconciliation_verdict{verdict=\"match\"} 0",
    };
    for (const auto& line : lines) require(text.find(line + "\n") != std::string::npos, "missing: " + line);
    // Every cause is present from the first scrape, so increase() sees its first stop.
    const std::vector<std::string> causes{"fence", "unknown_outcome", "conflict", "refused_evidence",
                                          "control_change", "reconciliation_mismatch", "reconciliation_exhausted",
                                          "capacity", "corrupt_state", "wrong_cluster", "authority"};
    for (const auto& cause : causes) {
        require(text.find("velocita_gateway_stops_total{cause=\"" + cause + "\"} ") != std::string::npos,
                "stops_total lacks cause " + cause);
        require(text.find("velocita_gateway_stop_cause{cause=\"" + cause + "\"} ") != std::string::npos,
                "stop_cause lacks cause " + cause);
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"equal_position_at_a_clean_cut_matches", equal_position_at_a_clean_cut_matches},
        {"different_position_at_a_clean_cut_is_a_mismatch_with_both_values",
         different_position_at_a_clean_cut_is_a_mismatch_with_both_values},
        {"absent_risk_account_is_a_flat_kernel_position", absent_risk_account_is_a_flat_kernel_position},
        {"only_the_served_row_is_compared", only_the_served_row_is_compared},
        {"unavailable_venue_concludes_nothing", unavailable_venue_concludes_nothing},
        {"a_read_older_than_the_gateway_has_seen_is_not_comparable",
         a_read_older_than_the_gateway_has_seen_is_not_comparable},
        {"a_read_older_than_the_venue_committed_cut_is_not_comparable",
         a_read_older_than_the_venue_committed_cut_is_not_comparable},
        {"venue_owned_reports_make_the_cut_not_comparable", venue_owned_reports_make_the_cut_not_comparable},
        {"pending_gateway_evidence_makes_the_cut_not_comparable",
         pending_gateway_evidence_makes_the_cut_not_comparable},
        {"an_open_order_makes_the_cut_not_comparable", an_open_order_makes_the_cut_not_comparable},
        {"a_venue_report_for_an_order_the_kernel_does_not_hold_is_a_mismatch",
         a_venue_report_for_an_order_the_kernel_does_not_hold_is_a_mismatch},
        {"a_truncated_pending_list_is_not_comparable", a_truncated_pending_list_is_not_comparable},
        {"only_a_fence_is_rearmable", only_a_fence_is_rearmable},
        {"every_cause_and_verdict_renders_from_the_first_scrape", every_cause_and_verdict_renders_from_the_first_scrape},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << std::endl;
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << name << ": " << error.what() << std::endl;
        }
    }
    return failures == 0 ? 0 : 1;
}
