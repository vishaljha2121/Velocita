#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include "velocita/v2/archive_catalog.hpp"
#include "velocita/v2/archive_segment.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

namespace {

std::vector<std::byte> archive_hex(std::string_view text) {
    require(text.size() % 2 == 0, "audited command hex has odd length");
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t at = 0; at < text.size(); at += 2)
        bytes.push_back(std::byte(std::stoul(std::string(text.substr(at, 2)), nullptr, 16)));
    return bytes;
}

const std::vector<std::byte>& audited_archive_command() {
    static const auto bytes = archive_hex(
        "56324b43380000000b000600010000000000000001000000000000000100000000000000000000000000000000000000000000000000000000000000"
        "0100000000000000c09e6e99301067081b53cb14f6a92f073dd470c59b26b3294ed59e468b8ec22b");
    return bytes;
}

Command audited_value() {
    Command value;
    value.identity = {CallerId{1}, RequestSequence{1}, {}};
    value.identity.payload_digest.bytes[0] = std::byte{1};
    value.body = AdvanceKillEpoch{KillEpoch{1}};
    return value;
}

struct DecodedArchive {
    ArchiveCatalogHeader header;
    std::vector<ArchiveCatalogRow> rows;
};

DecodedArchive read_catalog(const std::filesystem::path& node_root,
                            const VerifiedStaticIdentity& identity) {
    const auto bytes = read_bytes(node_root / "v2-archive" / "v2-archive-catalog.bin");
    DecodedArchive result;
    result.rows.resize(kMaxArchiveCatalogRows);
    std::size_t count{};
    require(decode_archive_catalog(bytes, identity, result.header, result.rows, count) ==
                ArchiveCatalogError::none,
            "authoritative named archive catalog did not decode");
    result.rows.resize(count);
    return result;
}

bool segment_contains(const std::filesystem::path& node_root,
                      const VerifiedStaticIdentity& identity,
                      const ArchiveCatalogRow& row, std::uint64_t wanted_index,
                      std::span<const std::byte> wanted_payload) {
    const auto path = node_root / "v2-archive" /
        ("s-" + to_hex(row.segment_digest) + ".bin");
    const auto bytes = read_bytes(path);
    struct Seen {
        std::uint64_t index;
        std::span<const std::byte> payload;
        bool matched{};
    } seen{wanted_index, wanted_payload};
    ArchiveSegmentScratch scratch;
    ArchiveSegmentMetadata metadata;
    const auto decoded = decode_archive_segment(bytes, identity,
        [](const ArchiveRecordView& record, void* raw) noexcept {
            auto& expected = *static_cast<Seen*>(raw);
            if (record.index == expected.index)
                expected.matched = std::ranges::equal(record.payload, expected.payload);
            return true;
        }, &seen, scratch, metadata);
    require(decoded.error == ArchiveSegmentError::none,
            "catalog-named archive segment did not fully decode");
    require(metadata.first_index == row.first_index && metadata.last_index == row.last_index &&
                metadata.segment_digest == row.segment_digest,
            "catalog row did not describe its named segment");
    return seen.matched;
}

void admit_without_taking(Cluster& cluster, const Command& command) {
    auto& leader = cluster.elect();
    const auto outcome = leader.propose(encoded(command));
    require(outcome.status == ProposeStatus::admitted, "unread-result command was refused");
    for (int round = 0; round < 100 && leader.status().applied_index < outcome.index; ++round)
        cluster.settle(1);
    require(leader.status().applied_index >= outcome.index, "unread-result command did not apply");
}

void test_actual_archive_compaction_restart_and_extension(const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    require(encoded(audited_value()) == audited_archive_command(),
            "runtime encoder disagrees with independently authored command bytes");
    admit_without_taking(cluster, audited_value());
    admit_without_taking(cluster, command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
    admit_without_taking(cluster, command(3, AdvanceKillEpoch{KillEpoch{2}}));
    cluster.settle(20);
    auto& node = cluster.elect();
    const auto node_root = root / "1";
    require(std::ranges::any_of(identity_test::read_wal(node_root), [](const velocita::WalRecord& r) {
                return r.index == 1 && r.payload == audited_archive_command();
            }), "audited command was not durably present before compaction");

    const auto cut = identity_test::complete_compaction(node);
    require(cut == 3, "first archive compaction did not cover exactly commit 3");
    const auto first = read_catalog(node_root, node.static_identity());
    require(first.header.generation == 1 && first.rows.size() == 1 &&
                first.rows[0].first_index == 1 && first.rows[0].last_index == 3,
            "first catalog was not the complete [1,3] prefix");
    require(segment_contains(node_root, node.static_identity(), first.rows[0], 1,
                             audited_archive_command()),
            "catalog-named segment lost the exact independently authored command");
    for (const auto& record : identity_test::read_wal(node_root))
        require(record.index > cut, "verified compacted prefix remained in the real WAL");

    for (std::uint64_t expected = 1; expected <= 3; ++expected) {
        const auto result = node.take_committed_result();
        require(result && result->identity.request_sequence.value() == expected,
                "compaction lost or reordered an unread READY result");
    }
    require(!node.take_committed_result(), "unexpected fourth committed result");

    const auto before = node.status();
    require(before.archive_objects == 1 && before.archive_bytes == first.rows[0].segment_file_length &&
                before.archive_byte_limit == velocita::v2::kDefaultArchiveLogicalByteLimit &&
                before.archive_object_limit == velocita::v2::kMaxArchiveCatalogRows,
            "status does not report the archive's one segment, its bytes and the limits");
    cluster.restart(1);
    require(cluster.node(1).status().archive_objects == 1 &&
                cluster.node(1).status().archive_bytes == first.rows[0].segment_file_length,
            "restart did not recount the archive from the catalog");
    require(cluster.node(1).status().state_digest == before.state_digest &&
                cluster.node(1).status().applied_index == before.applied_index,
            "restart did not continue from archived compaction state");
    const auto restarted_catalog = read_catalog(node_root, cluster.node(1).static_identity());
    require(restarted_catalog.rows.size() == 1 &&
                restarted_catalog.rows[0] == first.rows[0] &&
                segment_contains(node_root, cluster.node(1).static_identity(),
                    restarted_catalog.rows[0], 1, audited_archive_command()),
            "restart did not re-read the exact archived golden command bytes");
    (void)cluster.commit(command(4, AdvanceKillEpoch{KillEpoch{3}}));
    (void)cluster.commit(command(5, SetExecutorGeneration{ExecutorGeneration{2}}));
    cluster.settle(20);
    const auto second_cut = identity_test::complete_compaction(cluster.elect());
    require(second_cut == 5, "second archive compaction did not advance to commit 5");
    const auto second = read_catalog(node_root, cluster.node(1).static_identity());
    require(second.header.generation == 2 && second.rows.size() == 2 &&
                second.rows[0] == first.rows[0] && second.rows[1].first_index == 4 &&
                second.rows[1].last_index == 5,
            "second catalog did not byte-preserve row one and append row two");
    const auto grown = cluster.node(1).status();
    require(grown.archive_objects == 2 &&
                grown.archive_bytes == std::uint64_t{second.rows[0].segment_file_length} + second.rows[1].segment_file_length,
            "status did not follow the archive after a second compaction");
}

void create_compacted_fixture(const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)cluster.commit(audited_value());
    (void)cluster.commit(command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
    require(identity_test::complete_compaction(cluster.elect()) == 2,
            "corruption fixture did not compact");
}

void require_restart_refusal(const std::filesystem::path& root, const char* message) {
    bool refused{};
    try { Cluster recovered(root, 1, 0xAB, true); }
    catch (const std::exception&) { refused = true; }
    require(refused, message);
}

void test_missing_and_malformed_archive_refuse_recovery(const std::filesystem::path& root) {
    const auto missing = root / "missing";
    create_compacted_fixture(missing);
    std::filesystem::remove_all(missing / "1" / "v2-archive");
    require_restart_refusal(missing, "restart accepted a missing archive after WAL reclamation");

    const auto malformed = root / "malformed";
    create_compacted_fixture(malformed);
    const auto catalog = malformed / "1" / "v2-archive" / "v2-archive-catalog.bin";
    auto bytes = read_bytes(catalog);
    require(bytes.size() > kArchiveCatalogHeaderBytes, "catalog corruption fixture is too short");
    bytes[kArchiveCatalogHeaderBytes] ^= std::byte{1};
    write_bytes(catalog, bytes);
    require_restart_refusal(malformed, "restart accepted a malformed authoritative catalog");
}

void test_two_host_mode_refuses_local_only_compaction(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;
    for (std::int32_t id : {1, 2}) {
        auto value = identity_test::identity(id, {1, 2});
        value.deployment_mode = DeploymentMode::two_host_pilot;
        nodes.push_back(std::make_unique<ReplicatedKernel>(
            VerifiedIdentityStartup::bootstrap(root / std::to_string(id), value,
                                               catalog_test::standard_catalog())));
    }
    const auto route = [&] {
        std::vector<velocita::PeerMessage> messages;
        for (auto& sender : nodes)
            while (auto message = identity_test::take_outbound_message(*sender))
                messages.push_back(std::move(*message));
        for (const auto& message : messages)
            identity_test::deliver_peer(*nodes[static_cast<std::size_t>(message.recipient_id - 1)],
                                        message,
                                        nodes[static_cast<std::size_t>(message.sender_id - 1)]->static_identity());
    };
    ReplicatedKernel* leader{};
    for (int round = 0; round < 2000 && leader == nullptr; ++round) {
        for (auto& node : nodes) node->flush_durability();
        for (auto& node : nodes) node->tick(10);
        route();
        for (auto& node : nodes) if (node->is_leader()) leader = node.get();
    }
    require(leader != nullptr, "two-host fixture did not elect a two-voter leader");
    const auto proposed = leader->propose(encoded(audited_value()));
    require(proposed.status == ProposeStatus::admitted, "two-host fixture command refused");
    for (int round = 0; round < 400 && leader->status().applied_index < proposed.index; ++round) {
        for (auto& node : nodes) node->flush_durability();
        for (auto& node : nodes) node->tick(10);
        route();
    }
    require(leader->status().applied_index == proposed.index && proposed.index > 0,
            "two-host fixture command did not commit");
    require(leader->compact() == 0 && !leader->compaction_pending(),
            "two-host mode used local archive evidence to start compaction");
}

struct AutomaticRoot {
    std::filesystem::path path;
    AutomaticRoot() {
        std::array<char, 64> pattern{};
        const std::string prefix = "/tmp/v2-archive-compaction-XXXXXX";
        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        const auto* created = ::mkdtemp(pattern.data());
        if (created == nullptr) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~AutomaticRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

}

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::filesystem::exists(argv[1]))) {
        std::cerr << "usage: v2_archive_compaction_tests [NEW_ROOT]\n";
        return 2;
    }
    try {
        std::unique_ptr<AutomaticRoot> automatic;
        std::filesystem::path root;
        if (argc == 2) {
            root = argv[1];
            std::filesystem::create_directories(root);
        } else {
            automatic = std::make_unique<AutomaticRoot>();
            root = automatic->path;
        }
        test_actual_archive_compaction_restart_and_extension(root / "integrated");
        test_missing_and_malformed_archive_refuse_recovery(root / "recovery-refusal");
        test_two_host_mode_refuses_local_only_compaction(root / "two-host");
        std::cout << "archive compaction integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
