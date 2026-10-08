#include "velocita/v2/node_metrics_text.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool has_line(const std::string& text, const std::string& line) {
    return text.find(line + "\n") != std::string::npos;
}

NodeHealth fixture() {
    NodeHealth h;
    h.node_id = 2;
    h.pid = 41;
    h.session_epoch = 4;
    h.role = 1;
    h.ready = true;
    h.term = 168;
    h.leader_id = 1;
    h.current_index = 12;
    h.commit_index = 11;
    h.applied_index = 10;
    h.durable_index = 12;
    h.turns = 9000;
    h.last_turn_monotonic_ns = 5'000'000'000ULL;
    h.auth_refusals = 3;
    h.fence_refusals = 4;
    h.peer_busy_holds = 5;
    h.peer_backpressure = 6;
    h.ingress_refusals = 7;
    h.results_sent = 8;
    h.results_dropped = 9;
    h.secret_reloads = 2;
    h.secret_reload_failures = 1;
    h.retained_ordinary = 6100;
    h.retained_protected = 3;
    h.compaction_floor = 5900;
    h.snapshot_index = 5800;
    h.stranded_voters = 1;
    h.compactions = 12;
    h.last_compaction_ms = 350;
    h.receipts = 200;
    h.pinned_receipts = 17;
    h.pinned_receipts_peak = 31;
    h.archive_bytes = 700'000'000;
    h.archive_byte_limit = 1'073'741'824;
    h.archive_objects = 1'209;
    h.archive_object_limit = 4'096;
    h.policies = 5;
    h.quorum_step_downs = 4;
    h.ledger_count = 1;
    h.ledgers[0] = {2, 3, 12, -24'048'000, 24'000'000, -48'000, 48'000, -48'001, 10'000'000};
    h.ledgers[1] = {9, 9, 9, 9, 9, 9, 9, 9, 9};
    return h;
}

void known_voter_renders_every_series() {
    const auto text = render_node_metrics(fixture(), "pi", 7'250'000'000ULL);
    const std::vector<std::string> expected{
        "velocita_v2_up{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_telemetry_age_seconds{host=\"pi\",node=\"2\"} 2.250",
        "velocita_v2_role{host=\"pi\",node=\"2\",role=\"none\"} 0",
        "velocita_v2_role{host=\"pi\",node=\"2\",role=\"follower\"} 1",
        "velocita_v2_role{host=\"pi\",node=\"2\",role=\"candidate\"} 0",
        "velocita_v2_role{host=\"pi\",node=\"2\",role=\"leader\"} 0",
        "velocita_v2_ready{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_failed{host=\"pi\",node=\"2\"} 0",
        "velocita_v2_term{host=\"pi\",node=\"2\"} 168",
        "velocita_v2_leader_id{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_session_epoch{host=\"pi\",node=\"2\"} 4",
        "velocita_v2_log_index{host=\"pi\",node=\"2\",kind=\"current\"} 12",
        "velocita_v2_log_index{host=\"pi\",node=\"2\",kind=\"commit\"} 11",
        "velocita_v2_log_index{host=\"pi\",node=\"2\",kind=\"applied\"} 10",
        "velocita_v2_log_index{host=\"pi\",node=\"2\",kind=\"durable\"} 12",
        "velocita_v2_owner_turns_total{host=\"pi\",node=\"2\"} 9000",
        "velocita_v2_auth_refusals_total{host=\"pi\",node=\"2\"} 3",
        "velocita_v2_fence_refusals_total{host=\"pi\",node=\"2\"} 4",
        "velocita_v2_peer_busy_holds_total{host=\"pi\",node=\"2\"} 5",
        "velocita_v2_peer_backpressure_total{host=\"pi\",node=\"2\"} 6",
        "velocita_v2_ingress_refusals_total{host=\"pi\",node=\"2\"} 7",
        "velocita_v2_results_sent_total{host=\"pi\",node=\"2\"} 8",
        "velocita_v2_results_dropped_total{host=\"pi\",node=\"2\"} 9",
        "velocita_v2_secret_reloads_total{host=\"pi\",node=\"2\"} 2",
        "velocita_v2_secret_reload_failures_total{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_retained_log_entries{host=\"pi\",node=\"2\",pool=\"ordinary\"} 6100",
        "velocita_v2_retained_log_entries{host=\"pi\",node=\"2\",pool=\"protected\"} 3",
        "velocita_v2_retained_log_capacity{host=\"pi\",node=\"2\",pool=\"ordinary\"} 8128",
        "velocita_v2_retained_log_capacity{host=\"pi\",node=\"2\",pool=\"protected\"} 64",
        "velocita_v2_compaction_floor{host=\"pi\",node=\"2\"} 5900",
        "velocita_v2_snapshot_index{host=\"pi\",node=\"2\"} 5800",
        "velocita_v2_stranded_voters{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_compactions_total{host=\"pi\",node=\"2\"} 12",
        "velocita_v2_last_compaction_seconds{host=\"pi\",node=\"2\"} 0.350",
        "velocita_v2_receipts{host=\"pi\",node=\"2\",state=\"held\"} 200",
        "velocita_v2_receipts{host=\"pi\",node=\"2\",state=\"pinned\"} 17",
        "velocita_v2_pinned_receipts_peak{host=\"pi\",node=\"2\"} 31",
        "velocita_v2_receipt_capacity{host=\"pi\",node=\"2\"} 256",
        "velocita_v2_quorum_step_downs_total{host=\"pi\",node=\"2\"} 4",
        "velocita_v2_policies{host=\"pi\",node=\"2\"} 5",
        "velocita_v2_policy_capacity{host=\"pi\",node=\"2\"} 8",
        "velocita_v2_account_position_lots{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} 12",
        "velocita_v2_account_cash_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} -24048000",
        "velocita_v2_account_cost_basis_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} 24000000",
        "velocita_v2_account_realized_pnl_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} -48000",
        "velocita_v2_account_fees_paid_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} 48000",
        "velocita_v2_account_window_realized_pnl_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} -48001",
        "velocita_v2_account_max_window_loss_micros{host=\"pi\",node=\"2\",account=\"2\",instrument=\"3\"} 10000000",
        "velocita_v2_archive_bytes{host=\"pi\",node=\"2\"} 700000000",
        "velocita_v2_archive_byte_limit{host=\"pi\",node=\"2\"} 1073741824",
        "velocita_v2_archive_segments{host=\"pi\",node=\"2\"} 1209",
        "velocita_v2_archive_segment_limit{host=\"pi\",node=\"2\"} 4096",
    };
    for (const auto& line : expected) require(has_line(text, line), "missing or wrong line: " + line);
    require(text.find("account=\"9\"") == std::string::npos, "a ledger beyond the reported count was published");
    require(text.find("# TYPE velocita_v2_auth_refusals_total counter\n") != std::string::npos,
            "counters are not typed as counters");
    require(text.find("# TYPE velocita_v2_term gauge\n") != std::string::npos, "gauges are not typed");
}

void unknown_voter_is_down_not_zero() {
    const auto text = render_node_metrics(std::nullopt, "nuc", 1);
    require(has_line(text, "velocita_v2_up{host=\"nuc\",node=\"unknown\"} 0"), "unknown voter not reported down");
    std::size_t samples = 0;
    for (std::size_t at = text.find("\nvelocita_v2_"); at != std::string::npos;
         at = text.find("\nvelocita_v2_", at + 1))
        ++samples;
    if (text.rfind("velocita_v2_", 0) == 0) ++samples;
    require(samples == 1, "an unknown voter rendered samples other than up=0");
}

void clock_skew_never_reports_negative_age() {
    auto h = fixture();
    h.last_turn_monotonic_ns = 9'000'000'000ULL;
    const auto text = render_node_metrics(h, "pi", 7'000'000'000ULL);
    require(has_line(text, "velocita_v2_telemetry_age_seconds{host=\"pi\",node=\"2\"} 0.000"),
            "future stamp not clamped to zero age");
}

}

int main() {
    try {
        known_voter_renders_every_series();
        unknown_voter_is_down_not_zero();
        clock_skew_never_reports_negative_age();
        std::cout << "node metrics text tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node metrics text tests failed: " << error.what() << '\n';
        return 1;
    }
}
