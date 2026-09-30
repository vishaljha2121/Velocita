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

// Phase 6. The deterministic reducer placed behind consensus.
//
// ADR 0001 puts the reducer at the centre and everything else outside it, and
// this class is the seam that keeps that true under replication: it owns the
// Raft server, the log and the metadata, and it calls `apply` only from the
// apply callback. The reducer itself gains nothing, loses nothing, and does not
// know it is replicated - which is the property that makes a single-voter run
// and a three-voter run the same test.
//
// Deliberately a SEPARATE class from V1's `RaftNode` rather than a
// generalisation of it. V1 is accepted and gated; the storage, peer wire and
// checksum layers it owns are payload-generic. V2 peer admission now wraps the
// unchanged inner wire body in identity-bound framing; V1 is not changed.

// What the caller learns at the moment it offers a command. A command that is
// merely ADMITTED has no outcome yet: only commit produces one, and asking for
// it earlier is the mistake G-035 exists to prevent.
enum class ProposeStatus : std::uint8_t {
    // Appended to the leader's log. The result arrives later, via
    // `take_committed_result`.
    admitted = 1,
    // This node is not the leader. `leader_id` names who is, when known.
    not_leader,
    // Invalid canonical command bytes, or the local Raft server refused the entry.
    refused,
    // Staging capacity is down to the margin reserved for control commands, so
    // a normal-lane command is turned away while a control one would still be
    // admitted. Distinct from `refused` because the caller's correct response is
    // different: this one says retry later, that one says something is wrong.
    normal_lane_exhausted,
    // Offered on `ProposeLane::control`, but it is not an authenticated control
    // command: the wrong kind of command, a caller outside the operator band,
    // or bytes that do not decode at all.
    not_a_control_command,
    authority_busy,
    // No result obligation accepted; retry after consumer drain.
    result_capacity_exhausted,
    log_capacity_exhausted,
    persistence_busy,
};

// WHICH LANE A COMMAND IS PROPOSED ON.
//
// The mirror of the gateway's `SendQuotaView::emergency_lane_remaining`, on the
// cluster side and for the same reason: a kill that has to queue behind normal
// traffic is not an independent kill path. A flood of strategy commands fills
// the staging arena, and without a reservation the command that stops the flood
// is the one that cannot get in.
//
// The reservation is a MARGIN, not an exemption. Control commands may draw on
// the reserved blocks and nothing else may, but when those are gone too they are
// refused like anything else - unbounded admission is exactly what the arena's
// backpressure exists to prevent, and overwriting bytes that are not yet durable
// to deliver a kill would trade one failure for a worse one.
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

    // Source compatibility for admission-focused callers. Busy is deliberately
    // not "none": the frame was not accepted and must be retried by its caller.
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

// Staging blocks that only `ProposeLane::control` may consume.
inline constexpr std::size_t kControlLaneReservedBlocks = 4;

struct ProposeOutcome {
    ProposeStatus status{ProposeStatus::refused};
    std::int32_t leader_id{-1};
    std::int64_t term{};
    std::int64_t index{};
};

// Everything a convergence check needs, and nothing a caller could mistake for
// an economic fact. `state_digest` is the canonical snapshot digest, so it
// covers state version, orders, reservations, outbox and receipts at once -
// comparing it across voters is strictly stronger than comparing any field.
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
    // Leader only: voters whose next entry lies inside this node's compacted
    // prefix. They need a snapshot install to catch up (M6).
    std::uint32_t stranded_voters{};
    // The highest index this voter knows every voter holds durably (it only
    // rises). Compaction never cuts above it.
    std::int64_t compaction_floor{};
    // Highest index reclaimed into a snapshot (0: nothing compacted).
    std::int64_t snapshot_index{};
};

// Diagnostic committed-state copy, not a ReadIndex or fresh send permission.
// `authoritative` reports only this node's local leader belief, which can survive
// isolation. New-risk delivery requires a separate request-correlated proof.
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
    // `checkpoints` is a test-only seam, defaulted off and carrying no
    // behaviour of its own: it lets a test observe or park inside the storage
    // layer at a named point. The truncation/writer interaction cannot be
    // pinned any other way - it is a race between two threads, and a test that
    // merely runs both and hopes is not evidence.
    explicit ReplicatedKernel(VerifiedIdentityStartup startup,
                              StorageCheckpoints checkpoints = {},
                              MetadataCheckpoints metadata_checkpoints = {},
                              ArchiveStoreConfig archive_config = {},
                              HistoryRecoveryConfig history_config = {},
                              Digest256 archive_destination_identity = {});
    ~ReplicatedKernel();
    ReplicatedKernel(const ReplicatedKernel&) = delete;
    ReplicatedKernel& operator=(const ReplicatedKernel&) = delete;

    // Raft periodic work/apply. In worker mode this never drives disk I/O;
    // flush_durability pumps owned persistence requests/completions separately.
    // The explicit no-worker test mode still saves metadata synchronously.
    void tick(int elapsed_ms);

    // Submit/poll durability without waiting in worker mode; a return alone
    // does not imply completion. No-worker test mode drains synchronously.
    void flush_durability();

    // Move all live persistence off this thread. Until this is called, metadata
    // and WAL work run inline as an explicit deterministic test mode. Once the
    // writer is started, hard state is only saved by that worker; there is no
    // owner-thread fallback.
    void start_writer();
    // Explicit cold/test transition; joins and refuses any pending fence.
    // It must not be used as a live progress fallback.
    void stop_writer();

    [[nodiscard]] WalMetrics wal_metrics() const;


    [[nodiscard]] bool is_leader() const;

    // Offer canonical V2 command bytes to consensus. Never applies them here:
    // the reducer is invoked from the apply callback and nowhere else, so a
    // command that is proposed and never commits has no economic effect at all.
    //
    // A retry of an already-applied command is deliberately NOT short-circuited
    // from the retained receipt. Serving it from local state would be a read
    // whose correctness depends on still being leader, and the reducer already
    // answers an identical command with its original result when the retry
    // commits. Correctness first; the log cost of a duplicate is bounded by the
    // caller's own retry policy.
    [[nodiscard]] ProposeOutcome propose(std::span<const std::byte> command_bytes,
                                         ProposeLane lane = ProposeLane::normal);

    [[nodiscard]] std::optional<CommittedResult> take_committed_result();
    // Diagnostic owner-thread counters remain readable after a failure latch.
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

    // Replication prerequisite only: admission/application is NOT send permission.
    [[nodiscard]] ProposeOutcome propose_authority_barrier(const AuthorityBarrier& request);
    [[nodiscard]] std::optional<AuthorityProof> take_authority_proof();
    void abandon_authority_request(const AuthorityBarrier& request);
    [[nodiscard]] std::int64_t authority_drain_index() const;

    // Admission errors leave Raft untouched. No public raw-message bypass.
    [[nodiscard]] PeerReceiveResult receive_peer(std::span<const std::byte> bound_frame);
    // Copies into caller-owned bounded storage; no payload borrow escapes the owner.
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

    // Highest log index whose bytes are on disk. Distinct from the commit index
    // on purpose: the gap between them is where asynchronous durability lives,
    // and collapsing the two is the mistake this phase exists to prevent.
    [[nodiscard]] std::uint64_t durable_index() const noexcept {
        return arena_.durable_index();
    }

    // Candidate discovery only; neither role value proves quorum freshness.
    [[nodiscard]] CommittedOutboxView committed_outbox() const;

    // Nonblocking owner pump: start one fenced archive/snapshot/WAL transaction,
    // then collect its completion on later calls. Zero means pending or no cut.
    // No disk work or worker join occurs here. Mutating admission pauses while
    // pending; only completed verified coverage releases retained log ownership.
    std::uint64_t compact();
    // compact() only while the applied index is at or below the compaction
    // floor, so no voter is ever left needing an entry this node reclaims.
    // Continues a compaction already in progress. The node uses only this.
    std::uint64_t compact_within_floor();
    [[nodiscard]] bool compaction_pending() const noexcept { return !!compaction_; }
    [[nodiscard]] bool persistence_pending() const noexcept;
    // Native transport boundary. A request is not authority; only the private
    // authenticated destination verifier can produce a valid completion.
    [[nodiscard]] std::optional<ArchiveDestinationRequest> archive_verification_request() const noexcept;
    [[nodiscard]] bool accept_archive_verification(ArchiveDestinationEvidence&& evidence);
    [[nodiscard]] bool history_recovery_pending() const noexcept {
        return history_recovery_cut_ != 0;
    }

    // Diagnostic reducer-state view only. Neither this view nor `is_leader()`
    // proves quorum freshness; new-risk sending requires a request-correlated
    // authority proof.
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
    // Reports this node's durable index to Raft, which uses it for the local
    // node's own quorum contribution. See the pinned patch in deploy/patches.
    static raft_index_t durable_idx(raft_server_t*, void*);

    void persist_commit_index();
    void require_usable() const;
    int queue_outbound(const PeerMessageView& message) noexcept;
    // Follower only: tell the leader what is newly durable without waiting to be asked.
    void send_early_ack();
    // The highest index this node knows every voter holds durably: its own
    // durable index and each peer's match (true facts even when stale) and
    // what leaders have told it. Valid on any role.
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
    [[nodiscard]] bool archive_admission(std::uint64_t resulting_last_index,
                                         bool protected_lane) const noexcept;

    // Construct before any store; release only after every writer/store dies.
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
    // Staging plus the segment it drains into. `log_offer` stages and returns;
    // the fsync happens in `flush_durability`, driven from `tick`, so the owner
    // loop no longer pays a sync per record.
    WalBlockArena arena_;
    std::unique_ptr<WalSegment> segment_;
    // Declared after the segment and arena it refers to, so it is destroyed
    // first and its thread is joined while both are still alive.
    std::unique_ptr<WalWriter> writer_;
    SnapshotStore snapshots_;
    PersistentMetadata metadata_;
    PersistentMetadata desired_metadata_;
    // What this follower may acknowledge before the leader asks again. It
    // only ever claims min(verified this term, durable): see follower_ack.hpp.
    FollowerAckTracker follower_ack_;
    // The highest compaction floor this voter has learned. Entries every voter
    // holds are never truncated, so the floor only rises.
    std::int64_t known_floor_{};
    // Highest index reclaimed by `log_poll` during the current `raft_end_snapshot`.
    std::uint64_t polled_through_{};
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

// The canonical digest of a replica's state. Extracted so a convergence check
// and a snapshot install agree by construction about what "the same state"
// means.
[[nodiscard]] Digest256 state_digest_of(const TradingState& state);

}  // namespace velocita::v2
