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

// What a caller needs from the network: offer an ingress envelope to a voter,
// and receive response envelopes addressed to it. Aeron in the binaries, an
// in-process fake in tests.
class ClientTransport {
public:
    virtual ~ClientTransport() = default;
    virtual bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) = 0;
    virtual std::optional<std::size_t> receive_response(std::span<std::byte> buffer) = 0;
};

// One voter as the caller sees it: the key that signs ingress to it, and the
// keys its responses may verify under (current first, then rotation overlap).
struct ClientRoute {
    std::int32_t node{};
    AuthKey ingress;
    std::vector<AuthKey> responses;
};

struct ClientTiming {
    // Before admission a resend recovers a lost packet or a redirect. After the
    // leader ADMITS the request, a resend only appends a duplicate log entry,
    // so it waits long enough to cover a leader change instead.
    std::chrono::milliseconds resend_before_admit{500};
    std::chrono::milliseconds resend_after_admit{5000};
};

enum class SubmitState : std::uint8_t { idle, pending, committed, refused, timed_out };

struct SubmitOutcome {
    using TimePoint = std::chrono::steady_clock::time_point;
    SubmitState state{SubmitState::idle};
    ApplyResult result{};                // committed only
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t from{};                 // voter whose response settled the request
    std::uint8_t refusal_reason{};
    std::int32_t leader_hint{-1};
    std::uint32_t sends{};               // ingress envelopes offered for this request
    std::optional<TimePoint> first_send, admitted, committed;
};

// The authenticated caller side of the node protocol, as a non-blocking state
// machine with an injected clock: one request in flight, redirects followed,
// resends paced by admission, responses verified and session-fenced per voter,
// and the voter that admitted a request kept as the first target of the next.
class NodeClientSession {
public:
    using Clock = std::chrono::steady_clock;

    // ROUTES is the initial preference order. SESSION_EPOCH must be fresh for
    // every process start (the caller's session-epoch file), or voters will
    // refuse the new session's frames as replays.
    NodeClientSession(std::uint64_t session_epoch, std::vector<ClientRoute> routes, ClientTransport& transport,
                      ClientTiming timing = {});

    // Starts one request. False when a request is already in flight or the
    // command does not encode; nothing is sent in either case.
    [[nodiscard]] bool submit(const Command& command, ProposeLane lane, Clock::time_point now,
                              Clock::duration timeout);

    // Sends whatever is due, drains responses, and reports the current outcome.
    // A settled outcome (committed, refused, timed_out) is reported once; the
    // session is then idle and ready for the next submit.
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
    bool rotate_before_next_{};  // the last send went unanswered: try the next voter
    SubmitOutcome outcome_{};
    std::vector<std::byte> envelope_;
    std::vector<std::byte> inbound_;
};

}  // namespace velocita::v2
