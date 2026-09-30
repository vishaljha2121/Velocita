#include "velocita/v2/gateway_link.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <algorithm>
#include <stdexcept>

namespace velocita::v2 {
namespace {

// "Something is wrong" ends the request; capacity/busy says "later".
bool final_refusal(std::uint8_t reason) noexcept {
    switch (reason) {
    case static_cast<std::uint8_t>(ProposeStatus::refused):
    case static_cast<std::uint8_t>(ProposeStatus::not_a_control_command):
    case static_cast<std::uint8_t>(IngressRefusal::caller_mismatch):
    case static_cast<std::uint8_t>(IngressRefusal::malformed_command):
    case static_cast<std::uint8_t>(IngressRefusal::not_gateway_caller):
    case static_cast<std::uint8_t>(IngressRefusal::authority_lost):
        return true;
    default:
        return false;
    }
}

StateChunkHeader without_index(StateChunkHeader header) noexcept {
    header.chunk_index = 0;
    return header;
}

}  // namespace

GatewayLink::GatewayLink(std::uint64_t session_epoch, std::array<std::byte, 16> process_incarnation,
                         std::vector<ClientRoute> routes, ClientTransport& transport, GatewayLinkTiming timing)
    : session_epoch_(session_epoch), incarnation_(process_incarnation), transport_(transport), timing_(timing),
      target_(routes.empty() ? 0 : routes.front().node), body_(kMaxIngressBodyBytes),
      snapshot_(kMaxStateSnapshotBytes), state_(std::make_unique<TradingState>()),
      scratch_(std::make_unique<StateSnapshotScratch>()), envelope_(kMaxAuthEnvelopeBytes),
      inbound_(kMaxAuthEnvelopeBytes) {
    if (routes.empty()) throw std::invalid_argument("a gateway link needs at least one voter");
    if (std::all_of(incarnation_.begin(), incarnation_.end(), [](std::byte b) { return b == std::byte{0}; }))
        throw std::invalid_argument("a gateway link needs a nonzero process incarnation");
    for (auto& route : routes)
        routes_.push_back(Route{route.node, std::move(route.ingress), std::move(route.responses),
                                SessionSequencer(session_epoch), SessionFence{}});
}

GatewayLink::~GatewayLink() = default;

GatewayLink::Route* GatewayLink::find(std::int32_t node) noexcept {
    const auto at = std::find_if(routes_.begin(), routes_.end(), [&](const Route& r) { return r.node == node; });
    return at == routes_.end() ? nullptr : &*at;
}

std::int32_t GatewayLink::next_after(std::int32_t node) const noexcept {
    for (std::size_t i = 0; i < routes_.size(); ++i)
        if (routes_[i].node == node) return routes_[(i + 1) % routes_.size()].node;
    return routes_.front().node;
}

bool GatewayLink::start(LinkRequest request, std::span<const std::byte> body, const ResponseCorrelation& correlation,
                        Tick now, Tick timeout) {
    std::copy(body.begin(), body.end(), body_.begin());
    body_bytes_ = body.size();
    pending_ = {};
    pending_.request = request;
    pending_.correlation = correlation;
    pending_.deadline = now + timeout;
    pending_.next_send = now;
    pending_.answered = true;  // the first send goes to the current target
    outcome_ = {};
    outcome_.request = request;
    outcome_.status = LinkStatus::pending;
    assembly_ = {};
    return true;
}

bool GatewayLink::submit(const Command& command, ProposeLane lane, Tick now, Tick timeout) {
    if (busy()) return false;
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    if (encoded.error != KernelCodecError::none) return false;
    std::array<std::byte, kMaxIngressBodyBytes> framed{};
    const auto ingress = encode_ingress(lane, std::span(bytes).first(encoded.bytes_written), framed);
    if (ingress.error != NodeProtocolError::none) return false;
    return start(LinkRequest::command, std::span(framed).first(ingress.bytes_written),
                 {command.identity.request_sequence.value(), command.identity.payload_digest}, now, timeout);
}

bool GatewayLink::request_state(Tick now, Tick timeout) {
    if (busy()) return false;
    GatewayRequest request{GatewayRequestKind::state_read, incarnation_, ++next_request_id_, {}};
    std::array<std::byte, kGatewayAuthorityRequestBytes> body{};
    const auto encoded = encode_gateway_request(request, body);
    if (encoded.error != NodeProtocolError::none) return false;
    const auto bytes = std::span<const std::byte>(body).first(encoded.bytes_written);
    return start(LinkRequest::state, bytes, gateway_correlation(bytes), now, timeout);
}

bool GatewayLink::request_authority(const AuthorityBarrier& barrier, Tick now, Tick timeout) {
    if (busy() || barrier.incarnation != incarnation_) return false;
    GatewayRequest request{GatewayRequestKind::authority, incarnation_, ++next_request_id_, barrier};
    std::array<std::byte, kGatewayAuthorityRequestBytes> body{};
    const auto encoded = encode_gateway_request(request, body);
    if (encoded.error != NodeProtocolError::none) return false;
    const auto bytes = std::span<const std::byte>(body).first(encoded.bytes_written);
    if (!start(LinkRequest::authority, bytes, gateway_correlation(bytes), now, timeout)) return false;
    pending_.barrier = barrier;
    return true;
}

void GatewayLink::abandon() noexcept {
    pending_ = {};
    outcome_ = {};
    assembly_ = {};
}

void GatewayLink::send(Tick now) {
    if (!pending_.answered) target_ = next_after(target_);  // unanswered: try the next voter
    auto* route = find(target_);
    const auto sequence = route->sequencer.next();
    if (!sequence) throw std::runtime_error("gateway session sequence exhausted");
    const auto sealed = encode_auth_envelope(route->ingress, session_epoch_, *sequence,
                                             std::span(body_).first(body_bytes_), envelope_);
    if (sealed.error != AuthEnvelopeError::none) throw std::runtime_error("gateway ingress envelope failed");
    (void)transport_.offer_ingress(target_, std::span(envelope_).first(sealed.bytes_written));
    const bool command = pending_.request == LinkRequest::command;
    pending_.next_send = now + (!command ? timing_.resend_gateway
                                         : pending_.admitted ? timing_.resend_after_admit : timing_.resend_before_admit);
    pending_.answered = command && pending_.admitted;
}

void GatewayLink::settle(LinkStatus status, std::int32_t from) {
    outcome_.status = status;
    outcome_.from = from;
}

void GatewayLink::redirect(std::int32_t leader, Tick now) {
    outcome_.leader_hint = leader;
    target_ = find(leader) != nullptr ? leader : next_after(target_);
    pending_.admitted = false;
    pending_.answered = true;
    pending_.next_send = now;  // follow the redirect at once
}

void GatewayLink::on_command(std::int32_t from, std::span<const std::byte> body, Tick now) {
    NodeResponse response;
    if (pending_.request != LinkRequest::command || decode_response(body, response) != NodeProtocolError::none ||
        response.correlation != pending_.correlation) {
        ++counters_.uncorrelated;
        return;
    }
    switch (response.kind) {
    case ResponseKind::committed: {
        ApplyResult result;
        if (decode_kernel_result(response.result, result).error != KernelCodecError::none)
            throw std::runtime_error("committed result is not canonical");
        outcome_.result = result;
        outcome_.index = response.index;
        outcome_.term = response.term;
        return settle(LinkStatus::done, from);
    }
    case ResponseKind::admitted:
        if (from != target_) return;
        pending_.admitted = true;
        pending_.answered = true;
        pending_.next_send = now + timing_.resend_after_admit;
        return;
    case ResponseKind::refused:
        pending_.answered = true;
        if (response.reason == static_cast<std::uint8_t>(ProposeStatus::not_leader))
            return redirect(response.leader_id, now);
        if (final_refusal(response.reason)) {
            outcome_.refusal_reason = response.reason;
            outcome_.leader_hint = response.leader_id;
            settle(LinkStatus::refused, from);
        }
        return;
    default:
        return;
    }
}

void GatewayLink::on_refusal(std::int32_t from, std::span<const std::byte> body, Tick now) {
    GatewayRefusal refusal;
    if (pending_.request == LinkRequest::none || pending_.request == LinkRequest::command ||
        decode_gateway_refusal(body, refusal) != NodeProtocolError::none ||
        refusal.correlation != pending_.correlation) {
        ++counters_.uncorrelated;
        return;
    }
    pending_.answered = true;
    if (refusal.reason == static_cast<std::uint8_t>(ProposeStatus::not_leader)) return redirect(refusal.leader_id, now);
    if (final_refusal(refusal.reason)) {
        outcome_.refusal_reason = refusal.reason;
        outcome_.leader_hint = refusal.leader_id;
        settle(LinkStatus::refused, from);
    }
}

void GatewayLink::on_chunk(std::int32_t from, std::span<const std::byte> body) {
    StateChunkView chunk;
    if (pending_.request != LinkRequest::state || decode_state_chunk(body, chunk) != NodeProtocolError::none ||
        chunk.header.correlation != pending_.correlation || chunk.header.responder_id != from) {
        ++counters_.uncorrelated;
        return;
    }
    pending_.answered = true;
    // Chunks of one snapshot share every header field but the index; a chunk of
    // a different snapshot (a resend answered after the state moved) restarts.
    const auto header = without_index(chunk.header);
    if (!assembly_.header || *assembly_.header != header || assembly_.from != from) assembly_ = {header, from, 0};
    const auto offset = static_cast<std::size_t>(chunk.header.chunk_index) * kStateChunkPayloadBytes;
    std::copy(chunk.chunk.begin(), chunk.chunk.end(), snapshot_.begin() + static_cast<std::ptrdiff_t>(offset));
    assembly_.received |= 1U << chunk.header.chunk_index;
    if (assembly_.received != (1U << header.chunk_count) - 1U) return;

    const auto bytes = std::span<const std::byte>(snapshot_).first(header.snapshot_bytes);
    const auto decoded = sha256_digest(bytes) == header.snapshot_digest
                             ? decode_state_snapshot(bytes, *state_, *scratch_)
                             : DecodeStateSnapshotResult{StateSnapshotError::digest_mismatch, {}};
    if (decoded.error != StateSnapshotError::none) {
        outcome_.refusal_reason = 0;
        return settle(LinkStatus::refused, from);
    }
    outcome_.reading = {header.responder_id, header.leader_id, header.term, header.applied_index,
                        header.authority_domain, decoded.state_digest};
    settle(LinkStatus::done, from);
}

void GatewayLink::on_grant(std::int32_t from, std::span<const std::byte> body) {
    ResponseCorrelation correlation;
    AuthorityProofView view;
    if (pending_.request != LinkRequest::authority ||
        decode_authority_granted(body, correlation, view) != NodeProtocolError::none ||
        correlation != pending_.correlation) {
        ++counters_.uncorrelated;
        return;
    }
    // Correlation already binds the exact request body; these restate the
    // parts of it the proof must carry so a mismatched grant never becomes one.
    if (view.request != pending_.barrier || view.request.incarnation != incarnation_ || view.issuer_id != from) {
        ++counters_.proofs_refused;
        return;
    }
    outcome_.proof.emplace(AuthorityProof(view));
    settle(LinkStatus::done, from);
}

void GatewayLink::handle(std::span<const std::byte> frame, Tick now) {
    AuthRoute route;
    Route* from = nullptr;
    if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::response ||
        (from = find(route.sender)) == nullptr) {
        ++counters_.unauthentic;
        return;
    }
    VerifiedAuthEnvelope verified;
    bool authentic = false;
    for (const auto& key : from->responses)
        authentic = authentic || verify_auth_envelope(key, frame, verified) == AuthEnvelopeError::none;
    if (!authentic) {
        ++counters_.unauthentic;
        return;
    }
    // Only an authentic frame may advance the voter's remembered session.
    if (from->fence.admit(verified.header.session_epoch, verified.header.sequence) != SessionFence::Decision::accept) {
        ++counters_.fenced;
        return;
    }
    switch (response_kind_byte(verified.body)) {
    case static_cast<std::uint8_t>(ResponseKind::gateway_refused): return on_refusal(route.sender, verified.body, now);
    case static_cast<std::uint8_t>(ResponseKind::state_chunk): return on_chunk(route.sender, verified.body);
    case static_cast<std::uint8_t>(ResponseKind::authority_granted): return on_grant(route.sender, verified.body);
    default: return on_command(route.sender, verified.body, now);
    }
}

LinkOutcome GatewayLink::pump(Tick now) {
    if (outcome_.status == LinkStatus::pending) {
        while (outcome_.status == LinkStatus::pending) {
            const auto size = transport_.receive_response(inbound_);
            if (!size) break;
            handle(std::span<const std::byte>(inbound_).first(*size), now);
        }
        if (outcome_.status == LinkStatus::pending && now >= pending_.deadline) outcome_.status = LinkStatus::timed_out;
        if (outcome_.status == LinkStatus::pending && now >= pending_.next_send) send(now);
    } else {
        // Idle: drain late answers so they cannot pile up behind the next request.
        while (const auto size = transport_.receive_response(inbound_))
            handle(std::span<const std::byte>(inbound_).first(*size), now);
    }
    if (outcome_.status == LinkStatus::pending || outcome_.status == LinkStatus::idle) return outcome_;
    auto reported = std::move(outcome_);
    pending_ = {};
    outcome_ = {};
    return reported;
}

}  // namespace velocita::v2
