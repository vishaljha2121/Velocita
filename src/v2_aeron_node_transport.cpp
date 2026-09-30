#include "velocita/v2/aeron_node_transport.hpp"

#include "FragmentAssembler.h"

#include <stdexcept>

namespace velocita::v2 {

std::string udp_channel(const Endpoint& endpoint) {
    return "aeron:udp?endpoint=" + endpoint.host + ":" + std::to_string(endpoint.port);
}

namespace {

// Whole reassembled messages in a small bounded inbox. Oversized or overflowing
// input is dropped here and counted: nothing allocates for an unauthenticated frame.
struct BoundedInbox {
    std::shared_ptr<aeron::Subscription> subscription;
    std::deque<std::vector<std::byte>> inbox;
    std::uint64_t* dropped;
    aeron::FragmentAssembler assembler;

    BoundedInbox(std::shared_ptr<aeron::Subscription> source, std::uint64_t* dropped_counter)
        : subscription(std::move(source)), dropped(dropped_counter),
          assembler([this](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset,
                           aeron::util::index_t length, aeron::Header&) {
              if (static_cast<std::size_t>(length) > kMaxAuthEnvelopeBytes ||
                  inbox.size() >= AeronNodeTransport::kInboxFrames) {
                  ++*dropped;
                  return;
              }
              const auto* begin = reinterpret_cast<const std::byte*>(buffer.buffer() + offset);
              inbox.emplace_back(begin, begin + length);
          }) {}
};

}  // namespace

struct AeronNodeTransport::Inbound : BoundedInbox {
    using BoundedInbox::BoundedInbox;
};

AeronNodeTransport::AeronNodeTransport(AeronRuntime& runtime, const NodeConfig& config) {
    const VoterConfig* self = nullptr;
    for (const auto& voter : config.voters) {
        if (voter.id == config.node_id) {
            self = &voter;
            continue;
        }
        peers_.emplace(voter.id, runtime.add_publication(udp_channel(voter.endpoint), kPeerStreamId));
    }
    if (!self) throw std::runtime_error("node is not among its configured voters");
    peer_ = std::make_unique<Inbound>(runtime.add_subscription(udp_channel(self->endpoint), kPeerStreamId), &dropped_);
    ingress_ = std::make_unique<Inbound>(
        runtime.add_subscription(udp_channel(config.ingress_endpoint), kIngressStreamId), &dropped_);
    for (const auto& caller : config.callers)
        responses_.emplace(caller.key_id,
                           runtime.add_publication(udp_channel(caller.response_endpoint), kResponseStreamId));
}

AeronNodeTransport::~AeronNodeTransport() = default;

std::optional<std::size_t> AeronNodeTransport::receive(Channel channel, std::span<std::byte> buffer) {
    auto& inbound = channel == Channel::peer ? *peer_ : *ingress_;
    if (inbound.inbox.empty()) inbound.subscription->poll(inbound.assembler.handler(), kPollFragments);
    while (!inbound.inbox.empty()) {
        auto frame = std::move(inbound.inbox.front());
        inbound.inbox.pop_front();
        if (frame.size() > buffer.size()) {
            ++dropped_;
            continue;
        }
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
    return std::nullopt;
}

namespace {

bool offered(aeron::Publication& publication, std::span<const std::byte> bytes) {
    const auto result = AeronRuntime::offer(publication, bytes);
    if (result >= 0) return true;
    if (result == aeron::PUBLICATION_CLOSED || result == aeron::MAX_POSITION_EXCEEDED)
        throw std::runtime_error("Aeron publication became unusable");
    return false;  // back-pressured, not connected or admin action: the caller keeps the frame
}

}  // namespace

bool AeronNodeTransport::offer_peer(std::int32_t voter_id, std::span<const std::byte> envelope) {
    const auto found = peers_.find(voter_id);
    if (found == peers_.end()) throw std::runtime_error("offer to an unconfigured voter");
    return offered(*found->second, envelope);
}

bool AeronNodeTransport::offer_response(std::int32_t caller_key_id, std::span<const std::byte> envelope) {
    const auto found = responses_.find(caller_key_id);
    if (found == responses_.end()) return false;
    return offered(*found->second, envelope);
}

struct AeronClientTransport::Inbound : BoundedInbox {
    using BoundedInbox::BoundedInbox;
};

AeronClientTransport::AeronClientTransport(AeronRuntime& runtime, const std::map<std::int32_t, Endpoint>& voters,
                                           const Endpoint& response_endpoint) {
    if (voters.empty()) throw std::runtime_error("client transport needs at least one voter");
    for (const auto& [id, endpoint] : voters)
        ingress_.emplace(id, runtime.add_publication(udp_channel(endpoint), kIngressStreamId));
    responses_ = std::make_unique<Inbound>(runtime.add_subscription(udp_channel(response_endpoint), kResponseStreamId),
                                           &dropped_);
}

AeronClientTransport::~AeronClientTransport() = default;

bool AeronClientTransport::offer_ingress(std::int32_t node, std::span<const std::byte> envelope) {
    const auto found = ingress_.find(node);
    if (found == ingress_.end()) throw std::runtime_error("offer to an unconfigured voter");
    return offered(*found->second, envelope);
}

std::optional<std::size_t> AeronClientTransport::receive_response(std::span<std::byte> buffer) {
    auto& inbound = *responses_;
    if (inbound.inbox.empty()) inbound.subscription->poll(inbound.assembler.handler(), AeronNodeTransport::kPollFragments);
    while (!inbound.inbox.empty()) {
        auto frame = std::move(inbound.inbox.front());
        inbound.inbox.pop_front();
        if (frame.size() > buffer.size()) {
            ++dropped_;
            continue;
        }
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
    return std::nullopt;
}

}  // namespace velocita::v2
