#include <cstddef>
extern "C" {
#include <raft.h>
}
#include <stdexcept>

namespace {

struct Durability {
    raft_index_t index = 0;
};

int persist_term(raft_server_t*, void*, raft_term_t, int) { return 0; }
int persist_vote(raft_server_t*, void*, int) { return 0; }
int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }
int applylog(raft_server_t*, void*, raft_entry_t*, raft_index_t) { return 0; }
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
    cbs.send_appendentries = &send_appendentries;
    cbs.durable_idx = &durable_idx;
    return cbs;
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
    test_peer_ack_before_local_flush_is_reconsidered();
    test_null_callback_preserves_synchronous_behavior();
    test_previous_term_entry_is_not_committed_by_recheck();
    return 0;
}
