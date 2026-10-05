// Client side of the node protocol: submit a command and wait for its result.
#pragma once

#include "velocita/v2/auth_envelope.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/node_protocol.hpp"
#include "velocita/v2/replication.hpp"
#include "velocita/v2/session_fence.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace velocita::v2 {

class ClientTransport {
public:
    virtual ~ClientTransport() = default;
    virtual bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) = 0;
    virtual std::optional<std::size_t> receive_response(std::span<std::byte> buffer) = 0;
};

struct ClientRoute {
    std::int32_t node{};
    AuthKey ingress;
    std::vector<AuthKey> responses;
};

struct ClientTiming {
    std::chrono::milliseconds resend_before_admit{500};
    std::chrono::milliseconds resend_after_admit{5000};
};

enum class SubmitState : std::uint8_t { idle, pending, committed, refused, timed_out };

struct SubmitOutcome {
    using TimePoint = std::chrono::steady_clock::time_point;
    SubmitState state{SubmitState::idle};
    ApplyResult result{};
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t from{};
    std::uint8_t refusal_reason{};
    std::int32_t leader_hint{-1};
    std::uint32_t sends{};
    std::optional<TimePoint> first_send, admitted, committed;
};

class NodeClientSession {
public:
    using Clock = std::chrono::steady_clock;

    NodeClientSession(std::uint64_t session_epoch, std::vector<ClientRoute> routes, ClientTransport& transport,
                      ClientTiming timing = {});

    [[nodiscard]] bool submit(const Command& command, ProposeLane lane, Clock::time_point now,
                              Clock::duration timeout);

    SubmitOutcome pump(Clock::time_point now);

    [[nodiscard]] bool busy() const noexcept { return outcome_.state == SubmitState::pending; }
    [[nodiscard]] std::int32_t target() const noexcept { return target_; }

private:
    struct Route {
        std::int32_t node{};
        AuthKey ingress;
        std::vector<AuthKey> responses;
        SessionSequencer sequencer;
        SessionFence fence{};
    };

    Route* find(std::int32_t node) noexcept;
    std::int32_t next_after(std::int32_t node) const noexcept;
    void send(Clock::time_point now);
    void handle(std::span<const std::byte> frame, Clock::time_point now);

    std::uint64_t session_epoch_;
    std::vector<Route> routes_;
    ClientTransport& transport_;
    ClientTiming timing_;
    std::int32_t target_;
    std::vector<std::byte> body_;
    std::size_t body_bytes_{};
    ResponseCorrelation expected_{};
    Clock::time_point deadline_{};
    Clock::time_point next_send_{};
    bool admitted_{};
    bool rotate_before_next_{};
    SubmitOutcome outcome_{};
    std::vector<std::byte> envelope_;
    std::vector<std::byte> inbound_;
};

}
