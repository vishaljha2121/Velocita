#pragma once

#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/local_generation.hpp"
#include "velocita/v2/bound_peer.hpp"
#include "velocita/v2/outbound_mailbox.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/snapshot_store.hpp"
#include "velocita/v2/wal_arena.hpp"
#include "velocita/v2/replication.hpp"
#include "v2_catalog_test_support.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>

namespace identity_test {
using namespace velocita::v2;

// Test driver only: production compact() is a nonblocking owner pump. Joining
// afterward restores the fixture's explicitly synchronous WAL-driving mode.
inline std::uint64_t complete_compaction(ReplicatedKernel& node) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        if (const auto cut = node.compact()) { node.stop_writer(); return cut; }
        if (!node.compaction_pending() && node.retained_log_count() < 2) return 0;
        std::this_thread::yield();
    }
    throw std::runtime_error("fixture compaction did not complete: applied=" +
        std::to_string(node.status().applied_index) + " retained=" +
        std::to_string(node.retained_log_count()) + " pending=" + std::to_string(node.compaction_pending()));
}

inline StaticIdentity identity(std::int32_t id, std::vector<std::int32_t> members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    if (members.empty()) members.push_back(id);
    if (members.size() > kMaxStaticVoters) throw std::logic_error("fixture voter bound exceeded");
    StaticIdentity value;
    value.incarnation[0] = static_cast<std::byte>(lineage_tag);
    value.incarnation[1] = std::byte{0x5A};
    value.catalog_root = catalog.root();
    value.voter_count = static_cast<std::uint16_t>(members.size());
    std::copy(members.begin(), members.end(), value.voters.begin());
    value.local_node_id = id;
    return value;
}

inline VerifiedStaticIdentity verified_identity(const StaticIdentity& value) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    VerifiedStaticIdentity verified;
    if (encoded.error != StaticIdentityCodecError::none ||
        VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), verified) !=
            StaticIdentityCodecError::none) {
        throw std::logic_error("invalid verified identity fixture");
    }
    return verified;
}

// Test-only dynamic adapter. Production egress consumes owned bounded frame bytes.
template <typename Sender>
inline std::optional<velocita::PeerMessage> take_outbound_message(Sender& sender) {
    std::array<std::byte, kMaxBoundPeerBytes> bytes{};
    const auto taken = sender.take_outbound_frame(bytes);
    if (taken.error == OutboundTakeError::empty) return std::nullopt;
    if (taken.error != OutboundTakeError::none)
        throw std::runtime_error("fixture egress buffer cannot hold frame");
    auto target = sender.static_identity().value();
    target.local_node_id = taken.recipient_id;
    const auto receiver_identity = verified_identity(target);
    velocita::PeerMessage message;
    BoundFrameScratch scratch;
    if (decode_bound_peer(std::span(bytes).first(taken.bytes_written), receiver_identity,
                          message, scratch) != BoundFrameError::none)
        throw std::runtime_error("fixture egress frame did not decode for recipient");
    return message;
}

// Fixture readers explicitly recover the stored local binding; they do not
// relabel an artifact with the identity of the target under test.
inline VerifiedStaticIdentity stored_identity(const std::filesystem::path& path) {
    std::ifstream input(path / kStaticIdentityFileName, std::ios::binary);
    std::array<std::byte, kMaxStaticIdentityBytes + 1> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    VerifiedStaticIdentity result;
    if (VerifiedStaticIdentity::decode(std::span{bytes}.first(input.gcount()), result) !=
        StaticIdentityCodecError::none) throw std::runtime_error("fixture identity file invalid");
    return result;
}

inline std::filesystem::path runtime_directory(const std::filesystem::path& root) {
    const auto identity = stored_identity(root);
    std::ifstream input(root / "v2-active-generation.bin", std::ios::binary);
    std::array<std::byte, kLocalGenerationSelectorBytes + 1> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    LocalGenerationSelector selector;
    if (decode_local_generation_selector(std::span{bytes}.first(input.gcount()),
            identity, selector) != LocalGenerationError::none)
        throw std::runtime_error("fixture active generation selector invalid");
    std::ostringstream name;
    name << "g-" << std::hex << std::nouppercase << std::setfill('0')
         << std::setw(16) << selector.generation;
    return root / "v2-generations" / name.str();
}

inline BoundMetadataStore metadata_store(const std::filesystem::path& path,
                                         velocita::MetadataCheckpoints checkpoints = {}) {
    return BoundMetadataStore(runtime_directory(path), stored_identity(path), checkpoints);
}
inline SnapshotStore snapshot_store(const std::filesystem::path& path,
                                     StorageCheckpoints checkpoints = {}) {
    return SnapshotStore(runtime_directory(path), stored_identity(path), checkpoints);
}
inline WalSegment wal_segment(const std::filesystem::path& path,
                               StorageCheckpoints checkpoints = {}) {
    return WalSegment(runtime_directory(path), stored_identity(path), checkpoints);
}
inline std::vector<velocita::WalRecord> read_wal(const std::filesystem::path& path) {
    return WalSegment::read_existing_records(runtime_directory(path), stored_identity(path));
}

// Deliberately accepts malformed inner commands for admission-negative tests.
inline std::vector<std::byte> raw_record(const velocita::WalRecord& record,
                                        const VerifiedStaticIdentity& identity) {
    std::vector<std::byte> payload(24 + record.payload.size());
    const auto put = [&](std::size_t at, std::size_t width, std::uint64_t value) {
        for (std::size_t i = 0; i < width; ++i) payload[at + i] = std::byte((value >> (8*i)) & 255);
    };
    put(0, 8, record.index); put(8, 8, static_cast<std::uint64_t>(record.term));
    put(16, 4, static_cast<std::uint32_t>(record.entry_id));
    put(20, 4, static_cast<std::uint32_t>(record.entry_type));
    std::copy(record.payload.begin(), record.payload.end(), payload.begin() + 24);
    std::vector<std::byte> bytes(kBoundFrameOverheadBytes + payload.size());
    BoundFrameScratch scratch;
    if (encode_bound_frame(BoundFrameKind::wal_record, identity, payload, bytes, scratch).error !=
        BoundFrameError::none) throw std::runtime_error("fixture record framing failed");
    return bytes;
}

// Corruption fixtures bypass the semantic encoder only to plant bytes which
// must be refused by recovery. This is never a production store API.
class RawWalFixture {
public:
    explicit RawWalFixture(std::filesystem::path path)
        : root_(std::move(path)), path_(runtime_directory(root_)) {}
    void append(const velocita::WalRecord& record) {
        ++sequence_;
        const auto bytes = frame_block(sequence_, sequence_, raw_record(record, stored_identity(root_)));
        std::ofstream output(path_ / "v2-wal-blocks.bin", std::ios::binary | std::ios::app);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!output) throw std::runtime_error("raw WAL fixture write failed");
    }
private:
    std::filesystem::path root_;
    std::filesystem::path path_;
    std::uint64_t sequence_{};
};

// Uses the generic frame encoder intentionally: malformed canonical peer bodies
// must still reach the public byte-boundary decoder in negative fixtures.
inline std::vector<std::byte> frame_peer_body(
    const velocita::PeerMessage& raw, const VerifiedStaticIdentity& sender_identity) {
    const auto body = encode_bound_peer_body(raw);
    std::array<std::byte, kMaxBoundPeerBytes> frame{};
    BoundFrameScratch scratch{};
    const auto result = encode_bound_frame(BoundFrameKind::peer, sender_identity,
        body, frame, scratch);
    if (result.error != BoundFrameError::none) {
        throw std::runtime_error("peer fixture did not frame");
    }
    return {frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

template <typename Receiver>
inline void deliver_peer(Receiver& receiver, const velocita::PeerMessage& raw,
                         const VerifiedStaticIdentity& sender_identity) {
    const auto frame = frame_peer_body(raw, sender_identity);
    const auto error = receiver.receive_peer(frame);
    if (error != BoundFrameError::none) {
        throw std::runtime_error("peer fixture receive refused");
    }
}

// Crafted-message fixtures use a manifest-compatible identity for the declared
// sender; this is lineage plumbing, not transport authentication.
inline VerifiedStaticIdentity declared_sender_identity(
    const velocita::PeerMessage& raw, std::vector<std::int32_t> receiver_members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    return verified_identity(identity(raw.sender_id, std::move(receiver_members), catalog, lineage_tag));
}

inline VerifiedStaticIdentity declared_sender_identity(
    const velocita::PeerMessage& raw, const VerifiedStaticIdentity& receiver_identity) {
    auto value = receiver_identity.value();
    value.local_node_id = raw.sender_id;
    return verified_identity(value);
}

template <typename Receiver>
inline void deliver_declared_peer(Receiver& receiver, const velocita::PeerMessage& raw) {
    deliver_peer(receiver, raw, declared_sender_identity(raw, receiver.static_identity()));
}

inline VerifiedIdentityStartup bootstrap(const std::filesystem::path& path, std::int32_t id,
    std::vector<std::int32_t> members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    std::filesystem::create_directories(path.parent_path());
    return VerifiedIdentityStartup::bootstrap(path, identity(id, std::move(members), catalog, lineage_tag), catalog);
}

inline VerifiedIdentityStartup recover(const std::filesystem::path& path, std::int32_t id,
    std::vector<std::int32_t> members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    return VerifiedIdentityStartup::recover(path, identity(id, std::move(members), catalog, lineage_tag), catalog);
}

// Corruption fixtures must publish identity before deliberately planting history.
// This is not adoption of existing bytes and never selects bootstrap implicitly.
inline void provision(const std::filesystem::path& path, std::int32_t id,
    std::vector<std::int32_t> members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    auto owner = bootstrap(path, id, std::move(members), catalog, lineage_tag);
}

inline Digest256 authority_domain(std::vector<std::int32_t> members,
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog(),
    std::uint8_t lineage_tag = 0xAB) {
    if (members.empty()) throw std::logic_error("domain fixture needs explicit voters");
    const auto id = members.front();
    const auto value = identity(id, std::move(members), catalog, lineage_tag);
    return verified_identity(value).authority_domain();
}
} // namespace identity_test
