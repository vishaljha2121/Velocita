#include "velocita/v2/outbound_mailbox.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita;
using namespace velocita::v2;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
VerifiedStaticIdentity context(int local) {
    auto value = identity_test::identity(local, {1, 2, 3}, catalog_test::standard_catalog());
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    VerifiedStaticIdentity result;
    require(encoded.error == StaticIdentityCodecError::none &&
        VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), result) == StaticIdentityCodecError::none,
        "identity fixture");
    return result;
}
std::vector<std::byte> command(std::uint64_t sequence) {
    velocita::v2::Command value;
    value.identity = {CallerId{1}, RequestSequence{sequence}, {}};
    value.identity.payload_digest.bytes[0] = std::byte{1};
    value.body = AdvanceKillEpoch{KillEpoch{sequence}};
    std::array<std::byte, kMaxKernelCommandBytes> output{};
    const auto encoded = encode_kernel_command(value, output);
    require(encoded.error == KernelCodecError::none, "command fixture");
    return {output.begin(), output.begin() + encoded.bytes_written};
}
PeerMessageView view(const PeerMessage& message) {
    return {message.sender_id, message.recipient_id, std::visit([](const auto& value) -> BorrowedPeerPayload {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, AppendEntries>) return BorrowedAppendEntries{};
        else return value;
    }, message.payload)};
}
void encoder_bytes_and_borrowing() {
    const auto sender = context(1);
    BoundFrameScratch first_scratch, second_scratch;
    std::array<std::byte, kMaxBoundPeerBytes> first{}, second{};
    const std::array<PeerMessage, 3> small{{
        {1, 2, RequestVote{7, 1, 4, 3}}, {1, 2, RequestVoteResponse{7, true}},
        {1, 2, AppendEntriesResponse{7, false, 4, 1}},
    }};
    const std::array<std::size_t, 3> expected_sizes{{136, 113, 129}};
    std::size_t small_index{};
    for (const auto& message : small) {
        const auto owned = encode_bound_peer(message, sender, first, first_scratch);
        const auto borrowed = encode_bound_peer(view(message), sender, second, second_scratch);
        require(owned.error == BoundFrameError::none && borrowed.error == BoundFrameError::none &&
            owned.bytes_written == borrowed.bytes_written &&
            std::equal(first.begin(), first.begin() + owned.bytes_written, second.begin()) &&
            owned.bytes_written == expected_sizes[small_index++], "small golden bytes/size");
    }
    std::array<std::vector<std::byte>, 16> payloads;
    std::array<PeerLogEntry, 16> owned_entries;
    std::array<BorrowedPeerLogEntry, 16> borrowed_entries;
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        payloads[i] = command(i + 1);
        owned_entries[i] = {3, static_cast<std::int32_t>(i), 0, payloads[i]};
        borrowed_entries[i] = {3, static_cast<std::int32_t>(i), 0, payloads[i]};
    }
    PeerMessage owned{1, 2, AppendEntries{8, 10, 3, 9, {owned_entries.begin(), owned_entries.end()}}};
    PeerMessageView borrowed{1, 2, BorrowedAppendEntries{8, 10, 3, 9, borrowed_entries}};
    const auto a = encode_bound_peer(owned, sender, first, first_scratch);
    const auto b = encode_bound_peer(borrowed, sender, second, second_scratch);
    require(a.error == BoundFrameError::none && b.error == BoundFrameError::none && a.bytes_written == b.bytes_written &&
        std::equal(first.begin(), first.begin() + a.bytes_written, second.begin()), "AE golden bytes");
    borrowed.recipient_id = 1;
    require(encode_bound_peer(borrowed, sender, second, second_scratch).error == BoundFrameError::invalid_route,
        "borrowed route semantics");

    // Deferred transport owns canonical bytes, never the caller's entry spans.
    OutboundMailboxes boxes(sender);
    borrowed.recipient_id = 2;
    const auto expected = encode_bound_peer(borrowed, sender, first, first_scratch);
    require(boxes.enqueue(borrowed, second_scratch).error == OutboundEnqueueError::none, "borrowed enqueue");
    payloads[0][0] ^= std::byte{0xff};
    const auto taken = boxes.take(second);
    require(taken.error == OutboundTakeError::none && taken.bytes_written == expected.bytes_written &&
        std::equal(first.begin(), first.begin() + expected.bytes_written, second.begin()), "borrowed bytes escaped");

    auto alias_payload = command(77);
    BorrowedPeerLogEntry expected_entry{3, 77, 0, alias_payload};
    PeerMessageView expected_view{1, 2, BorrowedAppendEntries{8, 10, 3, 9, {&expected_entry, 1}}};
    const auto alias_expected = encode_bound_peer(expected_view, sender, first, first_scratch);
    std::copy(alias_payload.begin(), alias_payload.end(), second.begin());
    BorrowedPeerLogEntry alias_entry{3, 77, 0, std::span<const std::byte>{second}.first(alias_payload.size())};
    PeerMessageView alias_view{1, 2, BorrowedAppendEntries{8, 10, 3, 9, {&alias_entry, 1}}};
    const auto alias_encoded = encode_bound_peer(alias_view, sender, second, second_scratch);
    require(alias_encoded.error == BoundFrameError::none && alias_encoded.bytes_written == alias_expected.bytes_written &&
        std::equal(first.begin(), first.begin() + alias_expected.bytes_written, second.begin()), "output/input alias");

    OutboundMailboxes allocation_free(sender);
    allocation_probe::count = 0; allocation_probe::enabled = true;
    const auto allocation_encoded = encode_bound_peer(expected_view, sender, first, first_scratch);
    const auto allocation_enqueue = allocation_free.enqueue(expected_view, second_scratch);
    const auto allocation_take = allocation_free.take(second);
    allocation_probe::enabled = false;
    require(allocation_encoded.error == BoundFrameError::none &&
        allocation_enqueue.error == OutboundEnqueueError::none &&
        allocation_take.error == OutboundTakeError::none && allocation_probe::count == 0,
        "borrowed AppendEntries path allocated");
}
void replacement_copyout_and_ownership() {
    const auto sender = context(1); BoundFrameScratch scratch; OutboundMailboxes boxes(sender);
    PeerMessage vote{1, 2, RequestVote{4, 1, 0, 0}};
    require(boxes.enqueue(vote, scratch).error == OutboundEnqueueError::none, "first enqueue");
    vote.payload = RequestVote{3, 1, 0, 0};
    require(boxes.enqueue(vote, scratch).error == OutboundEnqueueError::stale_term, "lower term");
    vote.payload = RequestVote{4, 1, 2, 1};
    const auto replacement = boxes.enqueue(vote, scratch);
    require(replacement.error == OutboundEnqueueError::none && replacement.replaced && boxes.size() == 1 &&
        boxes.counters().replacements == 1, "same-term whole replacement");
    require(boxes.enqueue(PeerMessage{1, 2, RequestVoteResponse{4, true}}, scratch).error == OutboundEnqueueError::none &&
        boxes.size() == 2, "cross kind");
    std::array<std::byte, 1> short_output{};
    require(boxes.take(short_output).error == OutboundTakeError::output_too_small && boxes.size() == 2,
        "undersized consumed");
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    const auto first = boxes.take(output); const auto saved = output;
    require(first.error == OutboundTakeError::none && boxes.size() == 1, "take");
    vote.payload = RequestVote{5, 1, 2, 1};
    require(boxes.enqueue(vote, scratch).error == OutboundEnqueueError::none &&
        std::equal(saved.begin(), saved.begin() + first.bytes_written, output.begin()), "owned bytes");
    require(boxes.counters().stale_term_refusals == 1, "counter");

    OutboundMailboxes exact(sender);
    PeerMessage success{1, 2, AppendEntriesResponse{6, true, 10, 3}};
    PeerMessage failure{1, 2, AppendEntriesResponse{6, false, 4, 2}};
    require(exact.enqueue(success, scratch).error == OutboundEnqueueError::none &&
        exact.enqueue(failure, scratch).replaced, "same-term response replacement");
    const auto expected_failure = encode_bound_peer(failure, sender, output, scratch);
    std::array<std::byte, kMaxBoundPeerBytes> drained{};
    const auto exact_take = exact.take(drained);
    require(exact_take.bytes_written == expected_failure.bytes_written &&
        std::equal(output.begin(), output.begin() + expected_failure.bytes_written, drained.begin()),
        "response fields were merged");
    require(exact.enqueue(PeerMessage{1, 2, AppendEntriesResponse{5, true, 99, 1}}, scratch).error ==
        OutboundEnqueueError::stale_term, "drain lost term watermark");

    OutboundMailboxes preserved(sender);
    require(preserved.enqueue(success, scratch).error == OutboundEnqueueError::none, "preserve setup");
    const auto expected_success = encode_bound_peer(success, sender, output, scratch);
    require(preserved.enqueue(PeerMessage{1, 1, AppendEntriesResponse{7, false, 1, 1}}, scratch).error ==
        OutboundEnqueueError::invalid_peer, "invalid route accepted");
    const auto preserved_take = preserved.take(drained);
    require(preserved_take.bytes_written == expected_success.bytes_written &&
        std::equal(output.begin(), output.begin() + expected_success.bytes_written, drained.begin()),
        "invalid enqueue changed prior slot");

}
void fair_cursor_under_refill() {
    const auto sender = context(1); BoundFrameScratch scratch; OutboundMailboxes boxes(sender);
    auto fill = [&](int peer) {
        require(boxes.enqueue(PeerMessage{1, peer, RequestVote{9, 1, 0, 0}}, scratch).error == OutboundEnqueueError::none, "rv");
        require(boxes.enqueue(PeerMessage{1, peer, RequestVoteResponse{9, true}}, scratch).error == OutboundEnqueueError::none, "rvr");
        require(boxes.enqueue(PeerMessageView{1, peer, BorrowedAppendEntries{9, 0, 0, 0, {}}}, scratch).error == OutboundEnqueueError::none, "ae");
        require(boxes.enqueue(PeerMessage{1, peer, AppendEntriesResponse{9, true, 2, 1}}, scratch).error == OutboundEnqueueError::none, "aer");
    };
    fill(2); fill(3);
    std::array<std::byte, kMaxBoundPeerBytes> output{}; std::array<bool, 8> seen{};
    for (std::size_t i = 0; i < seen.size(); ++i) {
        const auto taken = boxes.take(output); require(taken.error == OutboundTakeError::none, "fair take");
        seen[(taken.recipient_id == 2 ? 0U : 4U) + static_cast<unsigned>(taken.kind) - 1] = true;
        if (taken.kind == PeerMessageType::request_vote_response)
            require(boxes.enqueue(PeerMessage{1, taken.recipient_id, RequestVoteResponse{9, false}}, scratch).error == OutboundEnqueueError::none, "refill");
    }
    require(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "starvation");
}
// A held (unreachable) peer's frames stay queued and keep coalescing, while
// every other peer's frames still come out: one dead voter must not stall
// the rest of a three-voter cluster.
void held_peer_does_not_stall_others() {
    const auto sender = context(1); BoundFrameScratch scratch; OutboundMailboxes boxes(sender);
    for (const int peer : {2, 3}) {
        require(boxes.enqueue(PeerMessage{1, peer, RequestVote{9, 1, 0, 0}}, scratch).error == OutboundEnqueueError::none, "hold rv");
        require(boxes.enqueue(PeerMessage{1, peer, AppendEntriesResponse{9, true, 2, 1}}, scratch).error == OutboundEnqueueError::none, "hold aer");
    }
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    const std::array<std::int32_t, 1> held_three{3};
    for (int i = 0; i < 2; ++i) {
        const auto taken = boxes.take(output, held_three);
        require(taken.error == OutboundTakeError::none && taken.recipient_id == 2, "held peer's frame taken, or live peer stalled");
    }
    require(boxes.take(output, held_three).error == OutboundTakeError::empty && boxes.size() == 2, "held frames lost");
    const PeerMessage newer{1, 3, RequestVote{10, 1, 0, 0}};
    require(boxes.enqueue(newer, scratch).replaced && boxes.size() == 2, "held peer's queue is not bounded by replacement");
    const std::array<std::int32_t, 2> held_both{2, 3};
    require(boxes.take(output, held_both).error == OutboundTakeError::empty, "frame taken for a held peer");
    std::array<std::byte, kMaxBoundPeerBytes> expected{};
    const auto expected_vote = encode_bound_peer(newer, sender, expected, scratch);
    bool saw_newer = false;
    for (int i = 0; i < 2; ++i) {
        const auto taken = boxes.take(output);
        require(taken.error == OutboundTakeError::none && taken.recipient_id == 3, "released peer's frames missing");
        if (taken.kind == PeerMessageType::request_vote)
            saw_newer = taken.bytes_written == expected_vote.bytes_written &&
                std::equal(expected.begin(), expected.begin() + expected_vote.bytes_written, output.begin());
    }
    require(saw_newer && boxes.empty(), "released peer did not receive its latest frame");
}
} // namespace
int main() {
    try {
        encoder_bytes_and_borrowing(); replacement_copyout_and_ownership(); fair_cursor_under_refill();
        held_peer_does_not_stall_others();
        std::cout << "v2 outbound mailbox tests passed; owner_bytes=" << sizeof(OutboundMailboxes) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v2 outbound mailbox test failure: " << error.what() << '\n'; return 1;
    }
}
