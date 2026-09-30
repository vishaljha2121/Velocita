#include <cstddef>
extern "C" {
#include <raft.h>
}
#include <stdexcept>

namespace {

struct Durability {
    raft_index_t index = 0;
    int before_apply_calls = 0;
    int apply_calls = 0;
    raft_index_t before_apply_index = 0;
    int before_apply_result = 0;
};

int persist_term(raft_server_t*, void*, raft_term_t, int) { return 0; }
int persist_vote(raft_server_t*, void*, int) { return 0; }
int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }
int applylog(raft_server_t*, void* data, raft_entry_t*, raft_index_t) {
    ++static_cast<Durability*>(data)->apply_calls;
    return 0;
}
int before_apply(raft_server_t*, void* data, raft_entry_t*, raft_index_t index) {
    auto& state = *static_cast<Durability*>(data);
    ++state.before_apply_calls;
    state.before_apply_index = index;
    return state.before_apply_result;
}
int send_appendentries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*) {
    return 0;
}
raft_index_t durable_idx(raft_server_t*, void* data) {
    return static_cast<Durability*>(data)->index;
}

raft_cbs_t callbacks() {
    raft_cbs_t cbs{};
    cbs.persist_term = &persist_term;
    cbs.persist_vote = &persist_vote;
    cbs.log_offer = &log_offer;
    cbs.applylog = &applylog;
    cbs.before_apply = &before_apply;
    cbs.send_appendentries = &send_appendentries;
    cbs.durable_idx = &durable_idx;
    return cbs;
}

void require(bool condition, const char* message);
void append_one(raft_server_t* raft);

raft_server_t* single_node(Durability& state, raft_cbs_t& cbs) {
    raft_server_t* raft = raft_new();
    require(raft != nullptr, "raft_new failed");
    raft_set_callbacks(raft, &cbs, &state);
    raft_add_node(raft, nullptr, 1, 1);
    raft_set_current_term(raft, 1);
    raft_become_leader(raft);
    return raft;
}

void test_before_apply_refusal_preserves_index_and_skips_applylog() {
    Durability state;
    constexpr int refusal = -77;
    state.before_apply_result = refusal;
    raft_cbs_t cbs = callbacks();
    raft_server_t* raft = single_node(state, cbs);
    append_one(raft);
    raft_set_commit_idx(raft, 1);

    require(raft_apply_all(raft) == refusal,
            "before-apply refusal was not propagated");
    require(state.before_apply_calls == 1, "before-apply callback did not run exactly once");
    require(state.before_apply_index == 1, "before-apply callback received wrong index");
    require(state.apply_calls == 0, "applylog ran after before-apply refusal");
    require(raft_get_last_applied_idx(raft) == 0,
            "before-apply refusal advanced last_applied_idx");
    raft_free(raft);
}

void test_periodic_auto_apply_honors_before_apply() {
    Durability state;
    state.before_apply_result = RAFT_ERR_SHUTDOWN;
    raft_cbs_t cbs = callbacks();
    raft_server_t* raft = single_node(state, cbs);
    append_one(raft);
    state.index = 1;

    require(raft_periodic(raft, 0) == RAFT_ERR_SHUTDOWN,
            "periodic did not propagate before-apply refusal");
    require(raft_get_commit_idx(raft) == 1, "periodic test never committed the entry");
    require(state.before_apply_calls == 1, "periodic did not reach before-apply callback");
    require(state.apply_calls == 0, "periodic called applylog after refusal");
    require(raft_get_last_applied_idx(raft) == 0,
            "periodic refusal advanced last_applied_idx");
    raft_free(raft);
}

void test_begin_snapshot_auto_apply_honors_before_apply() {
    Durability state;
    raft_cbs_t cbs = callbacks();
    raft_server_t* raft = single_node(state, cbs);
    append_one(raft);
    append_one(raft);
    raft_set_commit_idx(raft, 2);
    state.before_apply_result = RAFT_ERR_SHUTDOWN;

    require(raft_get_current_idx(raft) == 2 && raft_get_commit_idx(raft) == 2,
            "snapshot test did not create a committed two-entry log");
    require(raft_begin_snapshot(raft, 0) == RAFT_ERR_SHUTDOWN,
            "begin_snapshot did not propagate before-apply refusal");
    require(state.before_apply_calls == 1, "begin_snapshot did not reach before-apply callback");
    require(state.before_apply_index == 1, "begin_snapshot guarded the wrong entry");
    require(state.apply_calls == 0, "begin_snapshot called applylog after refusal");
    require(raft_get_last_applied_idx(raft) == 0,
            "begin_snapshot refusal advanced last_applied_idx");
    raft_free(raft);
}

void test_null_before_apply_preserves_application_behavior() {
    Durability state;
    raft_cbs_t cbs = callbacks();
    cbs.before_apply = nullptr;
    raft_server_t* raft = single_node(state, cbs);
    append_one(raft);
    raft_set_commit_idx(raft, 1);

    require(raft_apply_all(raft) == 0, "NULL before-apply callback refused application");
    require(state.before_apply_calls == 0, "NULL before-apply callback somehow ran");
    require(state.apply_calls == 1, "NULL before-apply callback skipped applylog");
    require(raft_get_last_applied_idx(raft) == 1,
            "NULL before-apply callback changed applied-index behavior");
    raft_free(raft);
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void append_one(raft_server_t* raft) {
    msg_entry_t entry{};
    entry.id = 7;
    entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = const_cast<char*>("entry");
    entry.data.len = 5;
    msg_entry_response_t response{};
    require(raft_recv_entry(raft, &entry, &response) == 0,
            "raft_recv_entry failed");
}

void test_peer_ack_before_local_flush_is_reconsidered() {
    Durability durability;
    raft_cbs_t cbs = callbacks();
    raft_server_t* raft = raft_new();
    require(raft != nullptr, "raft_new failed");
    raft_set_callbacks(raft, &cbs, &durability);
    raft_add_node(raft, nullptr, 1, 1);
    raft_node_t* peer = raft_add_node(raft, nullptr, 2, 0);
    raft_add_node(raft, nullptr, 3, 0);
    raft_set_current_term(raft, 1);
    raft_become_leader(raft);

    append_one(raft);
    msg_appendentries_response_t response{};
    response.term = 1;
    response.success = 1;
    response.current_idx = 1;
    response.first_idx = 1;
    require(raft_recv_appendentries_response(raft, peer, &response) == 0,
            "peer response failed");
    require(raft_get_commit_idx(raft) == 0,
            "entry committed before local durability");

    durability.index = 1;
    require(raft_periodic(raft, 0) == 0, "periodic failed");
    require(raft_get_commit_idx(raft) == 1,
            "durability advance did not reconsider peer quorum");
    raft_free(raft);
}

void test_null_callback_preserves_synchronous_behavior() {
    raft_cbs_t cbs = callbacks();
    cbs.durable_idx = nullptr;
    cbs.before_apply = nullptr;
    cbs.applylog = nullptr;
    raft_server_t* raft = raft_new();
    require(raft != nullptr, "raft_new failed");
    raft_set_callbacks(raft, &cbs, nullptr);
    raft_add_node(raft, nullptr, 1, 1);
    raft_node_t* peer = raft_add_node(raft, nullptr, 2, 0);
    raft_add_node(raft, nullptr, 3, 0);
    raft_set_current_term(raft, 1);
    raft_become_leader(raft);
    append_one(raft);
    msg_appendentries_response_t response{};
    response.term = 1;
    response.success = 1;
    response.current_idx = 1;
    require(raft_recv_appendentries_response(raft, peer, &response) == 0,
            "NULL callback peer response failed");
    require(raft_get_commit_idx(raft) == 1,
            "NULL durable callback changed historical commit behavior");
    require(raft_periodic(raft, 0) == 0, "NULL callback periodic failed");
    require(raft_get_commit_idx(raft) == 1,
            "NULL callback periodic behavior changed");
    raft_free(raft);
}

void test_previous_term_entry_is_not_committed_by_recheck() {
    Durability durability;
    raft_cbs_t cbs = callbacks();
    raft_server_t* raft = raft_new();
    require(raft != nullptr, "raft_new failed");
    raft_set_callbacks(raft, &cbs, &durability);
    raft_add_node(raft, nullptr, 1, 1);
    raft_node_t* peer = raft_add_node(raft, nullptr, 2, 0);
    raft_add_node(raft, nullptr, 3, 0);
    raft_set_current_term(raft, 1);
    raft_become_leader(raft);
    append_one(raft);
    raft_set_current_term(raft, 2);
    msg_appendentries_response_t response{};
    response.term = 2;
    response.success = 1;
    response.current_idx = 1;
    require(raft_recv_appendentries_response(raft, peer, &response) == 0,
            "previous-term peer response failed");
    durability.index = 1;
    require(raft_periodic(raft, 0) == 0, "previous-term periodic failed");
    require(raft_get_commit_idx(raft) == 0,
            "previous-term entry was committed by durability recheck");
    raft_free(raft);
}

} // namespace

int main() {
    test_before_apply_refusal_preserves_index_and_skips_applylog();
    test_periodic_auto_apply_honors_before_apply();
    test_begin_snapshot_auto_apply_honors_before_apply();
    test_null_before_apply_preserves_application_behavior();
    test_peer_ack_before_local_flush_is_reconsidered();
    test_null_callback_preserves_synchronous_behavior();
    test_previous_term_entry_is_not_committed_by_recheck();
    return 0;
}
