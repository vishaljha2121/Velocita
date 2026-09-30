#pragma once

#include "velocita/v2/authority_proof.hpp"
#include "velocita/v2/gateway.hpp"
#include "velocita/v2/node_client.hpp"
#include "velocita/v2/node_protocol.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace velocita::v2 {

// The gateway caller's whole session with the cluster: kernel commands, state
// reads and authority requests share one caller key, so they must share one
// ingress sequencer and one response fence per voter. (A NodeClientSession
// beside this object would issue colliding sequences and the node's replay
// fence would refuse one of them.) Command behaviour mirrors NodeClientSession.
// Non-blocking, one request in flight, time injected as Tick milliseconds.
struct GatewayLinkTiming {
    Tick resend_before_admit{500};
    Tick resend_after_admit{5000};
    Tick resend_gateway{500};  // state read / authority request left unanswered
};

enum class LinkRequest : std::uint8_t { none, command, state, authority };
enum class LinkStatus : std::uint8_t { idle, pending, done, refused, timed_out };

struct StateReading {
    std::int32_t responder_id{};
    std::int32_t leader_id{};
    std::int64_t term{};
    std::int64_t applied_index{};
    Digest256 authority_domain{};
    Digest256 state_digest{};
};

struct LinkOutcome {
    LinkRequest request{LinkRequest::none};
    LinkStatus status{LinkStatus::idle};
    ApplyResult result{};          // command, done
    std::int64_t term{};           // command, done
    std::int64_t index{};          // command, done
    std::int32_t from{};           // voter whose response settled the request
    std::uint8_t refusal_reason{}; // refused; 0 = corrupt state from an authentic voter
    std::int32_t leader_hint{-1};
    StateReading reading{};        // state, done; the state itself is GatewayLink::state()
    std::optional<AuthorityProof> proof;  // authority, done
};

struct GatewayLinkCounters {
    std::uint64_t unauthentic{};   // no route, wrong role, or MAC refused
    std::uint64_t fenced{};        // authentic but replayed or stale epoch
    std::uint64_t uncorrelated{};  // authentic, fenced, but answers nothing pending
    std::uint64_t proofs_refused{};// correlated grant whose content does not match the request
};

class GatewayLink {
public:
    GatewayLink(std::uint64_t session_epoch, std::array<std::byte, 16> process_incarnation,
                std::vector<ClientRoute> routes, ClientTransport& transport, GatewayLinkTiming timing = {});
    ~GatewayLink();
    GatewayLink(const GatewayLink&) = delete;
    GatewayLink& operator=(const GatewayLink&) = delete;

    // Each starts one request; false (nothing sent) when busy or unencodable.
    [[nodiscard]] bool submit(const Command& command, ProposeLane lane, Tick now, Tick timeout);
    [[nodiscard]] bool request_state(Tick now, Tick timeout);
    [[nodiscard]] bool request_authority(const AuthorityBarrier& barrier, Tick now, Tick timeout);

    // Sends what is due, drains responses, reports the outcome. A settled
    // outcome is reported once; the link is then idle.
    LinkOutcome pump(Tick now);
    // Forget the in-flight request; its late answers become uncorrelated.
    void abandon() noexcept;

    [[nodiscard]] bool busy() const noexcept { return pending_.request != LinkRequest::none; }
    [[nodiscard]] std::int32_t target() const noexcept { return target_; }
    [[nodiscard]] const TradingState& state() const noexcept { return *state_; }
    [[nodiscard]] const GatewayLinkCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] const std::array<std::byte, 16>& incarnation() const noexcept { return incarnation_; }

private:
    struct Route {
        std::int32_t node{};
        AuthKey ingress;
        std::vector<AuthKey> responses;
        SessionSequencer sequencer;
        SessionFence fence{};
    };
    struct Pending {
        LinkRequest request{LinkRequest::none};
        ResponseCorrelation correlation{};
        AuthorityBarrier barrier{};
        Tick deadline{};
        Tick next_send{};
        bool admitted{};
        bool answered{};  // any correlated response since the last send
    };
    struct Assembly {
        std::optional<StateChunkHeader> header;  // chunk_index zeroed
        std::int32_t from{};
        std::uint32_t received{};  // bitmask of chunk indices
    };

    [[nodiscard]] bool start(LinkRequest request, std::span<const std::byte> body,
                             const ResponseCorrelation& correlation, Tick now, Tick timeout);
    Route* find(std::int32_t node) noexcept;
    [[nodiscard]] std::int32_t next_after(std::int32_t node) const noexcept;
    void send(Tick now);
    void handle(std::span<const std::byte> frame, Tick now);
    void on_command(std::int32_t from, std::span<const std::byte> body, Tick now);
    void on_refusal(std::int32_t from, std::span<const std::byte> body, Tick now);
    void on_chunk(std::int32_t from, std::span<const std::byte> body);
    void on_grant(std::int32_t from, std::span<const std::byte> body);
    void redirect(std::int32_t leader, Tick now);
    void settle(LinkStatus status, std::int32_t from);

    std::uint64_t session_epoch_;
    std::array<std::byte, 16> incarnation_;
    std::vector<Route> routes_;
    ClientTransport& transport_;
    GatewayLinkTiming timing_;
    std::int32_t target_;
    std::uint64_t next_request_id_{};
    std::vector<std::byte> body_;
    std::size_t body_bytes_{};
    Pending pending_{};
    LinkOutcome outcome_{};
    Assembly assembly_{};
    std::vector<std::byte> snapshot_;
    std::unique_ptr<TradingState> state_;
    std::unique_ptr<StateSnapshotScratch> scratch_;
    std::vector<std::byte> envelope_;
    std::vector<std::byte> inbound_;
    GatewayLinkCounters counters_{};
};

}  // namespace velocita::v2
