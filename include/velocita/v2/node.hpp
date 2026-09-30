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

// Owner-thread transport seam (M2 decision B). Every call is nonblocking. A
// false offer is backpressure, never an error; the node keeps the frame.
class NodeTransport {
public:
    enum class Channel : std::uint8_t { peer, ingress };
    virtual ~NodeTransport() = default;
    // Copies at most one whole frame into BUFFER; nullopt when none is waiting.
    // A frame larger than BUFFER is discarded by the transport and counted there.
    [[nodiscard]] virtual std::optional<std::size_t> receive(Channel channel, std::span<std::byte> buffer) = 0;
    [[nodiscard]] virtual bool offer_peer(std::int32_t voter_id, std::span<const std::byte> envelope) = 0;
    [[nodiscard]] virtual bool offer_response(std::int32_t caller_key_id, std::span<const std::byte> envelope) = 0;
};

// One route's secrets during rotation (M2_SECRETS_ROTATION_REVIEW.md): frames
// are SENT under `current` and ACCEPTED under current, next or previous.
struct SecretSet {
    AuthSecret current;
    std::optional<AuthSecret> next{};
    std::optional<AuthSecret> previous{};
};

// Per-link and per-caller secrets, already loaded from their 0600 files.
struct NodeSecrets {
    std::map<std::int32_t, SecretSet> voters;   // peer voter id -> link secrets
    std::map<std::int32_t, SecretSet> callers;  // caller key id -> caller secrets
};

// Bounded diagnostic counters; the M3a metrics page exports these.
struct NodeCounters {
    // Conservation: peer_frames_fenced == peer_frames_accepted +
    // peer_frame_errors + NodeStatus::held_peer_frames. No authenticated frame
    // is lost because the kernel was busy.
    std::uint64_t peer_frames_fenced{};
    std::uint64_t peer_frames_accepted{};
    std::uint64_t peer_frame_errors{};      // authenticated but refused by the bound-frame decoder
    std::uint64_t ingress_frames_accepted{};
    std::uint64_t auth_refusals{};          // structure, route or tag
    std::uint64_t fence_refusals{};         // stale epoch or replayed sequence
    std::uint64_t peer_busy_holds{};        // kernel busy; body held and retried
    std::uint64_t peer_backpressure{};      // offer refused; frame stashed
    std::uint64_t ingress_refusals{};       // any refused response
    std::uint64_t results_sent{};
    std::uint64_t results_dropped{};        // no configured caller or offer refused
    std::uint64_t responses_dropped{};      // admitted/refused responses the transport refused
    std::uint64_t compactions{};            // completed floor-gated compactions
    std::uint64_t last_compaction_ms{};     // owner-loop time the last one took (Raft paused)
    std::uint64_t gateway_state_reads{};    // snapshots served to the gateway caller
    std::uint64_t gateway_proofs_sent{};    // authority proofs answered
    std::uint64_t gateway_refusals{};       // gateway requests refused (any reason)
};

// How long an admitted gateway barrier may wait for its proof before the node
// abandons it and answers authority_lost. Bounds the single pending slot only;
// send freshness is the gateway's own request-start bound.
inline constexpr int kGatewayAuthorityNodeBoundMs = 5000;

struct NodeStatus {
    ReplicaStatus replica{};
    bool ready{};
    std::uint64_t durable_index{};
    std::uint64_t held_peer_frames{};
    std::uint64_t session_epoch{};
    NodeCounters counters{};
};

// The M2 node: owns one ReplicatedKernel and drives it from a single owner
// thread. Only the thread calling turn() may touch this object. The WAL worker
// is started at construction; there is no owner-thread persistence fallback.
// Any kernel failure latches: turn() rethrows it forever and the process must
// exit (fail-stop). The node never tries to continue on ambiguous storage.
class V2Node final {
public:
    // METADATA_CHECKPOINTS is the kernel's test-only seam, defaulted off and
    // carrying no behaviour: it lets a test park the WAL worker mid-save.
    V2Node(const NodeConfig& config, const VerifiedExecutionCatalog& catalog, NodeSecrets secrets,
           std::uint64_t session_epoch, NodeTransport& transport,
           MetadataCheckpoints metadata_checkpoints = {});
    ~V2Node();
    V2Node(const V2Node&) = delete;
    V2Node& operator=(const V2Node&) = delete;

    // One bounded owner-loop turn: peer ingress, durability pump, Raft tick,
    // peer egress, caller ingress, committed results. ELAPSED_MS feeds Raft.
    void turn(int elapsed_ms);

    [[nodiscard]] NodeStatus status() const;
    // Rotation reload. All-or-nothing: every configured peer and caller must be
    // present, or nothing changes and false is returned. Sessions, fences,
    // sequencers and held frames are untouched; only keys are replaced.
    [[nodiscard]] bool replace_secrets(NodeSecrets secrets);

    // Shutdown: stop reading caller ingress; peers and durability keep running.
    void close_ingress() noexcept { ingress_open_ = false; }
    // True when no persistence or compaction work is outstanding.
    [[nodiscard]] bool quiescent() const noexcept;
    [[nodiscard]] std::int32_t node_id() const noexcept { return node_id_; }
    // Diagnostic applied-state view for in-process tests; grants nothing.
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
    int compact_wait_ms_{};       // since the last attempt to start one
    std::uint64_t compaction_ms_{}; // owner-loop time the one in flight has taken
    NodeTransport& transport_;
    std::unique_ptr<ReplicatedKernel> kernel_;
    std::map<std::int32_t, std::unique_ptr<PeerLink>> peers_;
    std::map<std::int32_t, std::unique_ptr<CallerLink>> callers_;
    std::vector<std::int32_t> peer_ids_;
    std::map<std::int32_t, std::uint64_t> caller_ids_;    // caller key id -> CallerId
    std::map<std::uint64_t, std::int32_t> caller_by_id_;  // CallerId -> caller key id
    std::unique_ptr<KernelCommandCodecScratch> codec_scratch_;
    std::vector<std::byte> inbound_;    // one frame, kMaxAuthEnvelopeBytes
    std::vector<std::byte> outbound_;   // one frame, kMaxAuthEnvelopeBytes
    NodeCounters counters_{};
    std::optional<std::uint64_t> gateway_caller_;
    struct PendingGatewayAuthority {
        std::int32_t key_id{};
        AuthorityBarrier barrier{};
        ResponseCorrelation correlation{};
        int waited_ms{};
    };
    std::optional<PendingGatewayAuthority> gateway_authority_;
    std::vector<std::byte> snapshot_;   // one applied-state snapshot, kMaxStateSnapshotBytes
    std::vector<std::byte> gateway_body_;  // one gateway response body
    bool failed_{};
    bool ingress_open_{true};
};

}  // namespace velocita::v2
