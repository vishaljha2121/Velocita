#include "velocita/v2/node_metrics_text.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/retained_log.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace velocita::v2 {
namespace {

struct Writer {
    std::ostringstream out;
    std::string labels;

    void type(const char* name, const char* kind, const char* help) {
        out << "# HELP " << name << ' ' << help << '\n' << "# TYPE " << name << ' ' << kind << '\n';
    }
    template <typename T>
    void sample(const char* name, T value, std::string_view extra = {}) {
        out << name << '{' << labels << extra << "} " << value << '\n';
    }
};

}

std::string render_node_metrics(const std::optional<NodeHealth>& health, std::string_view host,
                                const std::uint64_t now_monotonic_ns) {
    Writer w;
    w.labels = "host=\"" + std::string(host) + "\",node=\"" +
               (health ? std::to_string(health->node_id) : std::string("unknown")) + "\"";
    w.type("velocita_v2_up", "gauge", "1 when this voter's health page is readable and published.");
    w.sample("velocita_v2_up", health ? 1 : 0);
    if (!health) return w.out.str();
    const auto& h = *health;

    const auto age_ns = now_monotonic_ns > h.last_turn_monotonic_ns ? now_monotonic_ns - h.last_turn_monotonic_ns : 0;
    std::array<char, 32> age{};
    std::snprintf(age.data(), age.size(), "%.3f", static_cast<double>(age_ns) / 1e9);
    w.type("velocita_v2_telemetry_age_seconds", "gauge", "Seconds since the voter last published health.");
    w.sample("velocita_v2_telemetry_age_seconds", age.data());

    w.type("velocita_v2_role", "gauge", "One-hot Raft role of this voter.");
    static constexpr std::array<const char*, 4> kRoles{"none", "follower", "candidate", "leader"};
    for (std::size_t r = 0; r < kRoles.size(); ++r)
        w.sample("velocita_v2_role", h.role == r ? 1 : 0, std::string(",role=\"") + kRoles[r] + "\"");

    const auto gauge = [&](const char* name, const char* help, auto value) {
        w.type(name, "gauge", help);
        w.sample(name, value);
    };
    gauge("velocita_v2_ready", "1 when the voter accepts caller commands (recovery barrier passed).", h.ready ? 1 : 0);
    gauge("velocita_v2_failed", "1 when the voter latched a fatal failure (fail-stop).", h.failed ? 1 : 0);
    gauge("velocita_v2_term", "Current Raft term; a change means an election happened.", h.term);
    gauge("velocita_v2_leader_id", "Leader this voter currently follows (-1 unknown).", h.leader_id);
    gauge("velocita_v2_session_epoch", "Process incarnation; increases on every restart.", h.session_epoch);

    w.type("velocita_v2_log_index", "gauge", "Raft log positions: current (appended), durable, commit, applied.");
    w.sample("velocita_v2_log_index", h.current_index, ",kind=\"current\"");
    w.sample("velocita_v2_log_index", h.commit_index, ",kind=\"commit\"");
    w.sample("velocita_v2_log_index", h.applied_index, ",kind=\"applied\"");
    w.sample("velocita_v2_log_index", h.durable_index, ",kind=\"durable\"");

    w.type("velocita_v2_retained_log_entries", "gauge", "Raft log entries held in memory, per pool.");
    w.sample("velocita_v2_retained_log_entries", h.retained_ordinary, ",pool=\"ordinary\"");
    w.sample("velocita_v2_retained_log_entries", h.retained_protected, ",pool=\"protected\"");
    w.type("velocita_v2_retained_log_capacity", "gauge", "Fixed retained-log capacity, per pool.");
    w.sample("velocita_v2_retained_log_capacity", kRetainedLogOrdinaryEntries, ",pool=\"ordinary\"");
    w.sample("velocita_v2_retained_log_capacity", kRetainedLogProtectedEntries, ",pool=\"protected\"");
    gauge("velocita_v2_compaction_floor", "Highest index every voter is known to hold durably; compaction stays below it.",
          h.compaction_floor);
    gauge("velocita_v2_snapshot_index", "Highest index compacted into a snapshot (0: none).", h.snapshot_index);
    gauge("velocita_v2_stranded_voters", "Leader: voters behind its snapshot; they need repair to catch up.",
          h.stranded_voters);
    std::array<char, 32> compaction{};
    std::snprintf(compaction.data(), compaction.size(), "%.3f", static_cast<double>(h.last_compaction_ms) / 1e3);
    gauge("velocita_v2_last_compaction_seconds", "How long the last compaction paused Raft progress.",
          compaction.data());

    w.type("velocita_v2_receipts", "gauge", "Request receipts: held (in the table) and pinned (cannot be dropped yet).");
    w.sample("velocita_v2_receipts", h.receipts, ",state=\"held\"");
    w.sample("velocita_v2_receipts", h.pinned_receipts, ",state=\"pinned\"");
    gauge("velocita_v2_pinned_receipts_peak", "Most pinned receipts after any command since this process started.",
          h.pinned_receipts_peak);
    gauge("velocita_v2_receipt_capacity", "Fixed receipt table capacity.", kMaxRequestReceipts);
    gauge("velocita_v2_policies", "Policy slots used; a slot is never freed within an incarnation.", h.policies);
    gauge("velocita_v2_policy_capacity", "Fixed policy table capacity.", kMaxPolicies);
    const std::array<std::pair<const char*, const char*>, 7> ledger_metrics{{
        {"velocita_v2_account_position_lots", "Position the kernel holds for the account, in lots."},
        {"velocita_v2_account_cash_micros", "Net cash the kernel booked from fills since the account was created."},
        {"velocita_v2_account_cost_basis_micros", "Cost of the lots held, at average cost."},
        {"velocita_v2_account_realized_pnl_micros", "Realized P&L after fees since the account was created."},
        {"velocita_v2_account_fees_paid_micros", "Fees the kernel booked from fills."},
        {"velocita_v2_account_window_realized_pnl_micros", "Realized P&L since the operator last reset the loss window."},
        {"velocita_v2_account_max_window_loss_micros", "Loss limit: new risk stops at this realized loss; 0 means none."}}};
    for (std::size_t metric = 0; metric < ledger_metrics.size(); ++metric) {
        w.type(ledger_metrics[metric].first, "gauge", ledger_metrics[metric].second);
        for (std::size_t index = 0; index < h.ledger_count && index < h.ledgers.size(); ++index) {
            const auto& l = h.ledgers[index];
            const std::array<std::int64_t, 7> values{l.position_lots, l.cash_micros, l.cost_basis_micros,
                l.realized_pnl_micros, l.fees_paid_micros, l.window_realized_pnl_micros, l.max_window_loss_micros};
            w.sample(ledger_metrics[metric].first, values[metric],
                     ",account=\"" + std::to_string(l.account_id) + "\",instrument=\"" +
                         std::to_string(l.instrument_id) + "\"");
        }
    }
    gauge("velocita_v2_archive_bytes", "Bytes of compacted log held in the archive.", h.archive_bytes);
    gauge("velocita_v2_archive_byte_limit", "Archive byte budget; ordinary commands are refused 16 MiB before it.",
          h.archive_byte_limit);
    gauge("velocita_v2_archive_segments", "Archive segments in the catalog.", h.archive_objects);
    gauge("velocita_v2_archive_segment_limit", "Archive catalog rows; ordinary commands are refused 4 rows before it.",
          h.archive_object_limit);

    const auto counter = [&](const char* name, const char* help, std::uint64_t value) {
        w.type(name, "counter", help);
        w.sample(name, value);
    };
    counter("velocita_v2_owner_turns_total", "Owner-loop turns; stops rising if the loop stalls.", h.turns);
    counter("velocita_v2_auth_refusals_total", "Frames refused by authentication (forged, foreign or malformed).",
            h.auth_refusals);
    counter("velocita_v2_fence_refusals_total", "Authentic frames refused as replays or stale sessions.", h.fence_refusals);
    counter("velocita_v2_peer_busy_holds_total", "Peer frames held while local persistence was pending.",
            h.peer_busy_holds);
    counter("velocita_v2_peer_backpressure_total", "Owner turns where a peer offer was refused (peer slow or down).",
            h.peer_backpressure);
    counter("velocita_v2_ingress_refusals_total", "Caller commands refused (not leader, capacity, impersonation...).",
            h.ingress_refusals);
    counter("velocita_v2_results_sent_total", "Committed results delivered to callers.", h.results_sent);
    counter("velocita_v2_results_dropped_total", "Committed results no configured caller could receive.",
            h.results_dropped);
    counter("velocita_v2_secret_reloads_total", "Secret reloads applied (rotation steps).", h.secret_reloads);
    counter("velocita_v2_secret_reload_failures_total", "Secret reloads refused; old keys kept.",
            h.secret_reload_failures);
    counter("velocita_v2_compactions_total", "Completed floor-gated compactions.", h.compactions);
    return w.out.str();
}

}
