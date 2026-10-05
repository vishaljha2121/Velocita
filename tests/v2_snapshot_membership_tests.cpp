#include "velocita/v2/replication.hpp"
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

using velocita::PeerMessage;
using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = std::byte{marker};
    value.bytes[1] = std::byte{0x5A};
    return value;
}

Command command(const std::uint64_t sequence) {
    return {{CallerId{1}, RequestSequence{sequence},
             digest(static_cast<std::uint8_t>(0x60 + sequence))},
            SetStrategyMode{StrategyMode::normal, KillEpoch{},
                            ExecutorGeneration{}}};
}

std::vector<std::byte> encoded(const Command& value) {
    std::array<std::byte, kMaxKernelCommandBytes> buffer{};
    const auto result = encode_kernel_command(value, buffer);
    require(result.error == KernelCodecError::none, "fixture command did not encode");
    return {buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

struct Cluster {
    std::filesystem::path root;
    std::vector<std::int32_t> members{1, 2, 3};
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;
    std::set<std::int32_t> blocked;

    explicit Cluster(std::filesystem::path directory) : root(std::move(directory)) {
        open(1, true);
        open(2, true);
        open(3, true);
    }

    void open(const std::int32_t id, const bool bootstrap = false) {
        auto startup = bootstrap
            ? identity_test::bootstrap(root / std::to_string(id), id, members)
            : identity_test::recover(root / std::to_string(id), id, members);
        nodes.push_back(std::make_unique<ReplicatedKernel>(
            std::move(startup)));
    }

    ReplicatedKernel& node(const std::int32_t id) {
        for (auto& candidate : nodes) {
            if (candidate->status().node_id == id) return *candidate;
        }
        throw std::runtime_error("no such node");
    }

    void restart(const std::int32_t id) {
        for (auto& candidate : nodes) {
            if (candidate->status().node_id == id) {
                candidate.reset();
                break;
            }
        }
        nodes.erase(std::remove(nodes.begin(), nodes.end(), nullptr), nodes.end());
        open(id, false);
    }

    void route() {
        std::vector<PeerMessage> messages;
        for (auto& sender : nodes) {
            while (auto message = identity_test::take_outbound_message(*sender)) {
                messages.push_back(std::move(*message));
            }
        }
        for (const auto& message : messages) {
            if (blocked.count(message.sender_id) != 0 ||
                blocked.count(message.recipient_id) != 0) {
                continue;
            }
            for (auto& recipient : nodes) {
                if (recipient->status().node_id == message.recipient_id) {
                    identity_test::deliver_peer(*recipient, message,
                        node(message.sender_id).static_identity());
                    break;
                }
            }
        }
    }

    void tick(const int rounds) {
        for (int round = 0; round < rounds; ++round) {
            for (auto& candidate : nodes) candidate->flush_durability();
            for (auto& candidate : nodes) candidate->tick(10);
            route();
        }
    }

    ReplicatedKernel* leader() {
        for (auto& candidate : nodes) {
            if (candidate->is_leader()) return candidate.get();
        }
        return nullptr;
    }

    ReplicatedKernel& elect() {
        for (int round = 0; round < 2000; ++round) {
            tick(1);
            if (auto* chosen = leader()) return *chosen;
        }
        throw std::runtime_error("three-voter cluster did not elect a leader");
    }

    void commit(const std::uint64_t sequence) {
        auto& chosen = elect();
        require(chosen.propose(encoded(command(sequence))).status ==
                    ProposeStatus::admitted,
                "leader refused a command");
        for (int round = 0; round < 400; ++round) {
            tick(1);
            if (chosen.take_committed_result()) return;
        }
        throw std::runtime_error("command did not commit");
    }
};

void test_snapshot_restores_three_voter_membership(
    const std::filesystem::path& root) {
    Cluster cluster(root);
    cluster.commit(1);
    cluster.commit(2);
    cluster.commit(3);
    cluster.commit(4);

    auto& before_restart = cluster.node(1);
    const auto before = before_restart.status();
    require(before.commit_index > 1, "fixture did not produce a compactable prefix");
    const auto included = identity_test::complete_compaction(before_restart);
    require(included > 1, "snapshot did not include more than one committed entry");

    cluster.blocked.insert(1);
    cluster.restart(1);
    cluster.tick(250);
    const auto isolated = cluster.node(1).status();
    require(!cluster.node(1).is_leader(),
            "snapshot restore dropped peers and elected the restarted voter alone");
    require(isolated.commit_index <= before.commit_index,
            "isolated restarted voter committed without a quorum");

    cluster.blocked.erase(1);
    cluster.tick(200);
    cluster.commit(5);
    cluster.tick(100);
    const auto expected = cluster.node(1).status();
    for (const auto& candidate : cluster.nodes) {
        const auto status = candidate->status();
        require(status.state_digest == expected.state_digest,
                "three voters did not converge after snapshot restart");
        require(status.commit_index == expected.commit_index,
                "three voters have different committed prefixes after reconnect");
    }
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "velocita-v2-snapshot-membership-tests";
    std::filesystem::remove_all(root);
    try {
        std::filesystem::create_directories(root);
        test_snapshot_restores_three_voter_membership(root);
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "snapshot membership test failed: " << error.what() << '\n';
        return 1;
    }
}
