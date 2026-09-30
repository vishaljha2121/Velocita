#include <cstddef>
extern "C" {
#include <raft.h>
}

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
struct Trace { int sends{}; int votes{}; bool fail_vote{}; };
int term(raft_server_t*, void*, raft_term_t, raft_node_id_t) { return 0; }
int vote(raft_server_t*, void* raw, raft_node_id_t) {
    auto& trace = *static_cast<Trace*>(raw);
    ++trace.votes;
    return trace.fail_vote ? RAFT_ERR_SHUTDOWN : 0;
}
int send(raft_server_t*, void* raw, raft_node_t*, msg_requestvote_t*) {
    ++static_cast<Trace*>(raw)->sends;
    return 0;
}
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void campaign(bool fail_vote) {
    Trace trace{0, 0, fail_vote};
    auto node = std::unique_ptr<raft_server_t, decltype(&raft_free)>(raft_new(), raft_free);
    require(node != nullptr, "raft allocation failed");
    raft_cbs_t callbacks{};
    callbacks.persist_term = term;
    callbacks.persist_vote = vote;
    callbacks.send_requestvote = send;
    raft_set_callbacks(node.get(), &callbacks, &trace);
    raft_add_node(node.get(), nullptr, 1, 1);
    raft_add_node(node.get(), nullptr, 2, 0);
    const int result = raft_periodic(node.get(), 10000);
    require(trace.votes == 1, "campaign did not attempt self-vote persistence");
    if (fail_vote) {
        require(result == RAFT_ERR_SHUTDOWN,
                "self-vote persistence failure was ignored by campaign");
        require(trace.sends == 0, "campaign emitted RequestVote after failed self vote");
    } else {
        require(result == 0 && trace.sends == 1,
                "successful synchronous campaign changed with optional hooks absent");
    }
}
}
int main() {
    try {
        campaign(false);
        campaign(true);
        std::cout << "Raft persistence callback tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
