#include "velocita/shared_metrics.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("velocita-metrics-test-" + std::to_string(unique));
    const auto path = directory / "raft.metrics";
    try {
        {
            velocita::SharedMetrics metrics(2, path);
            metrics.record_tick(123456);
            metrics.record_owner_loop(100, 2'500'000, 9, 4, 7, 3);
            metrics.record_status({2, 1, "follower", 7, 1, 12, 11, 10, 42});
            metrics.record_request(64);
            metrics.record_peer_fragment(128);
            metrics.record_outbound_peer_message();
            metrics.set_peer_queue_depth(3);
            metrics.set_peer_queue_depth(1);
            metrics.record_offer(velocita::OfferChannel::peer,
                                 velocita::OfferOutcome::back_pressured);

            const auto snapshot = velocita::read_shared_metrics(path);
            require(snapshot.has_value(), "reader rejected a valid metrics page");
            require(snapshot->active && snapshot->header.node_id == 2,
                    "metrics identity or active flag is wrong");
            require(snapshot->role == velocita::MetricRole::follower &&
                        snapshot->leader_id == 1 && snapshot->term == 7,
                    "Raft status snapshot is wrong");
            require(snapshot->current_index == 12 && snapshot->commit_index == 11 &&
                        snapshot->applied_index == 10 && snapshot->counter_value == 42,
                    "Raft indexes or FSM value are wrong");
            require(snapshot->request_fragments_total == 1 &&
                        snapshot->request_bytes_total == 64 &&
                        snapshot->peer_fragments_total == 1 &&
                        snapshot->peer_bytes_total == 128,
                    "Aeron receive counters are wrong");
            require(snapshot->owner_loop_iterations_total == 100 &&
                        snapshot->owner_loop_max_gap_nanoseconds == 2'500'000 &&
                        snapshot->client_budget_exhausted_total == 9 &&
                        snapshot->peer_budget_exhausted_total == 4 &&
                        snapshot->client_poll_skipped_tick_slack_total == 7 &&
                        snapshot->client_poll_skipped_response_backlog_total == 3,
                    "owner-loop snapshot counters are wrong");
            require(snapshot->peer_queue_depth == 1 &&
                        snapshot->peer_queue_high_watermark == 3,
                    "queue gauge or high-watermark is wrong");
            require(snapshot->peer_offer_back_pressured_total == 1,
                    "Aeron offer outcome is wrong");
        }
        const auto stopped = velocita::read_shared_metrics(path);
        require(stopped.has_value() && !stopped->active,
                "writer shutdown did not clear the active flag");
        std::filesystem::remove_all(directory);
        std::cout << "all metrics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "metrics test failed: " << error.what() << '\n';
        return 1;
    }
}
