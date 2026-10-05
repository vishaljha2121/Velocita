// velocita-v2-status: prints a voter's health page.
#include "velocita/v2/node_health.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: velocita-v2-status HEALTH_FILE" << std::endl;
        return 64;
    }
    const auto health = velocita::v2::read_node_health(argv[1]);
    if (!health) {
        std::cout << "status=unknown" << std::endl;
        return 1;
    }
    static constexpr const char* kRoles[] = {"none", "follower", "candidate", "leader"};
    const auto& h = *health;
    std::cout << "node=" << h.node_id << " pid=" << h.pid << " epoch=" << h.session_epoch
              << " role=" << kRoles[h.role < 4 ? h.role : 0] << " ready=" << h.ready << " failed=" << h.failed
              << " term=" << h.term << " leader=" << h.leader_id << " current=" << h.current_index
              << " commit=" << h.commit_index << " applied=" << h.applied_index << " durable=" << h.durable_index
              << " turns=" << h.turns << " auth_refusals=" << h.auth_refusals
              << " fence_refusals=" << h.fence_refusals << " busy_holds=" << h.peer_busy_holds
              << " backpressure=" << h.peer_backpressure << " ingress_refusals=" << h.ingress_refusals
              << " results_sent=" << h.results_sent << " results_dropped=" << h.results_dropped
              << " secret_reloads=" << h.secret_reloads << " secret_reload_failures=" << h.secret_reload_failures
              << " retained_ordinary=" << h.retained_ordinary << " retained_protected=" << h.retained_protected
              << " floor=" << h.compaction_floor << " snapshot=" << h.snapshot_index
              << " stranded=" << h.stranded_voters << " compactions=" << h.compactions
              << " last_compaction_ms=" << h.last_compaction_ms
              << std::endl;
    return 0;
}
