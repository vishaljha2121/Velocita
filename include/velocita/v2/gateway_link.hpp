// The gateway's session with the cluster: commands, state reads and authority requests.
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

struct GatewayLinkTiming {
    Tick resend_before_admit{500};
    Tick resend_after_admit{5000};
    Tick resend_gateway{500};
};

enum class LinkRequest : std::uint8_t { none, command, state, authority, swap_authority };
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
    ApplyResult result{};
    std::int64_t term{};
    std::int64_t index{};
    std::int32_t from{};
    std::uint8_t refusal_reason{};
    std::int32_t leader_hint{-1};
    StateReading reading{};
    std::optional<AuthorityProof> proof;
    std::optional<SwapAuthorityProof> swap_proof;
};

struct GatewayLinkCounters {
    std::uint64_t unauthentic{};
    std::uint64_t fenced{};
    std::uint64_t uncorrelated{};
    std::uint64_t proofs_refused{};
};

class GatewayLink {
public:
    GatewayLink(std::uint64_t session_epoch, std::array<std::byte, 16> process_incarnation,
                std::vector<ClientRoute> routes, ClientTransport& transport, GatewayLinkTiming timing = {});
    ~GatewayLink();
    GatewayLink(const GatewayLink&) = delete;
    GatewayLink& operator=(const GatewayLink&) = delete;

    [[nodiscard]] bool submit(const Command& command, ProposeLane lane, Tick now, Tick timeout);
    [[nodiscard]] bool request_state(Tick now, Tick timeout);
    [[nodiscard]] bool request_authority(const AuthorityBarrier& barrier, Tick now, Tick timeout);
    [[nodiscard]] bool request_swap_authority(const AuthorityBarrier& barrier, Tick now, Tick timeout);

    LinkOutcome pump(Tick now);
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
        bool answered{};
    };
    struct Assembly {
        std::optional<StateChunkHeader> header;
        std::int32_t from{};
        std::uint32_t received{};
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
    void on_swap_grant(std::int32_t from, std::span<const std::byte> body);
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

}
