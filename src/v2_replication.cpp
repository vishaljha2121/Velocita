#include "velocita/v2/replication.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace velocita::v2 {
namespace {

std::string_view role_name(raft_server_t* server) {
    switch (raft_get_state(server)) {
        case RAFT_STATE_FOLLOWER: return "follower";
        case RAFT_STATE_CANDIDATE: return "candidate";
        case RAFT_STATE_LEADER: return "leader";
        default: return "none";
    }
}

}  // namespace

Digest256 state_digest_of(const TradingState& state) {
    // The canonical snapshot digest, so "the same state" means the same thing to
    // a convergence check and to a snapshot install. Anything cheaper - a state
    // version, an order count - can agree while the states differ.
    static thread_local std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(state, storage);
    if (encoded.error != StateSnapshotError::none) {
        throw std::runtime_error("replica state is not encodable");
    }
    return encoded.state_digest;
}

ReplicatedKernel::ReplicatedKernel(const std::int32_t node_id,
                                   std::filesystem::path data_dir,
                                   std::vector<std::int32_t> members,
                                   StorageCheckpoints checkpoints)
    : node_id_(node_id),
      members_(std::move(members)),
      // V2's own file names. V1 and V2 share this storage layer and share its
      // magic and version constants, so distinct names are what keeps a node
      // from reading the other milestone's directory as its own. See G-051.
      metadata_store_(data_dir, kV2MetadataFileName),
      wal_(data_dir, kV2WalFileName),
      segment_(std::make_unique<WalSegment>(data_dir, checkpoints)),
      writer_(std::make_unique<WalWriter>(*segment_, arena_)),
      snapshots_(std::move(data_dir)),
      metadata_(metadata_store_.load()),
      scratch_(std::make_unique<ApplyScratch>()),
      codec_scratch_(std::make_unique<KernelCommandCodecScratch>()),
      server_(raft_new()) {
    if (server_ == nullptr) throw std::runtime_error("raft_new failed");
    const auto release_server = [](raft_server_t* server) { raft_free(server); };
    std::unique_ptr<raft_server_t, decltype(release_server)> startup_guard(server_, release_server);

    raft_cbs_t table{};
    table.send_requestvote = &ReplicatedKernel::send_request_vote;
    table.send_appendentries = &ReplicatedKernel::send_append_entries;
    table.applylog = &ReplicatedKernel::apply_log;
    table.persist_vote = &ReplicatedKernel::persist_vote;
    table.persist_term = &ReplicatedKernel::persist_term;
    table.log_offer = &ReplicatedKernel::log_offer;
    table.log_poll = &ReplicatedKernel::log_poll;
    table.log_pop = &ReplicatedKernel::log_pop;
    table.log_clear = &ReplicatedKernel::log_clear;
    table.log_get_node_id = &ReplicatedKernel::log_get_node_id;
    // THE GATE. Without this callback Raft counts our vote as soon as an entry
    // is appended, which with an asynchronous log means before its bytes exist
    // on disk.
    table.durable_idx = &ReplicatedKernel::durable_idx;
    raft_set_callbacks(server_, &table, this);

    if (members_.empty()) members_.push_back(node_id_);
    std::sort(members_.begin(), members_.end());
    members_.erase(std::unique(members_.begin(), members_.end()), members_.end());
    if (!std::binary_search(members_.begin(), members_.end(), node_id_)) {
        throw std::runtime_error("cluster membership does not include the local node");
    }
    if (raft_add_node(server_, nullptr, node_id_, 1) == nullptr) {
        throw std::runtime_error("failed to add Raft member " + std::to_string(node_id_));
    }
    // INSTALL THE SNAPSHOT FIRST, then replay only what it does not cover.
    //
    // Without a snapshot this is replay from the beginning, which is the
    // strongest form: the reducer is deterministic, so replaying the committed
    // prefix reconstructs exactly the state that prefix produced, and a
    // divergence surfaces instead of hiding behind a state file. A snapshot is
    // the same guarantee for a prefix that has been reclaimed, and it is only
    // trusted because the canonical decoder revalidates every invariant before
    // it is accepted.
    std::uint64_t installed_through = 0;
    if (auto stored = snapshots_.load()) {
        const int began = raft_begin_load_snapshot(
            server_, stored->last_included_term,
            static_cast<raft_index_t>(stored->last_included_index));
        if (began == 0) {
            state_ = stored->state;
            installed_through = stored->last_included_index;
            if (raft_end_load_snapshot(server_) != 0) {
                throw std::runtime_error("failed to install the stored snapshot");
            }
        }
    }

    // `raft_begin_load_snapshot` retains only the local Raft node. Restore the
    // configured voter set after the snapshot so a restarted voter cannot
    // accidentally become a one-node cluster.
    for (const auto member : members_) {
        if (member == node_id_) continue;
        if (member <= 0 ||
            raft_add_node(server_, nullptr, member, member == node_id_) == nullptr) {
            throw std::runtime_error("failed to add Raft member " + std::to_string(member));
        }
    }

    // Hard state is restored AFTER the snapshot, not before.
    // `raft_begin_load_snapshot` assigns `current_term` from the snapshot and
    // clears the vote, so restoring them first silently loses both - and the
    // symptom appears far away, as a commit index that no longer fits the log.
    if (raft_set_current_term(server_, metadata_.term) != 0) {
        throw std::runtime_error("failed to restore Raft term");
    }
    if (metadata_.voted_for >= 0 &&
        raft_vote_for_nodeid(server_, metadata_.voted_for) != 0) {
        throw std::runtime_error("failed to restore Raft vote");
    }

    // Blocks first, then the legacy per-record file. A directory written by an
    // older build has no segment, and must still start.
    auto records = segment_->read_records();
    if (records.empty()) records = wal_.read_all();
    auto expected_index = installed_through + 1;
    for (const auto& record : records) {
        // Entries the snapshot already accounts for are skipped rather than
        // re-applied. A crash between a durable snapshot and its prefix
        // reclamation leaves exactly this overlap, and it has to be harmless.
        if (record.index <= installed_through) continue;
        if (record.index != expected_index++) {
            throw std::runtime_error("WAL history is not a contiguous snapshot suffix");
        }
        raft_entry_t entry{};
        entry.term = record.term;
        entry.id = record.entry_id;
        entry.type = record.entry_type;
        entry.data.buf = const_cast<std::byte*>(record.payload.data());
        entry.data.len = static_cast<unsigned int>(record.payload.size());
        if (raft_append_entry(server_, &entry) != 0) {
            throw std::runtime_error("failed to restore WAL entry " +
                                     std::to_string(record.index));
        }
    }
    // Records recovered from disk are durable by definition - they were read
    // from it. Telling the arena so is what lets a restarted node count itself
    // toward quorum for its own recovered log.
    std::uint64_t recovered_through = installed_through;
    for (const auto& record : records) {
        recovered_through = std::max(recovered_through, record.index);
    }
    arena_.seed_durable_index(recovered_through);

    if (metadata_.commit_index < 0) {
        throw std::runtime_error("metadata holds a negative commit index");
    }

    const auto log_end = raft_get_current_idx(server_);
    if (metadata_.commit_index > log_end) {
        throw std::runtime_error("recovery refused: committed WAL history is missing; preserve storage for repair");
    }
    raft_set_commit_idx(server_, std::max(static_cast<raft_index_t>(metadata_.commit_index),
        static_cast<raft_index_t>(installed_through)));
    if (raft_apply_all(server_) != 0) {
        throw std::runtime_error("failed to replay committed WAL entries: " +
                                 callback_error_);
    }
    restoring_ = false;
    (void)startup_guard.release();
}

ReplicatedKernel::~ReplicatedKernel() {
    if (server_ != nullptr) raft_free(server_);
}

void ReplicatedKernel::tick(const int elapsed_ms) {
    if (const int result = raft_periodic(server_, elapsed_ms); result != 0) {
        throw std::runtime_error("raft_periodic failed: " + std::to_string(result));
    }

    // A LEADER SERVES A NON-ZERO TERM. willemt promotes a lone voting node
    // straight to leader without campaigning - "only one voting node means it's
    // safe for us to become the leader" - which is correct for safety and leaves
    // `current_term` at zero. Raft's own model has no term-zero leader; zero
    // means no leader has ever existed.
    //
    // Left alone it is not cosmetic. Entries inherit the leader's term, so they
    // are written with term zero, and `raft_begin_load_snapshot` rejects a zero
    // term outright - so a single-voter cluster could take a snapshot and then
    // silently fail to install it on restart, recovering from the log prefix it
    // had already reclaimed. Restoring the invariant here is the narrowest place
    // that closes it, and on a multi-voter cluster the branch never fires
    // because an election has already raised the term.
    if (raft_is_leader(server_) != 0 && raft_get_current_term(server_) == 0) {
        if (raft_set_current_term(server_, 1) != 0) {
            throw std::runtime_error("failed to raise a leader's term above zero");
        }
    }

    persist_commit_index();
    if (const int result = raft_apply_all(server_); result != 0) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(result) +
                                 " " + callback_error_);
    }
}

void ReplicatedKernel::flush_durability() {
    // Seal whatever is open so a quiet period cannot leave recent records
    // staged indefinitely - the time half of the flush trigger; the arena's
    // block-full rotation is the space half.
    arena_.seal_open_block();

    // Hand sealed blocks to the writer. A full submission ring is backpressure
    // like any other: the blocks stay sealed and go on the next pass.
    bool submitted_any = false;
    while (const auto descriptor = arena_.next_unsubmitted()) {
        if (!writer_->submit(*descriptor)) break;
        ++blocks_submitted_;
        submitted_any = true;
    }

    // With no writer thread the I/O happens here, which is what keeps a test
    // deterministic. With one, the thread has already done it or will shortly.
    if (!writer_running_) {
        (void)writer_->poll();
    }
    (void)submitted_any;

    // Completions are applied BY THE OWNER, which is what keeps every mutation
    // of the arena on one thread and lets the writer read sealed bytes without
    // a lock.
    BlockDescriptor finished{};
    while (writer_->take_completion(finished)) {
        (void)arena_.complete(finished);
    }
}

void ReplicatedKernel::start_writer() {
    if (writer_running_) return;
    writer_running_ = true;
    writer_->start();
}

void ReplicatedKernel::stop_writer() {
    if (!writer_running_) return;
    writer_->stop();
    writer_running_ = false;
    // Anything the thread finished on its way out still needs applying, or the
    // arena would hold blocks that are durable and look sealed.
    BlockDescriptor finished{};
    while (writer_->take_completion(finished)) {
        (void)arena_.complete(finished);
    }
}

WalMetrics ReplicatedKernel::wal_metrics() const {
    WalMetrics metrics;
    metrics.blocks_submitted = blocks_submitted_;
    metrics.blocks_durable = writer_->blocks_written();
    metrics.backpressure_refusals = backpressure_refusals_;
    metrics.highest_staged_index = highest_staged_index_;
    metrics.durable_index = arena_.durable_index();
    return metrics;
}

bool ReplicatedKernel::is_leader() const { return raft_is_leader(server_) != 0; }

ProposeOutcome ReplicatedKernel::propose(const std::span<const std::byte> command_bytes,
                                         const ProposeLane lane) {
    ProposeOutcome outcome;
    outcome.leader_id = raft_get_current_leader(server_);
    outcome.term = raft_get_current_term(server_);
    outcome.index = raft_get_current_idx(server_);
    if (!is_leader()) {
        outcome.status = ProposeStatus::not_leader;
        return outcome;
    }

    // THE LANE IS A LABEL UNTIL IT IS CHECKED.
    //
    // `ProposeLane` is chosen by the caller, so naming the control lane is not
    // the same as being entitled to it. Two ways that would make the
    // reservation meaningless:
    //
    //   - label an ordinary command "control" and spend the kill path's
    //     reserved blocks on strategy traffic;
    //   - offer a real control command from an unauthenticated caller. The
    //     reducer refuses it - but only AFTER the entry has consumed reserved
    //     capacity, committed and replicated. A refusal that costs the resource
    //     it was protecting is a denial of service on the kill path.
    //
    // So the lane decodes and checks before the entry exists. That costs a
    // decode on a path taken once per operator action, and it is the difference
    // between a reservation and a bypass anyone may name.
    //
    // `RequestMassCancel` is admitted here but is NOT operator-only in the
    // reducer, and the split is deliberate. Mass cancel only reduces risk, and
    // requiring operator authority to reduce risk is the asymmetry error this
    // phase avoids everywhere else - anyone may flatten the book on the normal
    // lane. What an operator caller buys is the right to spend the KILL
    // RESERVE doing it, which is a claim on a scarce resource rather than a
    // claim about the action.
    if (lane == ProposeLane::control) {
        Command probe;
        const auto decoded =
            decode_kernel_command(command_bytes, probe, *codec_scratch_);
        const bool is_control =
            decoded.error == KernelCodecError::none &&
            (std::holds_alternative<AdvanceKillEpoch>(probe.body) ||
             std::holds_alternative<SetExecutorGeneration>(probe.body) ||
             std::holds_alternative<SetStrategyMode>(probe.body) ||
             std::holds_alternative<RequestMassCancel>(probe.body)) &&
            is_operator_caller(probe.identity.caller_id);
        if (!is_control) {
            outcome.status = ProposeStatus::not_a_control_command;
            return outcome;
        }
    }

    // THE RESERVATION, and it has to be here rather than in the arena.
    //
    // By the time `log_offer` calls `stage`, Raft has already accepted the entry
    // and assigned it an index; refusing there means unwinding a log the leader
    // has committed to. So the lane is checked before the entry exists, which is
    // the only point at which turning it away is free.
    if (lane == ProposeLane::normal &&
        arena_.free_blocks() <= kControlLaneReservedBlocks) {
        outcome.status = ProposeStatus::normal_lane_exhausted;
        return outcome;
    }

    // The entry id is a Raft-level hint, not an economic identity. The command's
    // real identity lives in its canonical bytes and is enforced by the reducer's
    // request receipt, so narrowing it to 31 bits here costs nothing.
    std::uint64_t seed = 0;
    for (const auto byte : command_bytes) {
        seed = seed * 1099511628211ULL ^ static_cast<std::uint64_t>(byte);
    }

    std::vector<std::byte> payload(command_bytes.begin(), command_bytes.end());
    msg_entry_t entry{};
    entry.id = static_cast<raft_entry_id_t>(seed & 0x7FFFFFFFU);
    entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = payload.data();
    entry.data.len = static_cast<unsigned int>(payload.size());
    msg_entry_response_t raft_response{};
    const int result = raft_recv_entry(server_, &entry, &raft_response);
    if (result == RAFT_ERR_NOT_LEADER) {
        outcome.status = ProposeStatus::not_leader;
        outcome.leader_id = raft_get_current_leader(server_);
        return outcome;
    }
    if (result != 0) {
        outcome.status = ProposeStatus::refused;
        return outcome;
    }

    persist_commit_index();
    if (const int applied = raft_apply_all(server_); applied != 0) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(applied) +
                                 " " + callback_error_);
    }
    outcome.status = ProposeStatus::admitted;
    outcome.index = raft_response.idx;
    outcome.term = raft_response.term;
    return outcome;
}

std::optional<CommittedResult> ReplicatedKernel::take_committed_result() {
    if (committed_results_.empty()) return std::nullopt;
    auto result = committed_results_.front();
    committed_results_.pop_front();
    return result;
}

std::optional<PeerMessage> ReplicatedKernel::take_outbound_message() {
    if (outbound_messages_.empty()) return std::nullopt;
    auto result = std::move(outbound_messages_.front());
    outbound_messages_.pop_front();
    return result;
}

ReplicaStatus ReplicatedKernel::status() const {
    ReplicaStatus value;
    value.node_id = node_id_;
    value.leader_id = raft_get_current_leader(server_);
    value.role = role_name(server_);
    value.term = raft_get_current_term(server_);
    value.current_index = raft_get_current_idx(server_);
    value.commit_index = raft_get_commit_idx(server_);
    value.applied_index = raft_get_last_applied_idx(server_);
    value.state_version = state_.state_version;
    value.outbox_effect_count = state_.outbox_effect_count;
    value.state_digest = state_digest_of(state_);
    return value;
}

CommittedOutboxView ReplicatedKernel::committed_outbox() const {
    CommittedOutboxView view;
    view.authoritative = raft_is_leader(server_) != 0;
    view.leader_id = raft_get_current_leader(server_);
    view.term = raft_get_current_term(server_);
    view.applied_index = raft_get_last_applied_idx(server_);
    view.effect_count = state_.outbox_effect_count;
    for (std::size_t index = 0; index < state_.outbox_effect_count; ++index) {
        view.effects[index] = state_.outbox_effects[index];
    }
    return view;
}

std::uint64_t ReplicatedKernel::compact() {
    const auto applied = raft_get_last_applied_idx(server_);
    if (applied <= 0) return 0;

    // The term OF THE ENTRY at `applied`, not the server's current term. They
    // are different quantities: a snapshot describes a log position, and the
    // position's term is what a peer needs to decide whether the snapshot is
    // ahead of its own log. Using the current term also reads zero on a node
    // that has not yet campaigned, which `raft_begin_load_snapshot` rejects
    // outright - so the mistake shows up as a snapshot that silently refuses to
    // install on restart.
    const auto* last = raft_get_entry_from_idx(server_, applied);
    if (last == nullptr) return 0;
    const auto term = last->term;

    if (raft_begin_snapshot(server_, 0) != 0) return 0;
    try {
        // DURABLE BEFORE RECLAIMED. `save` returns only after fsync, rename and
        // a directory fsync, so by the time any entry is polled away the
        // snapshot that replaces it is on disk. The reverse order loses entries
        // that nothing covers, and a crash is the only witness.
        snapshots_.save(state_, static_cast<std::uint64_t>(applied), term);
    } catch (...) {
        (void)raft_cancel_snapshot(server_);
        throw;
    }

    polled_through_ = 0;
    if (raft_end_snapshot(server_) != 0) {
        throw std::runtime_error("failed to end the snapshot: " + callback_error_);
    }
    if (polled_through_ != 0) {
        const bool resume_writer = writer_running_;
        stop_writer();
        flush_durability();
        segment_->discard_through(polled_through_);
        // Reclaiming the WAL prefix last means a crash here leaves entries the
        // snapshot already covers. Restart skips them by index, so the overlap
        // costs a little disk and nothing else. Failing the other way would
        // cost committed entries.
        wal_.discard_through(polled_through_);
        if (resume_writer) start_writer();
    }
    return static_cast<std::uint64_t>(applied);
}

void ReplicatedKernel::persist_commit_index() {
    // NEVER PERSIST A COMMIT INDEX BEYOND WHAT IS DURABLE.
    //
    // willemt counts the leader's own vote unconditionally, so `commit_idx` can
    // momentarily name an entry this node has staged but not synced. Recording
    // that would be the one unrecoverable version of the mistake: a restart
    // would read a commit index pointing past the end of its own log.
    //
    // Clamping keeps the persisted hard state a statement about bytes on disk.
    //
    // With the pinned durability patch this should never bind: Raft no longer
    // counts our vote before the bytes are on disk, so the commit index cannot
    // outrun durability in the first place. It stays as the cheaper of two
    // checks - if the callback is ever unwired, this is what stops a restart
    // reading a commit index past the end of its own log.
    const auto durable = static_cast<raft_index_t>(arena_.durable_index());
    const auto raw = raft_get_commit_idx(server_);
    const auto commit = (durable > 0 && durable < raw) ? durable : raw;
    if (commit == metadata_.commit_index) return;
    metadata_.term = raft_get_current_term(server_);
    metadata_.voted_for = raft_get_voted_for(server_);
    metadata_.commit_index = commit;
    metadata_store_.save(metadata_);
}

void ReplicatedKernel::receive_peer(const PeerMessage& message) {
    if (message.recipient_id != node_id_ || message.sender_id == node_id_) {
        throw std::runtime_error("peer message has invalid route");
    }
    auto* sender = raft_get_node(server_, message.sender_id);
    if (sender == nullptr) throw std::runtime_error("peer message came from a non-member");

    int result = 0;
    if (const auto* value = std::get_if<RequestVote>(&message.payload)) {
        msg_requestvote_t request{value->term, value->candidate_id, value->last_log_index,
                                  value->last_log_term};
        msg_requestvote_response_t response{};
        result = raft_recv_requestvote(server_, sender, &request, &response);
        outbound_messages_.push_back(
            {node_id_, message.sender_id,
             RequestVoteResponse{response.term, response.vote_granted != 0}});
    } else if (const auto* value = std::get_if<RequestVoteResponse>(&message.payload)) {
        msg_requestvote_response_t response{value->term, value->vote_granted ? 1 : 0};
        result = raft_recv_requestvote_response(server_, sender, &response);
    } else if (const auto* value = std::get_if<AppendEntries>(&message.payload)) {
        std::vector<raft_entry_t> entries(value->entries.size());
        for (std::size_t i = 0; i < value->entries.size(); ++i) {
            const auto& source = value->entries[i];
            entries[i].term = source.term;
            entries[i].id = source.id;
            entries[i].type = source.type;
            entries[i].data.buf = const_cast<std::byte*>(source.payload.data());
            entries[i].data.len = static_cast<unsigned int>(source.payload.size());
        }
        msg_appendentries_t request{};
        request.term = value->term;
        request.prev_log_idx = value->previous_log_index;
        request.prev_log_term = value->previous_log_term;
        request.leader_commit = value->leader_commit;
        request.n_entries = static_cast<int>(entries.size());
        request.entries = entries.data();
        msg_appendentries_response_t response{};
        result = raft_recv_appendentries(server_, sender, &request, &response);

        // A FOLLOWER'S SUCCESS MUST NOT COVER ENTRIES IT HAS ONLY STAGED.
        //
        // The leader turns `current_idx` straight into this follower's match
        // index and counts it toward quorum. Reporting an index whose bytes are
        // not yet on disk is therefore the same defect as a leader counting its
        // own undurable copy - just on the other side of the wire, and not
        // fixable by the patch that fixed that one, because this number is
        // produced here.
        //
        // Clamping costs a round trip: the leader sees a lower match, resends,
        // and the follower reports the higher index once its writer has caught
        // up. That is the correct trade - the alternative is a quorum formed
        // around bytes a power cut would erase.
        auto reported = response.current_idx;
        const auto durable = static_cast<raft_index_t>(arena_.durable_index());
        if (response.success != 0 && reported > durable) reported = durable;

        outbound_messages_.push_back(
            {node_id_, message.sender_id,
             AppendEntriesResponse{response.term, response.success != 0, reported,
                                   response.first_idx}});
    } else if (const auto* value = std::get_if<AppendEntriesResponse>(&message.payload)) {
        msg_appendentries_response_t response{};
        response.term = value->term;
        response.success = value->success ? 1 : 0;
        response.current_idx = value->current_index;
        response.first_idx = value->first_index;
        result = raft_recv_appendentries_response(server_, sender, &response);
    }
    if (result != 0 && result != RAFT_ERR_NOT_LEADER) {
        throw std::runtime_error("raft peer handling failed: " + std::to_string(result) +
                                 " " + callback_error_);
    }
    persist_commit_index();
    if (const int applied = raft_apply_all(server_); applied != 0) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(applied) +
                                 " " + callback_error_);
    }
}

int ReplicatedKernel::apply_log(raft_server_t*, void* user_data, raft_entry_t* entry,
                                const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    try {
        const auto bytes = std::span<const std::byte>(
            static_cast<const std::byte*>(entry->data.buf), entry->data.len);

        // THE ONLY CALL TO apply() IN THIS CLASS. G-035 is not a rule the code
        // follows, it is the shape of the code: there is no other path from a
        // command to the reducer, so a command that never commits cannot have an
        // economic effect.
        Command command;
        const auto decoded =
            decode_kernel_command(bytes, command, *self.codec_scratch_);
        if (decoded.error != KernelCodecError::none) {
            self.callback_error_ = "committed entry did not decode: type=" +
                std::to_string(entry->type) + " len=" +
                std::to_string(entry->data.len) + " codec=" +
                std::to_string(static_cast<int>(decoded.error));
            return RAFT_ERR_SHUTDOWN;
        }
        const auto result = apply(self.state_, command, *self.scratch_);

        if (!self.restoring_) {
            self.committed_results_.push_back(
                {command.identity, result, entry->term, index});
        }
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::send_request_vote(raft_server_t*, void* user_data, raft_node_t* node,
                                        msg_requestvote_t* message) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.outbound_messages_.push_back(
        {self.node_id_, raft_node_get_id(node),
         RequestVote{message->term, message->candidate_id, message->last_log_idx,
                     message->last_log_term}});
    return 0;
}

int ReplicatedKernel::send_append_entries(raft_server_t*, void* user_data,
                                          raft_node_t* node,
                                          msg_appendentries_t* message) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    AppendEntries append{};
    append.term = message->term;
    append.previous_log_index = message->prev_log_idx;
    append.previous_log_term = message->prev_log_term;
    append.leader_commit = message->leader_commit;
    for (int i = 0; i < message->n_entries; ++i) {
        const auto& entry = message->entries[i];
        const auto* begin = static_cast<const std::byte*>(entry.data.buf);
        append.entries.push_back({entry.term, entry.id, entry.type,
                                  std::vector<std::byte>(begin, begin + entry.data.len)});
    }
    self.outbound_messages_.push_back(
        {self.node_id_, raft_node_get_id(node), std::move(append)});
    return 0;
}

int ReplicatedKernel::persist_vote(raft_server_t*, void* user_data, raft_node_id_t vote) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.metadata_.voted_for = vote;
    if (self.restoring_) return 0;
    try {
        self.metadata_store_.save(self.metadata_);
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::persist_term(raft_server_t*, void* user_data, raft_term_t term,
                                   raft_node_id_t vote) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.metadata_.term = term;
    self.metadata_.voted_for = vote;
    if (self.restoring_) return 0;
    try {
        self.metadata_store_.save(self.metadata_);
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_offer(raft_server_t*, void* user_data, raft_entry_t* entry,
                                const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    try {
        const auto* begin = static_cast<const std::byte*>(entry->data.buf);
        std::vector<std::byte> payload(begin, begin + entry->data.len);

        // Take ownership BEFORE anything else can read the entry, and repoint
        // the log's own copy at it. `entry` here is the stored entry, so this
        // replaces the caller's short-lived pointer with one whose lifetime the
        // node controls. Without it, applying after commit reads freed memory.
        auto& owned = self.log_payloads_[static_cast<std::uint64_t>(index)];
        owned = std::move(payload);
        entry->data.buf = owned.data();

        if (self.restoring_) return 0;

        // STAGE, DO NOT SYNC. This runs inside a Raft callback and therefore
        // inside the owner loop; the whole objective of the phase is that it
        // must not block on the disk. Durability follows in `flush_durability`.
        const auto framed = encode_wal_record({static_cast<std::uint64_t>(index),
                                               entry->term, entry->id, entry->type,
                                               owned});
        switch (self.arena_.stage(framed, static_cast<std::uint64_t>(index))) {
            case StageOutcome::staged:
                self.highest_staged_index_ =
                    std::max(self.highest_staged_index_,
                             static_cast<std::uint64_t>(index));
                return 0;
            case StageOutcome::backpressure:
            case StageOutcome::record_too_large:
                ++self.backpressure_refusals_;
                // Bounded refusal. Accepting an entry the log cannot make
                // durable would be exactly the overwrite the arena refuses, one
                // level up.
                self.callback_error_ = "WAL staging refused the entry";
                return RAFT_ERR_SHUTDOWN;
        }
        return RAFT_ERR_SHUTDOWN;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_poll(raft_server_t*, void* user_data, raft_entry_t*,
                               const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.log_payloads_.erase(static_cast<std::uint64_t>(index));
    self.polled_through_ =
        std::max(self.polled_through_, static_cast<std::uint64_t>(index));
    return 0;
}

int ReplicatedKernel::log_pop(raft_server_t*, void* user_data, raft_entry_t*,
                              const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.log_payloads_.erase(self.log_payloads_.lower_bound(
                                 static_cast<std::uint64_t>(index)),
                             self.log_payloads_.end());
    if (self.restoring_) return 0;
    try {
        // Both stores, or a restart reads back entries Raft has popped. The
        // arena's own staged bytes for those indices are dropped with the
        // blocks they live in.
        const bool resume_writer = self.writer_running_;
        self.stop_writer();
        self.flush_durability();
        self.wal_.truncate_from(static_cast<std::uint64_t>(index));
        self.segment_->truncate_from(static_cast<std::uint64_t>(index));
        self.arena_.rewind_durable_index(static_cast<std::uint64_t>(index - 1));
        self.highest_staged_index_ = static_cast<std::uint64_t>(index - 1);
        if (resume_writer) self.start_writer();
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_clear(raft_server_t*, void* user_data, raft_entry_t*,
                                raft_index_t) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    self.log_payloads_.clear();
    if (self.restoring_) return 0;
    try {
        const bool resume_writer = self.writer_running_;
        self.stop_writer();
        self.flush_durability();
        self.wal_.truncate_from(1);
        self.segment_->truncate_from(1);
        self.arena_.rewind_durable_index(0);
        self.highest_staged_index_ = 0;
        if (resume_writer) self.start_writer();
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t) {
    return -1;
}

raft_index_t ReplicatedKernel::durable_idx(raft_server_t*, void* user_data) {
    const auto& self = *static_cast<const ReplicatedKernel*>(user_data);
    return static_cast<raft_index_t>(self.arena_.durable_index());
}

}  // namespace velocita::v2
