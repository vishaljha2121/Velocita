#include "v2_node_test_support.hpp"
#include "velocita/v2/elapsed_ticks.hpp"

namespace {

void elapsed_time_is_counted_without_losing_fractions() {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start{};
    ElapsedMilliseconds<Clock> clock(start);
    auto now = start;
    long total = 0;
    for (int turn = 0; turn < 1'000; ++turn) {
        now += std::chrono::microseconds(2'300);
        const int elapsed = clock.take(now);
        require(elapsed == 2 || elapsed == 3, "a 2.3 ms turn was counted as " + std::to_string(elapsed) + " ms");
        total += elapsed;
    }
    require(total == 2'300, "1,000 turns of 2.3 ms were counted as " + std::to_string(total) + " ms, not 2,300");
    require(clock.take(now) == 0 && clock.take(now - std::chrono::seconds(1)) == 0,
            "time that did not advance, or went back, was counted");
    require(clock.take(now + std::chrono::microseconds(999)) == 0 &&
                clock.take(now + std::chrono::microseconds(1'000)) == 1,
            "a millisecond was counted early or late");
}

void redirect_admit_and_commit() {
    Base base("v2-node-commit");
    Cluster cluster(base.path);
    const auto leader = cluster.elect();
    const auto follower = leader == 1 ? 2 : 1;

    cluster.network.at(follower).ingress_inbox.push_back(cluster.caller.ingress(follower, authored(1)));
    cluster.run_until([&] { return cluster.response_for(1, ResponseKind::refused).has_value(); }, "redirect");
    const auto redirect = *cluster.response_for(1, ResponseKind::refused);
    require(redirect.reason == static_cast<std::uint8_t>(ProposeStatus::not_leader) && redirect.leader_id == leader,
            "follower did not redirect to the actual leader");
    require(redirect.correlation.payload_digest == digest(1), "redirect lost the request correlation");

    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(1)));
    cluster.run_until([&] { return cluster.response_for(1, ResponseKind::committed).has_value(); }, "commit");
    require(cluster.response_for(1, ResponseKind::admitted).has_value(), "leader never acknowledged admission");
    const auto committed = *cluster.response_for(1, ResponseKind::committed);
    require(committed.result == expected_result(1), "committed result differs from the independent reducer");
    require(committed.correlation.payload_digest == digest(1), "committed response lost its correlation");
    for (const auto& [from, response] : cluster.received)
        if (response.kind == ResponseKind::committed && response.correlation.request_sequence == 1)
            require(response.result == committed.result, "voters returned different committed results");
    (void)cluster.busy_holds();
}

void busy_kernel_holds_frames_instead_of_dropping_them() {
    Base base("v2-node-busy");
    Cluster cluster(base.path);
    const auto leader = cluster.elect();
    const auto follower = leader == 1 ? 2 : 1;
    cluster.run(20);
    auto& gate = *cluster.gates.at(follower);
    gate.armed = true;
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(1)));
    cluster.run_until([&] { return gate.parked.load(); }, "the follower worker to park");
    // Election/startup can already have incremented this cumulative counter.
    // Require a new hold while this particular persistence gate is parked.
    const auto holds_before = cluster.node(follower).status().counters.peer_busy_holds;
    cluster.run_until([&] { return cluster.node(follower).status().counters.peer_busy_holds > holds_before; },
                      "an authentic frame to meet the busy follower");
    require(cluster.node(follower).status().held_peer_frames == 1, "busy follower is not holding the frame");
    gate.released = true;
    cluster.run_until([&] { return cluster.response_for(1, ResponseKind::committed).has_value(); },
                      "commit after the worker resumed");
    require(cluster.response_for(1, ResponseKind::committed)->result == expected_result(1),
            "result after a busy episode differs from the independent reducer");
    require(cluster.busy_holds() > 0, "busy episode not recorded");
}

void forged_replayed_and_mismatched_frames_have_no_effect() {
    Base base("v2-node-auth");
    Cluster cluster(base.path);
    const auto leader = cluster.elect();
    const auto follower = leader == 1 ? 2 : 1;
    cluster.run(20);

    const velocita::PeerMessage vote{follower, leader, RequestVote{1000, follower, 0, 0}};
    const auto term_before = cluster.node(leader).status().replica.term;
    const auto body = identity_test::frame_peer_body(
        vote, identity_test::verified_identity(identity_test::identity(follower, {1, 2}, catalog_test::standard_catalog())));
    Bytes forged(kMaxAuthEnvelopeBytes);
    const auto env = encode_auth_envelope(key(kWrongSeed, AuthRole::peer, follower, leader), 50, 50, body, forged);
    require(env.error == AuthEnvelopeError::none, "forged fixture did not encode");
    forged.resize(env.bytes_written);
    const auto refusals_before = cluster.node(leader).status().counters.auth_refusals;
    cluster.network.at(leader).peer_inbox.push_back(forged);
    cluster.run(5);
    require(cluster.node(leader).status().counters.auth_refusals == refusals_before + 1,
            "forged peer frame was not counted as an authentication refusal");
    require(cluster.node(leader).status().replica.term == term_before && cluster.leader() == leader,
            "forged frame changed the leader's term or role");

    Bytes authentic;
    for (const auto& [recipient, frame] : cluster.network.captured)
        if (recipient == leader) authentic = frame;
    require(!authentic.empty(), "no authentic frame captured");
    const auto fence_before = cluster.node(leader).status().counters.fence_refusals;
    cluster.network.at(leader).peer_inbox.push_back(authentic);
    cluster.run(1);
    require(cluster.node(leader).status().counters.fence_refusals > fence_before,
            "replayed authentic frame was not refused by the session fence");

    const auto index_before = cluster.node(leader).status().replica.current_index;
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(2, 8)));
    cluster.run_until([&] { return cluster.response_for(2, ResponseKind::refused).has_value(); }, "mismatch refusal");
    require(cluster.response_for(2, ResponseKind::refused)->reason ==
                static_cast<std::uint8_t>(IngressRefusal::caller_mismatch),
            "impersonated CallerId not refused as caller_mismatch");
    require(cluster.node(leader).status().replica.current_index == index_before,
            "impersonating command reached the log");
    (void)cluster.busy_holds();
}

void restarted_node_retires_its_old_session() {
    Base base("v2-node-restart");
    Cluster cluster(base.path);
    auto leader = cluster.elect();
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(1)));
    cluster.run_until([&] { return cluster.response_for(1, ResponseKind::committed).has_value(); }, "first commit");

    const std::int32_t restarted = 2, survivor = 1;
    Bytes old_session;
    for (const auto& [recipient, frame] : cluster.network.captured)
        if (recipient == survivor) old_session = frame;
    require(!old_session.empty(), "no old-session frame captured");

    cluster.nodes.at(restarted).reset();
    cluster.network.at(restarted).peer_inbox.clear();
    cluster.epochs[restarted] = 2;
    cluster.start(restarted, NodeStartup::recover);
    leader = cluster.elect();
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(2)));
    cluster.run_until([&] { return cluster.response_for(2, ResponseKind::committed).has_value(); }, "post-restart commit");
    require(cluster.response_for(2, ResponseKind::committed)->result == expected_result(2),
            "post-restart result differs from the independent reducer");

    const auto fence_before = cluster.node(survivor).status().counters.fence_refusals;
    cluster.network.at(survivor).peer_inbox.push_back(old_session);
    cluster.run(1);
    require(cluster.node(survivor).status().counters.fence_refusals == fence_before + 1,
            "frame from the restarted node's previous session was accepted");
    require(cluster.node(restarted).status().session_epoch == 2, "restarted node did not adopt its new epoch");
    (void)cluster.busy_holds();
}

void partition_and_backpressure_recover() {
    Base base("v2-node-partition");
    Cluster cluster(base.path);
    auto leader = cluster.elect();
    cluster.network.partitioned = true;
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(1)));
    cluster.run(300);
    require(!cluster.response_for(1, ResponseKind::committed).has_value(), "committed without its only peer");

    cluster.network.partitioned = false;
    cluster.network.at(1).block_offers = true;
    cluster.run_until([&] { return cluster.node(1).status().counters.peer_backpressure > 0; },
                      "a blocked offer to be counted");
    const auto blocked_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (std::chrono::steady_clock::now() < blocked_until) cluster.run(1);
    const auto& refused = cluster.network.at(1).refused;
    require(refused.size() > 1, "blocked offers were not retried");
    for (const auto& retry : refused)
        require(retry == refused.front(), "a held envelope was replaced while its peer was blocked");
    cluster.network.at(1).block_offers = false;
    const auto captured_before = cluster.network.captured.size();
    cluster.run(1);
    require(cluster.network.captured.size() > captured_before &&
                cluster.network.captured[captured_before].second == refused.front(),
            "stashed envelope was not the first frame offered after backpressure cleared");

    leader = cluster.elect();
    int rounds = 0;
    try {
        cluster.run_until([&] {
            if (!cluster.response_for(2, ResponseKind::admitted) && rounds++ % 200 == 0) {
                leader = cluster.elect();
                cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(2)));
            }
            return cluster.response_for(1, ResponseKind::committed).has_value() &&
                   cluster.response_for(2, ResponseKind::committed).has_value();
        }, "both commands committed after heal");
    } catch (const std::exception& error) {
        std::string state = std::string(error.what()) + " [1 committed=" +
            (cluster.response_for(1, ResponseKind::committed) ? "yes" : "no") + ", 2 committed=" +
            (cluster.response_for(2, ResponseKind::committed) ? "yes" : "no") + ", 2 admitted=" +
            (cluster.response_for(2, ResponseKind::admitted) ? "yes" : "no") + ", 2 refused reason=" +
            (cluster.response_for(2, ResponseKind::refused)
                 ? std::to_string(static_cast<int>(cluster.response_for(2, ResponseKind::refused)->reason)) : "none") +
            ", submitted to " +
            std::to_string(leader) + "]";
        for (auto& [id, node] : cluster.nodes) {
            const auto st = node->status();
            state += " node" + std::to_string(id) + "{" + std::string(st.replica.role) + " term=" +
                     std::to_string(st.replica.term) + " current=" + std::to_string(st.replica.current_index) +
                     " commit=" + std::to_string(st.replica.commit_index) + "}";
        }
        throw std::runtime_error(state);
    }
    require(cluster.response_for(2, ResponseKind::committed)->result == expected_result(2),
            "healed result differs from the independent reducer");
    (void)cluster.busy_holds();
}

SecretSet secret_set(std::uint8_t current, std::optional<std::uint8_t> next = {},
                     std::optional<std::uint8_t> previous = {}) {
    SecretSet set{secret(current)};
    if (next) set.next = secret(*next);
    if (previous) set.previous = secret(*previous);
    return set;
}

NodeSecrets rotation_secrets(std::int32_t self, SecretSet link, SecretSet caller) {
    NodeSecrets secrets;
    secrets.voters.emplace(self == 1 ? 2 : 1, std::move(link));
    secrets.callers.emplace(kCallerKey, std::move(caller));
    return secrets;
}

void rotation_across_live_nodes_has_no_outage() {
    Base base("v2-node-rotation");
    Cluster cluster(base.path);
    auto leader = cluster.elect();
    std::uint64_t sequence = 0;
    const auto commit = [&](const std::string& step) {
        ++sequence;
        leader = cluster.elect();
        cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(sequence)));
        cluster.run_until([&] { return cluster.response_for(sequence, ResponseKind::committed).has_value(); },
                          "commit during rotation step: " + step);
        require(cluster.response_for(sequence, ResponseKind::committed)->result == expected_result(sequence),
                step + ": result differs from the independent reducer");
    };
    const auto refusals = [&] {
        return cluster.node(1).status().counters.auth_refusals + cluster.node(2).status().counters.auth_refusals;
    };
    const auto apply = [&](std::int32_t node, SecretSet link, SecretSet caller) {
        require(cluster.node(node).replace_secrets(rotation_secrets(node, std::move(link), std::move(caller))),
                "valid rotation secrets refused on node " + std::to_string(node));
    };
    commit("before rotation");
    const auto refusals_before = refusals();

    apply(1, secret_set(kLinkSeed, kNewLinkSeed), secret_set(kCallerSeed, kNewCallerSeed));
    commit("one voter accepts the new secrets");
    apply(2, secret_set(kLinkSeed, kNewLinkSeed), secret_set(kCallerSeed, kNewCallerSeed));
    cluster.caller.accept_seeds = {kCallerSeed, kNewCallerSeed};
    cluster.caller.send_seed = kNewCallerSeed;
    commit("new secrets accepted");

    apply(1, secret_set(kNewLinkSeed, {}, kLinkSeed), secret_set(kNewCallerSeed, {}, kCallerSeed));
    commit("mixed promotion");
    apply(2, secret_set(kNewLinkSeed, {}, kLinkSeed), secret_set(kNewCallerSeed, {}, kCallerSeed));
    commit("both promoted");

    for (const std::int32_t node : {1, 2})
        apply(node, secret_set(kNewLinkSeed), secret_set(kNewCallerSeed));
    cluster.caller.accept_seeds = {kNewCallerSeed};
    commit("old secrets retired");
    require(refusals() == refusals_before, "rotation refused an authentic frame");

    const auto follower = leader == 1 ? 2 : 1;
    const velocita::PeerMessage vote{follower, leader, RequestVote{1, follower, 0, 0}};
    const auto body = identity_test::frame_peer_body(
        vote, identity_test::verified_identity(identity_test::identity(follower, {1, 2}, catalog_test::standard_catalog())));
    Bytes old_frame(kMaxAuthEnvelopeBytes);
    const auto env = encode_auth_envelope(key(kLinkSeed, AuthRole::peer, follower, leader), 900, 1, body, old_frame);
    require(env.error == AuthEnvelopeError::none, "old-secret fixture did not encode");
    old_frame.resize(env.bytes_written);
    cluster.network.at(leader).peer_inbox.push_back(old_frame);
    cluster.run(2);
    require(refusals() == refusals_before + 1, "frame under the retired secret was accepted");

    const auto refuses_cleanly = [&](NodeSecrets secrets) {
        try {
            return !cluster.node(1).replace_secrets(std::move(secrets));
        } catch (...) {
            throw std::runtime_error("incomplete reload threw instead of refusing");
        }
    };
    NodeSecrets without_peer;
    without_peer.callers.emplace(kCallerKey, secret_set(kCallerSeed));
    require(refuses_cleanly(std::move(without_peer)), "reload missing the peer was applied");
    NodeSecrets without_caller;
    without_caller.voters.emplace(2, secret_set(kLinkSeed));
    require(refuses_cleanly(std::move(without_caller)), "reload missing the caller was applied");
    commit("after refused reload");
    (void)cluster.busy_holds();
}

std::int32_t commit(Cluster& cluster, std::uint64_t sequence, const std::string& step) {
    const auto leader = cluster.elect();
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(sequence)));
    cluster.run_until([&] { return cluster.response_for(sequence, ResponseKind::committed).has_value(); },
                      "commit: " + step);
    require(cluster.response_for(sequence, ResponseKind::committed)->result == expected_result(sequence),
            step + ": result differs from the independent reducer");
    return leader;
}

void require_converged(Cluster& cluster, const std::string& step) {
    const auto leader = cluster.elect();
    cluster.run_until([&] {
        const auto target = cluster.node(leader).status().replica.commit_index;
        for (auto& [id, node] : cluster.nodes)
            if (node->status().replica.applied_index != target) return false;
        return true;
    }, "every running voter to apply the leader's commit: " + step);
    const auto digest = cluster.node(leader).status().replica.state_digest;
    for (auto& [id, node] : cluster.nodes)
        require(node->status().replica.state_digest == digest,
                step + ": voter " + std::to_string(id) + " holds a different state");
}

void three_voters_commit_without_an_unreachable_follower() {
    Base base("v2-node-three-follower");
    Cluster cluster(base.path, kThree);
    const auto leader = commit(cluster, 1, "all three voters");
    const auto lost = leader == 3 ? 2 : 3;
    cluster.stop(lost);
    require(commit(cluster, 2, "one follower unreachable") == leader,
            "losing a follower moved leadership");
    require_converged(cluster, "two of three");
    const auto outage_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (std::chrono::steady_clock::now() < outage_until) cluster.run(1);
    const auto& refused = cluster.network.at(leader).refused;
    require(refused.size() > 1, "offers to the stopped voter were not retried");
    for (const auto& retry : refused)
        require(retry == refused.front(), "the held envelope for a stopped voter was replaced");

    cluster.restart(lost);
    commit(cluster, 3, "the lost voter rejoined");
    require_converged(cluster, "after rejoin");
    require(cluster.node(lost).status().replica.applied_index == cluster.node(leader).status().replica.applied_index,
            "rejoined voter did not catch up");
    (void)cluster.busy_holds();
}

void three_voters_elect_a_new_leader_when_the_leader_is_lost() {
    Base base("v2-node-three-leader");
    Cluster cluster(base.path, kThree);
    const auto old_leader = commit(cluster, 1, "all three voters");
    cluster.stop(old_leader);
    require(commit(cluster, 2, "leader lost") != old_leader, "a stopped voter is still reported as leader");
    require_converged(cluster, "survivors");

    cluster.restart(old_leader);
    commit(cluster, 3, "the old leader rejoined");
    require_converged(cluster, "after rejoin");
    (void)cluster.busy_holds();
}

void three_voters_compact_below_the_floor_and_keep_committing() {
    Base base("v2-node-compaction");
    Cluster cluster(base.path, {});
    cluster.voters = kThree;
    cluster.compact_at_entries = 16;
    for (const auto id : cluster.voters) {
        cluster.epochs[id] = 1;
        cluster.start(id, NodeStartup::bootstrap);
    }
    std::uint64_t sequence = 0;
    for (int i = 0; i < 5; ++i) commit(cluster, ++sequence, "below the threshold");
    const auto quiet_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < quiet_until) cluster.run(1);
    for (auto& [id, node] : cluster.nodes)
        require(node->status().counters.compactions == 0,
                "voter " + std::to_string(id) + " compacted below its threshold");
    for (int i = 0; i < 35; ++i) commit(cluster, ++sequence, "filling the log");
    cluster.run_until([&] {
        for (auto& [id, node] : cluster.nodes)
            if (node->status().counters.compactions == 0) return false;
        return true;
    }, "every voter to compact past its threshold");
    for (auto& [id, node] : cluster.nodes)
        require(node->status().replica.snapshot_index > 0 &&
                    node->status().replica.snapshot_index <= node->status().replica.compaction_floor,
                "voter " + std::to_string(id) + " compacted above its floor");

    const auto leader = cluster.elect();
    const auto stopped = leader == 3 ? 2 : 3;
    const auto held = cluster.node(stopped).status().replica.current_index;
    cluster.stop(stopped);
    for (int i = 0; i < 40; ++i) commit(cluster, ++sequence, "one voter stopped");
    const auto pinned_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < pinned_until) cluster.run(1);
    for (auto& [id, node] : cluster.nodes)
        require(node->status().replica.snapshot_index <= held,
                "voter " + std::to_string(id) + " reclaimed entries the stopped voter never received: snapshot " +
                    std::to_string(node->status().replica.snapshot_index) + " > " + std::to_string(held));

    cluster.restart(stopped);
    commit(cluster, ++sequence, "the stopped voter returned");
    require_converged(cluster, "after gated compaction");
    require(cluster.node(cluster.elect()).status().replica.stranded_voters == 0,
            "the returning voter was stranded");
    cluster.run_until([&] {
        for (auto& [id, node] : cluster.nodes)
            if (node->status().replica.snapshot_index <= held) return false;
        return true;
    }, "compaction to resume past the pinned cut once every voter holds the log");
}

void three_voters_fail_over_after_compacting() {
    Base base("v2-node-compaction-failover");
    Cluster cluster(base.path, {});
    cluster.voters = kThree;
    cluster.compact_at_entries = 16;
    for (const auto id : cluster.voters) {
        cluster.epochs[id] = 1;
        cluster.start(id, NodeStartup::bootstrap);
    }
    std::uint64_t sequence = 0;
    for (int i = 0; i < 30; ++i) commit(cluster, ++sequence, "filling the log");
    cluster.run_until([&] {
        for (auto& [id, node] : cluster.nodes)
            if (node->status().counters.compactions == 0) return false;
        return true;
    }, "every voter to compact");
    const auto old_leader = cluster.elect();
    cluster.stop(old_leader);
    for (int i = 0; i < 5; ++i) {
        const auto leader = commit(cluster, ++sequence, "after the compacted leader was lost");
        require(leader != old_leader, "the stopped leader is still reported as leader");
    }
    require_converged(cluster, "survivors after compaction");
    for (auto& [id, node] : cluster.nodes)
        require(!node->status().replica.role.empty(), "fixture");
}

void three_voters_refuse_to_commit_without_a_quorum() {
    Base base("v2-node-three-quorum");
    Cluster cluster(base.path, kThree);
    const auto leader = commit(cluster, 1, "all three voters");
    require_converged(cluster, "before the quorum is lost");
    const auto committed_before = cluster.node(leader).status().replica.commit_index;
    for (const auto id : kThree)
        if (id != leader) cluster.stop(id);
    cluster.network.at(leader).ingress_inbox.push_back(cluster.caller.ingress(leader, authored(2)));
    cluster.run(600);
    require(!cluster.response_for(2, ResponseKind::committed).has_value(), "committed with one voter of three");
    require(cluster.node(leader).status().replica.commit_index == committed_before,
            "lone voter advanced its commit index without a quorum");
    (void)cluster.busy_holds();
}

}

int main() {
    try {
        elapsed_time_is_counted_without_losing_fractions();
        redirect_admit_and_commit();
        busy_kernel_holds_frames_instead_of_dropping_them();
        forged_replayed_and_mismatched_frames_have_no_effect();
        restarted_node_retires_its_old_session();
        partition_and_backpressure_recover();
        rotation_across_live_nodes_has_no_outage();
        three_voters_commit_without_an_unreachable_follower();
        three_voters_elect_a_new_leader_when_the_leader_is_lost();
        three_voters_refuse_to_commit_without_a_quorum();
        three_voters_compact_below_the_floor_and_keep_committing();
        three_voters_fail_over_after_compacting();
        std::cout << "node tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node tests failed: " << error.what() << '\n';
        return 1;
    }
}
