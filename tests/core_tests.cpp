#include "velocita/raft_node.hpp"
#include "velocita/storage.hpp"
#include "velocita/wire.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void elect(velocita::RaftNode& node) {
    for (int i = 0; i < 30 && !node.is_leader(); ++i) node.tick(100);
    require(node.is_leader(), "single node did not elect itself");
}

void test_wire() {
    const velocita::Command command{velocita::CommandType::increment, 42, -7};
    const auto decoded = velocita::decode_command(velocita::encode_command(command));
    const auto decoded_fixed = velocita::decode_command(velocita::encode_command_fixed(command));
    require(decoded.type == command.type && decoded.request_id == 42 && decoded.amount == -7,
            "command wire round trip failed");
    require(decoded_fixed.type == command.type && decoded_fixed.request_id == 42 &&
                decoded_fixed.amount == -7,
            "fixed command wire round trip failed");

    velocita::Response response{velocita::ResponseStatus::ok, 42, 9, 3, 8};
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    response.benchmark = {velocita::kBenchmarkTimingValid, 10, 20, 30, 40, 50};
#endif
    const auto decoded_response = velocita::decode_response(velocita::encode_response(response));
    require(decoded_response.status == response.status && decoded_response.value == 9 &&
                decoded_response.term == 3 && decoded_response.index == 8,
            "response wire round trip failed");
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    require(decoded_response.benchmark.flags == velocita::kBenchmarkTimingValid &&
                decoded_response.benchmark.ingress_to_admission_ns == 10 &&
                decoded_response.benchmark.local_wal_append_ns == 20 &&
                decoded_response.benchmark.quorum_append_ns == 30 &&
                decoded_response.benchmark.admission_to_commit_ns == 40 &&
                decoded_response.benchmark.commit_to_apply_ns == 50,
            "benchmark response timing wire round trip failed");
#endif

    velocita::PeerMessage peer{1, 2, velocita::AppendEntries{
        4, 7, 3, 6, {{4, 99, 0, velocita::encode_command(command)}}}};
    const auto decoded_peer = velocita::decode_peer_message(velocita::encode_peer_message(peer));
    const auto& append = std::get<velocita::AppendEntries>(decoded_peer.payload);
    require(decoded_peer.sender_id == 1 && decoded_peer.recipient_id == 2 && append.term == 4 &&
                append.entries.size() == 1 &&
                velocita::decode_command(append.entries.front().payload).request_id == 42,
            "peer wire round trip failed");
}

void route_messages(std::vector<std::unique_ptr<velocita::RaftNode>>& nodes) {
    std::vector<velocita::PeerMessage> messages;
    for (auto& node : nodes) {
        auto outbound = node->take_outbound_messages();
        messages.insert(messages.end(), std::make_move_iterator(outbound.begin()),
                        std::make_move_iterator(outbound.end()));
    }
    for (const auto& message : messages) {
        require(message.recipient_id >= 1 && message.recipient_id <= static_cast<int>(nodes.size()),
                "peer message has unknown recipient");
        nodes[static_cast<std::size_t>(message.recipient_id - 1)]->receive_peer(
            velocita::decode_peer_message(velocita::encode_peer_message(message)));
    }
}

void test_three_node_cluster(const std::filesystem::path& root) {
    std::vector<std::unique_ptr<velocita::RaftNode>> nodes;
    for (int id = 1; id <= 3; ++id) {
        nodes.push_back(std::make_unique<velocita::RaftNode>(id, root / std::to_string(id),
                                                            std::vector<std::int32_t>{1, 2, 3}));
    }
    velocita::RaftNode* leader = nullptr;
    for (int round = 0; round < 1000 && leader == nullptr; ++round) {
        for (auto& node : nodes) node->tick(10);
        route_messages(nodes);
        for (auto& node : nodes) {
            if (node->is_leader()) {
                require(leader == nullptr, "cluster elected multiple leaders in one term");
                leader = node.get();
            }
        }
    }
    require(leader != nullptr, "three-node cluster did not elect a leader");
    require(!leader->propose({velocita::CommandType::increment, 9001, 11}),
            "new multi-node proposal completed before quorum acknowledgement");

    for (int round = 0; round < 1000; ++round) {
        for (auto& node : nodes) node->tick(10);
        route_messages(nodes);
        bool converged = true;
        for (const auto& node : nodes) {
            const auto status = node->status();
            converged = converged && status.commit_index == 1 && status.applied_index == 1 &&
                        status.counter_value == 11;
        }
        if (converged) {
            const auto completions = leader->take_completed_responses();
            require(completions.size() == 1 && completions.front().request_id == 9001 &&
                        completions.front().value == 11,
                    "leader did not complete the client request after quorum commit");
            return;
        }
    }
    throw std::runtime_error("three-node cluster did not converge after a proposal");
}

void test_restart_and_dedup(const std::filesystem::path& data_dir) {
    {
        velocita::RaftNode node(1, data_dir);
        elect(node);
        const auto first = node.submit({velocita::CommandType::increment, 1001, 5});
        require(first.status == velocita::ResponseStatus::ok && first.value == 5, "increment failed");
        const auto duplicate = node.submit({velocita::CommandType::increment, 1001, 5});
        require(duplicate.value == 5, "duplicate command was applied twice");
        const auto read = node.submit({velocita::CommandType::get, 1002, 0});
        require(read.value == 5, "read returned wrong value");
        require(node.status().current_index == 2, "duplicate unexpectedly grew the log");
    }
    {
        velocita::RaftNode node(1, data_dir);
        require(node.status().counter_value == 5, "committed state was not replayed after restart");
        require(node.status().commit_index == 2 && node.status().applied_index == 2,
                "commit/applied indexes were not restored");
        elect(node);
        const auto duplicate = node.submit({velocita::CommandType::increment, 1001, 5});
        require(duplicate.value == 5, "dedup state did not survive restart");
    }

    const velocita::WriteAheadLog wal(data_dir);
    require(wal.read_all().size() == 2, "WAL record count mismatch");
}

}  // namespace

int main() {
    try {
        test_wire();
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto data_dir = std::filesystem::temp_directory_path() /
                              ("velocita-raft-test-" + std::to_string(unique));
        std::filesystem::create_directories(data_dir);
        try {
            test_restart_and_dedup(data_dir);
            test_three_node_cluster(data_dir / "cluster");
            std::filesystem::remove_all(data_dir);
        } catch (...) {
            std::filesystem::remove_all(data_dir);
            throw;
        }
        std::cout << "all core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return 1;
    }
}
