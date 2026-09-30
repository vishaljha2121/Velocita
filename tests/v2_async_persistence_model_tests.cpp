#define V2_COMPACTION_MODEL_FIXTURE_ONLY 1
#include "v2_compaction_persistence_model_tests.cpp"
#undef V2_COMPACTION_MODEL_FIXTURE_ONLY

namespace {
using namespace velocita;
using namespace velocita::v2;
using compaction_disk_model::Model;
using compaction_disk_model::Names;

struct AsyncModelRoot {
    std::filesystem::path path;
    AsyncModelRoot() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-async-model-XXXXXX").string();
        std::vector<char> value(pattern.begin(), pattern.end());
        value.push_back('\0');
        const char* made = ::mkdtemp(value.data());
        require(made != nullptr, "async model mkdtemp failed");
        path = std::filesystem::canonical(made);
    }
    ~AsyncModelRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

std::vector<std::byte> async_command(const std::uint64_t sequence) {
    velocita::v2::Command value;
    value.identity.caller_id = CallerId{1};
    value.identity.request_sequence = RequestSequence{sequence};
    value.identity.payload_digest.bytes[0] = std::byte(sequence);
    value.body = AdvanceKillEpoch{KillEpoch{sequence}};
    std::array<std::byte, kMaxKernelCommandBytes> storage{};
    const auto encoded_value = encode_kernel_command(value, storage);
    require(encoded_value.error == KernelCodecError::none, "async model command encode failed");
    return {storage.begin(), storage.begin() + encoded_value.bytes_written};
}

bool same_async_record(const WalRecord& lhs, const WalRecord& rhs) {
    return lhs.index == rhs.index && lhs.term == rhs.term &&
        lhs.entry_id == rhs.entry_id && lhs.entry_type == rhs.entry_type &&
        lhs.payload == rhs.payload;
}

struct PromiseTrace {
    bool ack_three{};
    bool applied_three{};
    bool result_published{};
};

struct ScenarioResult {
    int events{};
    PromiseTrace promises{};
};

ScenarioResult composed_async_scenario(const Names schedule, const int cut) {
    AsyncModelRoot live;
    AsyncModelRoot recovered;
    const auto node_root = live.path / "node";
    const auto image_root = recovered.path / "node";
    const auto identity = identity_test::identity(1, {1, 2});
    std::vector<WalRecord> old_records;
    std::vector<WalRecord> new_records;
    PromiseTrace promises;
    bool completed{};
    bool failed{};

    auto node = std::make_unique<ReplicatedKernel>(
        VerifiedIdentityStartup::bootstrap(node_root, identity,
                                           catalog_test::standard_catalog()));
    AppendEntries seed{2, 0, 0, 1, {}};
    for (int index = 1; index <= 3; ++index)
        seed.entries.push_back({1, index, 0, async_command(static_cast<std::uint64_t>(index))});
    identity_test::deliver_declared_peer(*node, {2, 1, seed});
    node->flush_durability();
    node->tick(0);
    while (identity_test::take_outbound_message(*node)) {}
    old_records = identity_test::read_wal(node_root);
    const auto seed_metadata = identity_test::metadata_store(node_root).load();
    require(node->status().applied_index == 1 && seed_metadata.commit_index >= 1 &&
                node->state().kill_epoch == KillEpoch{1},
            "async model seed lacks durable committed prefix 1");
    require(old_records.size() == 3, "async model seed lacks exact old suffix");
    for (std::size_t at = 0; at < old_records.size(); ++at)
        require(old_records[at].index == at + 1 && old_records[at].term == 1 &&
                    old_records[at].entry_id == static_cast<std::int32_t>(at + 1) &&
                    old_records[at].payload == async_command(at + 1),
                "async model old WAL differs from authored bytes");
    new_records = {old_records.front(),
        WalRecord{2, 3, 20, 0, async_command(20)},
        WalRecord{3, 3, 30, 0, async_command(30)}};

    Model model(node_root, schedule);
    model.cut = cut;
    compaction_disk_model::active.store(&model, std::memory_order_release);
    node->start_writer();
    const auto observe_promises = [&] {
        promises.applied_three = promises.applied_three || node->status().applied_index >= 3;
        promises.result_published = promises.result_published || node->ready_result_count() >= 3;
        try {
            while (auto message = identity_test::take_outbound_message(*node)) {
                if (const auto* response = std::get_if<AppendEntriesResponse>(&message->payload))
                    promises.ack_three = promises.ack_three ||
                        (response->success && response->current_index >= 3);
            }
        } catch (const std::exception&) {}
    };
    try {
        if (cut != 0) {
            const AppendEntries replacement{3, 1, 1, 0,
                {{3, 20, 0, async_command(20)}, {3, 30, 0, async_command(30)}}};
            identity_test::deliver_declared_peer(*node, {2, 1, replacement});

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline &&
                   (node->persistence_pending() || node->durable_index() < 3)) {
                node->flush_durability();
                node->tick(0);
                observe_promises();
                std::this_thread::yield();
            }
            if (!node->persistence_pending() && node->durable_index() >= 3) {
                observe_promises();
                const AppendEntries commit_probe{3, 3, 3, 3, {}};
                identity_test::deliver_declared_peer(*node, {2, 1, commit_probe});
                while (std::chrono::steady_clock::now() < deadline &&
                       (node->persistence_pending() || node->status().applied_index < 3)) {
                    node->flush_durability();
                    node->tick(0);
                    observe_promises();
                    std::this_thread::yield();
                }
                observe_promises();
                completed = promises.ack_three && promises.applied_three &&
                    promises.result_published && !node->persistence_pending();
            }
        }
    } catch (const std::exception&) {
        observe_promises();
        failed = true;
    }

    node.reset();
    compaction_disk_model::active.store(nullptr, std::memory_order_release);
    require(!model.bad.load(), model.wrong_thread.load()
        ? "async model intercepted protocol-owner I/O"
        : "async model hook raised an internal exception");
    require(cut == 0 ? !failed && model.events.load() == 0 : cut < 0 ? completed && !failed :
                model.frozen.load() && failed && model.events.load() == cut,
            "async model missed a cut or successful baseline promise");
    model.materialize(model.root_key, image_root);

    // Inspect independently authored durable bytes BEFORE recovery can resync
    // any directory or file. Refusal never excuses loss of committed prefix 1.
    const auto image_wal = identity_test::read_wal(image_root);
    require(!image_wal.empty() && same_async_record(image_wal.front(), old_records.front()),
            "crash image lost or changed committed prefix 1");
    const bool old_image = image_wal.size() == old_records.size() &&
        std::ranges::equal(image_wal, old_records, same_async_record);
    const bool cut_image = image_wal.size() == 1 &&
        same_async_record(image_wal.front(), old_records.front());
    const bool new_image = image_wal.size() == new_records.size() &&
        std::ranges::equal(image_wal, new_records, same_async_record);
    require(old_image || cut_image || new_image,
            "crash image contains a partial or invented suffix transaction");

    std::optional<PersistentMetadata> durable_metadata;
    const auto runtime_path = identity_test::runtime_directory(image_root);
    const auto metadata_path = runtime_path / kV2MetadataFileName;
    if (std::filesystem::exists(metadata_path)) {
        const auto bytes = read_bytes(metadata_path);
        PersistentMetadata decoded{};
        BoundFrameScratch scratch{};
        if (decode_bound_metadata(bytes, identity_test::verified_identity(identity),
                                  decoded, scratch) == BoundFrameError::none)
            durable_metadata = decoded;
    }
    if (new_image) {
        require(durable_metadata && durable_metadata->term == 3 &&
                    durable_metadata->voted_for == -1 &&
                    (durable_metadata->commit_index == 1 ||
                     durable_metadata->commit_index == 3),
                "new-term WAL lacks exact M1/M2 metadata prerequisite");
    }
    if (old_image)
        require(durable_metadata && durable_metadata->voted_for == -1 &&
                    durable_metadata->commit_index == 1 &&
                    (durable_metadata->term == 2 || durable_metadata->term == 3),
                "old WAL lacks exact M0/M1 metadata tuple");
    if (cut_image)
        require(durable_metadata && durable_metadata->term == 3 &&
                    durable_metadata->voted_for == -1 && durable_metadata->commit_index == 1,
                "truncated WAL lacks exact M1 metadata prerequisite");
    if (durable_metadata && durable_metadata->commit_index == 3)
        require(new_image, "M2 commit checkpoint covers absent replacement bytes");
    if (promises.ack_three)
        require(new_image && durable_metadata && durable_metadata->term >= 3,
                "durable ACK3 escaped without exact replacement WAL and term metadata");
    if (promises.applied_three || promises.result_published)
        require(new_image && durable_metadata && durable_metadata->commit_index >= 3,
                "apply/result promise escaped without exact WAL and commit checkpoint");

    bool invalid_temp{};
    const auto temporary_path = runtime_path / (std::string{kV2MetadataFileName} + ".tmp");
    if (std::filesystem::exists(temporary_path)) {
        const auto bytes = read_bytes(temporary_path);
        PersistentMetadata temporary{};
        BoundFrameScratch scratch{};
        invalid_temp = decode_bound_metadata(bytes, identity_test::verified_identity(identity),
                                             temporary, scratch) != BoundFrameError::none;
    }
    std::unique_ptr<ReplicatedKernel> restart;
    bool bound_metadata_refusal{};
    try {
        restart = std::make_unique<ReplicatedKernel>(
            VerifiedIdentityStartup::recover(image_root, identity,
                                             catalog_test::standard_catalog()));
    } catch (const BoundMetadataError&) {
        bound_metadata_refusal = true;
        if (!invalid_temp || cut < 0) throw;
    }
    require(invalid_temp ? bound_metadata_refusal : bool(restart),
            "recovery outcome does not match independently inspected metadata temp");
    const auto recovered_wal = identity_test::read_wal(image_root);
    require(std::ranges::equal(recovered_wal, image_wal, same_async_record),
            "recovery changed modeled crash WAL evidence");
    if (restart) {
        require(restart->status().applied_index == durable_metadata->commit_index,
                "recovery did not apply the exact durable checkpoint");
        if (restart->status().applied_index >= 3)
            require(new_image && restart->state().kill_epoch == KillEpoch{30},
                    "accepted recovery applied a non-authored suffix");
    }
    if (cut < 0)
        require(restart && promises.ack_three && promises.applied_three &&
                    promises.result_published && restart->status().applied_index == 3 &&
                    restart->state().kill_epoch == KillEpoch{30},
                "successful composed baseline did not produce real promises and recovery");
    return {model.events.load(), promises};
}

#include "v2_async_ballot_model_fixture.hpp"

} // namespace

int main() {
    std::string active_case;
    try {
        int cuts{};
        for (const auto schedule : {Names::explicit_sync, Names::eager_all,
                                    Names::eager_wal_replace}) {
            active_case = "ballot baseline schedule=" + std::to_string(static_cast<int>(schedule));
            const auto ballot = ballot_async_scenario(schedule, -1);
            require(ballot.granted && ballot.events > 0, "ballot baseline was vacuous");
            for (int cut = 1; cut <= ballot.events; ++cut) {
                active_case = "ballot schedule=" + std::to_string(static_cast<int>(schedule)) +
                    " cut=" + std::to_string(cut);
                (void)ballot_async_scenario(schedule, cut);
                ++cuts;
            }
            std::cout << "ballot persistence model schedule="
                      << static_cast<int>(schedule) << " cuts=" << ballot.events << '\n';
            active_case = "composed seed schedule=" + std::to_string(static_cast<int>(schedule));
            (void)composed_async_scenario(schedule, 0);
            active_case = "composed baseline schedule=" + std::to_string(static_cast<int>(schedule));
            const auto baseline = composed_async_scenario(schedule, -1);
            require(baseline.events > 0 && baseline.promises.ack_three &&
                        baseline.promises.applied_three && baseline.promises.result_published,
                    "async model baseline was vacuous");
            for (int cut = 1; cut <= baseline.events; ++cut) {
                active_case = "composed schedule=" + std::to_string(static_cast<int>(schedule)) +
                    " cut=" + std::to_string(cut);
                (void)composed_async_scenario(schedule, cut);
                ++cuts;
            }
            std::cout << "async persistence model schedule="
                      << static_cast<int>(schedule) << " cuts=" << baseline.events << '\n';
        }
        std::cout << "async persistence crash-image model passed " << cuts << " cuts\n";
        return 0;
    } catch (const std::exception& error) {
        compaction_disk_model::active.store(nullptr, std::memory_order_release);
        std::cerr << "async persistence model: " << active_case << ": " << error.what() << '\n';
        return 1;
    }
}
