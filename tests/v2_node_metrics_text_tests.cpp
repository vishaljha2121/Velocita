#include "velocita/v2/node_metrics_text.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Expected lines are hand-written from the fixture below, never produced by the
// renderer under test.

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
    h.role = 1;  // follower
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
        // Retained Raft log: entries held per pool, against the fixed capacity
        // (8,192 = 8,128 ordinary + 64 reserved for operator control).
        "velocita_v2_retained_log_entries{host=\"pi\",node=\"2\",pool=\"ordinary\"} 6100",
        "velocita_v2_retained_log_entries{host=\"pi\",node=\"2\",pool=\"protected\"} 3",
        "velocita_v2_retained_log_capacity{host=\"pi\",node=\"2\",pool=\"ordinary\"} 8128",
        "velocita_v2_retained_log_capacity{host=\"pi\",node=\"2\",pool=\"protected\"} 64",
        "velocita_v2_compaction_floor{host=\"pi\",node=\"2\"} 5900",
        "velocita_v2_snapshot_index{host=\"pi\",node=\"2\"} 5800",
        "velocita_v2_stranded_voters{host=\"pi\",node=\"2\"} 1",
        "velocita_v2_compactions_total{host=\"pi\",node=\"2\"} 12",
        "velocita_v2_last_compaction_seconds{host=\"pi\",node=\"2\"} 0.350",
    };
    for (const auto& line : expected) require(has_line(text, line), "missing or wrong line: " + line);
    require(text.find("# TYPE velocita_v2_auth_refusals_total counter\n") != std::string::npos,
            "counters are not typed as counters");
    require(text.find("# TYPE velocita_v2_term gauge\n") != std::string::npos, "gauges are not typed");
}

void unknown_voter_is_down_not_zero() {
    const auto text = render_node_metrics(std::nullopt, "nuc", 1);
    require(has_line(text, "velocita_v2_up{host=\"nuc\",node=\"unknown\"} 0"), "unknown voter not reported down");
    // No other velocita_v2 sample may appear: absence, not zeros.
    std::size_t samples = 0;
    for (std::size_t at = text.find("\nvelocita_v2_"); at != std::string::npos;
         at = text.find("\nvelocita_v2_", at + 1))
        ++samples;
    if (text.rfind("velocita_v2_", 0) == 0) ++samples;
    require(samples == 1, "an unknown voter rendered samples other than up=0");
}

void clock_skew_never_reports_negative_age() {
    auto h = fixture();
    h.last_turn_monotonic_ns = 9'000'000'000ULL;  // stamp "after" now
    const auto text = render_node_metrics(h, "pi", 7'000'000'000ULL);
    require(has_line(text, "velocita_v2_telemetry_age_seconds{host=\"pi\",node=\"2\"} 0.000"),
            "future stamp not clamped to zero age");
}

}  // namespace

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
