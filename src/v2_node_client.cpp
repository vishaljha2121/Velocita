#include "velocita/v2/node_client.hpp"

#include "velocita/v2/kernel_codec.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace velocita::v2 {

namespace {

bool final_refusal(std::uint8_t reason) noexcept {
    if (reason >= 64)
        return reason != static_cast<std::uint8_t>(IngressRefusal::busy) &&
               reason != static_cast<std::uint8_t>(IngressRefusal::not_ready);
    return reason == static_cast<std::uint8_t>(ProposeStatus::refused) ||
           reason == static_cast<std::uint8_t>(ProposeStatus::not_a_control_command);
}

}

NodeClientSession::NodeClientSession(std::uint64_t session_epoch, std::vector<ClientRoute> routes,
                                     ClientTransport& transport, ClientTiming timing)
    : session_epoch_(session_epoch), transport_(transport), timing_(timing),
      target_(routes.empty() ? 0 : routes.front().node), body_(kMaxIngressBodyBytes),
      envelope_(kMaxAuthEnvelopeBytes), inbound_(kMaxAuthEnvelopeBytes) {
    if (routes.empty()) throw std::invalid_argument("a client session needs at least one voter");
    for (auto& route : routes)
        routes_.push_back(Route{route.node, std::move(route.ingress), std::move(route.responses),
                                SessionSequencer(session_epoch), SessionFence{}});
}

NodeClientSession::Route* NodeClientSession::find(std::int32_t node) noexcept {
    const auto at = std::find_if(routes_.begin(), routes_.end(), [&](const Route& r) { return r.node == node; });
    return at == routes_.end() ? nullptr : &*at;
}

std::int32_t NodeClientSession::next_after(std::int32_t node) const noexcept {
    for (std::size_t i = 0; i < routes_.size(); ++i)
        if (routes_[i].node == node) return routes_[(i + 1) % routes_.size()].node;
    return routes_.front().node;
}

bool NodeClientSession::submit(const Command& command, ProposeLane lane, Clock::time_point now,
                               Clock::duration timeout) {
    if (busy()) return false;
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    if (encoded.error != KernelCodecError::none) return false;
    const auto framed = encode_ingress(lane, std::span(bytes).first(encoded.bytes_written), body_);
    if (framed.error != NodeProtocolError::none) return false;
    body_bytes_ = framed.bytes_written;
    expected_ = {command.identity.request_sequence.value(), command.identity.payload_digest};
    outcome_ = {};
    outcome_.state = SubmitState::pending;
    deadline_ = now + timeout;
    next_send_ = now;
    admitted_ = false;
    rotate_before_next_ = false;
    return true;
}

void NodeClientSession::send(Clock::time_point now) {
    if (rotate_before_next_) target_ = next_after(target_);
    auto* route = find(target_);
    const auto sequence = route->sequencer.next();
    if (!sequence) throw std::runtime_error("client session sequence exhausted");
    const auto sealed = encode_auth_envelope(route->ingress, session_epoch_, *sequence,
                                             std::span(body_).first(body_bytes_), envelope_);
    if (sealed.error != AuthEnvelopeError::none) throw std::runtime_error("ingress envelope failed");
    if (transport_.offer_ingress(target_, std::span(envelope_).first(sealed.bytes_written))) {
        ++outcome_.sends;
        if (!outcome_.first_send) outcome_.first_send = now;
    }
    next_send_ = now + (admitted_ ? timing_.resend_after_admit : timing_.resend_before_admit);
    rotate_before_next_ = !admitted_;
}

void NodeClientSession::handle(std::span<const std::byte> frame, Clock::time_point now) {
    AuthRoute route;
    if (peek_auth_route(frame, route) != AuthEnvelopeError::none || route.role != AuthRole::response) return;
    auto* from = find(route.sender);
    if (from == nullptr) return;
    VerifiedAuthEnvelope verified;
    bool authentic = false;
    for (const auto& key : from->responses)
        authentic = authentic || verify_auth_envelope(key, frame, verified) == AuthEnvelopeError::none;
    if (!authentic || from->fence.admit(verified.header.session_epoch, verified.header.sequence) !=
                          SessionFence::Decision::accept)
        return;
    NodeResponse response;
    if (decode_response(verified.body, response) != NodeProtocolError::none || response.correlation != expected_)
        return;

    switch (response.kind) {
    case ResponseKind::committed: {
        ApplyResult result;
        if (decode_kernel_result(response.result, result).error != KernelCodecError::none)
            throw std::runtime_error("committed result is not canonical");
        outcome_.state = SubmitState::committed;
        outcome_.result = result;
        outcome_.index = static_cast<std::uint64_t>(response.index);
        outcome_.term = response.term;
        outcome_.from = route.sender;
        outcome_.committed = now;
        return;
    }
    case ResponseKind::admitted:
        if (route.sender != target_) return;
        admitted_ = true;
        rotate_before_next_ = false;
        if (!outcome_.admitted) outcome_.admitted = now;
        next_send_ = now + timing_.resend_after_admit;
        return;
    case ResponseKind::refused:
        if (response.reason == static_cast<std::uint8_t>(ProposeStatus::not_leader)) {
            outcome_.leader_hint = response.leader_id;
            target_ = find(response.leader_id) != nullptr ? response.leader_id : next_after(target_);
            admitted_ = false;
            rotate_before_next_ = false;
            next_send_ = now;
        } else if (final_refusal(response.reason)) {
            outcome_.state = SubmitState::refused;
            outcome_.refusal_reason = response.reason;
            outcome_.leader_hint = response.leader_id;
            outcome_.from = route.sender;
        }
        return;
    case ResponseKind::gateway_refused:
    case ResponseKind::state_chunk:
    case ResponseKind::authority_granted:
        return;
    }
}

SubmitOutcome NodeClientSession::pump(Clock::time_point now) {
    if (outcome_.state == SubmitState::pending) {
        while (outcome_.state == SubmitState::pending) {
            const auto size = transport_.receive_response(inbound_);
            if (!size) break;
            handle(std::span<const std::byte>(inbound_).first(*size), now);
        }
        if (outcome_.state == SubmitState::pending && now >= deadline_) outcome_.state = SubmitState::timed_out;
        if (outcome_.state == SubmitState::pending && now >= next_send_) send(now);
    }
    const auto reported = outcome_;
    if (reported.state != SubmitState::pending) outcome_ = {};
    return reported;
}

}
