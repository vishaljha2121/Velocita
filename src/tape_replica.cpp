#include "velocita/tape/replica.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace velocita::tape {
namespace {

constexpr std::size_t kMaxEntriesPerAppend = 32;
constexpr std::size_t kMaxAppendBytes = 32 * 1024;

TapeReplica& self_of(void* user_data) { return *static_cast<TapeReplica*>(user_data); }

}

TapeReplica::TapeReplica(ReplicaConfig config, Send send, Applied applied)
    : config_(std::move(config)), send_(std::move(send)), applied_(std::move(applied)), wal_(config_.data_dir),
      state_(make_tape_state()) {
    server_ = raft_new();
    if (!server_) throw std::runtime_error("cannot create the Raft server");
    raft_cbs_t callbacks{};
    callbacks.send_requestvote = cb_send_requestvote;
    callbacks.send_appendentries = cb_send_appendentries;
    callbacks.applylog = cb_applylog;
    callbacks.persist_vote = cb_persist_vote;
    callbacks.persist_term = cb_persist_term;
    callbacks.log_offer = cb_log_offer;
    callbacks.log_poll = cb_log_poll;
    callbacks.log_pop = cb_log_pop;
    callbacks.log_get_node_id = cb_log_get_node_id;
    callbacks.durable_idx = cb_durable_idx;
    raft_set_callbacks(server_, &callbacks, this);
    for (const auto id : config_.members) raft_add_node(server_, nullptr, id, id == config_.node_id ? 1 : 0);
    raft_set_election_timeout(server_, config_.election_timeout_ms);
    raft_set_request_timeout(server_, config_.request_timeout_ms);
    raft_set_prevote(server_, 1);

    const auto metadata = wal_.load_metadata();
    if (metadata.term > 0) raft_set_current_term(server_, metadata.term);
    if (metadata.voted_for >= 0) raft_vote_for_nodeid(server_, metadata.voted_for);
    {
        const auto recovered = wal_.recover();
        for (const auto& record : recovered.records) {
            const auto bytes = recovered.bytes(record);
            raft_entry_t entry{};
            entry.term = record.term;
            entry.type = record.type;
            entry.data.buf = const_cast<std::byte*>(bytes.data());
            entry.data.len = static_cast<unsigned int>(bytes.size());
            if (raft_append_entry(server_, &entry) != 0) throw StorageError("cannot restore the Raft log");
        }
    }
    restoring_ = false;
}

TapeReplica::~TapeReplica() {
    if (server_) raft_free(server_);
}

void TapeReplica::flush() { wal_.sync(); }

void TapeReplica::tick(int elapsed_ms) {
    now_ms_ += static_cast<std::uint64_t>(elapsed_ms > 0 ? elapsed_ms : 0);
    flush();
    const int rc = raft_periodic(server_, elapsed_ms);
    if (rc != 0 && rc != RAFT_ERR_NOT_LEADER) throw std::runtime_error("raft_periodic failed: " + std::to_string(rc));
    flush();
}

bool TapeReplica::is_leader() const { return raft_is_leader(server_) != 0; }

ReplicaStatus TapeReplica::status() const {
    ReplicaStatus s;
    s.node_id = config_.node_id;
    s.leader_id = raft_get_current_leader(server_);
    s.leader = is_leader();
    s.term = raft_get_current_term(server_);
    s.current_index = static_cast<std::uint64_t>(raft_get_current_idx(server_));
    s.commit_index = static_cast<std::uint64_t>(raft_get_commit_idx(server_));
    s.applied_index = static_cast<std::uint64_t>(raft_get_last_applied_idx(server_));
    s.synced_index = wal_.synced_index();
    s.wal_bytes = wal_.bytes();
    return s;
}

ProposeStatus TapeReplica::propose(std::span<const std::byte> entry_bytes) {
    if (!is_leader()) return ProposeStatus::not_leader;
    if (entry_bytes.empty() || entry_bytes.size() > kMaxEntryBytes) return ProposeStatus::refused;
    msg_entry_t entry{};
    entry.term = raft_get_current_term(server_);
    entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = const_cast<std::byte*>(entry_bytes.data());
    entry.data.len = static_cast<unsigned int>(entry_bytes.size());
    msg_entry_response_t response{};
    const int rc = raft_recv_entry(server_, &entry, &response);
    if (rc == RAFT_ERR_NOT_LEADER) return ProposeStatus::not_leader;
    if (rc != 0) return ProposeStatus::refused;
    return ProposeStatus::proposed;
}

void TapeReplica::receive(const PeerMessage& message) {
    raft_node_t* sender = raft_get_node(server_, message.sender_id);
    if (!sender || message.recipient_id != config_.node_id) return;
    int rc = 0;
    if (const auto* v = std::get_if<RequestVote>(&message.payload)) {
        msg_requestvote_t request{v->term, v->candidate_id, v->last_log_index, v->last_log_term, v->prevote ? 1 : 0};
        msg_requestvote_response_t response{};
        rc = raft_recv_requestvote(server_, sender, &request, &response);
        flush();
        if (rc == 0)
            send_({config_.node_id, message.sender_id,
                   RequestVoteResponse{response.term, response.vote_granted != 0, response.prevote != 0}});
    } else if (const auto* v = std::get_if<RequestVoteResponse>(&message.payload)) {
        msg_requestvote_response_t response{v->term, v->vote_granted ? 1 : 0, v->prevote ? 1 : 0};
        rc = raft_recv_requestvote_response(server_, sender, &response);

    } else if (const auto* v = std::get_if<AppendEntries>(&message.payload)) {
        std::vector<msg_entry_t> entries(v->entries.size());
        for (std::size_t i = 0; i < v->entries.size(); ++i) {
            entries[i].term = v->entries[i].term;
            entries[i].id = v->entries[i].id;
            entries[i].type = v->entries[i].type;
            entries[i].data.buf = const_cast<std::byte*>(v->entries[i].payload.data());
            entries[i].data.len = static_cast<unsigned int>(v->entries[i].payload.size());
        }
        msg_appendentries_t request{};
        request.term = v->term;
        request.prev_log_idx = v->previous_log_index;
        request.prev_log_term = v->previous_log_term;
        request.leader_commit = v->leader_commit;
        request.n_entries = static_cast<int>(entries.size());
        request.entries = entries.data();
        msg_appendentries_response_t response{};
        rc = raft_recv_appendentries(server_, sender, &request, &response);
        flush();
        if (rc == 0) {
            auto reported = response.current_idx;
            const auto durable = static_cast<raft_index_t>(wal_.synced_index());
            if (response.success != 0 && reported > durable) reported = durable;
            send_({config_.node_id, message.sender_id,
                   AppendEntriesResponse{response.term, response.success != 0, reported, response.first_idx}});
        }
    } else if (const auto* v = std::get_if<AppendEntriesResponse>(&message.payload)) {
        msg_appendentries_response_t response{};
        response.term = v->term;
        response.success = v->success ? 1 : 0;
        response.current_idx = v->current_index;
        response.first_idx = v->first_index;
        if (const auto it = in_flight_.find(message.sender_id); it != in_flight_.end()) {
            auto& flight = it->second;
            if (!v->success) {
                flight.sent_through = 0;
            } else if (static_cast<std::uint64_t>(v->current_index) > flight.acknowledged) {
                flight.acknowledged = static_cast<std::uint64_t>(v->current_index);
                flight.wait_ms = static_cast<std::uint64_t>(config_.resend_after_ms);
            }
        }
        rc = raft_recv_appendentries_response(server_, sender, &response);
    }
    if (rc != 0 && rc != RAFT_ERR_NOT_LEADER) throw std::runtime_error("Raft message handling failed: " + std::to_string(rc));
}

std::uint64_t TapeReplica::highest_request_in_log(std::uint8_t recorder, std::uint64_t session) const {
    std::uint64_t highest = 0;
    const auto& s = state_->sessions[recorder - 1];
    if (s.started && s.session == session) highest = s.last_request;
    for (const auto& [index, key] : requests_by_index_)
        if (std::get<0>(key) == recorder && std::get<1>(key) == session && std::get<2>(key) > highest)
            highest = std::get<2>(key);
    return highest;
}

std::uint64_t TapeReplica::highest_tick_in_log() const {
    std::uint64_t highest = state_->last_tick;
    for (const auto& [index, number] : ticks_by_index_)
        if (number > highest) highest = number;
    return highest;
}

void TapeReplica::index_entry(std::uint64_t index, std::span<const std::byte> bytes) {
    const auto entry = decode_entry(bytes);
    if (!entry) return;
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, Observation> || std::is_same_v<T, ObservationPart> ||
                          std::is_same_v<T, RecorderEvent>) {
                const RequestKey key{e.header.recorder, e.header.session, e.header.request_seq};
                requests_by_index_[index] = key;
                in_log_.insert(key);
            } else if constexpr (std::is_same_v<T, Tick>) {
                ticks_by_index_[index] = e.number;
            } else if constexpr (std::is_same_v<T, HeadSignature>) {
                heads_by_index_[index] = {e.tick, e.voter};
                head_sigs_in_log_.insert({e.tick, e.voter});
            }
        },
        *entry);
}

void TapeReplica::unindex_entry(std::uint64_t index) {
    if (const auto it = requests_by_index_.find(index); it != requests_by_index_.end()) {
        in_log_.erase(it->second);
        requests_by_index_.erase(it);
    }
    ticks_by_index_.erase(index);
    if (const auto it = heads_by_index_.find(index); it != heads_by_index_.end()) {
        head_sigs_in_log_.erase(it->second);
        heads_by_index_.erase(it);
    }
}

int TapeReplica::cb_send_requestvote(raft_server_t*, void* user_data, raft_node_t* node, msg_requestvote_t* m) {
    auto& self = self_of(user_data);
    self.send_({self.config_.node_id, raft_node_get_id(node),
                RequestVote{m->term, m->candidate_id, m->last_log_idx, m->last_log_term, m->prevote != 0}});
    return 0;
}

int TapeReplica::cb_send_appendentries(raft_server_t*, void* user_data, raft_node_t* node, msg_appendentries_t* m) {
    auto& self = self_of(user_data);
    const auto id = raft_node_get_id(node);
    const auto& cfg = self.config_;
    auto& flight = self.in_flight_[id];
    if (flight.term != m->term) {
        const auto tokens = flight.tokens;
        const auto tokens_at = flight.tokens_at_ms;
        flight = {};
        flight.term = m->term;
        flight.tokens = tokens;
        flight.tokens_at_ms = tokens_at;
    }
    if (flight.wait_ms == 0) flight.wait_ms = static_cast<std::uint64_t>(cfg.resend_after_ms);
    const auto burst = static_cast<double>(cfg.append_burst_bytes);
    if (flight.tokens < 0) flight.tokens = burst;
    flight.tokens = std::min(burst, flight.tokens + static_cast<double>(cfg.append_bytes_per_second) *
                                                        static_cast<double>(self.now_ms_ - flight.tokens_at_ms) / 1000.0);
    flight.tokens_at_ms = self.now_ms_;
    const auto first = static_cast<std::uint64_t>(m->prev_log_idx) + 1;
    int skip = 0;
    if (flight.sent_through >= first) {
        if (self.now_ms_ - flight.sent_at_ms < flight.wait_ms)
            skip = static_cast<int>(std::min<std::uint64_t>(flight.sent_through - first + 1, static_cast<std::uint64_t>(m->n_entries)));
        else
            flight.wait_ms = std::min<std::uint64_t>(flight.wait_ms * 2, static_cast<std::uint64_t>(cfg.resend_after_max_ms));
    }
    AppendEntries out;
    out.term = m->term;
    out.previous_log_index = m->prev_log_idx + skip;
    out.previous_log_term = skip > 0 ? m->entries[skip - 1].term : m->prev_log_term;
    out.leader_commit = m->leader_commit;
    std::size_t total = 0;
    for (int i = skip; i < m->n_entries && out.entries.size() < kMaxEntriesPerAppend; ++i) {
        const auto& e = m->entries[i];
        if (!out.entries.empty() && total + e.data.len > kMaxAppendBytes) break;
        if (static_cast<double>(total + e.data.len) > flight.tokens) break;
        const auto* p = static_cast<const std::byte*>(e.data.buf);
        out.entries.push_back({e.term, e.id, e.type, std::vector<std::byte>(p, p + e.data.len)});
        total += e.data.len;
    }
    if (!out.entries.empty()) {
        flight.sent_through = std::max<std::uint64_t>(flight.sent_through,
                                                      static_cast<std::uint64_t>(out.previous_log_index) + out.entries.size());
        flight.sent_at_ms = self.now_ms_;
        flight.tokens -= static_cast<double>(total);
    }
    self.send_({self.config_.node_id, id, std::move(out)});
    return 0;
}

int TapeReplica::cb_applylog(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t index) {
    auto& self = self_of(user_data);
    const auto bytes = std::span(static_cast<const std::byte*>(entry->data.buf), entry->data.len);
    const auto result = apply_entry(*self.state_, self.config_.tape, static_cast<std::uint64_t>(index), bytes);
    self.unindex_entry(static_cast<std::uint64_t>(index));
    if (self.applied_) self.applied_(static_cast<std::uint64_t>(index), bytes, result);
    return 0;
}

int TapeReplica::cb_persist_vote(raft_server_t* server, void* user_data, raft_node_id_t vote) {
    auto& self = self_of(user_data);
    if (self.restoring_) return 0;
    try {
        self.wal_.store_metadata({raft_get_current_term(server), vote});
    } catch (const std::exception&) {
        return RAFT_ERR_SHUTDOWN;
    }
    return 0;
}

int TapeReplica::cb_persist_term(raft_server_t*, void* user_data, raft_term_t term, raft_node_id_t vote) {
    auto& self = self_of(user_data);
    if (self.restoring_) return 0;
    try {
        self.wal_.store_metadata({term, vote});
    } catch (const std::exception&) {
        return RAFT_ERR_SHUTDOWN;
    }
    return 0;
}

int TapeReplica::cb_log_offer(raft_server_t*, void* user_data, raft_entry_t* entry, raft_index_t index) {
    auto& self = self_of(user_data);
    const auto at = static_cast<std::uint64_t>(index);
    if (at != self.base_index_ + self.payloads_.size()) return RAFT_ERR_SHUTDOWN;
    const auto* p = static_cast<const std::byte*>(entry->data.buf);
    self.payloads_.emplace_back(p, p + entry->data.len);
    entry->data.buf = self.payloads_.back().data();
    try {
        if (!self.restoring_) self.wal_.append(at, entry->term, entry->type, self.payloads_.back());
    } catch (const std::exception&) {
        self.payloads_.pop_back();
        return RAFT_ERR_SHUTDOWN;
    }
    self.index_entry(at, self.payloads_.back());
    return 0;
}

int TapeReplica::cb_log_poll(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }

int TapeReplica::cb_log_pop(raft_server_t*, void* user_data, raft_entry_t*, raft_index_t index) {
    auto& self = self_of(user_data);
    const auto at = static_cast<std::uint64_t>(index);
    if (self.payloads_.empty() || at != self.base_index_ + self.payloads_.size() - 1) return RAFT_ERR_SHUTDOWN;
    self.unindex_entry(at);
    self.payloads_.pop_back();
    try {
        if (!self.restoring_) self.wal_.truncate_from(at);
    } catch (const std::exception&) {
        return RAFT_ERR_SHUTDOWN;
    }
    return 0;
}

int TapeReplica::cb_log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return -1; }

raft_index_t TapeReplica::cb_durable_idx(raft_server_t*, void* user_data) {
    return static_cast<raft_index_t>(self_of(user_data).wal_.synced_index());
}

}
