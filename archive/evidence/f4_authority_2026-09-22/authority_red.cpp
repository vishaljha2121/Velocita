// Permanent pre-contract reproducer; intentionally RED, not a passing CTest suite.
#define main replication_suite_main
#include "../../../tests/v2_replication_tests.cpp"
#undef main
#include "velocita/v2/gateway.hpp"

int main(int argc, char** argv) {
    if (argc != 3 || std::filesystem::exists(argv[2])) return 2;
    try {
        const std::string mode = argv[1];
        Cluster cluster(argv[2], mode == "partition" ? 3 : 2);
        const auto sequence = build_authorized_submit(cluster);
        cluster.settle(100);
        auto& old = cluster.elect();
        const auto effect = old.state().outbox_effects[0].effect_id;
        const auto control_from = [](const ReplicatedKernel& node) {
            return AuthorityReading{node.committed_outbox().authoritative,
                node.state().executor_generation, node.state().kill_epoch,
                node.state().strategy_mode};
        };
        GatewayControlView control{old.state().executor_generation,
            old.state().kill_epoch, old.state().strategy_mode, 0, 50, 10000};
        const auto admitted = [&] {
            return evaluate_pre_send(old.state(), effect, control,
                {VenueId{9}, VenueCapabilitiesVersion{8}, true, true}, {1, 1},
                &catalog_test::standard_catalog()).permitted();
        };
        if (mode == "partition") {
            const auto old_id = old.status().node_id;
            cluster.partitioned.insert(old_id);
            cluster.settle(500);
            ReplicatedKernel* replacement = nullptr;
            for (auto& node : cluster.nodes)
                if (node->status().node_id != old_id && node->is_leader())
                    replacement = node.get();
            require(replacement != nullptr, "setup: majority elected no replacement");
            cluster.drain_results();
            require(replacement->propose(encoded(command(sequence + 1,
                        AdvanceKillEpoch{KillEpoch{2}})), ProposeLane::control).status ==
                        ProposeStatus::admitted, "setup: kill not admitted");
            cluster.settle(200);
            require(replacement->state().kill_epoch == KillEpoch{2},
                    "setup: kill did not commit");
            const bool renewed = renew_authority(control, control_from(old), 10000);
            const bool permitted = admitted();
            std::cout << "majority_kill=" << replacement->state().kill_epoch.value()
                      << " stale_kill=" << old.state().kill_epoch.value()
                      << " renewed=" << renewed << " permitted=" << permitted << '\n';
            require(!permitted, "RED: stale leader renewed permission after majority kill");
        } else if (mode == "delayed") {
            const auto reply = control_from(old); // Captured while both voters are healthy.
            cluster.partitioned.insert(old.status().node_id);
            // A reply captured at tick 1 arrives at tick 10000, beyond bound 50.
            const bool renewed = renew_authority(control, reply, 10000);
            const bool permitted = admitted();
            std::cout << "captured_at=1 received_at=10000 bound=50 renewed=" << renewed
                      << " permitted=" << permitted << '\n';
            require(!permitted, "RED: delayed old reply acquired fresh send permission");
        } else return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
