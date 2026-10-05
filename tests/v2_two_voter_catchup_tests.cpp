#include "velocita/v2/replication.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

using namespace velocita;
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(std::uint8_t marker) {
    Digest256 value{};
    value.bytes[0] = std::byte{marker};
    value.bytes[1] = std::byte{0x2b};
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

Digest256 expected_digest_after(std::uint64_t count) {
    TradingState state{};
    ApplyScratch scratch{};
    for (std::uint64_t sequence = 1; sequence <= count; ++sequence)
        require(apply(state, authored_command(sequence), scratch,
                      &catalog_test::standard_catalog()).code == ApplyCode::applied,
                "independent reducer rejected an authored command");
    return state_digest_of(state);
}

struct Pair {
    std::filesystem::path root;
    std::vector<std::int32_t> members{1, 2};
    std::map<std::int32_t, std::unique_ptr<ReplicatedKernel>> nodes;
    std::set<std::int32_t> blocked;

    explicit Pair(std::filesystem::path directory) : root(std::move(directory)) {
        for (const auto id : members)
            nodes[id] = std::make_unique<ReplicatedKernel>(identity_test::bootstrap(
                path(id), id, members, catalog_test::standard_catalog()));
    }

    std::filesystem::path path(std::int32_t id) const { return root / std::to_string(id); }
    ReplicatedKernel& node(std::int32_t id) { return *nodes.at(id); }

    void crash_and_recover(std::int32_t id) {
        nodes.at(id).reset();
        nodes[id] = std::make_unique<ReplicatedKernel>(identity_test::recover(
            path(id), id, members, catalog_test::standard_catalog()));
    }

    ReplicatedKernel* leader() {
        for (auto& [id, candidate] : nodes)
            if (candidate->is_leader()) return candidate.get();
        return nullptr;
    }

    void deliver(std::int32_t sender, std::int32_t recipient) {
        while (auto message = identity_test::take_outbound_message(node(sender))) {
            if (message->recipient_id != recipient) continue;
            identity_test::deliver_peer(node(recipient), *message, node(sender).static_identity());
        }
    }

    void route() {
        std::vector<PeerMessage> messages;
        for (auto& [id, sender] : nodes)
            while (auto message = identity_test::take_outbound_message(*sender))
                messages.push_back(std::move(*message));
        for (const auto& message : messages) {
            if (blocked.contains(message.sender_id) || blocked.contains(message.recipient_id))
                continue;
            identity_test::deliver_peer(node(message.recipient_id), message,
                                        node(message.sender_id).static_identity());
        }
    }

    void settle(int rounds) {
        for (int round = 0; round < rounds; ++round) {
            for (auto& [id, node] : nodes) node->flush_durability();
            for (auto& [id, node] : nodes) node->tick(10);
            route();
        }
    }

    ReplicatedKernel& elect() {
        for (int round = 0; round < 2000; ++round) {
            settle(1);
            if (leader() != nullptr) {
                settle(20);
                if (auto* stable = leader()) return *stable;
            }
        }
        throw std::runtime_error("two-voter pair did not elect a leader");
    }

    std::int64_t commit(std::uint64_t sequence) {
        auto& chosen = elect();
        const auto proposed = chosen.propose(encoded(authored_command(sequence)));
        require(proposed.status == ProposeStatus::admitted, "healthy pair refused an authored command");
        for (int round = 0; round < 800; ++round) {
            settle(1);
            while (auto result = chosen.take_committed_result()) {
                require(result->result.code == ApplyCode::applied,
                        "pair disagreed with the independent reducer");
                if (result->index == proposed.index) return result->index;
            }
        }
        throw std::runtime_error("healthy pair did not commit an authored command");
    }

    void drain_results() {
        for (auto& [id, node] : nodes)
            while (node->take_committed_result()) {}
    }

    std::int32_t peer_of(std::int32_t id) const { return id == members[0] ? members[1] : members[0]; }
};

std::filesystem::path fresh_root(const std::filesystem::path& base, const char* leaf) {
    const auto root = base / leaf;
    require(!std::filesystem::exists(root), "refusing to reuse an existing fixture root");
    std::filesystem::create_directories(root);
    return root;
}

void partitioned_peer_catches_up_from_its_own_log(const std::filesystem::path& base) {
    Pair pair(fresh_root(base, "partitioned"));
    constexpr std::uint64_t kCommittedTogether = 6;
    constexpr std::uint64_t kProposedAlone = 6;

    std::int64_t committed_together = 0;
    for (std::uint64_t sequence = 1; sequence <= kCommittedTogether; ++sequence)
        committed_together = pair.commit(sequence);
    pair.settle(50);
    pair.drain_results();

    const auto leader_id = pair.elect().status().node_id;
    const auto peer_id = pair.peer_of(leader_id);
    require(pair.node(peer_id).durable_index() >= static_cast<std::uint64_t>(committed_together),
            "peer did not durably hold the jointly committed prefix");

    pair.blocked.insert(peer_id);
    for (std::uint64_t sequence = kCommittedTogether + 1;
         sequence <= kCommittedTogether + kProposedAlone; ++sequence)
        require(pair.node(leader_id).propose(encoded(authored_command(sequence))).status ==
                    ProposeStatus::admitted,
                "leader refused to stage work while its peer was partitioned");
    pair.settle(400);

    require(pair.node(leader_id).status().commit_index == committed_together,
            "leader committed without its only peer's durable copy");
    const auto cut = identity_test::complete_compaction(pair.node(leader_id));
    require(cut == static_cast<std::uint64_t>(committed_together),
            "leader compaction cut does not equal the jointly committed prefix");
    require(cut <= pair.node(peer_id).durable_index(),
            "leader compacted history its peer does not durably hold");

    pair.crash_and_recover(peer_id);
    pair.blocked.clear();
    pair.settle(2000);

    const auto staged_end = committed_together + static_cast<std::int64_t>(kProposedAlone);
    for (const auto id : pair.members)
        require(pair.node(id).durable_index() == static_cast<std::uint64_t>(staged_end),
                "voter " + std::to_string(id) + " does not durably hold the healed suffix");

    const auto leader_after_heal = pair.elect().status();
    if (leader_after_heal.term > 0 && leader_after_heal.commit_index < staged_end)
        require(leader_after_heal.commit_index == committed_together,
                "old-term suffix committed partially without a current-term entry");
    constexpr auto kDrainSequence = kCommittedTogether + kProposedAlone + 1;
    const auto drain_index = pair.commit(kDrainSequence);
    require(drain_index == staged_end + 1, "current-term drain did not follow the healed suffix");
    pair.settle(100);

    const auto final_index = drain_index;
    const auto expected = expected_digest_after(kDrainSequence);
    for (const auto id : pair.members) {
        const auto status = pair.node(id).status();
        require(status.commit_index == final_index && status.applied_index == final_index,
                "voter " + std::to_string(id) + " did not reach the final authored index");
        require(status.state_digest == expected,
                "voter " + std::to_string(id) + " diverged from the independent reducer");
    }
}

void unsynced_peer_acknowledgement_cannot_advance_commit_or_compaction(
    const std::filesystem::path& base) {
    Pair pair(fresh_root(base, "unsynced"));
    constexpr std::uint64_t kCommittedTogether = 3;

    std::int64_t committed_together = 0;
    for (std::uint64_t sequence = 1; sequence <= kCommittedTogether; ++sequence)
        committed_together = pair.commit(sequence);
    pair.settle(50);
    pair.drain_results();

    auto& leader = pair.elect();
    const auto leader_id = leader.status().node_id;
    const auto peer_id = pair.peer_of(leader_id);
    for (auto& [id, node] : pair.nodes)
        while (identity_test::take_outbound_message(*node)) {}

    const auto next = kCommittedTogether + 1;
    const auto proposed = leader.propose(encoded(authored_command(next)));
    require(proposed.status == ProposeStatus::admitted, "leader refused the unsynced probe");
    require(proposed.index == committed_together + 1, "probe did not extend the committed prefix");
    leader.flush_durability();
    require(leader.durable_index() == static_cast<std::uint64_t>(proposed.index),
            "leader copy of the probe is not durable");

    pair.deliver(leader_id, peer_id);
    require(pair.node(peer_id).status().current_index == proposed.index,
            "peer did not stage the probe");
    require(pair.node(peer_id).durable_index() == static_cast<std::uint64_t>(committed_together),
            "fixture precondition: the probe must be staged but not durable on the peer");
    pair.deliver(peer_id, leader_id);

    require(leader.status().commit_index == committed_together,
            "leader counted a staged, unsynced peer acknowledgement toward quorum");
    const auto cut = identity_test::complete_compaction(leader);
    require(cut <= static_cast<std::uint64_t>(committed_together),
            "leader compacted past the peer's durable prefix");

    pair.crash_and_recover(peer_id);
    require(pair.node(peer_id).status().current_index == committed_together,
            "power loss did not discard the peer's unsynced probe");

    pair.settle(2000);
    require(pair.node(peer_id).durable_index() >= static_cast<std::uint64_t>(proposed.index),
            "recovered peer did not durably reacquire the probe from the leader's log");
    const auto drain_index = pair.commit(next + 1);
    require(drain_index == proposed.index + 1, "current-term drain did not follow the probe");
    pair.settle(100);
    const auto expected = expected_digest_after(next + 1);
    for (const auto id : pair.members) {
        const auto status = pair.node(id).status();
        require(status.applied_index == drain_index,
                "voter " + std::to_string(id) + " did not recover the probe through the log");
        require(status.state_digest == expected,
                "voter " + std::to_string(id) + " diverged from the independent reducer");
    }
}

std::filesystem::path make_base() {
    auto pattern = (std::filesystem::temp_directory_path() / "v2-two-voter-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    const auto* made = ::mkdtemp(bytes.data());
    require(made != nullptr, "two-voter mkdtemp failed");
    return std::filesystem::canonical(made);
}

}

int main() {
    const auto base = make_base();
    int status = 0;
    try {
        partitioned_peer_catches_up_from_its_own_log(base);
        unsynced_peer_acknowledgement_cannot_advance_commit_or_compaction(base);
        std::cout << "two-voter catch-up tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "two-voter catch-up tests failed: " << error.what() << '\n';
        status = 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    return status;
}
