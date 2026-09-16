#pragma once

#include "velocita/storage.hpp"
#include "velocita/wire.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/snapshot_store.hpp"
#include "velocita/v2/wal_arena.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/storage_checkpoint.hpp"

extern "C" {
#include "raft.h"
}

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocita::v2 {

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
// checksum layers it owns are already payload-generic and are reused here
// unchanged, so the duplication is the Raft callback table and nothing
// economic.

// What the caller learns at the moment it offers a command. A command that is
// merely ADMITTED has no outcome yet: only commit produces one, and asking for
// it earlier is the mistake G-035 exists to prevent.
enum class ProposeStatus : std::uint8_t {
    // Appended to the leader's log. The result arrives later, via
    // `take_committed_result`.
    admitted = 1,
    // This node is not the leader. `leader_id` names who is, when known.
    not_leader,
    // The local Raft server refused the entry.
    refused,
    // Staging capacity is down to the margin reserved for control commands, so
    // a normal-lane command is turned away while a control one would still be
    // admitted. Distinct from `refused` because the caller's correct response is
    // different: this one says retry later, that one says something is wrong.
    normal_lane_exhausted,
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

// Staging blocks that only `ProposeLane::control` may consume.
inline constexpr std::size_t kControlLaneReservedBlocks = 4;

struct ProposeOutcome {
    ProposeStatus status{ProposeStatus::refused};
    std::int32_t leader_id{-1};
    std::int64_t term{};
    std::int64_t index{};
};

// A command that COMMITTED and was applied. `result` is the reducer's own
// answer, byte-identical on every voter, because every voter applied the same
// bytes to the same prior state.
struct CommittedResult {
    CommandIdentity identity{};
    ApplyResult result{};
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
};

// A read of committed outbox state, carrying the authority that was true AT THE
// MOMENT OF THE READ.
//
// The gateway must not act on a deposed leader's view. `TradingState` alone
// cannot say whether it is current, so a bare reference invites exactly that
// mistake; this makes the caller hold the evidence alongside the data, and
// `authoritative` is the single question worth asking.
//
// Not a read-index read. A read-index round trip proves leadership at the read's
// linearization point and is the stricter form; this proves only that THIS node
// believed itself leader when the bytes were taken, which is sufficient while
// the gateway is in-process with the voter and must be revisited when it is not.
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
    ReplicatedKernel(std::int32_t node_id, std::filesystem::path data_dir,
                     std::vector<std::int32_t> members = {},
                     StorageCheckpoints checkpoints = {});
    ~ReplicatedKernel();
    ReplicatedKernel(const ReplicatedKernel&) = delete;
    ReplicatedKernel& operator=(const ReplicatedKernel&) = delete;

    // Raft's periodic work and apply. Deliberately does NOT drive the disk:
    // flushing is `flush_durability`, and the owner loop decides when to call
    // it. The same discipline the arena uses for its writer and the simulator
    // uses for time - nothing that decides behaviour may be implicit, and a
    // tick that silently synced would make the gap between appended and durable
    // unobservable, including to a test trying to prove it is enforced.
    void tick(int elapsed_ms);

    // Drain staged blocks to disk and sync. Until this runs, entries this node
    // has appended are NOT durable and it will not count itself toward quorum
    // for them.
    void flush_durability();

    // Move the writing off this thread. Until this is called, `flush_durability`
    // does the I/O inline - which keeps tests deterministic and is why the
    // thread is opt-in rather than implied by construction.
    void start_writer();
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

    void receive_peer(const PeerMessage& message);
    [[nodiscard]] std::optional<PeerMessage> take_outbound_message();

    [[nodiscard]] ReplicaStatus status() const;

    // Highest log index whose bytes are on disk. Distinct from the commit index
    // on purpose: the gap between them is where asynchronous durability lives,
    // and collapsing the two is the mistake this phase exists to prevent.
    [[nodiscard]] std::uint64_t durable_index() const noexcept {
        return arena_.durable_index();
    }

    // The committed outbox, with the authority under which it was read. A
    // caller that ignores `authoritative` is reading a follower's or a deposed
    // leader's copy, which is stale by definition.
    [[nodiscard]] CommittedOutboxView committed_outbox() const;

    // Capture a durable snapshot at the current applied index and reclaim the
    // log prefix it covers. Ordering is the whole point and is enforced here
    // rather than documented: the snapshot is fsynced and renamed BEFORE any
    // entry is reclaimed, so a crash in between leaves a durable snapshot and a
    // log that still holds the prefix - recoverable - rather than the reverse,
    // which loses entries nothing covers.
    //
    // Returns the last index included, or zero when there was nothing to take.
    std::uint64_t compact();

    // Read-only view for tests and for the gateway's committed-outbox read. It
    // is authoritative only on a leader that has not been deposed; callers that
    // need that guarantee must check `is_leader()` at the same moment, and a
    // read-index discipline is the stricter form deferred with Phase 7.
    [[nodiscard]] const TradingState& state() const noexcept { return state_; }

private:
    static int send_request_vote(raft_server_t*, void*, raft_node_t*, msg_requestvote_t*);
    static int send_append_entries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*);
    static int apply_log(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int persist_vote(raft_server_t*, void*, raft_node_id_t);
    static int persist_term(raft_server_t*, void*, raft_term_t, raft_node_id_t);
    static int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_poll(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_pop(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_clear(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    // Reports this node's durable index to Raft, which uses it for the local
    // node's own quorum contribution. See the pinned patch in deploy/patches.
    static raft_index_t durable_idx(raft_server_t*, void*);

    void persist_commit_index();

    std::int32_t node_id_;
    std::vector<std::int32_t> members_;
    MetadataStore metadata_store_;
    WriteAheadLog wal_;
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
    // Highest index reclaimed by `log_poll` during the current `raft_end_snapshot`.
    std::uint64_t polled_through_{};
    std::uint64_t blocks_submitted_{};
    std::uint64_t backpressure_refusals_{};
    std::uint64_t highest_staged_index_{};
    bool writer_running_{false};

    TradingState state_{};
    std::unique_ptr<ApplyScratch> scratch_;
    std::unique_ptr<KernelCommandCodecScratch> codec_scratch_;

    raft_server_t* server_{};
    bool restoring_{true};
    std::string callback_error_;
    std::deque<PeerMessage> outbound_messages_;
    std::deque<CommittedResult> committed_results_;

    // OWNED ENTRY PAYLOADS, keyed by log index.
    //
    // `log_append_entry` in willemt/raft does
    // `memcpy(&entries[back], ety, sizeof(raft_entry_t))`, which copies the
    // struct and therefore only the `data.buf` POINTER. The caller's buffer
    // would have to outlive the entry, and a proposal's buffer cannot: commit
    // happens after `propose` returns, and a slow follower can be re-sent an
    // entry arbitrarily later.
    //
    // So this node owns every payload the log can still reference, and
    // `log_offer` repoints the stored entry at the copy held here. Bounded by
    // the log: entries leave on poll, pop and clear.
    std::map<std::uint64_t, std::vector<std::byte>> log_payloads_;
};

// The canonical digest of a replica's state. Extracted so a convergence check
// and a snapshot install agree by construction about what "the same state"
// means.
[[nodiscard]] Digest256 state_digest_of(const TradingState& state);

}  // namespace velocita::v2
