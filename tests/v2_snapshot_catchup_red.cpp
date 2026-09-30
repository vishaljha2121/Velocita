#include "velocita/v2/replication.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita;
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(std::uint8_t marker) {
    Digest256 value{};
    value.bytes[0] = std::byte{marker};
    value.bytes[1] = std::byte{0x5a};
    return value;
}

velocita::v2::Command authored_command(std::uint64_t sequence) {
    return {{CallerId{7}, RequestSequence{sequence},
             digest(static_cast<std::uint8_t>(sequence))},
            AdvanceKillEpoch{KillEpoch{sequence}}};
}

std::vector<std::byte> encoded(const velocita::v2::Command& command) {
    std::array<std::byte, kMaxKernelCommandBytes> storage{};
    const auto result = encode_kernel_command(command, storage);
    require(result.error == KernelCodecError::none, "authored command did not encode");
    return {storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

struct Cluster {
    std::filesystem::path root;
    std::vector<std::int32_t> members{1, 2, 3};
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;
    std::set<std::int32_t> blocked;
    std::int32_t traced_lagger{};
    std::uint64_t append_deliveries_to_lagger{};
    std::uint64_t failed_append_replies_from_lagger{};

    explicit Cluster(std::filesystem::path directory) : root(std::move(directory)) {
        for (const auto id : members) {
            nodes.push_back(std::make_unique<ReplicatedKernel>(
                identity_test::bootstrap(root / std::to_string(id), id, members,
                                         catalog_test::standard_catalog())));
        }
    }

    ReplicatedKernel& node(std::int32_t id) {
        for (auto& candidate : nodes)
            if (candidate->status().node_id == id) return *candidate;
        throw std::runtime_error("node is absent");
    }

    ReplicatedKernel* leader() {
        for (auto& candidate : nodes)
            if (candidate->is_leader()) return candidate.get();
        return nullptr;
    }

    void route() {
        std::vector<PeerMessage> messages;
        for (auto& sender : nodes)
            while (auto message = identity_test::take_outbound_message(*sender))
                messages.push_back(std::move(*message));

        for (const auto& message : messages) {
            if (blocked.contains(message.sender_id) || blocked.contains(message.recipient_id))
                continue;
            if (message.recipient_id == traced_lagger &&
                std::holds_alternative<AppendEntries>(message.payload))
                ++append_deliveries_to_lagger;
            if (message.sender_id == traced_lagger) {
                if (const auto* response = std::get_if<AppendEntriesResponse>(&message.payload);
                    response != nullptr && !response->success)
                    ++failed_append_replies_from_lagger;
            }
            identity_test::deliver_peer(node(message.recipient_id), message,
                                        node(message.sender_id).static_identity());
        }
    }

    void settle(int rounds) {
        for (int round = 0; round < rounds; ++round) {
            for (auto& node : nodes) node->flush_durability();
            for (auto& node : nodes) node->tick(10);
            route();
        }
    }

    ReplicatedKernel& elect() {
        for (int round = 0; round < 2000; ++round) {
            settle(1);
            if (auto* candidate = leader()) {
                settle(20);
                if (auto* stable = leader()) return *stable;
            }
        }
        throw std::runtime_error("cluster did not elect a leader");
    }

    CommittedResult commit(const velocita::v2::Command& command) {
        auto& chosen = elect();
        require(chosen.propose(encoded(command)).status == ProposeStatus::admitted,
                "healthy quorum refused an authored command");
        for (int round = 0; round < 800; ++round) {
            settle(1);
            if (auto result = chosen.take_committed_result()) return *result;
        }
        throw std::runtime_error("healthy quorum did not commit an authored command");
    }
};

void snapshot_catchup_must_recover_a_lagging_voter(const std::filesystem::path& root) {
    Cluster cluster(root);
    const auto leader_id = cluster.elect().status().node_id;
    const auto lagger_id = *std::find_if(cluster.members.begin(), cluster.members.end(),
                                         [leader_id](auto id) { return id != leader_id; });
    const auto healthy_peer_id = *std::find_if(
        cluster.members.begin(), cluster.members.end(),
        [leader_id, lagger_id](auto id) { return id != leader_id && id != lagger_id; });
    cluster.blocked.insert(lagger_id);

    TradingState expected{};
    ApplyScratch expected_scratch{};
    std::int64_t expected_final_index = 0;
    constexpr std::uint64_t kAuthoredCommandCount = 12;
    for (std::uint64_t sequence = 1; sequence <= kAuthoredCommandCount; ++sequence) {
        const auto command = authored_command(sequence);
        require(apply(expected, command, expected_scratch,
                      &catalog_test::standard_catalog()).code == ApplyCode::applied,
                "independent reducer rejected an authored command");
        const auto committed = cluster.commit(command);
        require(committed.result.code == ApplyCode::applied,
                "cluster disagreed with independent reducer");
        if (sequence == 1) expected_final_index = committed.index + kAuthoredCommandCount - 1;
        require(committed.index == expected_final_index -
                                      static_cast<std::int64_t>(kAuthoredCommandCount - sequence),
                "authored commands did not occupy one contiguous log suffix");
    }
    cluster.settle(100);
    require(expected.kill_epoch == KillEpoch{kAuthoredCommandCount} &&
                expected.state_version == kAuthoredCommandCount,
            "independent authored expectation has the wrong kill epoch or version");
    const auto expected_digest = state_digest_of(expected);
    require(cluster.node(leader_id).status().state_digest == expected_digest &&
                cluster.node(healthy_peer_id).status().state_digest == expected_digest,
            "healthy quorum did not reach independently reduced state");

    const auto leader_snapshot = identity_test::complete_compaction(cluster.node(leader_id));
    const auto peer_snapshot = identity_test::complete_compaction(cluster.node(healthy_peer_id));
    require(leader_snapshot == static_cast<std::uint64_t>(expected_final_index) &&
                peer_snapshot == static_cast<std::uint64_t>(expected_final_index),
            "healthy replicas did not compact the complete authored prefix");
    require(identity_test::snapshot_store(root / std::to_string(leader_id)).load().has_value() &&
                identity_test::snapshot_store(root / std::to_string(healthy_peer_id)).load().has_value(),
            "healthy replicas lack durable local snapshots");

    cluster.traced_lagger = lagger_id;
    cluster.blocked.erase(lagger_id);
    cluster.settle(2000); // Drive actual AppendEntries backtracking/retries.

    const auto recovered = cluster.node(lagger_id).status();
    std::cout << "snapshot-catchup trace: leader=" << leader_id
              << " healthy_peer=" << healthy_peer_id << " lagger=" << lagger_id
              << " compacted_through=" << expected_final_index
              << " lagger_applied=" << recovered.applied_index
              << " lagger_commit=" << recovered.commit_index
              << " append_deliveries=" << cluster.append_deliveries_to_lagger
              << " failed_append_replies=" << cluster.failed_append_replies_from_lagger << '\n';
    if (recovered.applied_index != expected_final_index) {
        require(cluster.append_deliveries_to_lagger > 1 &&
                    cluster.failed_append_replies_from_lagger > 1,
                "failed catch-up fixture did not exercise real repeated retries");
    }
    require(recovered.applied_index == expected_final_index,
            "lagging voter could not cross the compacted prefix via snapshot install");
    require(recovered.state_digest == expected_digest,
            "lagging voter did not recover the independently authored state");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: v2_snapshot_catchup_red ROOT");
        const std::filesystem::path root(argv[1]);
        require(!std::filesystem::exists(root), "refusing to replace an existing fixture root");
        std::filesystem::create_directories(root);
        snapshot_catchup_must_recover_a_lagging_voter(root);
        std::cout << "snapshot catch-up test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "snapshot catch-up test failed: " << error.what() << '\n';
        return 1;
    }
}
