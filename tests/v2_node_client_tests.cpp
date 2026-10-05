#include "velocita/v2/node_client.hpp"
#include "v2_node_test_support.hpp"

namespace {

using Clock = NodeClientSession::Clock;

struct SessionTransport final : ClientTransport {
    Network& network;
    explicit SessionTransport(Network& n) : network(n) {}
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        network.at(node).ingress_inbox.emplace_back(envelope.begin(), envelope.end());
        return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> buffer) override {
        if (network.responses.empty()) return std::nullopt;
        auto frame = std::move(network.responses.front());
        network.responses.pop_front();
        require(frame.size() <= buffer.size(), "response exceeds client buffer");
        std::copy(frame.begin(), frame.end(), buffer.begin());
        return frame.size();
    }
};

std::vector<ClientRoute> routes(const std::vector<std::int32_t>& order, std::uint8_t seed = kCallerSeed) {
    std::vector<ClientRoute> out;
    for (const auto node : order) {
        std::vector<AuthKey> responses;
        responses.push_back(key(seed, AuthRole::response, node, kCallerKey));
        out.push_back(ClientRoute{node, key(seed, AuthRole::ingress, kCallerKey, node), std::move(responses)});
    }
    return out;
}

SubmitOutcome settle(Cluster& cluster, NodeClientSession& session, Clock::time_point& now,
                     std::chrono::milliseconds step = std::chrono::milliseconds(1), int limit = 30000) {
    for (int round = 0; round < limit; ++round) {
        auto outcome = session.pump(now);
        if (outcome.state != SubmitState::pending) return outcome;
        cluster.run(1);
        now += step;
    }
    throw std::runtime_error("request never settled");
}

struct Fixture {
    Base base;
    Cluster cluster;
    SessionTransport transport;
    std::int32_t leader{};
    explicit Fixture(const char* leaf) : base(leaf), cluster(base.path), transport(cluster.network) {
        cluster.deliver_responses_to_caller = false;
        leader = cluster.elect();
    }
    std::int32_t follower() const { return leader == 1 ? 2 : 1; }
};

void commits_through_the_leader() {
    Fixture f("v2-client-commit");
    NodeClientSession session(5, routes({f.leader, f.follower()}), f.transport);
    auto now = Clock::now();
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(30)), "submit refused");
    require(!session.submit(authored(2), ProposeLane::normal, now, std::chrono::seconds(30)),
            "a second request was accepted while one was in flight");
    const auto outcome = settle(f.cluster, session, now);
    require(outcome.state == SubmitState::committed, "request did not commit");
    std::array<std::byte, kKernelResultBytes> bytes{};
    require(encode_kernel_result(outcome.result, bytes).error == KernelCodecError::none &&
                bytes == expected_result(1),
            "committed result differs from the independent reducer");
    require(outcome.sends == 1, "a request the leader admitted was sent more than once");
    require(outcome.first_send && outcome.admitted && outcome.committed &&
                *outcome.first_send <= *outcome.admitted && *outcome.admitted <= *outcome.committed,
            "timestamps are missing or out of order");
    require(!session.busy() && session.pump(now).state == SubmitState::idle,
            "a settled outcome was reported more than once");
}

void follows_a_redirect_and_then_sticks_to_the_leader() {
    Fixture f("v2-client-redirect");
    NodeClientSession session(5, routes({f.follower(), f.leader}), f.transport);
    auto now = Clock::now();
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(30)), "submit refused");
    const auto first = settle(f.cluster, session, now);
    require(first.state == SubmitState::committed, "redirected request did not commit");
    require(first.sends == 2, "expected one send to the follower and one to the leader");
    require(session.target() == f.leader, "session did not keep the leader for the next request");
    require(session.submit(authored(2), ProposeLane::normal, now, std::chrono::seconds(30)), "submit refused");
    const auto second = settle(f.cluster, session, now);
    require(second.state == SubmitState::committed && second.sends == 1,
            "the next request did not go straight to the leader");
    std::array<std::byte, kKernelResultBytes> bytes{};
    require(encode_kernel_result(second.result, bytes).error == KernelCodecError::none &&
                bytes == expected_result(2),
            "a response to an earlier request settled the next one");
}

void an_admitted_request_is_not_resent_early() {
    Fixture f("v2-client-admitted");
    f.cluster.network.partitioned = true;
    ClientTiming timing;
    NodeClientSession session(5, routes({f.leader, f.follower()}), f.transport, timing);
    auto now = Clock::now();
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(60)), "submit refused");
    SubmitOutcome outcome;
    for (int i = 0; i < 2000 && !(outcome = session.pump(now)).admitted; ++i) f.cluster.run(1);
    require(outcome.admitted.has_value(), "leader never admitted the request");
    now = *outcome.first_send + timing.resend_after_admit - std::chrono::milliseconds(1);
    outcome = session.pump(now);
    require(outcome.sends == 1, "an admitted request was resent before the post-admission interval");
    now += std::chrono::milliseconds(2);
    outcome = session.pump(now);
    require(outcome.sends == 2, "an admitted request was not resent after the post-admission interval");
    now += timing.resend_after_admit - std::chrono::milliseconds(2);
    outcome = session.pump(now);
    require(outcome.sends == 2, "a resend after admission fell back to the pre-admission pace");
}

struct RecordingTransport final : ClientTransport {
    SessionTransport& inner;
    std::vector<Bytes> seen;
    std::deque<Bytes> injected;
    explicit RecordingTransport(SessionTransport& t) : inner(t) {}
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        return inner.offer_ingress(node, envelope);
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> buffer) override {
        if (!injected.empty()) {
            auto frame = std::move(injected.front());
            injected.pop_front();
            std::copy(frame.begin(), frame.end(), buffer.begin());
            return frame.size();
        }
        const auto size = inner.receive_response(buffer);
        if (size) seen.emplace_back(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*size));
        return size;
    }
};

void a_permanent_propose_refusal_is_final() {
    Fixture f("v2-client-wrong-lane");
    NodeClientSession session(5, routes({f.leader, f.follower()}), f.transport);
    auto now = Clock::now();
    const velocita::v2::Command ordinary{{CallerId{kCallerId}, RequestSequence{1}, digest(1)},
                                         RecordSourceEvent{SourceId{1}, SourceGeneration{1}, SourceSequence{1}, digest(9)}};
    require(session.submit(ordinary, ProposeLane::control, now, std::chrono::seconds(30)), "submit refused");
    const auto outcome = settle(f.cluster, session, now);
    require(outcome.state == SubmitState::refused &&
                outcome.refusal_reason == static_cast<std::uint8_t>(ProposeStatus::not_a_control_command),
            "a permanent propose refusal was retried instead of reported");
    require(outcome.sends == 1, "a permanently refused request was resent");
}

void a_replayed_redirect_cannot_cause_a_duplicate_send() {
    Fixture f("v2-client-replay");
    RecordingTransport recording(f.transport);
    NodeClientSession session(5, routes({f.follower(), f.leader}), recording);
    auto now = Clock::now();
    f.cluster.network.partitioned = true;
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(60)), "submit refused");
    SubmitOutcome outcome;
    for (int i = 0; i < 4000 && !(outcome = session.pump(now)).admitted; ++i) f.cluster.run(1);
    require(outcome.admitted && outcome.sends == 2, "redirect and admission did not happen as scripted");
    Bytes refusal;
    for (const auto& frame : recording.seen) {
        AuthRoute route;
        if (peek_auth_route(frame, route) == AuthEnvelopeError::none && route.sender == f.follower()) refusal = frame;
    }
    require(!refusal.empty(), "no refusal from the follower was recorded");
    recording.injected.push_back(refusal);
    outcome = session.pump(now);
    require(outcome.sends == 2 && session.target() == f.leader,
            "a replayed redirect was believed: it re-targeted or resent an admitted request");
}

void unreachable_voters_time_out_with_paced_resends() {
    Fixture f("v2-client-timeout");
    for (const std::int32_t id : {1, 2}) f.cluster.stop(id);
    NodeClientSession session(5, routes({1, 2}), f.transport);
    auto now = Clock::now();
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(2)), "submit refused");
    SubmitOutcome outcome;
    for (int ms = 0; ms <= 2100 && (outcome = session.pump(now)).state == SubmitState::pending; ++ms)
        now += std::chrono::milliseconds(1);
    require(outcome.state == SubmitState::timed_out, "request did not time out");
    require(outcome.sends >= 4 && outcome.sends <= 5, "resends were not paced at the pre-admission interval");
    require(!f.cluster.network.at(1).ingress_inbox.empty() && !f.cluster.network.at(2).ingress_inbox.empty(),
            "unanswered resends did not rotate across voters");
}

void a_final_refusal_stops_the_request() {
    Fixture f("v2-client-refused");
    NodeClientSession session(5, routes({f.leader, f.follower()}), f.transport);
    auto now = Clock::now();
    require(session.submit(authored(1, 8), ProposeLane::normal, now, std::chrono::seconds(30)), "submit refused");
    const auto outcome = settle(f.cluster, session, now);
    require(outcome.state == SubmitState::refused &&
                outcome.refusal_reason == static_cast<std::uint8_t>(IngressRefusal::caller_mismatch),
            "impersonation was not reported as a final refusal");
    require(outcome.sends == 1, "a finally refused request was resent");
}

void responses_under_a_foreign_key_are_ignored() {
    Fixture f("v2-client-foreign");
    auto wrong = routes({f.leader, f.follower()});
    for (auto& route : wrong) {
        route.responses.clear();
        route.responses.push_back(key(kWrongSeed, AuthRole::response, route.node, kCallerKey));
    }
    NodeClientSession session(5, std::move(wrong), f.transport);
    auto now = Clock::now();
    require(session.submit(authored(1), ProposeLane::normal, now, std::chrono::seconds(1)), "submit refused");
    SubmitOutcome outcome;
    for (int ms = 0; ms <= 1100 && (outcome = session.pump(now)).state == SubmitState::pending; ++ms) {
        f.cluster.run(1);
        now += std::chrono::milliseconds(1);
    }
    require(outcome.state == SubmitState::timed_out && !outcome.admitted,
            "a response the session cannot authenticate was believed");
}

}

int main() {
    try {
        commits_through_the_leader();
        follows_a_redirect_and_then_sticks_to_the_leader();
        an_admitted_request_is_not_resent_early();
        unreachable_voters_time_out_with_paced_resends();
        a_final_refusal_stops_the_request();
        a_permanent_propose_refusal_is_final();
        a_replayed_redirect_cannot_cause_a_duplicate_send();
        responses_under_a_foreign_key_are_ignored();
        std::cout << "node client tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node client tests failed: " << error.what() << '\n';
        return 1;
    }
}
