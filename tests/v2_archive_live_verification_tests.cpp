#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include <sys/random.h>
#include <cerrno>
namespace archive_entropy_fault {
thread_local bool fail{};
int getentropy(void* bytes, std::size_t size) {
    if (fail) { errno = EIO; return -1; }
    return ::getentropy(bytes, size);
}
}
#define getentropy archive_entropy_fault::getentropy
#include "../src/v2_replication.cpp"
#undef getentropy

namespace velocita::v2 {
struct ArchiveDestinationTestAccess {
    static ArchiveDestinationResult verify(const std::filesystem::path& root,
        const VerifiedStaticIdentity& destination, const VerifiedStaticIdentity& publisher,
        const ArchiveDestinationRequest& request) {
        const auto identity = read_bytes(root / "v2-archive" / "v2-identity.bin");
        const auto catalog = read_bytes(root / "v2-archive" / "v2-execution-catalog.bin");
        return ArchiveDestinationVerifier::verify_existing(root, destination, publisher,
            identity, catalog, catalog_test::standard_catalog(), {}, request);
    }
};
}

namespace {
Digest256 identity_pin(const VerifiedStaticIdentity& identity) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(identity.value(), bytes);
    require(encoded.error == StaticIdentityCodecError::none, "destination identity did not encode");
    return sha256_digest(std::span{bytes}.first(encoded.bytes_written));
}

ArchiveDestinationEvidence verify_on_worker(const std::filesystem::path& root,
    const VerifiedStaticIdentity& destination, const VerifiedStaticIdentity& publisher,
    const ArchiveDestinationRequest& request) {
    ArchiveDestinationResult result;
    std::thread worker([&] {
        result = ArchiveDestinationTestAccess::verify(root, destination, publisher, request);
    });
    worker.join();
    require(result.error == ArchiveDestinationError::none && result.evidence.verified(),
            "simulated authenticated destination failed exact archive verification");
    return std::move(result.evidence);
}

struct ExpectedArchive {
    std::array<std::vector<std::byte>, 3> payloads;
    std::array<bool, 3> seen{};
    static bool visit(const ArchiveRecordView& row, void* context) noexcept {
        auto& expected = *static_cast<ExpectedArchive*>(context);
        if (row.index < 1 || row.index > expected.payloads.size() || row.term != 1 ||
            row.entry_id != static_cast<std::int32_t>(row.index) || row.entry_type != RAFT_LOGTYPE_NORMAL)
            return false;
        const auto at = static_cast<std::size_t>(row.index - 1);
        if (expected.seen[at] || !std::ranges::equal(row.payload, expected.payloads[at])) return false;
        expected.seen[at] = true;
        return true;
    }
};

void require_exact_remote_history(const std::filesystem::path& backup,
                                  const VerifiedStaticIdentity& publisher) {
    ExpectedArchive expected;
    for (std::uint64_t index = 1; index <= expected.payloads.size(); ++index)
        expected.payloads[index - 1] = encoded(command(index, AdvanceKillEpoch{KillEpoch{index}}));
    std::size_t segments{};
    for (const auto& entry : std::filesystem::directory_iterator(backup / "v2-archive")) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("s-") || !name.ends_with(".bin")) continue;
        const auto bytes = read_bytes(entry.path());
        ArchiveSegmentScratch scratch;
        ArchiveSegmentMetadata metadata;
        const auto decoded = decode_archive_segment(bytes, publisher, &ExpectedArchive::visit,
                                                     &expected, scratch, metadata);
        require(decoded.error == ArchiveSegmentError::none,
                "independent archive segment failed exact authored decode");
        ++segments;
    }
    require(segments != 0 && std::ranges::all_of(expected.seen, [](bool value) { return value; }),
            "independent archive lost or duplicated authored commands 1..3");
}

enum class RemoteDamage { none, missing, corrupt };

void entropy_failure_refuses_startup(const std::filesystem::path& root) {
    auto identity = identity_test::identity(1, {1, 2});
    identity.deployment_mode = DeploymentMode::two_host_pilot;
    { auto startup = VerifiedIdentityStartup::bootstrap(root, identity, catalog_test::standard_catalog()); }
    const auto before = directory_image(root);
    Digest256 destination{};
    destination.bytes[0] = std::byte{1};
    auto startup = VerifiedIdentityStartup::recover(root, identity, catalog_test::standard_catalog());
    bool refused{};
    archive_entropy_fault::fail = true;
    try {
        ReplicatedKernel node(std::move(startup), {}, {}, {}, {}, destination);
    } catch (const std::runtime_error& error) {
        refused = std::string_view(error.what()) == "archive verification session entropy unavailable";
    }
    archive_entropy_fault::fail = false;
    require(refused && directory_image(root) == before,
            "entropy failure started a node or changed durable history");
}

void live_compaction_requests_fresh_backup(const std::filesystem::path& root, bool corrupt_attempt,
                                         bool replay_before_restart = false,
                                         RemoteDamage damage = RemoteDamage::none) {
    auto identity = identity_test::identity(1, {1, 2});
    identity.deployment_mode = DeploymentMode::two_host_pilot;
    { auto startup = VerifiedIdentityStartup::bootstrap(root, identity,
                                                       catalog_test::standard_catalog()); }
    identity_test::RawWalFixture wal(root);
    for (std::uint64_t i = 1; i <= 3; ++i) {
        auto value = command(i, AdvanceKillEpoch{KillEpoch{i}});
        wal.append({i, 1, static_cast<std::int32_t>(i), RAFT_LOGTYPE_NORMAL, encoded(value)});
    }
    identity_test::metadata_store(root).save({1, -1, 3});
    auto destination_value = identity;
    destination_value.local_node_id = 2;
    const auto destination_identity = identity_test::verified_identity(destination_value);
    const auto destination = identity_pin(destination_identity);
    const auto backup = root.parent_path() / (root.filename().string() + "-backup");
    Digest256 final_digest{};
    ArchiveDestinationEvidence previous_response;
    {
    ReplicatedKernel node(VerifiedIdentityStartup::recover(root, identity,
        catalog_test::standard_catalog()), {}, {}, {}, {}, destination);
    require(node.state().kill_epoch == KillEpoch{3} && node.state().state_version == 3,
            "cold committed fixture did not apply the three authored commands");
    const auto before = read_bytes(identity_test::runtime_directory(root) / "v2-wal-blocks.bin");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!node.archive_verification_request() &&
           std::chrono::steady_clock::now() < deadline) {
        require(node.compact() == 0, "live compaction reclaimed before independent verification");
        std::this_thread::yield();
    }
    const auto request = node.archive_verification_request();
    require(request.has_value(),
            "configured two-host compaction never requested fresh destination verification");
    require(request->last_index == 3 && request->destination_identity == destination,
            "backup request does not match pinned destination or committed cut");
    require(read_bytes(identity_test::runtime_directory(root) / "v2-wal-blocks.bin") == before &&
                node.retained_log_count() == 3,
            "missing backup verification released source history");
    const auto fenced = node.status();
    node.tick(1'000'000);
    require(node.status().term == fenced.term && node.status().role == fenced.role &&
                node.status().state_digest == fenced.state_digest &&
                !node.is_leader() && !node.take_committed_result(),
            "backup wait released consensus or results");
    std::filesystem::create_directories(backup);
    std::filesystem::copy(root / "v2-archive", backup / "v2-archive",
                          std::filesystem::copy_options::recursive);
    auto expected = *request;
    if (corrupt_attempt) ++expected.attempt;
    auto evidence = verify_on_worker(backup, destination_identity, node.static_identity(), expected);
    if (replay_before_restart)
        previous_response = verify_on_worker(backup, destination_identity, node.static_identity(), expected);
    if (corrupt_attempt) {
        bool refused{};
        try { (void)node.accept_archive_verification(std::move(evidence)); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused && read_bytes(identity_test::runtime_directory(root) / "v2-wal-blocks.bin") == before &&
                    node.retained_log_count() == 3,
                "wrong-attempt backup completion authorized history deletion");
        return;
    }
    require(node.accept_archive_verification(std::move(evidence)), "fresh backup completion rejected");
    require(!node.accept_archive_verification(std::move(evidence)), "duplicate backup completion accepted");
    std::uint64_t compacted{};
    const auto completed_by = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (compacted == 0 && std::chrono::steady_clock::now() < completed_by) {
        compacted = node.compact();
        std::this_thread::yield();
    }
    require(compacted == 3 && !node.compaction_pending(),
            "verified independent history did not complete real WAL compaction");
    const auto remaining = WalSegment::read_existing_records(
        identity_test::runtime_directory(root), node.static_identity());
    require(std::none_of(remaining.begin(), remaining.end(), [](const auto& record) { return record.index <= 3; }),
            "accepted compaction did not remove covered WAL records");
    require_exact_remote_history(backup, node.static_identity());
    final_digest = node.status().state_digest;
    }
    ReplicatedKernel recovered(VerifiedIdentityStartup::recover(root, identity,
        catalog_test::standard_catalog()), {}, {}, {}, {}, destination);
    require(recovered.persistence_pending() && recovered.archive_verification_request() &&
                !recovered.is_leader() && recovered.status().state_digest == final_digest,
            "reclaimed live recovery served without fresh backup evidence");
    const auto recovered_request = *recovered.archive_verification_request();
    if (damage != RemoteDamage::none) {
        auto segment = std::filesystem::path{};
        for (const auto& entry : std::filesystem::directory_iterator(backup / "v2-archive")) {
            const auto name = entry.path().filename().string();
            if (name.starts_with("s-") && name.ends_with(".bin")) { segment = entry.path(); break; }
        }
        require(!segment.empty(), "remote damage fixture found no archive segment");
        if (damage == RemoteDamage::missing) {
            std::filesystem::remove(segment);
        } else {
            auto bytes = read_bytes(segment);
            constexpr auto payload_offset = kArchiveSegmentHeaderBytes + kArchiveRecordHeaderBytes;
            require(bytes.size() > payload_offset, "remote corruption fixture segment too small");
            bytes[payload_offset] ^= std::byte{1};
            write_bytes(segment, bytes);
        }
        const auto refused = ArchiveDestinationTestAccess::verify(
            backup, destination_identity, recovered.static_identity(), recovered_request);
        require(refused.error != ArchiveDestinationError::none && !refused.evidence.verified(),
                "missing/corrupt remote history minted recovery evidence");
        const auto fenced = recovered.status();
        recovered.tick(1'000'000);
        const auto proposal = recovered.propose(encoded(command(4, AdvanceKillEpoch{KillEpoch{4}})));
        const PeerMessage higher{2, 1, velocita::RequestVoteResponse{fenced.term + 10, false}};
        const auto frame = identity_test::frame_peer_body(
            higher, identity_test::declared_sender_identity(higher, recovered.static_identity()));
        const auto peer = recovered.receive_peer(frame);
        std::array<std::byte, kMaxBoundPeerBytes> output{};
        require(recovered.persistence_pending() && recovered.status().term == fenced.term &&
                    recovered.status().state_digest == final_digest && !recovered.is_leader() &&
                    proposal.status == ProposeStatus::persistence_busy &&
                    peer.status == PeerReceiveStatus::busy &&
                    recovered.take_outbound_frame(output).error == OutboundTakeError::empty &&
                    !recovered.take_authority_proof() && !recovered.take_committed_result(),
                "remote verification refusal released protocol, output, proof, result or state");
        return;
    }
    if (replay_before_restart) {
        bool refused{};
        try { (void)recovered.accept_archive_verification(std::move(previous_response)); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused && recovered.persistence_pending() && recovered.status().state_digest == final_digest,
                "pre-restart backup response released fresh recovery barrier");
        return;
    }
    auto fresh = verify_on_worker(backup, destination_identity, recovered.static_identity(), recovered_request);
    require(recovered.accept_archive_verification(std::move(fresh)) && !recovered.persistence_pending() &&
                recovered.status().state_digest == final_digest,
            "fresh backup verification did not release exact recovered state");
}
}

int main() {
    std::array<char, 64> name{};
    const std::string pattern = "/tmp/v2-live-archive.XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    if (!::mkdtemp(name.data())) return 2;
    const auto root = std::filesystem::path(name.data());
    try {
        entropy_failure_refuses_startup(root / "entropy-failure");
        live_compaction_requests_fresh_backup(root / "node", false);
        live_compaction_requests_fresh_backup(root / "wrong-attempt", true);
        live_compaction_requests_fresh_backup(root / "restart-replay", false, true);
        live_compaction_requests_fresh_backup(root / "missing-remote", false, false,
                                              RemoteDamage::missing);
        live_compaction_requests_fresh_backup(root / "corrupt-remote", false, false,
                                              RemoteDamage::corrupt);
        std::filesystem::remove_all(root);
        std::cout << "live archive verification tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live archive verification test failed: " << error.what() << '\n';
        return 1;
    }
}
