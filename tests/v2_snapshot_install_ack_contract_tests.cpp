#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
#include <raft.h>
}

namespace {
constexpr raft_index_t kSnapshotIndex = 12;
constexpr raft_index_t kSuffixEnd = 20;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct ObservedState { raft_index_t durable_index{kSuffixEnd}; raft_index_t applied_count{}; std::uint64_t applied_id_sum{}; };
int persist_term(raft_server_t*, void*, raft_term_t, int) { return 0; }
int persist_vote(raft_server_t*, void*, int) { return 0; }
int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }
int log_pop(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }
int send_appendentries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*) { return 0; }
raft_index_t durable_index(raft_server_t*, void* data) { return static_cast<ObservedState*>(data)->durable_index; }
int apply(raft_server_t*, void* data, raft_entry_t* entry, raft_index_t) {
    auto& state = *static_cast<ObservedState*>(data);
    ++state.applied_count;
    state.applied_id_sum += static_cast<std::uint64_t>(entry->id);
    return 0;
}
raft_cbs_t callbacks() {
    raft_cbs_t value{};
    value.persist_term = &persist_term; value.persist_vote = &persist_vote;
    value.log_offer = &log_offer; value.log_pop = &log_pop;
    value.send_appendentries = &send_appendentries; value.durable_idx = &durable_index;
    value.applylog = &apply;
    return value;
}
void append(raft_server_t* raft, raft_term_t term, raft_index_t id) {
    raft_entry_t entry{};
    entry.term = term; entry.id = id; entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = const_cast<char*>("entry"); entry.data.len = 5;
    require(raft_append_entry(raft, &entry) == 0, "fixture append failed");
}

class HonestDivergenceFixture {
  public:
    HonestDivergenceFixture() : leader_callbacks_(callbacks()), follower_callbacks_(callbacks()) {
        leader_.reset(raft_new());
        follower_.reset(raft_new());
        require(leader_ && follower_, "raft_new failed");
        raft_set_callbacks(leader_.get(), &leader_callbacks_, &leader_state_);
        raft_set_callbacks(follower_.get(), &follower_callbacks_, &follower_state_);
        raft_add_node(leader_.get(), nullptr, 1, 1);
        leader_peer_ = raft_add_node(leader_.get(), nullptr, 2, 0);
        raft_add_node(leader_.get(), nullptr, 3, 0);
        follower_leader_ = raft_add_node(follower_.get(), nullptr, 1, 0);
        raft_add_node(follower_.get(), nullptr, 2, 1);
        raft_add_node(follower_.get(), nullptr, 3, 0);
        for (raft_index_t i = 1; i <= kSnapshotIndex; ++i) { append(leader_.get(), 5, i); append(follower_.get(), 5, i); }
        for (raft_index_t i = kSnapshotIndex + 1; i <= kSuffixEnd; ++i) { append(leader_.get(), 7, i); append(follower_.get(), 6, 1000 + i); }
        raft_set_commit_idx(leader_.get(), kSnapshotIndex);
        raft_set_commit_idx(follower_.get(), kSnapshotIndex);
        require(raft_apply_all(leader_.get()) == 0 && raft_apply_all(follower_.get()) == 0, "fixture prefix apply failed");
        raft_set_current_term(leader_.get(), 7);
        raft_become_leader(leader_.get());
        raft_set_current_term(follower_.get(), 7);
        require(raft_get_entry_from_idx(follower_.get(), 13)->term == 6 && raft_get_entry_from_idx(follower_.get(), 13)->id == 1013,
                "fixture lacks divergent old-term receiver suffix");
        require(raft_get_entry_from_idx(leader_.get(), 13)->term == 7 && raft_get_entry_from_idx(leader_.get(), 13)->id == 13,
                "fixture lacks current-term leader replacement suffix");
    }
    HonestDivergenceFixture(const HonestDivergenceFixture&) = delete;
    HonestDivergenceFixture& operator=(const HonestDivergenceFixture&) = delete;
    HonestDivergenceFixture(HonestDivergenceFixture&&) = delete;
    HonestDivergenceFixture& operator=(HonestDivergenceFixture&&) = delete;

    void leader_receives(msg_appendentries_response_t response) {
        require(raft_recv_appendentries_response(leader_.get(), leader_peer_, &response) == 0, "leader rejected acknowledgement");
        require(raft_apply_all(leader_.get()) == 0, "leader apply failed");
    }
    msg_appendentries_response_t replace_suffix_by_ordinary_appendentries() {
        raft_entry_t entries[kSuffixEnd - kSnapshotIndex]{};
        for (raft_index_t i = kSnapshotIndex + 1; i <= kSuffixEnd; ++i) {
            auto& entry = entries[i - kSnapshotIndex - 1];
            entry.term = 7; entry.id = i; entry.type = RAFT_LOGTYPE_NORMAL;
            entry.data.buf = const_cast<char*>("entry"); entry.data.len = 5;
        }
        msg_appendentries_t request{};
        request.term = 7; request.prev_log_idx = kSnapshotIndex; request.prev_log_term = 5;
        request.leader_commit = kSnapshotIndex;
        request.n_entries = kSuffixEnd - kSnapshotIndex;
        request.entries = entries;
        msg_appendentries_response_t response{};
        require(raft_recv_appendentries(follower_.get(), follower_leader_, &request, &response) == 0,
                "ordinary AppendEntries replacement failed");
        require(response.success && response.current_idx == kSuffixEnd, "ordinary AppendEntries did not verify suffix");
        for (raft_index_t i = kSnapshotIndex + 1; i <= kSuffixEnd; ++i) {
            const auto* entry = raft_get_entry_from_idx(follower_.get(), i);
            require(entry != nullptr && entry->term == 7 && entry->id == i,
                    "ordinary AppendEntries did not exactly replace divergent suffix");
        }
        return response;
    }
    void send_commit_heartbeat_to_follower() {
        msg_appendentries_t request{};
        request.term = 7;
        request.prev_log_idx = kSuffixEnd;
        request.prev_log_term = 7;
        request.leader_commit = kSuffixEnd;
        msg_appendentries_response_t response{};
        require(raft_recv_appendentries(follower_.get(), follower_leader_, &request, &response) == 0 && response.success,
                "commit heartbeat failed");
        require(raft_apply_all(follower_.get()) == 0, "follower apply failed");
    }
    raft_index_t match() const { return raft_node_get_match_idx(leader_peer_); }
    raft_index_t commit() const { return raft_get_commit_idx(leader_.get()); }
    raft_index_t applied() const { return raft_get_last_applied_idx(leader_.get()); }
    const ObservedState& state() const { return leader_state_; }
    const ObservedState& follower_state() const { return follower_state_; }
  private:
    raft_cbs_t leader_callbacks_{}; raft_cbs_t follower_callbacks_{};
    ObservedState leader_state_{}; ObservedState follower_state_{};
    std::unique_ptr<raft_server_t, decltype(&raft_free)> leader_{nullptr, &raft_free};
    std::unique_ptr<raft_server_t, decltype(&raft_free)> follower_{nullptr, &raft_free};
    raft_node_t* leader_peer_{}; raft_node_t* follower_leader_{};
};

msg_appendentries_response_t ack(raft_index_t reported) {
    msg_appendentries_response_t response{};
    response.term = 7; response.success = 1; response.current_idx = reported; response.first_idx = 1;
    return response;
}
void verify_contract(bool overclaim) {
    constexpr std::uint64_t sum12 = (kSnapshotIndex * (kSnapshotIndex + 1)) / 2;
    constexpr std::uint64_t sum20 = (kSuffixEnd * (kSuffixEnd + 1)) / 2;
    HonestDivergenceFixture fixture;
    fixture.leader_receives(ack(overclaim ? kSuffixEnd : kSnapshotIndex));
    require(fixture.commit() == kSnapshotIndex && fixture.applied() == kSnapshotIndex &&
                fixture.state().applied_count == kSnapshotIndex && fixture.state().applied_id_sum == sum12,
            "install ACK committed/applied unverified retained suffix: commit=" + std::to_string(fixture.commit()) +
                " applied=" + std::to_string(fixture.applied()));
    require(fixture.match() == kSnapshotIndex, "install ACK granted replication credit beyond S");

    const auto verified = fixture.replace_suffix_by_ordinary_appendentries();
    fixture.leader_receives(verified);
    require(fixture.match() == kSuffixEnd && fixture.commit() == kSuffixEnd && fixture.applied() == kSuffixEnd &&
                fixture.state().applied_count == kSuffixEnd && fixture.state().applied_id_sum == sum20,
            "verified suffix did not produce expected leader state");
    fixture.send_commit_heartbeat_to_follower();
    require(fixture.follower_state().applied_count == kSuffixEnd &&
                fixture.follower_state().applied_id_sum == sum20,
            "commit heartbeat did not produce exact follower state");
    fixture.leader_receives(ack(kSnapshotIndex));
    require(fixture.match() == kSuffixEnd && fixture.commit() == kSuffixEnd && fixture.applied() == kSuffixEnd &&
                fixture.state().applied_count == kSuffixEnd && fixture.state().applied_id_sum == sum20,
            "stale snapshot ACK regressed or reapplied verified state");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool overclaim = argc == 2 && std::strcmp(argv[1], "--overclaim") == 0;
        require(argc == 1 || overclaim, "usage: v2_snapshot_install_ack_contract_tests [--overclaim]");
        verify_contract(overclaim);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "snapshot install ACK contract failed: " << error.what() << '\n';
        return 1;
    }
}
