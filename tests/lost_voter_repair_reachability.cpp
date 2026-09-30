#include "velocita/v2/replication.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace velocita;
using namespace velocita::v2;
using Image = std::map<std::string, std::vector<char>>;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

Image image(const std::filesystem::path& directory) {
    Image result;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        require(item.is_regular_file(), "fixture contains a non-file entry");
        std::ifstream input(item.path(), std::ios::binary);
        require(input.good(), "cannot read fixture image");
        result.emplace(item.path().filename().string(),
                       std::vector<char>{std::istreambuf_iterator<char>{input}, {}});
    }
    return result;
}

std::vector<std::byte> command_bytes() {
    velocita::v2::Command command;
    command.identity = {CallerId{7}, RequestSequence{1}, {}};
    command.identity.payload_digest.bytes[0] = std::byte{0x31};
    command.body = AdvanceKillEpoch{KillEpoch{7}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "authored command encode failed");
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

void route(ReplicatedKernel& from, ReplicatedKernel& to) {
    while (auto message = identity_test::take_outbound_message(from))
        identity_test::deliver_peer(to, *message, from.static_identity());
}

Image exercise(const std::filesystem::path& root, bool leader_flush) {
    const auto lost = root / "lost";
    const auto donor = root / "donor";
    const auto bytes = command_bytes();
    std::int64_t donor_term{};
    {
        auto first = std::make_unique<ReplicatedKernel>(
            identity_test::bootstrap(lost, 1, {1, 2}));
        auto second = std::make_unique<ReplicatedKernel>(
            identity_test::bootstrap(donor, 2, {1, 2}));
        first->tick(2000);
        for (int round = 0; round < 10 && !first->is_leader(); ++round) {
            route(*first, *second);
            route(*second, *first);
        }
        require(first->is_leader() && first->status().term == 1,
                "fixture did not elect node 1 in term 1");
        require(!second->is_leader(), "fixture has two leaders");
        const auto admitted = first->propose(bytes);
        require(admitted.status == ProposeStatus::admitted && admitted.index == 1,
                "fixture did not admit exactly the authored first command");
        if (leader_flush) first->flush_durability();

        // Deliver only messages advertising commit zero. Stop before a later
        // leader heartbeat can communicate a newly learned quorum decision.
        for (int round = 0; round < 20; ++round) {
            first->tick(100);
            while (auto message = identity_test::take_outbound_message(*first)) {
                if (const auto* append = std::get_if<AppendEntries>(&message->payload)) {
                    require(append->leader_commit == 0,
                            "fixture accidentally communicated commitment to donor");
                }
                identity_test::deliver_peer(*second, *message, first->static_identity());
            }
            second->flush_durability();
            route(*second, *first);
            if (second->durable_index() == 1 &&
                (!leader_flush || first->status().commit_index == 1)) break;
        }
        require(second->durable_index() == 1 && second->status().commit_index == 0 &&
                    second->status().applied_index == 0 && second->state().kill_epoch == KillEpoch{0},
                "donor must retain index 1 without knowing it is committed");
        require(first->durable_index() == (leader_flush ? 1U : 0U),
                "leader durability did not match the authored scenario");
        require(first->status().commit_index == (leader_flush ? 1 : 0),
                "leader commitment did not follow the two durable copies");
        require(first->state().kill_epoch == KillEpoch{leader_flush ? 7U : 0U} &&
                    first->state().state_version == (leader_flush ? 1U : 0U),
                "explicit authored state oracle disagrees");
        const auto result = first->take_committed_result();
        require(result.has_value() == leader_flush,
                "result notification did not follow quorum commitment");
        if (result) require(result->index == 1 && result->result.code == ApplyCode::applied,
                            "committed result does not describe authored entry");
        const auto metadata = identity_test::metadata_store(donor).load();
        donor_term = metadata.term;
        require(metadata.term == 1 && metadata.voted_for == 1 && metadata.commit_index == 0,
                "donor persisted checkpoint differs from independent expectation");
        const auto records = identity_test::read_wal(donor);
        require(records.size() == 1 && records[0].index == 1 && records[0].term == 1 &&
                    records[0].entry_type == 0 && records[0].payload == bytes,
                "donor lost or changed the complete canonical unknown-commit suffix");
        std::cout << "case=" << (leader_flush ? "quorum_committed" : "only_donor_durable")
                  << " leader_commit=" << first->status().commit_index
                  << " leader_durable=" << first->durable_index()
                  << " donor_term=" << metadata.term << " donor_vote=" << metadata.voted_for
                  << " donor_commit=" << metadata.commit_index
                  << " donor_durable=" << second->durable_index()
                  << " donor_applied=" << second->status().applied_index
                  << " donor_suffix_records=" << records.size() << '\n';
    }

    // Model loss of one participating voter's hard state without deleting the
    // evidence file. No bootstrap, metadata reconstruction or repair API is used.
    std::filesystem::rename(identity_test::runtime_directory(lost) /
                                kV2MetadataFileName,
                            root / "quarantined-lost-metadata.bin");
    const auto lost_before = image(lost);
    bool refused = false;
    try {
        ReplicatedKernel unauthorized(identity_test::recover(lost, 1, {1, 2}));
    } catch (const BoundMetadataError& error) {
        require(error.kind() == BoundMetadataErrorKind::absent,
                "missing voter failed for an unrelated metadata reason");
        refused = true;
    }
    require(refused, "ordinary recovery admitted a voter with lost hard state");
    require(image(lost) == lost_before, "safe refusal mutated the lost voter's files");
    const auto donor_before = image(donor);
    {
        ReplicatedKernel recovered(identity_test::recover(donor, 2, {1, 2}));
        require(recovered.status().term == donor_term && recovered.durable_index() == 1 &&
                    recovered.status().commit_index == 0 && recovered.status().applied_index == 0 &&
                    recovered.reserved_result_count() == 1 && recovered.ready_result_count() == 0,
                "donor restart erased or prematurely applied its unknown-commit obligation");
        require(!recovered.take_authority_proof().has_value(),
                "donor startup produced authority without a fresh quorum");
    }
    require(image(donor) == donor_before, "read/recovery changed complete donor evidence");
    std::cout << "safe_refusal=missing_hard_state unchanged_lost_files=1"
                 " donor_restart_commit=0 donor_restart_durable=1"
                 " donor_reserved_results=1 authority_proof=absent repair_api=absent\n";
    return donor_before;
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: lost_voter_repair_reachability FRESH_ROOT");
        const std::filesystem::path root{argv[1]};
        require(!std::filesystem::exists(root), "refusing an existing fixture root");
        std::filesystem::create_directories(root);
        const auto committed = exercise(root / "committed", true);
        const auto uncertain = exercise(root / "not-committed", false);
        require(committed == uncertain,
                "two traces did not leave byte-identical donor evidence");
        std::cout << "donor_images_identical=1 prior_quorum_outcomes_differ=1\n"
                     "characterization=PASS repair_completion=UNIMPLEMENTED"
                     " behavioral_repair_RED=NOT_CLAIMED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "lost-voter repair characterization failed: " << error.what() << '\n';
        return 1;
    }
}
