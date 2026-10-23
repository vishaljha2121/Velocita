// Audit-only reproductions; not part of the passing acceptance suite.
#define main original_replication_suite_main
#include "../../../tests/v2_replication_tests.cpp"
#undef main
#include "velocita/v2/gateway.hpp"

std::uint64_t build_sendable(Cluster& cluster) {
    std::uint64_t seq = 0;
    const auto bundle = executable_bundle();
    (void)cluster.commit(command(++seq, AdvanceKillEpoch{KillEpoch{1}}));
    (void)cluster.commit(command(++seq, SetExecutorGeneration{ExecutorGeneration{1}}));
    (void)cluster.commit(command(++seq, SetStrategyMode{
        StrategyMode::normal, KillEpoch{1}, ExecutorGeneration{1}}));
    (void)cluster.commit(command(++seq, ConfigureRiskLimits{
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{},
        KillEpoch{1}, ExecutorGeneration{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), MoneyMicros::from_atoms(4000),
        MoneyMicros::from_atoms(8000), 1000000, 2500}));
    (void)cluster.commit(command(++seq, PublishPolicy{policy_artifact()}));
    (void)cluster.commit(command(++seq, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}));
    (void)cluster.commit(command(++seq, bundle));
    (void)cluster.commit(command(++seq, ActivateExecutablePolicy{
        PolicyId{10}, PolicyVersion{1}, bundle.executable_policy.artifact_digest}));
    (void)cluster.commit(command(++seq, RecordSourceEvent{
        SourceId{12}, SourceGeneration{1}, SourceSequence{1}, digest(0x44)}));
    (void)cluster.commit(command(++seq, EvaluateDecisionFrame{
        DecisionId{20}, EvaluationMode::authoritative, decision_frame()}));
    const auto authorized = cluster.commit(command(++seq, AuthorizeDecision{DecisionId{20}}));
    require(authorized.result.code == ApplyCode::applied, "probe setup: authorization failed");
    require(materialize_order_effect(cluster.elect().state(), authorized.result.effect_id()).has_value(),
            "probe setup: effect must be sendable before partition");
    return seq;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string mode = argv[1];
    const std::filesystem::path root = argv[2];
    if (std::filesystem::exists(root)) return 2;
    try {
        if (mode == "authority") {
            Cluster cluster(root, 3);
            const auto sequence = build_sendable(cluster);
            cluster.settle(100);
            auto& old = cluster.elect();
            const auto old_id = old.status().node_id;
            cluster.partitioned.insert(old_id);
            cluster.settle(500);
            ReplicatedKernel* replacement = nullptr;
            for (auto& node : cluster.nodes) {
                if (node->status().node_id != old_id && node->is_leader())
                    replacement = node.get();
            }
            require(replacement != nullptr, "probe setup: majority elected no leader");
            cluster.drain_results();
            const auto proposed = replacement->propose(encoded(command(
                sequence + 1, AdvanceKillEpoch{KillEpoch{2}})), ProposeLane::control);
            require(proposed.status == ProposeStatus::admitted, "probe setup: kill refused");
            cluster.settle(200);
            require(replacement->state().kill_epoch == KillEpoch{2},
                    "probe setup: majority did not commit kill");
            const auto view = old.committed_outbox();
            const auto& stale = old.state();
            GatewayControlView control{stale.executor_generation, stale.kill_epoch,
                                       stale.strategy_mode, 0, 50, 10000};
            const bool renewed = renew_authority(control,
                {view.authoritative, stale.executor_generation, stale.kill_epoch,
                 stale.strategy_mode}, 10000);
            const auto send = evaluate_pre_send(stale, stale.outbox_effects[0].effect_id,
                control, {VenueId{9}, VenueCapabilitiesVersion{8}, true, true}, {1, 1});
            std::cout << "majority_kill=" << replacement->state().kill_epoch.value()
                      << " stale_kill=" << stale.kill_epoch.value()
                      << " old_authoritative=" << view.authoritative
                      << " renewed=" << renewed << " permitted=" << send.permitted()
                      << " refusal=" << static_cast<int>(send.refusal) << '\n';
            require(!send.permitted(), "RED: isolated old leader renews new-risk authority after majority kill");
        } else if (mode == "membership") {
            Cluster cluster(root, 3);
            (void)build_sendable(cluster);
            cluster.settle(100);
            const auto victim = cluster.elect().status().node_id;
            require(cluster.node(victim).compact() > 0, "probe setup: no snapshot");
            cluster.partitioned.insert(victim);
            for (auto& node : cluster.nodes) {
                if (node->status().node_id != victim) continue;
                node.reset();
                node = std::make_unique<ReplicatedKernel>(victim,
                    root / std::to_string(victim), std::vector<std::int32_t>{victim});
                node->tick(1000);
                std::cout << "restarted_with_single_member_leader=" << node->is_leader() << '\n';
                require(!node->is_leader(), "RED: unchanged durable directory accepts a different quorum configuration");
                break;
            }
        } else if (mode == "zero-controls") {
            Cluster cluster(root, 1);
            (void)build_authorized_submit(cluster);
            const auto& state = cluster.elect().state();
            const auto effect = materialize_order_effect(state, state.outbox_effects[0].effect_id);
            std::cout << "authorized_effects=" << state.outbox_effect_count
                      << " materializable=" << effect.has_value() << '\n';
            require(effect.has_value(), "RED: public-command bootstrap authorizes an unreconstructable effect");
        } else if (mode == "replacement") {
            Cluster cluster(root, 3);
            (void)build_authorized_submit(cluster);
            cluster.settle(100);
            auto& leader = cluster.elect();
            const auto leader_id = leader.status().node_id;
            const auto compacted = leader.compact();
            require(compacted > 0, "probe setup: no compaction");
            std::int32_t replaced_id = 0;
            for (auto& node : cluster.nodes) {
                if (node->status().node_id == leader_id) continue;
                replaced_id = node->status().node_id;
                node.reset();
                node = std::make_unique<ReplicatedKernel>(replaced_id,
                    root / "fresh-replacement", cluster.members);
                break;
            }
            cluster.settle(5000);
            const auto applied = cluster.node(replaced_id).status().applied_index;
            std::cout << "compacted=" << compacted << " replacement_applied=" << applied << '\n';
            require(applied >= static_cast<std::int64_t>(compacted),
                    "RED: replacement cannot fetch compacted history");
        } else if (mode == "poison") {
            {
                ReplicatedKernel node(1, root);
                node.tick(1000);
                const std::array<std::byte, 1> invalid{std::byte{0xff}};
                const auto outcome = node.propose(invalid);
                std::cout << "malformed_admitted=" << (outcome.status == ProposeStatus::admitted) << '\n';
                try { node.flush_durability(); node.tick(0); }
                catch (const std::exception& error) { std::cout << "apply_error=" << error.what() << '\n'; }
            }
            bool refused = false;
            try {
                ReplicatedKernel restarted(1, root);
                restarted.tick(1000);
                restarted.tick(0);
            }
            catch (const std::exception& error) {
                refused = true;
                std::cout << "restart_error=" << error.what() << '\n';
            }
            require(!refused, "RED: malformed normal-lane proposal poisons durable restart");
        } else if (mode == "queues") {
            ReplicatedKernel node(1, root);
            node.tick(1000);
            const auto bytes = encoded(command(1,
                SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}}));
            std::size_t admitted = 0;
            for (int i = 0; i < 2000; ++i) {
                if (node.propose(bytes).status == ProposeStatus::admitted) ++admitted;
                node.flush_durability(); node.tick(0);
            }
            std::size_t results = 0;
            while (node.take_committed_result()) ++results;
            std::cout << "admitted=" << admitted << " queued_results=" << results
                      << " retained_log_entries=" << node.status().current_index << '\n';
            // A witness of growth, not an invented acceptable capacity threshold.
            require(results < 2000, "RED: stalled consumer receives no bounded-queue backpressure in 2000 proposals");
        } else return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
