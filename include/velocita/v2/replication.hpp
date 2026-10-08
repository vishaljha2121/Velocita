// The kernel behind Raft: proposal, durability, apply, compaction and authority proofs.
#pragma once

#include "velocita/storage.hpp"
#include "velocita/wire.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/committed_result.hpp"
#include "velocita/v2/result_ledger.hpp"
#include "velocita/v2/retained_log.hpp"
#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/bound_peer.hpp"
#include "velocita/v2/outbound_mailbox.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/authority_proof.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/snapshot_store.hpp"
#include "velocita/v2/wal_arena.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/storage_checkpoint.hpp"
#include "velocita/v2/archive_store.hpp"
#include "velocita/v2/archive_destination.hpp"
#include "velocita/v2/follower_ack.hpp"

extern "C" {
#include "raft.h"
}

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocita::v2 {

struct PersistenceFenceTestAccess;

struct HistoryRecoveryConfig {
    std::uint64_t maximum_wal_bytes{1ULL << 30};
};

enum class ProposeStatus : std::uint8_t {
    admitted = 1,
    not_leader,
    refused,
    normal_lane_exhausted,
    not_a_control_command,
    authority_busy,
    result_capacity_exhausted,
    log_capacity_exhausted,
    persistence_busy,
};

enum class ProposeLane : std::uint8_t {
    normal = 0,
    control,
};

enum class PeerReceiveStatus : std::uint8_t {
    processed = 0,
    accepted_deferred,
    busy,
};

struct PeerReceiveResult {
    PeerReceiveStatus status{PeerReceiveStatus::processed};
    BoundFrameError frame_error{BoundFrameError::none};

    friend bool operator==(const PeerReceiveResult& result, BoundFrameError error) noexcept {
        return result.status != PeerReceiveStatus::busy && result.frame_error == error;
    }
    friend bool operator==(BoundFrameError error, const PeerReceiveResult& result) noexcept {
        return result == error;
    }
    friend bool operator!=(const PeerReceiveResult& result, BoundFrameError error) noexcept {
        return !(result == error);
    }
};

inline constexpr std::size_t kControlLaneReservedBlocks = 4;

struct ProposeOutcome {
    ProposeStatus status{ProposeStatus::refused};
    std::int32_t leader_id{-1};
    std::int64_t term{};
    std::int64_t index{};
};

struct AccountLedger {
    std::uint64_t account_id{};
    std::uint64_t instrument_id{};
    std::int64_t position_lots{};
    std::int64_t cash_micros{};
    std::int64_t cost_basis_micros{};
    std::int64_t realized_pnl_micros{};
    std::int64_t fees_paid_micros{};
    std::int64_t window_realized_pnl_micros{};
    std::int64_t max_window_loss_micros{};
    constexpr auto operator<=>(const AccountLedger&) const noexcept = default;
};

struct ReplicaStatus {
    std::int32_t node_id{};
    std::int32_t leader_id{-1};
    std::string_view role;
    std::int64_t term{};
    std::int64_t current_index{};
    std::int64_t commit_index{};
    std::int64_t applied_index{};
    std::uint64_t state_version{};
    std::size_t outbox_effect_count{};
    Digest256 state_digest{};
    std::uint64_t retained_ordinary{};
    std::uint64_t retained_protected{};
    std::uint32_t stranded_voters{};
    std::int64_t compaction_floor{};
    std::int64_t snapshot_index{};
    std::uint64_t receipts{};
    std::uint64_t pinned_receipts{};
    std::uint64_t pinned_receipts_peak{};
    std::uint64_t archive_bytes{};
    std::uint64_t archive_byte_limit{};
    std::uint64_t archive_objects{};
    std::uint64_t archive_object_limit{};
    std::uint64_t policies{};
    std::uint64_t quorum_step_downs{};
    std::uint64_t ledger_count{};
    std::array<AccountLedger, kMaxRiskAccounts> ledgers{};
};

struct CommittedOutboxView {
    bool authoritative{};
    std::int32_t leader_id{-1};
    std::int64_t term{};
    std::int64_t applied_index{};
    std::size_t effect_count{};
    std::array<OutboxEffectRecord, kMaxOutboxEffects> effects{};
};

class ReplicatedKernel {
public:
    explicit ReplicatedKernel(VerifiedIdentityStartup startup,
                              StorageCheckpoints checkpoints = {},
                              MetadataCheckpoints metadata_checkpoints = {},
                              ArchiveStoreConfig archive_config = {},
                              HistoryRecoveryConfig history_config = {},
                              Digest256 archive_destination_identity = {});
    ~ReplicatedKernel();
    ReplicatedKernel(const ReplicatedKernel&) = delete;
    ReplicatedKernel& operator=(const ReplicatedKernel&) = delete;

    void tick(int elapsed_ms);

    void flush_durability();

    void start_writer();
    void set_quorum_silence_limit(std::int64_t milliseconds) noexcept { quorum_silence_limit_ms_ = milliseconds; }
    void stop_writer();

    [[nodiscard]] WalMetrics wal_metrics() const;

    [[nodiscard]] bool is_leader() const;

    [[nodiscard]] ProposeOutcome propose(std::span<const std::byte> command_bytes,
                                         ProposeLane lane = ProposeLane::normal);

    [[nodiscard]] std::optional<CommittedResult> take_committed_result();
    [[nodiscard]] std::uint64_t result_capacity_refusals() const noexcept {
        return result_capacity_refusals_;
    }
    [[nodiscard]] std::size_t reserved_result_count() const noexcept {
        return results_->reserved_count();
    }
    [[nodiscard]] std::size_t ready_result_count() const noexcept {
        return results_->ready_count();
    }
    [[nodiscard]] std::uint64_t log_capacity_refusals() const noexcept { return log_capacity_refusals_; }
    [[nodiscard]] std::size_t retained_log_count() const noexcept { return log_payloads_->size(); }
    [[nodiscard]] std::size_t retained_log_bytes() const noexcept { return log_payloads_->live_bytes(); }

    [[nodiscard]] ProposeOutcome propose_authority_barrier(const AuthorityBarrier& request);
    [[nodiscard]] std::optional<AuthorityProof> take_authority_proof();
    void abandon_authority_request(const AuthorityBarrier& request);
    [[nodiscard]] std::int64_t authority_drain_index() const;

    [[nodiscard]] PeerReceiveResult receive_peer(std::span<const std::byte> bound_frame);
    [[nodiscard]] OutboundTakeResult take_outbound_frame(
        std::span<std::byte> output, std::span<const std::int32_t> held_peers = {});
    [[nodiscard]] std::size_t outbound_message_count() const noexcept { return outbound_->size(); }
    [[nodiscard]] const OutboundMailboxCounters& outbound_counters() const noexcept {
        return outbound_->counters();
    }

    [[nodiscard]] ReplicaStatus status() const;
    [[nodiscard]] const VerifiedStaticIdentity& static_identity() const noexcept {
        return startup_.identity();
    }

    [[nodiscard]] std::uint64_t durable_index() const noexcept {
        return arena_.durable_index();
    }

    [[nodiscard]] CommittedOutboxView committed_outbox() const;

    std::uint64_t compact();
    std::uint64_t compact_within_floor();
    [[nodiscard]] bool compaction_pending() const noexcept { return !!compaction_; }
    [[nodiscard]] bool persistence_pending() const noexcept;
    [[nodiscard]] std::optional<ArchiveDestinationRequest> archive_verification_request() const noexcept;
    [[nodiscard]] bool accept_archive_verification(ArchiveDestinationEvidence&& evidence);
    [[nodiscard]] bool history_recovery_pending() const noexcept {
        return history_recovery_cut_ != 0;
    }

    [[nodiscard]] const TradingState& state() const noexcept { return state_; }

private:
    friend struct PersistenceFenceTestAccess;
    static int send_request_vote(raft_server_t*, void*, raft_node_t*, msg_requestvote_t*);
    static int send_append_entries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*);
    static int apply_log(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int before_apply(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int persist_vote(raft_server_t*, void*, raft_node_id_t);
    static int persist_term(raft_server_t*, void*, raft_term_t, raft_node_id_t);
    static int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_poll(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_prepare_truncate(raft_server_t*, void*, raft_index_t);
    static int log_pop(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_clear(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static raft_index_t durable_idx(raft_server_t*, void*);

    void persist_commit_index();
    void require_usable() const;
    int queue_outbound(const PeerMessageView& message) noexcept;
    void send_early_ack();
    [[nodiscard]] std::int64_t compaction_floor() const noexcept;
    struct AdmissionCapacity { bool results{true}; bool log{true}; };
    [[nodiscard]] AdmissionCapacity admission_capacity(const AppendEntries& append) const;
    [[nodiscard]] bool release_log_results(std::uint64_t from) noexcept;
    [[nodiscard]] bool erase_log_suffix(std::uint64_t from) noexcept;
    struct CompactionJob;
    struct PersistenceJob;
    struct HistoryRecoveryJob;
    static void persist_history_recovery(void*);
    void pump_history_recovery();
    [[nodiscard]] bool accept_history_recovery_completion();
    static void persist_compaction(void*);
    static void persist_hard_state(void*);
    static void persist_truncate_suffix(void*);
    void stage_hard_state(const PersistentMetadata& desired);
    void submit_hard_state();
    void submit_truncate_suffix();
    void collect_persistence();
    [[nodiscard]] bool accept_persistence_completion();
    void open_archive();
    void note_archive_usage() noexcept;
    void step_down_without_quorum(int elapsed_ms) noexcept;
    [[nodiscard]] bool archive_admission(std::uint64_t resulting_last_index,
                                         bool protected_lane) const noexcept;

    VerifiedIdentityStartup startup_;
    std::int32_t node_id_;
    std::vector<std::int32_t> members_;
    const VerifiedExecutionCatalog& catalog_;
    const Digest256 authority_domain_;
    struct PendingAuthorityBarrier {
        AuthorityBarrier request;
        std::int64_t term;
        std::int64_t index;
    };
    std::optional<PendingAuthorityBarrier> pending_authority_barrier_;
    std::optional<AuthorityProof> authority_proof_;
    static_assert(sizeof(std::optional<PendingAuthorityBarrier>) <= 192);
    static_assert(sizeof(std::optional<AuthorityProof>) <= 512);
    [[nodiscard]] bool authority_proof_is_current(const AuthorityProofView& proof) const noexcept;
    void revoke_stale_authority();
    void validate_log_payload(std::int32_t type, std::span<const std::byte> bytes) const;
    bool catalog_dependency_failed_{};
    bool storage_failed_{};
    bool result_ownership_failed_{};
    bool outbound_failed_{};
    bool log_ownership_failed_{};
    BoundMetadataStore metadata_store_;
    StorageCheckpoints storage_checkpoints_;
    WalBlockArena arena_;
    std::unique_ptr<WalSegment> segment_;
    std::unique_ptr<WalWriter> writer_;
    SnapshotStore snapshots_;
    PersistentMetadata metadata_;
    PersistentMetadata desired_metadata_;
    FollowerAckTracker follower_ack_;
    std::int64_t known_floor_{};
    std::uint64_t polled_through_{};
    std::uint64_t pinned_receipts_peak_{};
    std::array<std::int64_t, kMaxStaticVoters> peer_silence_ms_{};
    std::int64_t quorum_silence_limit_ms_{};
    std::uint64_t quorum_step_downs_{};
    std::uint64_t archive_bytes_seen_{};
    std::uint64_t archive_objects_seen_{};
    std::uint64_t blocks_submitted_{};
    std::uint64_t backpressure_refusals_{};
    std::uint64_t highest_staged_index_{};
    bool writer_running_{false};
    std::unique_ptr<PersistenceJob> persistence_;
    std::uint64_t next_persistence_sequence_{1};
    std::optional<std::uint64_t> truncate_requested_;
    std::optional<std::uint64_t> truncate_grant_;
    std::array<std::byte, kMaxBoundPeerBytes> retry_frame_{};
    std::size_t retry_frame_size_{};

    TradingState state_{};
    std::unique_ptr<ApplyScratch> scratch_;
    std::unique_ptr<KernelCommandCodecScratch> codec_scratch_;
    std::unique_ptr<BoundFrameScratch> peer_scratch_;
    std::unique_ptr<ResultLedger> results_;
    std::unique_ptr<OutboundMailboxes> outbound_;
    std::unique_ptr<RetainedLog> log_payloads_;
    ArchiveStore archive_;
    ArchiveStoreConfig archive_config_;
    bool archive_open_{};
    std::unique_ptr<CompactionJob> compaction_;
    HistoryRecoveryConfig history_config_;
    std::uint64_t history_recovery_cut_{};
    std::unique_ptr<HistoryRecoveryJob> history_recovery_;
    Digest256 archive_destination_identity_{};
    Digest256 archive_session_nonce_{};
    bool archive_recovery_pending_{};
    std::optional<ArchiveDestinationRequest> archive_verification_request_;
    std::optional<ArchiveDestinationEvidence> archive_verification_evidence_;

    raft_server_t* server_{};
    bool restoring_{true};
    std::string callback_error_;
    std::uint64_t result_capacity_refusals_{};
    std::uint64_t log_capacity_refusals_{};
};

[[nodiscard]] Digest256 state_digest_of(const TradingState& state);

}
