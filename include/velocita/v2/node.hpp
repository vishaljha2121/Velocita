// One voter process: transport, authentication, the replicated kernel and result delivery.
#pragma once

#include "velocita/v2/auth_envelope.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/node_config.hpp"
#include "velocita/v2/node_protocol.hpp"
#include "velocita/v2/replication.hpp"
#include "velocita/v2/session_fence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace velocita::v2 {

class NodeTransport {
public:
    enum class Channel : std::uint8_t { peer, ingress };
    virtual ~NodeTransport() = default;
    [[nodiscard]] virtual std::optional<std::size_t> receive(Channel channel, std::span<std::byte> buffer) = 0;
    [[nodiscard]] virtual bool offer_peer(std::int32_t voter_id, std::span<const std::byte> envelope) = 0;
    [[nodiscard]] virtual bool offer_response(std::int32_t caller_key_id, std::span<const std::byte> envelope) = 0;
};

struct SecretSet {
    AuthSecret current;
    std::optional<AuthSecret> next{};
    std::optional<AuthSecret> previous{};
};

struct NodeSecrets {
    std::map<std::int32_t, SecretSet> voters;
    std::map<std::int32_t, SecretSet> callers;
};

struct NodeCounters {
    std::uint64_t peer_frames_fenced{};
    std::uint64_t peer_frames_accepted{};
    std::uint64_t peer_frame_errors{};
    std::uint64_t ingress_frames_accepted{};
    std::uint64_t auth_refusals{};
    std::uint64_t fence_refusals{};
    std::uint64_t peer_busy_holds{};
    std::uint64_t peer_backpressure{};
    std::uint64_t ingress_refusals{};
    std::uint64_t results_sent{};
    std::uint64_t results_dropped{};
    std::uint64_t responses_dropped{};
    std::uint64_t compactions{};
    std::uint64_t last_compaction_ms{};
    std::uint64_t gateway_state_reads{};
    std::uint64_t gateway_proofs_sent{};
    std::uint64_t gateway_refusals{};
    std::uint64_t ingress_rate_limited{};
};

inline constexpr int kGatewayAuthorityNodeBoundMs = 5000;

struct NodeStatus {
    ReplicaStatus replica{};
    bool ready{};
    std::uint64_t durable_index{};
    std::uint64_t held_peer_frames{};
    std::uint64_t session_epoch{};
    NodeCounters counters{};
};

class V2Node final {
public:
    V2Node(const NodeConfig& config, const VerifiedExecutionCatalog& catalog, NodeSecrets secrets,
           std::uint64_t session_epoch, NodeTransport& transport,
           MetadataCheckpoints metadata_checkpoints = {});
    ~V2Node();
    V2Node(const V2Node&) = delete;
    V2Node& operator=(const V2Node&) = delete;

    void turn(int elapsed_ms);

    [[nodiscard]] NodeStatus status() const;
    [[nodiscard]] bool replace_secrets(NodeSecrets secrets);

    void close_ingress() noexcept { ingress_open_ = false; }
    [[nodiscard]] bool quiescent() const noexcept;
    [[nodiscard]] std::int32_t node_id() const noexcept { return node_id_; }
    [[nodiscard]] const TradingState& applied_state() const noexcept { return kernel_->state(); }

private:
    struct PeerLink;
    struct CallerLink;

    void receive_peers();
    [[nodiscard]] bool deliver_peer(PeerLink& link, std::span<const std::byte> body);
    void send_peers();
    void receive_ingress();
    void send_results();
    void maybe_compact(int elapsed_ms);
    void answer(CallerLink& caller, const NodeResponse& response);
    [[nodiscard]] bool offer_body(CallerLink& caller, std::span<const std::byte> body);
    void handle_gateway(CallerLink& caller, std::span<const std::byte> body);
    void refuse_gateway(CallerLink& caller, const ResponseCorrelation& correlation, std::uint8_t reason);
    void serve_state(CallerLink& caller, const ResponseCorrelation& correlation);
    void service_gateway_authority(int elapsed_ms);
    [[nodiscard]] bool ready() const;
    void check_failed() const;
    struct KeySets;
    [[nodiscard]] std::optional<KeySets> derive_keys(const NodeSecrets& secrets) const;

    std::int32_t node_id_{};
    std::array<std::byte, 16> incarnation_{};
    std::uint64_t session_epoch_{};
    OwnerLoopBudgets budgets_{};
    std::uint32_t compact_at_entries_{};
    std::uint64_t ingress_rate_per_second_{};
    std::uint64_t ingress_burst_milli_{};
    int compact_wait_ms_{};
    std::uint64_t compaction_ms_{};
    NodeTransport& transport_;
    std::unique_ptr<ReplicatedKernel> kernel_;
    std::map<std::int32_t, std::unique_ptr<PeerLink>> peers_;
    std::map<std::int32_t, std::unique_ptr<CallerLink>> callers_;
    std::vector<std::int32_t> peer_ids_;
    std::map<std::int32_t, std::uint64_t> caller_ids_;
    std::map<std::uint64_t, std::int32_t> caller_by_id_;
    std::unique_ptr<KernelCommandCodecScratch> codec_scratch_;
    std::vector<std::byte> inbound_;
    std::vector<std::byte> outbound_;
    NodeCounters counters_{};
    std::optional<std::uint64_t> gateway_caller_;
    struct PendingGatewayAuthority {
        std::int32_t key_id{};
        AuthorityBarrier barrier{};
        ResponseCorrelation correlation{};
        int waited_ms{};
    };
    std::optional<PendingGatewayAuthority> gateway_authority_;
    std::vector<std::byte> snapshot_;
    std::vector<std::byte> gateway_body_;
    bool failed_{};
    bool ingress_open_{true};
};

}
