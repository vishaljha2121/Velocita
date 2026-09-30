// Reuse the public-command cluster fixture; never inject TradingState economics.
#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main
#include "velocita/v2/gateway_dispatch.hpp"
#include "velocita/v2/payload.hpp"
#include <atomic>

namespace {

std::array<std::byte, 16> incarnation(const unsigned value = 1) {
    std::array<std::byte, 16> result{};
    result[0] = static_cast<std::byte>(value);
    return result;
}

GatewayControlView controls(const ReplicatedKernel& node) {
    return {node.state().executor_generation, node.state().kill_epoch,
            node.state().strategy_mode, 100, 50, 100};
}

struct Transport {
    Tick now{100};
    std::size_t calls{};
    std::vector<std::byte> bytes;
    bool throws{};
    Tick advance_on_second_clock{};
    unsigned clock_reads{};
    static Tick clock(void* raw) noexcept {
        auto& self = *static_cast<Transport*>(raw);
        if (++self.clock_reads == 2) self.now += self.advance_on_second_clock;
        return self.now;
    }
    static void handoff(BoundOrderEffect binding, void* raw) {
        auto& self = *static_cast<Transport*>(raw);
        ++self.calls;
        const auto encoded = encode_payload(binding.effect);
        require(encoded.error == PayloadError::none, "transport received noncanonical effect");
        self.bytes = encoded.bytes;
        if (self.throws) throw std::runtime_error("transport may have sent bytes");
    }
    GatewayTransportBoundary boundary() { return {clock, handoff, this}; }
};

struct Composition {
    Cluster cluster;
    GatewayAuthority authority;
    GatewayDeliveryState delivery{};
    SendQuotaView quota{10, 2};
    Transport transport{};
    std::uint64_t sequence{};
    explicit Composition(const std::filesystem::path& root, std::size_t voters = 2)
        : cluster(root, voters, 0xAB),
          authority(incarnation(), cluster.nodes.front()->static_identity().authority_domain(), 50) {
        begin_gateway_recovery(delivery, cluster.elect().state(), 0);
        authority.activate_after_reconciliation();
        sequence = build_authorized_submit(cluster);
        cluster.settle();
        cluster.drain_results();
    }
    BoundOrderEffect binding(ReplicatedKernel& node) {
        const auto value = materialize_bound_order_effect(node.state(),
            node.state().outbox_effects[0].effect_id, catalog_test::standard_catalog());
        require(value.has_value(), "fixture failed to materialize bound effect");
        return *value;
    }
    AuthorityProof proof(ReplicatedKernel& node, Tick start = 100) {
        authority.observe_fence(controls(node), node.status().term);
        authority.activate_after_reconciliation();
        const auto request = authority.begin_request(binding(node), controls(node), start);
        require(request && node.propose_authority_barrier(*request).status == ProposeStatus::admitted,
                "composed proof request was not admitted");
        require(!node.take_authority_proof(), "proof escaped before durable quorum");
        cluster.settle();
        auto result = node.take_authority_proof();
        require(result.has_value(), "healthy committed marker yielded no proof");
        return *result;
    }
    bool send(ReplicatedKernel& node, const BoundOrderEffect& prepared) {
        return dispatch_gateway_send(authority, node.state(), prepared, controls(node),
            node.status().term,
            {VenueId{9}, VenueCapabilitiesVersion{8}, true, true}, quota, delivery,
            {}, {}, catalog_test::standard_catalog(), transport.boundary());
    }
};

void healthy_one_shot_and_unknown_retry(const std::filesystem::path& root,
                                       std::size_t voters = 2) {
    Composition fixture(root, voters);
    auto& node = fixture.cluster.elect();
    const auto prepared = fixture.binding(node);
    require(prepared.effect.price == PriceTicks::from_atoms(11) &&
                prepared.effect.quantity == QuantityLots::from_atoms(2) &&
                prepared.effect.account_id == AccountId{5}, "fixture economic oracle changed");
    const auto expected = encode_payload(prepared.effect).bytes;
    require(!fixture.send(node, prepared) && fixture.transport.calls == 0,
            "a prepared candidate sent without a committed proof");
    const auto proof = fixture.proof(node);
    require(fixture.authority.accept(proof, 101), "fresh proof was refused");
    fixture.transport.now = 102;
    require(fixture.send(node, prepared), "fresh one-shot proof did not reach transport");
    require(fixture.transport.calls == 1 && fixture.transport.bytes == expected &&
                fixture.quota.normal_lane_remaining == 9,
            "transport bytes or actual quota consumption differ from prepared intent");
    require(!fixture.authority.accept(proof, 103) && !fixture.send(node, prepared) &&
                fixture.transport.calls == 1, "consumed proof sent twice");
    const auto next = fixture.proof(node, 104);
    require(fixture.authority.accept(next, 105), "second proof was not valid");
    fixture.transport.now = 106;
    require(!fixture.send(node, prepared) && fixture.transport.calls == 1,
            "fresh authority bypassed unknown-outcome submit retry protection");
    require(record_unsent(fixture.delivery, prepared.effect.effect_id), "proven-unsent setup failed");
    // A refused final attempt drops authority; proven-unsent needs a fresh request.
    const auto third = fixture.proof(node, 107);
    require(fixture.authority.accept(third, 108), "proven-unsent fresh request failed");
    fixture.transport.now = 109;
    fixture.transport.throws = true;
    bool threw = false;
    try { (void)fixture.send(node, prepared); } catch (const std::runtime_error&) { threw = true; }
    require(threw && fixture.transport.calls == 2 &&
                find_attempt(fixture.delivery, prepared.effect.effect_id)->awaiting_response,
            "transport exception was incorrectly treated as proven unsent");
    require(!fixture.send(node, prepared) && fixture.transport.calls == 2,
            "exception restored consumed authority");
}

void delayed_and_queued_proof(const std::filesystem::path& root, bool at_handoff) {
    Composition fixture(root);
    auto& node = fixture.cluster.elect();
    const auto prepared = fixture.binding(node);
    const auto proof = fixture.proof(node, 100);
    if (at_handoff) require(fixture.authority.accept(proof, 101), "fresh arrival failed");
    else require(!fixture.authority.accept(proof, 151), "late reply acquired fresh arrival-time authority");
    fixture.transport.now = 151;
    require(!fixture.send(node, prepared) && fixture.transport.calls == 0,
            "expired queued work reached final transport boundary");
}

void partition_majority_kill(const std::filesystem::path& root, bool prior_proof = false) {
    Composition fixture(root, 3);
    auto& old = fixture.cluster.elect();
    const auto prepared = fixture.binding(old);
    if (prior_proof) {
        const auto proof = fixture.proof(old, 100);
        require(fixture.authority.accept(proof, 101), "pre-partition proof refused");
    }
    fixture.cluster.partitioned.insert(old.status().node_id);
    fixture.cluster.settle(500);
    ReplicatedKernel* majority = nullptr;
    for (auto& node : fixture.cluster.nodes)
        if (node->status().node_id != old.status().node_id && node->is_leader()) majority = node.get();
    require(majority, "partition fixture elected no replacement");
    require(majority->propose(encoded(command(++fixture.sequence, AdvanceKillEpoch{KillEpoch{2}})),
                ProposeLane::control).status == ProposeStatus::admitted, "majority kill not admitted");
    fixture.cluster.settle(100);
    require(majority->state().kill_epoch == KillEpoch{2} && old.state().kill_epoch == KillEpoch{1},
            "partition fixture lacks divergent kill state");
    if (prior_proof) {
        // A proof is an ordered point, not a lock on a future remote kill.
        fixture.transport.now = 102;
        require(fixture.send(old, prepared) && fixture.transport.calls == 1 &&
                    fixture.transport.bytes == encode_payload(prepared.effect).bytes,
                "valid prior proof incorrectly claimed to observe an unseen remote kill");
        fixture.authority.observe_fence(controls(*majority), majority->status().term);
        require(fixture.transport.calls == 1 &&
                    find_attempt(fixture.delivery, prepared.effect.effect_id)->awaiting_response &&
                    !fixture.send(old, prepared),
                "observed kill recalled an in-flight send or restored its authority");
        return;
    }
    const auto request = fixture.authority.begin_request(prepared, controls(old), 10000);
    require(request && old.propose_authority_barrier(*request).status == ProposeStatus::admitted,
            "old leader did not stage the authority attempt");
    fixture.cluster.settle(100);
    fixture.transport.now = 10001;
    require(!old.take_authority_proof() && !fixture.send(old, prepared) && fixture.transport.calls == 0,
            "isolated old leader sent despite majority kill");
}

void fences_versions_restart_and_cancel(const std::filesystem::path& root) {
    Composition fixture(root);
    auto& node = fixture.cluster.elect();
    const auto prepared = fixture.binding(node);
    const auto proof = fixture.proof(node);
    GatewayAuthority restarted(
        incarnation(2), fixture.cluster.nodes.front()->static_identity().authority_domain(), 50);
    restarted.activate_after_reconciliation();
    require(restarted.begin_request(prepared, controls(node), 100).has_value() &&
                !restarted.accept(proof, 101), "restart accepted another incarnation's proof");
    require(fixture.authority.accept(proof, 101), "proof acceptance failed");
    auto changed = prepared;
    changed.instrument_spec_version = InstrumentSpecVersion{99};
    fixture.transport.now = 102;
    require(!fixture.send(node, changed) && fixture.transport.calls == 0,
            "changed historical version sent under a matching effect digest");

    const auto next = fixture.proof(node, 103);
    require(fixture.authority.accept(next, 104), "proof acceptance failed");
    fixture.cluster.commit(command(++fixture.sequence, AdvanceKillEpoch{KillEpoch{2}}));
    fixture.cluster.settle();
    fixture.transport.now = 105;
    require(!fixture.send(node, prepared) && fixture.transport.calls == 0,
            "known kill before handoff failed to stop the send");
    const auto result = fixture.cluster.commit(command(++fixture.sequence,
        RequestCancel{prepared.effect.cluster_order_id}));
    require(result.result.code == ApplyCode::applied, "committed cancel fixture failed");
    const auto cancel = materialize_bound_order_effect(node.state(), result.result.effect_id(),
                                                       catalog_test::standard_catalog());
    require(cancel.has_value(), "cancel did not materialize");
    fixture.authority.stop_new_risk();
    fixture.quota = {0, 1};
    fixture.transport.now = 10000;
    require(fixture.send(node, *cancel) && fixture.transport.calls == 1 &&
                fixture.quota.emergency_lane_remaining == 0,
            "stop-new-risk or expired proof blocked committed emergency cancel");
    fixture.authority.stop_all_transport();
    fixture.quota = {0, 1};
    require(!fixture.send(node, *cancel) && fixture.transport.calls == 1,
            "stop-all-transport allowed a cancel write");
}

void clock_and_request_boundaries(const std::filesystem::path& root) {
    for (int mode = 0; mode < 5; ++mode) {
        Composition fixture(root / std::to_string(mode));
        auto& node = fixture.cluster.elect();
        const auto prepared = fixture.binding(node);
        const Tick start = mode == 4 ? std::numeric_limits<Tick>::max() - 20 : 100;
        const auto proof = fixture.proof(node, start);
        require(fixture.authority.accept(proof, start + 1), "clock fixture proof refused");
        fixture.transport.now = start + 2;
        if (mode == 0) fixture.transport.now = start + 50; // Inclusive bound.
        if (mode == 1) fixture.transport.advance_on_second_clock = 100;
        if (mode == 2) fixture.transport.now = start; // Regression since accept.
        if (mode == 3) {
            fixture.authority.abandon_request(); // Reconnect discards outstanding authority.
            require(fixture.authority.begin_request(prepared, controls(node), start + 2).has_value(),
                    "reconnected request not created");
            require(!fixture.authority.accept(proof, start + 3), "earlier request proof matched a later sequence");
            fixture.transport.now = start + 4;
        }
        const bool expected_send = mode == 0 || mode == 4;
        require(fixture.send(node, prepared) == expected_send &&
                    fixture.transport.calls == (expected_send ? 1U : 0U),
                "deadline edge, final clock sample, regression, sequence or overflow boundary failed");
    }
}

void historical_binding_must_match_at_accept(const std::filesystem::path& root) {
    Composition fixture(root);
    auto& node = fixture.cluster.elect();
    auto wrong = fixture.binding(node);
    wrong.venue_capabilities_version = VenueCapabilitiesVersion{99};
    fixture.authority.observe_fence(controls(node), node.status().term);
    const auto request = fixture.authority.begin_request(wrong, controls(node), 100);
    require(request && node.propose_authority_barrier(*request).status == ProposeStatus::admitted,
            "historical binding fixture did not enter consensus");
    fixture.cluster.settle();
    const auto proof = node.take_authority_proof();
    require(proof.has_value() && !fixture.authority.accept(*proof, 101),
            "matching effect digest hid a different historical capability version");
    require(!fixture.send(node, wrong) && fixture.transport.calls == 0,
            "wrong historical binding reached transport");
}

void observed_term_invalidates_final_handoff(const std::filesystem::path& root) {
    Composition fixture(root);
    auto& node = fixture.cluster.elect();
    const auto prepared = fixture.binding(node);
    const auto proof = fixture.proof(node);
    require(fixture.authority.accept(proof, 101), "term fixture proof refused");
    fixture.transport.now = 102;
    require(!dispatch_gateway_send(fixture.authority, node.state(), prepared, controls(node),
        node.status().term + 1, {VenueId{9}, VenueCapabilitiesVersion{8}, true, true},
        fixture.quota, fixture.delivery, {}, {}, catalog_test::standard_catalog(),
        fixture.transport.boundary()) && fixture.transport.calls == 0,
        "observed newer term was not enforced at final handoff");
}

void consume_once_and_attempt_budget(const std::filesystem::path& root) {
    {
        Composition fixture(root / "consume");
        auto& node = fixture.cluster.elect();
        const auto prepared = fixture.binding(node);
        const auto proof = fixture.proof(node);
        require(fixture.authority.accept(proof, 101), "one-shot fixture proof refused");
        require(fixture.authority.consume(prepared, controls(node), 102),
                "first direct proof consumption failed");
        require(!fixture.authority.consume(prepared, controls(node), 103),
                "second direct consumption reused the same proof");
    }
    Composition fixture(root / "attempts");
    auto& node = fixture.cluster.elect();
    const auto prepared = fixture.binding(node);
    const auto limit = GatewayTimeouts{}.max_send_attempts;
    for (std::uint32_t attempt = 0; attempt <= limit; ++attempt) {
        const Tick start = 100 + 10 * attempt;
        const auto proof = fixture.proof(node, start);
        require(fixture.authority.accept(proof, start + 1), "attempt fixture proof refused");
        fixture.transport.now = start + 2;
        const auto quota_before = fixture.quota.normal_lane_remaining;
        const auto calls_before = fixture.transport.calls;
        if (attempt < limit) {
            require(fixture.send(node, prepared) &&
                        fixture.transport.calls == calls_before + 1 &&
                        fixture.quota.normal_lane_remaining == quota_before - 1,
                    "available attempt budget refused or miscounted send");
            require(record_unsent(fixture.delivery, prepared.effect.effect_id),
                    "attempt fixture could not mark proven-unsent");
        } else {
            require(!fixture.send(node, prepared) &&
                        fixture.transport.calls == calls_before &&
                        fixture.quota.normal_lane_remaining == quota_before,
                    "fresh authority bypassed maximum attempts or spent blocked quota");
        }
    }
}

void async_marker_checkpoint(const std::filesystem::path& root) {
    struct Gate {
        std::atomic<bool> armed{}, parked{}, release{};
        static void observe(velocita::MetadataCheckpoint point, void* raw) {
            auto& gate = *static_cast<Gate*>(raw);
            if (!gate.armed.load() || point != velocita::MetadataCheckpoint::file_renamed) return;
            gate.parked.store(true);
            while (!gate.release.load()) std::this_thread::yield();
        }
    } gate;
    Composition fixture(root, 1);
    fixture.cluster.nodes[0].reset();
    fixture.cluster.nodes[0] = std::make_unique<ReplicatedKernel>(
        identity_test::recover(root / "1", 1, {1}), StorageCheckpoints{},
        velocita::MetadataCheckpoints{&Gate::observe, &gate});
    auto& node = *fixture.cluster.nodes[0];
    struct Release { Gate& gate; ~Release() { gate.release.store(true); } } release{gate};
    node.tick(1);
    const auto prepared = fixture.binding(node);
    fixture.authority.observe_fence(controls(node), node.status().term);
    fixture.authority.activate_after_reconciliation();
    const auto request = fixture.authority.begin_request(prepared, controls(node), 100);
    require(request.has_value(), "async marker request fixture refused");
    node.start_writer();
    const auto before = node.status().applied_index;
    require(node.propose_authority_barrier(*request).status == ProposeStatus::admitted,
            "async marker not admitted");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (node.durable_index() != static_cast<std::uint64_t>(before + 1) &&
           std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        std::this_thread::yield();
    }
    require(node.durable_index() == static_cast<std::uint64_t>(before + 1), "marker WAL not durable");
    gate.armed.store(true);
    node.tick(0);
    node.flush_durability();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!gate.parked.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    require(gate.parked.load(), "marker checkpoint did not reach directory-sync boundary");
    require(node.status().applied_index == before && !node.take_authority_proof(),
            "real authority marker applied or released proof before checkpoint durability");
    // Do not call dispatch without a proof here: its intentional refusal also
    // abandons the pending gateway request, invalidating the later acceptance.
    gate.release.store(true);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::optional<AuthorityProof> proof;
    while (!proof && std::chrono::steady_clock::now() < deadline) {
        node.flush_durability(); node.tick(0); proof = node.take_authority_proof();
        std::this_thread::yield();
    }
    require(proof && fixture.authority.accept(*proof, 101), "durable marker did not release usable proof");
    fixture.transport.now = 102;
    require(fixture.send(node, prepared) && fixture.transport.calls == 1 &&
                fixture.transport.bytes == encode_payload(prepared.effect).bytes,
            "durable proof did not authorize exactly the original effect payload");
}

}  // namespace

int main() {
    static_assert(!std::is_default_constructible_v<AuthorityProof>);
    try {
        const auto root = std::filesystem::temp_directory_path() /
            ("v2-f4-composed-" + std::to_string(::getpid()));
        std::filesystem::create_directories(root);
        healthy_one_shot_and_unknown_retry(root / "healthy");
        healthy_one_shot_and_unknown_retry(root / "single-voter-lab", 1);
        delayed_and_queued_proof(root / "late", false);
        delayed_and_queued_proof(root / "queued", true);
        partition_majority_kill(root / "partition");
        partition_majority_kill(root / "unseen-window", true);
        fences_versions_restart_and_cancel(root / "fences");
        clock_and_request_boundaries(root / "clock");
        historical_binding_must_match_at_accept(root / "historical");
        observed_term_invalidates_final_handoff(root / "term");
        consume_once_and_attempt_budget(root / "budgets");
        async_marker_checkpoint(root / "async-marker");
        std::filesystem::remove_all(root);
        std::cout << "F4 authority composition passed\n";
    } catch (const std::exception& error) {
        std::cerr << "F4 composition failed: " << error.what() << '\n';
        return 1;
    }
}
