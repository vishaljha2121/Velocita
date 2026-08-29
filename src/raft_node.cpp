#include "velocita/raft_node.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>

namespace velocita {
namespace {

std::string_view role_name(raft_server_t* server) {
    switch (raft_get_state(server)) {
        case RAFT_STATE_FOLLOWER: return "follower";
        case RAFT_STATE_CANDIDATE: return "candidate";
        case RAFT_STATE_LEADER: return "leader";
        default: return "none";
    }
}

Response make_response(const ResponseStatus status, const std::int32_t leader_id,
                       const std::uint64_t request_id, const std::int64_t value,
                       const std::int64_t term, const std::int64_t index) {
    Response response;
    response.status = status;
    response.leader_id = leader_id;
    response.request_id = request_id;
    response.value = value;
    response.term = term;
    response.index = index;
    return response;
}

}  // namespace

RaftNode::RaftNode(std::int32_t node_id, std::filesystem::path data_dir,
                   std::vector<std::int32_t> members, bool enable_trace)
    : node_id_(node_id),
      members_(std::move(members)),
      owner_thread_(std::this_thread::get_id()),
      metadata_store_(data_dir),
      wal_(std::move(data_dir)),
      metadata_(metadata_store_.load()),
      server_(raft_new())
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
      , benchmark_trace_(std::make_unique<BenchmarkTraceTable>(65'536))
#endif
      {
    if (server_ == nullptr) {
        throw std::runtime_error("raft_new failed");
    }

    raft_cbs_t table{};
    table.send_requestvote = &RaftNode::send_request_vote;
    table.send_appendentries = &RaftNode::send_append_entries;
    table.applylog = &RaftNode::apply_log;
    table.persist_vote = &RaftNode::persist_vote;
    table.persist_term = &RaftNode::persist_term;
    table.log_offer = &RaftNode::log_offer;
    table.log_poll = &RaftNode::log_poll;
    table.log_pop = &RaftNode::log_pop;
    table.log_clear = &RaftNode::log_clear;
    table.log_get_node_id = &RaftNode::log_get_node_id;
    table.log = enable_trace ? &RaftNode::log_message : nullptr;
    raft_set_callbacks(server_, &table, this);
    if (members_.empty()) members_.push_back(node_id_);
    std::sort(members_.begin(), members_.end());
    members_.erase(std::unique(members_.begin(), members_.end()), members_.end());
    if (!std::binary_search(members_.begin(), members_.end(), node_id_)) {
        throw std::runtime_error("cluster membership does not include the local node");
    }
    for (const auto member : members_) {
        if (member <= 0 || raft_add_node(server_, nullptr, member, member == node_id_) == nullptr) {
            throw std::runtime_error("failed to add Raft member " + std::to_string(member));
        }
    }

    if (raft_set_current_term(server_, metadata_.term) != 0) {
        throw std::runtime_error("failed to restore Raft term");
    }
    if (metadata_.voted_for >= 0 && raft_vote_for_nodeid(server_, metadata_.voted_for) != 0) {
        throw std::runtime_error("failed to restore Raft vote");
    }

    const auto records = wal_.read_all();
    for (const auto& record : records) {
        raft_entry_t entry{};
        entry.term = record.term;
        entry.id = record.entry_id;
        entry.type = record.entry_type;
        entry.data.buf = const_cast<std::byte*>(record.payload.data());
        entry.data.len = static_cast<unsigned int>(record.payload.size());
        if (raft_append_entry(server_, &entry) != 0) {
            throw std::runtime_error("failed to restore WAL entry " + std::to_string(record.index));
        }
    }
    if (metadata_.commit_index < 0 || metadata_.commit_index > raft_get_current_idx(server_)) {
        throw std::runtime_error("metadata commit index is outside the WAL");
    }
    raft_set_commit_idx(server_, metadata_.commit_index);
    if (raft_apply_all(server_) != 0) {
        throw std::runtime_error("failed to replay committed WAL entries");
    }
    restoring_ = false;
}

RaftNode::~RaftNode() {
    if (server_ != nullptr) {
        raft_free(server_);
    }
}

void RaftNode::assert_owner() const {
    if (std::this_thread::get_id() != owner_thread_) {
        throw std::logic_error("RaftNode accessed from a non-owner thread");
    }
}

void RaftNode::tick(int elapsed_ms) {
    assert_owner();
    if (const int result = raft_periodic(server_, elapsed_ms); result != 0) {
        throw std::runtime_error("raft_periodic failed: " + std::to_string(result));
    }
    persist_commit_index();
    if (const int result = raft_apply_all(server_); result != 0) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(result));
    }
}

bool RaftNode::is_leader() const {
    assert_owner();
    return raft_is_leader(server_) != 0;
}

std::optional<Response> RaftNode::propose(const Command& command) {
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    return propose(command, benchmark_monotonic_nanoseconds());
}

std::optional<Response> RaftNode::propose(const Command& command, const std::uint64_t ingress_ns) {
#endif
    assert_owner();
    if (!is_leader()) {
        return make_response(ResponseStatus::not_leader, raft_get_current_leader(server_),
                             command.request_id, fsm_.value(),
                             raft_get_current_term(server_), raft_get_current_idx(server_));
    }
    if (const auto prior = fsm_.result_for(command.request_id)) {
        return make_response(ResponseStatus::ok, node_id_, command.request_id, *prior,
                             raft_get_current_term(server_), raft_get_commit_idx(server_));
    }
    if (!pending_requests_.insert(command.request_id).second) return std::nullopt;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    (void)benchmark_trace_->begin(command.request_id, ingress_ns,
                                  benchmark_monotonic_nanoseconds());
#endif

    auto payload = encode_command(command);
    msg_entry_t entry{};
    entry.id = static_cast<raft_entry_id_t>(command.request_id & 0x7FFFFFFFU);
    entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = payload.data();
    entry.data.len = static_cast<unsigned int>(payload.size());
    msg_entry_response_t raft_response{};
    const int result = raft_recv_entry(server_, &entry, &raft_response);
    if (result == RAFT_ERR_NOT_LEADER) {
        pending_requests_.erase(command.request_id);
        return make_response(ResponseStatus::not_leader, raft_get_current_leader(server_),
                             command.request_id, fsm_.value(),
                             raft_get_current_term(server_), raft_get_current_idx(server_));
    }
    if (result != 0) {
        pending_requests_.erase(command.request_id);
        throw std::runtime_error("raft_recv_entry failed: " + std::to_string(result) + " " + callback_error_);
    }
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    benchmark_trace_->record_admitted(command.request_id, raft_response.idx,
                                      benchmark_monotonic_nanoseconds());
#endif

    persist_commit_index();
    if (const int apply_result = raft_apply_all(server_); apply_result != 0) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(apply_result) + " " + callback_error_);
    }
    return std::nullopt;
}

Response RaftNode::submit(const Command& command) {
    if (auto immediate = propose(command)) return *immediate;
    for (auto it = completed_responses_.begin(); it != completed_responses_.end(); ++it) {
        if (it->request_id == command.request_id) {
            const auto response = *it;
            completed_responses_.erase(it);
            return response;
        }
    }
    throw std::logic_error("submit cannot synchronously complete in a multi-node quorum; use propose");
}

NodeStatus RaftNode::status() const {
    assert_owner();
    return {node_id_, raft_get_current_leader(server_), role_name(server_), raft_get_current_term(server_), 1,
            raft_get_current_idx(server_), raft_get_commit_idx(server_),
            raft_get_last_applied_idx(server_), fsm_.value()};
}

std::optional<PeerMessage> RaftNode::take_outbound_message() {
    assert_owner();
    if (outbound_messages_.empty()) return std::nullopt;
    auto result = std::move(outbound_messages_.front());
    outbound_messages_.pop_front();
    return result;
}

std::optional<Response> RaftNode::take_completed_response() {
    assert_owner();
    if (completed_responses_.empty()) return std::nullopt;
    auto result = completed_responses_.front();
    completed_responses_.pop_front();
    return result;
}

#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
void RaftNode::persist_commit_index(const std::uint64_t quorum_append_ns) {
#else
void RaftNode::persist_commit_index() {
#endif
    const auto commit = raft_get_commit_idx(server_);
    if (commit == metadata_.commit_index) {
        return;
    }
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    const auto prior_commit = metadata_.commit_index;
#endif
    metadata_.term = raft_get_current_term(server_);
    metadata_.voted_for = raft_get_voted_for(server_);
    metadata_.commit_index = commit;
    metadata_store_.save(metadata_);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    benchmark_trace_->record_committed(prior_commit + 1, commit,
                                       benchmark_monotonic_nanoseconds(), quorum_append_ns);
#endif
}

void RaftNode::receive_peer(const PeerMessage& message) {
    assert_owner();
    if (message.recipient_id != node_id_ || message.sender_id == node_id_) {
        throw std::runtime_error("peer message has invalid route");
    }
    auto* sender = raft_get_node(server_, message.sender_id);
    if (sender == nullptr) throw std::runtime_error("peer message came from a non-member");

    int result = 0;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    std::uint64_t quorum_append_ns = 0;
#endif
    if (const auto* value = std::get_if<RequestVote>(&message.payload)) {
        if (value->candidate_id != message.sender_id) throw std::runtime_error("vote candidate does not match sender");
        msg_requestvote_t request{value->term, value->candidate_id, value->last_log_index, value->last_log_term};
        msg_requestvote_response_t response{};
        result = raft_recv_requestvote(server_, sender, &request, &response);
        outbound_messages_.push_back({node_id_, message.sender_id,
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
        outbound_messages_.push_back({node_id_, message.sender_id,
                                      AppendEntriesResponse{response.term, response.success != 0,
                                                            response.current_idx, response.first_idx}});
    } else if (const auto* value = std::get_if<AppendEntriesResponse>(&message.payload)) {
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
        quorum_append_ns = benchmark_peer_round_trip(
            message.sender_id, value->current_index, benchmark_monotonic_nanoseconds());
#endif
        msg_appendentries_response_t response{};
        response.term = value->term;
        response.success = value->success ? 1 : 0;
        response.current_idx = value->current_index;
        response.first_idx = value->first_index;
        result = raft_recv_appendentries_response(server_, sender, &response);
        if (result == RAFT_ERR_NOT_LEADER) result = 0;
    }
    if (result != 0) throw std::runtime_error("processing peer message failed: " + std::to_string(result) + " " + callback_error_);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    persist_commit_index(quorum_append_ns);
#else
    persist_commit_index();
#endif
    if (const int apply_result = raft_apply_all(server_); apply_result != 0) {
        throw std::runtime_error("applying peer commit failed: " + std::to_string(apply_result) + " " + callback_error_);
    }
}

int RaftNode::send_request_vote(raft_server_t*, void* user_data, raft_node_t* node, msg_requestvote_t* message) {
    auto& self = *static_cast<RaftNode*>(user_data);
    self.outbound_messages_.push_back({self.node_id_, raft_node_get_id(node),
        RequestVote{message->term, message->candidate_id, message->last_log_idx, message->last_log_term}});
    return 0;
}

int RaftNode::send_append_entries(raft_server_t*, void* user_data, raft_node_t* node, msg_appendentries_t* message) {
    auto& self = *static_cast<RaftNode*>(user_data);
    AppendEntries append;
    append.term = message->term;
    append.previous_log_index = message->prev_log_idx;
    append.previous_log_term = message->prev_log_term;
    append.leader_commit = message->leader_commit;
    append.entries.reserve(static_cast<std::size_t>(message->n_entries));
    for (int i = 0; i < message->n_entries; ++i) {
        const auto& entry = message->entries[i];
        const auto* begin = static_cast<const std::byte*>(entry.data.buf);
        append.entries.push_back({entry.term, entry.id, entry.type,
                                  std::vector<std::byte>(begin, begin + entry.data.len)});
    }
    self.outbound_messages_.push_back({self.node_id_, raft_node_get_id(node), std::move(append)});
    return 0;
}

int RaftNode::apply_log(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t index) {
    auto& self = *static_cast<RaftNode*>(user_data);
    try {
        const auto bytes = std::span<const std::byte>(static_cast<const std::byte*>(entry->data.buf), entry->data.len);
        const auto command = decode_command(bytes);
        const auto value = self.fsm_.apply(command);
        if (!self.restoring_ && self.pending_requests_.erase(command.request_id) != 0) {
            Response response = make_response(ResponseStatus::ok, self.node_id_,
                                              command.request_id, value, entry->term, index);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
            if (const auto timing = self.benchmark_trace_->complete(
                    command.request_id, benchmark_monotonic_nanoseconds())) {
                response.benchmark = *timing;
            }
#endif
            self.completed_responses_.push_back(response);
        }
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int RaftNode::persist_vote(raft_server_t*, void* user_data, raft_node_id_t vote) {
    auto& self = *static_cast<RaftNode*>(user_data);
    self.metadata_.voted_for = vote;
    if (self.restoring_) {
        return 0;
    }
    try {
        self.metadata_store_.save(self.metadata_);
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int RaftNode::persist_term(raft_server_t*, void* user_data, raft_term_t term, raft_node_id_t vote) {
    auto& self = *static_cast<RaftNode*>(user_data);
    self.metadata_.term = term;
    self.metadata_.voted_for = vote;
    if (self.restoring_) {
        return 0;
    }
    try {
        self.metadata_store_.save(self.metadata_);
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int RaftNode::log_offer(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t index) {
    auto& self = *static_cast<RaftNode*>(user_data);
    auto* owned = new (std::nothrow) std::byte[entry->data.len];
    if (owned == nullptr && entry->data.len != 0) {
        self.callback_error_ = "entry payload allocation failed";
        return RAFT_ERR_NOMEM;
    }
    if (entry->data.len != 0) {
        std::memcpy(owned, entry->data.buf, entry->data.len);
    }
    entry->data.buf = owned;

    if (self.restoring_) {
        return 0;
    }
    try {
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
        std::uint64_t request_id = 0;
        try {
            const auto bytes = std::span<const std::byte>(
                static_cast<const std::byte*>(owned), entry->data.len);
            request_id = decode_command(bytes).request_id;
        } catch (...) {
            request_id = 0;
        }
        const auto wal_begin = benchmark_monotonic_nanoseconds();
#endif
        self.wal_.append({static_cast<std::uint64_t>(index), entry->term, entry->id, entry->type,
                          std::vector<std::byte>(owned, owned + entry->data.len)});
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
        if (request_id != 0) {
            self.benchmark_trace_->record_wal(
                request_id, benchmark_monotonic_nanoseconds() - wal_begin);
        }
#endif
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        delete[] owned;
        entry->data.buf = nullptr;
        entry->data.len = 0;
        return RAFT_ERR_SHUTDOWN;
    }
}

int RaftNode::log_poll(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t) {
    auto& self = *static_cast<RaftNode*>(user_data);
    self.callback_error_ = "log compaction requires a snapshot and is disabled in v1";
    (void)entry;
    return RAFT_ERR_SHUTDOWN;
}

int RaftNode::log_pop(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t index) {
    auto& self = *static_cast<RaftNode*>(user_data);
    try {
        if (!self.restoring_) self.wal_.truncate_from(static_cast<std::uint64_t>(index));
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
    delete[] static_cast<std::byte*>(entry->data.buf);
    entry->data.buf = nullptr;
    entry->data.len = 0;
    return 0;
}

int RaftNode::log_clear(raft_server_t*, void*, raft_entry_t* entry, raft_index_t) {
    delete[] static_cast<std::byte*>(entry->data.buf);
    entry->data.buf = nullptr;
    entry->data.len = 0;
    return 0;
}

int RaftNode::log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t) {
    return -1;
}

void RaftNode::log_message(raft_server_t*, raft_node_t*, void*, const char* message) {
    std::clog << "raft: " << message << '\n';
}

#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
void RaftNode::benchmark_record_peer_offer(const std::int32_t peer_id,
                                           const std::int64_t last_index,
                                           const std::uint64_t offered_ns) noexcept {
    if (last_index > 0) benchmark_peer_offers_[peer_id] = {last_index, offered_ns};
}

std::uint64_t RaftNode::benchmark_peer_round_trip(
    const std::int32_t peer_id, const std::int64_t current_index,
    const std::uint64_t received_ns) const noexcept {
    const auto found = benchmark_peer_offers_.find(peer_id);
    if (found == benchmark_peer_offers_.end() || found->second.last_index != current_index ||
        received_ns < found->second.offered_ns) {
        return 0;
    }
    return received_ns - found->second.offered_ns;
}
#endif

}  // namespace velocita
