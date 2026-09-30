#include "velocita/v2/bound_peer.hpp"
#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/replication.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <fstream>
#include <map>
#include <new>
#include <stdexcept>
#include <unistd.h>

namespace allocation_probe {
thread_local bool enabled{};
thread_local std::size_t count{};
}
void* operator new(std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using namespace velocita;
using namespace velocita::v2;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
VerifiedStaticIdentity context(int local, int lineage = 0xAB) {
    auto value = identity_test::identity(local, {1, 2, 3},
        catalog_test::standard_catalog(), static_cast<std::uint8_t>(lineage));
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    auto encoded = encode_static_identity(value, bytes);
    VerifiedStaticIdentity result;
    require(encoded.error == StaticIdentityCodecError::none &&
        VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), result) ==
            StaticIdentityCodecError::none, "identity fixture");
    return result;
}
std::vector<std::byte> frame(std::span<const std::byte> inner,
    const VerifiedStaticIdentity& sender) {
    BoundFrameScratch scratch;
    std::vector<std::byte> result(kMaxBoundPeerBytes);
    auto encoded = encode_bound_frame(BoundFrameKind::peer, sender, inner, result, scratch);
    require(encoded.error == BoundFrameError::none, "generic framing fixture");
    result.resize(encoded.bytes_written);
    return result;
}
std::vector<std::byte> frame(const PeerMessage& message,
    const VerifiedStaticIdentity& sender) {
    return frame(encode_bound_peer_body(message), sender);
}
void put32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes.at(at+i) = std::byte((value >> (8*i)) & 255U);
}
std::vector<std::byte> command(std::uint64_t sequence = 1) {
    velocita::v2::Command value;
    value.identity = {CallerId{1}, RequestSequence{sequence}, {}};
    value.identity.payload_digest.bytes[0] = std::byte{1};
    value.body = AdvanceKillEpoch{KillEpoch{sequence}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    auto encoded = encode_kernel_command(value, bytes);
    require(encoded.error == KernelCodecError::none, "command fixture");
    return {bytes.begin(), bytes.begin()+encoded.bytes_written};
}
void refused(const std::vector<std::byte>& bytes, BoundFrameError expected) {
    BoundFrameScratch scratch;
    PeerMessage destination{3, 2, RequestVoteResponse{17, true}};
    const auto before = encode_bound_peer_body(destination);
    require(decode_bound_peer(bytes, context(2), destination, scratch) == expected,
        "wrong typed peer refusal");
    require(encode_bound_peer_body(destination) == before, "failed peer decode changed destination");
}
void compaction_floor_tests() {
    auto sender = context(1);
    auto receiver = context(2);
    BoundFrameScratch scratch;
    // Owning encoder: the floor survives the round trip.
    PeerMessage append{1, 2, AppendEntries{7, 0, 0, 0, {{7, 1, 0, command()}}}};
    std::get<AppendEntries>(append.payload).compaction_floor = 42;
    std::array<std::byte, kMaxBoundPeerBytes> bytes{};
    auto encoded = encode_bound_peer(append, sender, bytes, scratch);
    require(encoded.error == BoundFrameError::none, "floor encode refused");
    PeerMessage decoded;
    require(decode_bound_peer(std::span{bytes}.first(encoded.bytes_written), receiver, decoded, scratch) ==
                BoundFrameError::none &&
                std::get<AppendEntries>(decoded.payload).compaction_floor == 42,
            "the compaction floor did not survive the owning encoder");
    // Allocation-free encoder: the same.
    const auto payload = command();
    const std::array<BorrowedPeerLogEntry, 1> borrowed{{{7, 1, 0, payload}}};
    BorrowedAppendEntries view{7, 0, 0, 0, borrowed, 99};
    encoded = encode_bound_peer(PeerMessageView{1, 2, view}, sender, bytes, scratch);
    require(encoded.error == BoundFrameError::none, "view floor encode refused");
    require(decode_bound_peer(std::span{bytes}.first(encoded.bytes_written), receiver, decoded, scratch) ==
                BoundFrameError::none &&
                std::get<AppendEntries>(decoded.payload).compaction_floor == 99,
            "the compaction floor did not survive the view encoder");
    // A V1 body (older peer) is refused, not misread.
    refused(frame(encode_peer_message(append), sender), BoundFrameError::invalid_payload);
    // An AppendEntries body without its trailer is refused.
    auto body = encode_bound_peer_body(append);
    body.resize(body.size() - 8);
    refused(frame(body, sender), BoundFrameError::invalid_payload);
    // A negative floor is refused by both sides.
    std::get<AppendEntries>(append.payload).compaction_floor = -1;
    require(encode_bound_peer(append, sender, bytes, scratch).error == BoundFrameError::invalid_payload,
            "a negative floor was encoded");
    refused(frame(encode_bound_peer_body(append), sender), BoundFrameError::invalid_payload);
    view.compaction_floor = -1;
    require(encode_bound_peer(PeerMessageView{1, 2, view}, sender, bytes, scratch).error ==
                BoundFrameError::invalid_payload,
            "the allocation-free encoder accepted a negative floor");
}
void codec_tests() {
    auto sender = context(1);
    auto receiver = context(2);
    BoundFrameScratch scratch;
    const std::array<PeerMessage, 4> messages{{
        {1, 2, RequestVote{7, 1, 3, 2}},
        {1, 2, RequestVoteResponse{7, true}},
        {1, 2, AppendEntries{7, 0, 0, 0, {{7, 1, 0, command()}}}},
        {1, 2, AppendEntriesResponse{7, true, 3, 1}},
    }};
    auto malformed_batch = messages[2];
    std::get<AppendEntries>(malformed_batch.payload).entries.assign(3, PeerLogEntry{7, 1, 0, command()});
    auto truncated_inner = encode_bound_peer_body(malformed_batch);
    truncated_inner.pop_back();
    const auto malformed_later_entry = frame(truncated_inner, sender);
    PeerMessage preserved{3, 2, RequestVoteResponse{1, false}};
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    const auto late_error = decode_bound_peer(malformed_later_entry, receiver, preserved, scratch);
    allocation_probe::enabled = false;
    require(allocation_probe::count == 0, "malformed later entry allocated before complete preflight");
    require(late_error == BoundFrameError::invalid_payload, "later truncation was not refused");
    for (const auto& message : messages) {
        std::array<std::byte, kMaxBoundPeerBytes> bytes{};
        const auto encoded = encode_bound_peer(message, sender, bytes, scratch);
        require(encoded.error == BoundFrameError::none, "valid peer encoder refused");
        const auto input = std::span{bytes}.first(encoded.bytes_written);
        PeerMessage decoded;
        require(decode_bound_peer(input, receiver, decoded, scratch) == BoundFrameError::none,
            "valid peer decoder refused");
        require(encode_bound_peer_body(decoded) == encode_bound_peer_body(message), "peer roundtrip differs");
        refused(frame(message, context(1, 0xCD)), BoundFrameError::foreign_manifest);
        for (std::size_t n = 0; n < encoded.bytes_written; ++n) {
            PeerMessage unchanged = messages[1];
            require(decode_bound_peer(input.first(n), receiver, unchanged, scratch) != BoundFrameError::none,
                "truncated peer accepted");
            require(encode_bound_peer_body(unchanged) == encode_bound_peer_body(messages[1]), "truncation mutated output");
        }
        for (std::size_t n = 0; n < encoded.bytes_written; ++n) {
            auto output = bytes;
            require(encode_bound_peer(message, sender, std::span{output}.first(n), scratch).error ==
                BoundFrameError::output_too_small, "short encode did not refuse");
            require(output == bytes, "short encode mutated output");
        }
    }
    refused(frame(PeerMessage{1, 3, RequestVote{7, 1, 0, 0}}, sender), BoundFrameError::invalid_route);
    refused(frame(PeerMessage{2, 2, RequestVote{7, 2, 0, 0}}, sender), BoundFrameError::invalid_route);
    refused(frame(PeerMessage{4, 2, RequestVote{7, 4, 0, 0}}, sender), BoundFrameError::invalid_route);
    refused(frame(PeerMessage{1, 2, RequestVote{7, 3, 0, 0}}, sender), BoundFrameError::invalid_route);
    for (auto message : messages) {
        auto inner = encode_bound_peer_body(message);
        inner.push_back(std::byte{0});
        refused(frame(inner, sender), BoundFrameError::invalid_payload);
    }
    auto vote = encode_bound_peer_body(messages[0]);
    vote[28] = std::byte{1}; // RequestVote reserved word, independent of envelope integrity.
    refused(frame(vote, sender), BoundFrameError::invalid_payload);
    auto reply = encode_bound_peer_body(messages[1]);
    reply[24] = std::byte{2};
    refused(frame(reply, sender), BoundFrameError::invalid_payload);

    PeerMessage batch{1, 2, AppendEntries{7, 0, 0, 0, {}}};
    auto& entries = std::get<AppendEntries>(batch.payload).entries;
    entries.assign(kMaxBoundPeerEntries, PeerLogEntry{7, 1, 0, command()});
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    require(encode_bound_peer(batch, sender, output, scratch).error == BoundFrameError::none,
        "16-entry batch refused");
    entries.push_back(entries.front());
    refused(frame(batch, sender), BoundFrameError::capacity_exceeded);
    const auto before = output;
    require(encode_bound_peer(batch, sender, output, scratch).error == BoundFrameError::capacity_exceeded,
        "oversized outbound batch accepted");
    require(output == before, "oversized encode mutated output");
    entries.clear();
    auto inner = encode_bound_peer_body(batch);
    put32(inner, 48, 0xffffffffU);
    refused(frame(inner, sender), BoundFrameError::capacity_exceeded);
    entries.assign(1, PeerLogEntry{7, 1, 0, command()});
    inner = encode_bound_peer_body(batch);
    put32(inner, 68, 0xffffffffU);
    refused(frame(inner, sender), BoundFrameError::capacity_exceeded);
    entries.assign(3, PeerLogEntry{7, 1, 0, command()});
    inner = encode_bound_peer_body(batch);
    const auto third_length = 52 + 2 * (20 + command().size()) + 16;
    put32(inner, third_length, 0xffffffffU);
    refused(frame(inner, sender), BoundFrameError::capacity_exceeded);
    inner = encode_bound_peer_body(batch);
    inner.pop_back();
    refused(frame(inner, sender), BoundFrameError::invalid_payload);
    entries.resize(1);
    entries[0].payload.assign(kMaxKernelCommandBytes + 1, std::byte{});
    refused(frame(batch, sender), BoundFrameError::capacity_exceeded);
    entries[0].payload = command();
    entries[0].type = 17;
    refused(frame(batch, sender), BoundFrameError::invalid_payload);
    entries[0].type = 0;
    entries[0].payload[0] ^= std::byte{1};
    refused(frame(batch, sender), BoundFrameError::invalid_payload);
    // Framing is insufficient: an otherwise valid marker must name this authority domain.
    AuthorityBarrier barrier{};
    barrier.incarnation[0] = std::byte{1};
    barrier.request_sequence = 1;
    barrier.effect_id = EffectId{1};
    barrier.expected_kill_epoch = KillEpoch{1};
    barrier.expected_executor_generation = ExecutorGeneration{1};
    barrier.expected_effect_payload_digest.bytes[0] = std::byte{1};
    barrier.catalog_root = sender.value().catalog_root;
    barrier.authority_domain = context(1, 0xCD).authority_domain();
    std::array<std::byte, kAuthorityBarrierBytes> marker{};
    const auto marker_encoded = encode_authority_barrier(kAuthorityBarrierEntryType, barrier, marker);
    require(marker_encoded.error == AuthorityBarrierCodecError::none, "marker fixture");
    entries[0] = {7, 1, kAuthorityBarrierEntryType, {marker.begin(), marker.end()}};
    refused(frame(batch, sender), BoundFrameError::invalid_payload);
    barrier.authority_domain = sender.authority_domain();
    require(encode_authority_barrier(kAuthorityBarrierEntryType, barrier, marker).error ==
        AuthorityBarrierCodecError::none, "marker fixture reencode");
    entries[0].payload.assign(marker.begin(), marker.end());
    require(encode_bound_peer(batch, sender, output, scratch).error == BoundFrameError::none,
        "same-domain marker refused");
    require(encode_bound_peer(messages[0], receiver, output, scratch).error == BoundFrameError::invalid_route,
        "sender context mismatch accepted");
    require(encode_bound_peer(messages[0], VerifiedStaticIdentity{}, output, scratch).error ==
        BoundFrameError::invalid_identity, "unverified sender accepted");

    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    const std::array<PeerMessage, 14> invalid_coordinates{{
        {1, 2, RequestVote{-1, 1, 0, 0}},
        {1, 2, RequestVote{1, 1, -1, 0}},
        {1, 2, RequestVote{1, 1, 0, -1}},
        {1, 2, RequestVote{1, 1, max, 0}},
        {1, 2, RequestVoteResponse{-1, false}},
        {1, 2, AppendEntries{-1, 0, 0, 0, {}}},
        {1, 2, AppendEntries{1, -1, 0, 0, {}}},
        {1, 2, AppendEntries{1, 0, -1, 0, {}}},
        {1, 2, AppendEntries{1, 0, 0, -1, {}}},
        {1, 2, AppendEntries{1, 0, 0, 0, {{0, 1, 0, command()}}}},
        {1, 2, AppendEntries{1, 0, 0, 0, {{-1, 1, 0, command()}}}},
        {1, 2, AppendEntriesResponse{-1, false, 0, 1}},
        {1, 2, AppendEntriesResponse{1, false, -1, 1}},
        {1, 2, AppendEntriesResponse{1, false, max, 1}},
    }};
    for (const auto& invalid : invalid_coordinates)
        refused(frame(invalid, sender), BoundFrameError::invalid_payload);
    refused(frame(PeerMessage{1, 2, AppendEntriesResponse{1, false, 0, 0}}, sender),
        BoundFrameError::invalid_payload);

    const std::array<PeerMessage, 4> coordinate_edges{{
        {1, 2, RequestVote{0, 1, max - 1, 0}},
        {1, 2, RequestVoteResponse{0, false}},
        {1, 2, AppendEntries{0, max - 1, 0, 0, {}}},
        {1, 2, AppendEntriesResponse{0, false, max - 1, 1}},
    }};
    for (const auto& edge : coordinate_edges) {
        PeerMessage decoded;
        require(decode_bound_peer(frame(edge, sender), receiver, decoded, scratch) ==
            BoundFrameError::none, "representable coordinate edge refused");
    }
}

template<class T> concept HasRawIngress = requires(T& node, const PeerMessage& raw) {
    node.receive_peer(raw);
};
static_assert(!HasRawIngress<ReplicatedKernel>);

struct OwnedDirectory {
    std::filesystem::path path;
    OwnedDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-bound-peer-tests.XXXXXX").string();
        const auto* result = ::mkdtemp(pattern.data());
        require(result != nullptr, "mkdtemp failed");
        path = result;
    }
    ~OwnedDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
auto directory_image(const std::filesystem::path& directory) {
    std::map<std::string, std::string> image;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        image.emplace(entry.path().filename().string(),
            std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
    }
    return image;
}
void admission_tests() {
    OwnedDirectory directory;
    ReplicatedKernel receiver(identity_test::bootstrap(directory.path / "receiver", 2, {1, 2, 3}));
    const auto before = receiver.status();
    const auto files = directory_image(directory.path / "receiver");
    const auto assert_unchanged = [&] {
        const auto after = receiver.status();
        require(after.term == before.term && after.current_index == before.current_index &&
            after.commit_index == before.commit_index && after.applied_index == before.applied_index &&
            after.state_digest == before.state_digest && after.role == before.role,
            "refused input changed Raft or reducer");
        require(directory_image(directory.path / "receiver") == files, "refused input changed storage");
        require(!identity_test::take_outbound_message(receiver), "refused input emitted a Raft response");
    };
    const std::array<PeerMessage, 4> messages{{
        {1, 2, RequestVote{999, 1, 0, 0}},
        {1, 2, RequestVoteResponse{999, true}},
        {1, 2, AppendEntries{999, 0, 0, 0, {{999, 1, 0, command()}}}},
        {1, 2, AppendEntriesResponse{999, true, 1, 1}},
    }};
    for (const auto& message : messages) {
        const auto error = receiver.receive_peer(frame(message, context(1, 0xCD)));
        assert_unchanged();
        require(error == BoundFrameError::foreign_manifest,
            "foreign request/reply not refused before Raft");
    }
    auto bad = messages[2];
    std::get<AppendEntries>(bad.payload).entries[0].payload[0] ^= std::byte{1};
    require(receiver.receive_peer(frame(bad, context(1))) == BoundFrameError::invalid_payload,
        "invalid canonical command reached Raft");
    assert_unchanged();
    bad = messages[0];
    std::get<RequestVote>(bad.payload).candidate_id = 3;
    require(receiver.receive_peer(frame(bad, context(1))) == BoundFrameError::invalid_route,
        "candidate/sender disagreement reached Raft");
    assert_unchanged();
    require(receiver.receive_peer(encode_bound_peer_body(messages[0])) != BoundFrameError::none,
        "unbound old wire admitted");
    assert_unchanged();
    const std::array<PeerMessage, 4> invalid_higher_term_coordinates{{
        {1, 2, RequestVote{999, 1, -1, 0}},
        {1, 2, AppendEntries{999, -1, 0, 0, {}}},
        {1, 2, AppendEntries{999, 0, 0, 0, {{0, 1, 0, command()}}}},
        {1, 2, AppendEntriesResponse{999, false, -1, 1}},
    }};
    for (const auto& invalid : invalid_higher_term_coordinates) {
        require(receiver.receive_peer(frame(invalid, context(1))) ==
            BoundFrameError::invalid_payload,
            "invalid higher-term coordinate reached Raft");
        assert_unchanged();
    }
    require(receiver.receive_peer(frame(messages[0], context(1))) == BoundFrameError::none,
        "healthy vote refused");
    require(receiver.status().term == 999, "healthy vote did not reach Raft");
    auto reply = identity_test::take_outbound_message(receiver);
    require(reply && std::holds_alternative<RequestVoteResponse>(reply->payload) &&
        std::get<RequestVoteResponse>(reply->payload).vote_granted,
        "healthy vote produced no affirmative response");
}

void bounded_batch_progress() {
    OwnedDirectory directory;
    std::array<std::unique_ptr<ReplicatedKernel>, 3> nodes;
    for (int id = 1; id <= 3; ++id)
        nodes[id-1] = std::make_unique<ReplicatedKernel>(
            identity_test::bootstrap(directory.path / std::to_string(id), id, {1, 2, 3}));
    bool full_batch_seen = false;
    const auto route = [&] {
        struct Delivery { int recipient; std::vector<std::byte> bytes; };
        std::vector<Delivery> pending;
        for (auto& sender : nodes) {
            while (auto message = identity_test::take_outbound_message(*sender)) {
                if (auto* append = std::get_if<AppendEntries>(&message->payload)) {
                    require(append->entries.size() <= 16, "outgoing batch exceeds approved 16 entries");
                    full_batch_seen |= append->entries.size() == 16;
                }
                BoundFrameScratch scratch;
                std::vector<std::byte> bytes(kMaxBoundPeerBytes);
                auto encoded = encode_bound_peer(*message, sender->static_identity(), bytes, scratch);
                require(encoded.error == BoundFrameError::none, "actual outgoing message cannot encode");
                bytes.resize(encoded.bytes_written);
                pending.push_back({message->recipient_id, std::move(bytes)});
            }
        }
        for (const auto& delivery : pending)
            require(nodes.at(delivery.recipient-1)->receive_peer(delivery.bytes) == BoundFrameError::none,
                "healthy bounded cluster delivery refused");
    };
    const auto step = [&] {
        for (auto& node : nodes) { node->flush_durability(); node->tick(10); }
        route();
    };
    ReplicatedKernel* leader = nullptr;
    for (int i = 0; i < 2000 && !leader; ++i) {
        step();
        for (auto& node : nodes) if (node->is_leader()) leader = node.get();
    }
    require(leader, "bounded cluster failed election");
    for (int i = 0; i < 20; ++i) step();
    for (std::uint64_t i = 1; i <= 40; ++i)
        require(leader->propose(command(i)).status == ProposeStatus::admitted, "batch backlog proposal refused");
    // Followers have not received this backlog; one transport batch cannot contain all of it.
    leader->flush_durability();
    leader->tick(200);
    route();
    for (int i = 0; i < 200; ++i) step();
    require(full_batch_seen, "fixture never exercised full batch boundary");
    for (auto& node : nodes) {
        require(node->state().kill_epoch == KillEpoch{40}, "bounded prefix lost its later suffix");
        require(node->status().applied_index == 40 && node->status().commit_index == 40,
            "bounded batch cluster did not converge through all 40 entries");
    }
}

void verified_prefix_limits_commit(const bool heartbeat) {
    // Synthetic ingress regression, not a claimed honest-sender election trace.
    OwnedDirectory directory;
    const auto path = directory.path / "2";
    {
        ReplicatedKernel node(identity_test::bootstrap(path, 2, {1, 2, 3}));
        AppendEntries shared{1, 0, 0, 0, {}};
        for (int i = 1; i <= 16; ++i)
            shared.entries.push_back({1, i, RAFT_LOGTYPE_NORMAL, command(i)});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, shared});
        node.flush_durability();
        AppendEntries old_tail{2, 16, 1, 0, {}};
        for (int i = 17; i <= 20; ++i)
            old_tail.entries.push_back({2, i, RAFT_LOGTYPE_NORMAL, command(i + 100)});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, old_tail});
        node.flush_durability();
        require(node.status().current_index == 20 && node.status().commit_index == 0 &&
            node.state().kill_epoch == KillEpoch{}, "uncommitted divergent suffix fixture");
        while (identity_test::take_outbound_message(node)) {}

        auto prefix = shared;
        prefix.term = 3;
        prefix.leader_commit = 20;
        if (heartbeat) {
            prefix.previous_log_index = 16;
            prefix.previous_log_term = 1;
            prefix.entries.clear();
        }
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, prefix});
        require(node.state().kill_epoch == KillEpoch{16},
            "unverified divergent suffix changed economic state");
        require(node.status().commit_index == 16 && node.status().applied_index == 16,
            "commit passed the message-verified prefix");
        for (int i = 1; i <= 16; ++i) {
            auto result = node.take_committed_result();
            require(result && result->index == i &&
                result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(i)},
                "verified prefix result missing or replaced");
        }
        require(!node.take_committed_result(), "unverified suffix produced a result");
        const auto reply = identity_test::take_outbound_message(node);
        require(reply && std::holds_alternative<AppendEntriesResponse>(reply->payload) &&
            std::get<AppendEntriesResponse>(reply->payload).success &&
            std::get<AppendEntriesResponse>(reply->payload).current_index == 16,
            "reply acknowledged more than the verified prefix");

        AppendEntries repair{3, 16, 1, 20, {}};
        for (int i = 17; i <= 20; ++i)
            repair.entries.push_back({3, i, RAFT_LOGTYPE_NORMAL, command(i)});
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, repair});
        node.flush_durability();
        node.tick(0); // Checkpoint the newly durable committed prefix before restart.
        require(node.state().kill_epoch == KillEpoch{20} && node.status().applied_index == 20,
            "verified replacement suffix did not apply");
        for (int i = 17; i <= 20; ++i) {
            const auto result = node.take_committed_result();
            require(result && result->index == i &&
                result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(i)},
                "replacement result did not name the leader's command");
        }
        require(!node.take_committed_result(), "unexpected result after suffix repair");
    }
    ReplicatedKernel recovered(identity_test::recover(path, 2, {1, 2, 3}));
    require(recovered.state().kill_epoch == KillEpoch{20} && recovered.status().applied_index == 20,
        "repaired prefix did not survive restart");
}

void prefix_verification_boundaries() {
    OwnedDirectory directory;
    ReplicatedKernel node(identity_test::bootstrap(directory.path / "2", 2, {1, 2, 3}));
    const auto deliver = [&](const AppendEntries& append) {
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, append});
        const auto reply = identity_test::take_outbound_message(node);
        require(reply && std::holds_alternative<AppendEntriesResponse>(reply->payload),
            "prefix boundary reply missing");
        return std::get<AppendEntriesResponse>(reply->payload);
    };
    const auto empty = deliver(AppendEntries{1, 0, 0, 20, {}});
    require(empty.success && empty.current_index == 0 && node.status().commit_index == 0 &&
        node.status().applied_index == 0 && node.state().kill_epoch == KillEpoch{} &&
        !node.take_committed_result(), "empty heartbeat invented committed history");

    AppendEntries shared{1, 0, 0, 0, {}};
    for (int i = 1; i <= 16; ++i)
        shared.entries.push_back({1, i, RAFT_LOGTYPE_NORMAL, command(i)});
    (void)deliver(shared);
    node.flush_durability();
    shared.leader_commit = 8;
    const auto partial = deliver(shared);
    require(partial.success && partial.current_index == 16 && node.status().commit_index == 8 &&
        node.status().applied_index == 8 && node.state().kill_epoch == KillEpoch{8},
        "verified batch end overrode the leader's lower commit");
    for (int i = 1; i <= 8; ++i) {
        const auto result = node.take_committed_result();
        require(result && result->index == i, "lower commit result missing");
    }
    require(!node.take_committed_result(), "result beyond leader commit");
    const auto mismatch = deliver(AppendEntries{2, 16, 2, 20, {}});
    require(!mismatch.success && node.status().term == 2 && node.status().current_index == 15 &&
        node.status().commit_index == 8 && node.status().applied_index == 8 &&
        node.state().kill_epoch == KillEpoch{8} && !node.take_committed_result(),
        "failed previous-term verification advanced committed state");
}

void append_prefix_arithmetic_refuses() {
    const auto sender = context(1);
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    for (const auto previous : {-1LL, static_cast<long long>(max)})
        refused(frame(PeerMessage{1, 2, AppendEntries{1, previous, 1, 0, {}}}, sender),
            BoundFrameError::invalid_payload);
    AppendEntries overflow{1, max - 15, 1, 0, {}};
    overflow.entries.assign(16, PeerLogEntry{1, 1, RAFT_LOGTYPE_NORMAL, command()});
    refused(frame(PeerMessage{1, 2, overflow}, sender), BoundFrameError::invalid_payload);
    overflow.previous_log_index = max - 17;
    PeerMessage decoded;
    BoundFrameScratch scratch;
    require(decode_bound_peer(frame(PeerMessage{1, 2, overflow}, sender), context(2), decoded, scratch) ==
        BoundFrameError::none, "representable prefix edge refused");
}
} // namespace
int main() {
    try {
        const auto* selected = std::getenv("BOUND_PEER_CASE");
        const std::string test = selected ? selected : "all";
        require(test == "all" || test == "codec" || test == "admission" || test == "batch" ||
            test == "prefix" || test == "heartbeat-prefix" || test == "prefix-arithmetic" ||
            test == "prefix-boundaries",
            "unknown bound peer test selector");
        if (test == "all" || test == "codec") codec_tests();
        if (test == "all" || test == "codec") compaction_floor_tests();
        if (test == "all" || test == "admission") admission_tests();
        if (test == "all" || test == "batch") bounded_batch_progress();
        if (test == "all" || test == "prefix") verified_prefix_limits_commit(false);
        if (test == "all" || test == "heartbeat-prefix") verified_prefix_limits_commit(true);
        if (test == "all" || test == "prefix-arithmetic") append_prefix_arithmetic_refuses();
        if (test == "all" || test == "prefix-boundaries") prefix_verification_boundaries();
        std::cout << "bound peer codec/admission/batch progress passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
