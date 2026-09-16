#include "velocita/v2/replication.hpp"

#include "velocita/v2/artifact_digest.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;
using velocita::PeerMessage;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = std::byte{marker};
    value.bytes[1] = std::byte{0x5A};
    return value;
}

// --- the economic fixture, built from PUBLIC COMMANDS ONLY -------------------
//
// Deliberately not lifted from the kernel suites' in-memory fixtures. Those
// assign `TradingState` fields directly, which is right for testing a reducer in
// isolation and wrong here: the whole question is whether the reducer reaches
// the same state when every precondition has to survive proposal, replication
// and commit. A fixture that skipped consensus would be assuming the answer.

PolicyArtifact policy_artifact() {
    PolicyArtifact value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.schema_version = kPolicyArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(0x12);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(10), Directive::place_limit};
    return value;
}

StageExecutablePolicy executable_bundle() {
    StageExecutablePolicy value;
    value.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema.feature_schema_id = FeatureSchemaId{3};
    value.feature_schema.feature_count = 1;
    value.feature_schema.features[0] = {FeatureEconomicKind::price, 1};
    value.feature_schema.artifact_digest =
        compute_artifact_digest(value.feature_schema).digest;

    auto& executable = value.executable_policy;
    executable.schema_version = kExecutablePolicyArtifactSchemaVersion;
    executable.policy_id = PolicyId{10};
    executable.policy_version = PolicyVersion{1};
    executable.policy_artifact_digest = policy_artifact().artifact_digest;
    executable.feature_schema_id = value.feature_schema.feature_schema_id;
    executable.feature_schema_digest = value.feature_schema.artifact_digest;
    executable.template_count = 1;

    auto& action = executable.templates[0];
    action.schema_version = kActionTemplateSchemaVersion;
    action.directive = Directive::place_limit;
    action.side = Side::buy;
    action.time_in_force = TimeInForce::good_til_cancel;
    action.post_only = PostOnlyMode::required;
    action.order_slot = LogicalOrderSlot{1};
    action.instrument_spec_version = InstrumentSpecVersion{7};
    action.venue_capabilities_version = VenueCapabilitiesVersion{8};
    action.price_source = {EconomicValueSourceKind::feature, 0,
                           RoundingMode::toward_zero, {}, 1, {}};
    action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                              RoundingMode::toward_zero,
                              QuantityLots::from_atoms(2), 0};
    action.safety_caps = {QuantityLots::from_atoms(2),
                          MoneyMicros::from_atoms(10'000),
                          PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                          100, action.price_source};

    executable.rule_binding_count = 1;
    executable.rule_template_slots[0] = ActionTemplateSlot{1};
    executable.artifact_digest = compute_artifact_digest(executable).digest;

    value.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{9},
                        InstrumentId{4}, InstrumentSpecVersion{7},
                        MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(100), QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(10)};
    value.venue = {kVenueCapabilitiesSchemaVersion, VenueId{9},
                   VenueCapabilitiesVersion{8}, kVenueDirectivePlaceLimit,
                   kVenueTifGtc, 1};
    return value;
}

DecisionFrame decision_frame() {
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{1};
    value.source_id = SourceId{12};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{1};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(11);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = PositionVersion{1};
    value.risk_version = RiskVersion{1};
    value.raw_evidence_digest = digest(0x44);
    return value;
}

Command command(const std::uint64_t sequence, CommandBody body) {
    return {{CallerId{1}, RequestSequence{sequence},
             digest(static_cast<std::uint8_t>(0x60 + (sequence % 150)))},
            std::move(body)};
}

std::vector<std::byte> encoded(const Command& value) {
    std::array<std::byte, kMaxKernelCommandBytes> buffer{};
    const auto result = encode_kernel_command(value, buffer);
    require(result.error == KernelCodecError::none, "fixture command did not encode");
    return {buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(result.bytes_written)};
}

// --- the cluster harness ----------------------------------------------------

struct Cluster {
    std::filesystem::path root;
    std::vector<std::int32_t> members;
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;
    // Node ids whose peer traffic is dropped in both directions. This is how
    // quorum loss is modelled: the voters are alive and correct, they simply
    // cannot be reached, which is the realistic failure and the one that must
    // fail closed.
    std::set<std::int32_t> partitioned;

    Cluster(std::filesystem::path directory, const std::size_t voters)
        : root(std::move(directory)) {
        for (std::int32_t id = 1; id <= static_cast<std::int32_t>(voters); ++id) {
            members.push_back(id);
        }
        for (const auto id : members) open(id);
    }

    void open(const std::int32_t id) {
        nodes.push_back(std::make_unique<ReplicatedKernel>(
            id, root / std::to_string(id), members));
    }

    ReplicatedKernel& node(const std::int32_t id) {
        for (auto& candidate : nodes) {
            if (candidate->status().node_id == id) return *candidate;
        }
        throw std::runtime_error("no such node");
    }

    // Destroy and reconstruct one voter from its own durable files only. The
    // replacement shares nothing with its predecessor except the directory,
    // which is the entire point of a restart test.
    void restart(const std::int32_t id) {
        for (auto& candidate : nodes) {
            if (candidate != nullptr && candidate->status().node_id == id) {
                candidate.reset();
                break;
            }
        }
        nodes.erase(std::remove(nodes.begin(), nodes.end(), nullptr), nodes.end());
        open(id);
    }

    void route() {
        std::vector<PeerMessage> messages;
        for (auto& sender : nodes) {
            while (auto message = sender->take_outbound_message()) {
                messages.push_back(std::move(*message));
            }
        }
        for (const auto& message : messages) {
            if (partitioned.count(message.sender_id) != 0) continue;
            if (partitioned.count(message.recipient_id) != 0) continue;
            for (auto& recipient : nodes) {
                if (recipient->status().node_id != message.recipient_id) continue;
                // Through the wire codec, so a message that could not survive
                // encoding cannot pass here either.
                recipient->receive_peer(velocita::decode_peer_message(
                    velocita::encode_peer_message(message)));
                break;
            }
        }
    }

    // THE DURABILITY INVARIANT, checked after every single tick of every test
    // rather than in one place that could be skipped.
    //
    // With the pinned Raft patch a voter counts its own vote only for entries it
    // has made durable, so a commit index ahead of the durable index means the
    // gate is not doing its job. Checking it here makes every existing scenario
    // - elections, partitions, restarts, compaction - a witness for it.
    void check_durability_invariant() const {
        for (const auto& candidate : nodes) {
            const auto status = candidate->status();
            const auto durable = candidate->durable_index();
            require(status.commit_index <= static_cast<std::int64_t>(durable),
                    "node " + std::to_string(status.node_id) +
                        " committed past its durable index: commit " +
                        std::to_string(status.commit_index) + " durable " +
                        std::to_string(durable));
        }
    }

    void settle(const int rounds = 40) {
        for (int round = 0; round < rounds; ++round) {
            for (auto& candidate : nodes) candidate->flush_durability();
            for (auto& candidate : nodes) candidate->tick(10);
            route();
            check_durability_invariant();
        }
    }

    ReplicatedKernel* leader() {
        for (auto& candidate : nodes) {
            if (candidate->is_leader()) return candidate.get();
        }
        return nullptr;
    }

    ReplicatedKernel& elect() {
        for (int round = 0; round < 2000; ++round) {
            for (auto& candidate : nodes) candidate->flush_durability();
            for (auto& candidate : nodes) candidate->tick(10);
            route();
            check_durability_invariant();
            if (leader() != nullptr) {
                settle(20);
                if (auto* confirmed = leader()) return *confirmed;
            }
        }
        throw std::runtime_error("cluster did not elect a leader");
    }

    // Offer a command to the leader and drive the cluster until it commits.
    // Returns the committed result, which is the ONLY place an outcome exists.
    CommittedResult commit(const Command& value, const int rounds = 400) {
        auto& chosen = elect();
        const auto bytes = encoded(value);
        const auto outcome = chosen.propose(bytes);
        require(outcome.status == ProposeStatus::admitted,
                "leader refused a command: status " +
                    std::to_string(static_cast<int>(outcome.status)));
        for (int round = 0; round < rounds; ++round) {
            for (auto& candidate : nodes) candidate->flush_durability();
            for (auto& candidate : nodes) candidate->tick(10);
            route();
            check_durability_invariant();
            if (auto result = chosen.take_committed_result()) return *result;
        }
        throw std::runtime_error("command did not commit");
    }

    void drain_results() {
        for (auto& candidate : nodes) {
            while (candidate->take_committed_result()) {
            }
        }
    }
};

// Drive the cluster to an AUTHORIZED SUBMIT through consensus. Every
// precondition below is a replicated command, so reaching an outbox effect at
// all proves the reducer's full economic path survives the seam.
std::uint64_t build_authorized_submit(Cluster& cluster) {
    std::uint64_t sequence = 0;
    const auto bundle = executable_bundle();

    (void)cluster.commit(command(++sequence,
                                 SetStrategyMode{StrategyMode::normal, KillEpoch{},
                                                 ExecutorGeneration{}}));
    (void)cluster.commit(command(++sequence,
                                 ConfigureRiskLimits{
                                     AccountId{5}, InstrumentId{4},
                                     RiskPolicyVersion{6}, RiskVersion{},
                                     KillEpoch{}, ExecutorGeneration{},
                                     QuantityLots::from_atoms(4),
                                     QuantityLots::from_atoms(8),
                                     MoneyMicros::from_atoms(4'000),
                                     MoneyMicros::from_atoms(8'000), 1'000'000,
                                     2'500}));
    (void)cluster.commit(command(++sequence, PublishPolicy{policy_artifact()}));
    (void)cluster.commit(
        command(++sequence, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}));
    (void)cluster.commit(command(++sequence, bundle));
    (void)cluster.commit(command(
        ++sequence, ActivateExecutablePolicy{PolicyId{10}, PolicyVersion{1},
                                             bundle.executable_policy.artifact_digest}));
    (void)cluster.commit(command(++sequence,
                                 RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                   SourceSequence{1}, digest(0x44)}));
    const auto evaluated = cluster.commit(command(
        ++sequence,
        EvaluateDecisionFrame{DecisionId{20}, EvaluationMode::authoritative,
                              decision_frame()}));
    require(evaluated.result.decision_code == DecisionCode::action,
            "the replicated decision did not produce an action");
    const auto authorized =
        cluster.commit(command(++sequence, AuthorizeDecision{DecisionId{20}}));
    require(authorized.result.code == ApplyCode::applied &&
                authorized.result.effect_id().value() != 0,
            "authorization did not create an outbox effect through consensus");
    return sequence;
}

// --- G-035: only committed commands invoke the reducer ----------------------

void test_only_committed_commands_invoke_the_reducer(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();

    const auto before = leader.status();
    for (const auto& node : cluster.nodes) {
        require(node->status().state_digest == before.state_digest,
                "voters did not start from one state");
    }

    // Admitted, then deliberately NOT routed. The entry is in the leader's log
    // and no quorum has acknowledged it, so no voter may have applied it.
    const auto bytes = encoded(command(
        1, SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}}));
    const auto outcome = leader.propose(bytes);
    require(outcome.status == ProposeStatus::admitted, "leader refused the command");

    for (auto& node : cluster.nodes) node->tick(0);
    for (const auto& node : cluster.nodes) {
        const auto now = node->status();
        require(now.state_digest == before.state_digest,
                "an uncommitted command reached the reducer on node " +
                    std::to_string(now.node_id));
        require(now.applied_index == before.applied_index,
                "an uncommitted command advanced the applied index");
    }
    require(!leader.take_committed_result(),
            "an uncommitted command produced a result");

    // Now let it commit. The SAME command must now take effect exactly once.
    cluster.settle(200);
    const auto result = leader.take_committed_result();
    require(result.has_value(), "the command never committed");
    require(result->result.code == ApplyCode::applied, "the committed command failed");
    require(leader.status().state_digest != before.state_digest,
            "a committed command did not change the state");
}

// --- G-036: follower routing preserves command identity ---------------------

void test_follower_routing_preserves_command_identity(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;

    const auto value = command(
        1, SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}});
    const auto bytes = encoded(value);

    ReplicatedKernel* follower = nullptr;
    for (auto& node : cluster.nodes) {
        if (!node->is_leader()) follower = node.get();
    }
    require(follower != nullptr, "a three-voter cluster had no follower");

    const auto refused = follower->propose(bytes);
    require(refused.status == ProposeStatus::not_leader,
            "a follower accepted a command");
    require(refused.leader_id == leader_id,
            "a follower did not name the current leader, so a client cannot route");

    // Routed to the leader UNCHANGED - same identity, same bytes - and the
    // committed result carries that identity back. Routing that mutated identity
    // would break the reducer's dedup and make a retry a second mutation.
    const auto committed = cluster.commit(value);
    require(committed.identity == value.identity,
            "the committed result did not carry the original command identity");
}

// --- G-037: leader crash cut points -----------------------------------------

void test_a_leader_crash_before_commit_creates_no_effect(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    const auto sequence = build_authorized_submit(cluster);
    cluster.drain_results();

    const auto before = cluster.elect().status();
    require(before.outbox_effect_count == 1, "the fixture did not leave one effect");

    // A second authorization, admitted and then abandoned by killing the leader
    // before any peer hears about it.
    (void)cluster.commit(command(sequence + 1,
                                 RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                   SourceSequence{2}, digest(0x45)}));
    cluster.drain_results();

    auto frame = decision_frame();
    frame.source_sequence = SourceSequence{2};
    frame.venue_sequence = VenueSequence{2};
    frame.raw_evidence_digest = digest(0x45);
    const auto doomed = encoded(command(
        sequence + 2, EvaluateDecisionFrame{DecisionId{21},
                                            EvaluationMode::authoritative, frame}));
    auto& current = cluster.elect();
    require(current.propose(doomed).status == ProposeStatus::admitted,
            "the doomed command was refused");

    cluster.restart(current.status().node_id);
    cluster.settle(400);

    for (const auto& node : cluster.nodes) {
        require(node->state().decision_count <= 1,
                "a command that never committed left a decision behind on node " +
                    std::to_string(node->status().node_id));
        require(node->status().outbox_effect_count == before.outbox_effect_count,
                "a command that never committed created an outbox effect");
    }
}

void test_a_leader_crash_after_commit_reproduces_the_same_effect(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)build_authorized_submit(cluster);
    cluster.drain_results();
    cluster.settle(200);

    auto& leader = cluster.elect();
    const auto before = leader.status();
    require(before.outbox_effect_count == 1, "the fixture did not create an effect");
    const auto effect_before = leader.state().outbox_effects[0];

    cluster.restart(before.node_id);
    cluster.settle(400);

    auto& recovered = cluster.node(before.node_id);
    const auto after = recovered.status();
    require(after.outbox_effect_count == 1,
            "a committed effect did not survive the leader's restart");
    require(recovered.state().outbox_effects[0].effect_id == effect_before.effect_id,
            "the recovered effect had a different identity, so a retry would be a "
            "second order");
    require(after.state_digest == before.state_digest,
            "replaying the committed log reproduced a different state");
}

// --- G-036: a retry after a lost response returns the retained result --------

void test_a_retry_after_a_lost_response_returns_the_retained_result(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    const auto value = command(
        1, SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}});
    const auto first = cluster.commit(value);
    require(first.result.code == ApplyCode::applied, "the first attempt failed");
    const auto after_first = cluster.elect().status();

    // The client never saw the result and retries the IDENTICAL command. It
    // commits again - the log does not deduplicate - and the reducer answers
    // from its retained receipt without mutating anything.
    const auto second = cluster.commit(value);
    require(second.identity == first.identity, "the retry lost its identity");
    require(second.result.state_version == first.result.state_version,
            "the retry did not replay the original result");

    const auto after_second = cluster.elect().status();
    require(after_second.state_version == after_first.state_version,
            "the retry advanced the state machine a second time");
    for (const auto& node : cluster.nodes) {
        require(node->status().state_digest == after_first.state_digest,
                "the retry changed a voter's state");
    }
}

// --- G-038: quorum loss fails closed ----------------------------------------

void test_quorum_loss_creates_no_new_risk_decision(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    const auto sequence = build_authorized_submit(cluster);
    cluster.drain_results();
    cluster.settle(100);

    auto& leader = cluster.elect();
    const auto before = leader.status();

    // Both followers become unreachable. The leader is alive and correct and
    // simply cannot obtain a quorum, which is the realistic failure.
    for (const auto& node : cluster.nodes) {
        const auto id = node->status().node_id;
        if (id != before.node_id) cluster.partitioned.insert(id);
    }

    auto& isolated = cluster.node(before.node_id);
    const auto bytes = encoded(command(
        sequence + 1, RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                        SourceSequence{2}, digest(0x45)}));
    const auto outcome = isolated.propose(bytes);

    // Admission into a local log is not authority. What must not happen is an
    // APPLIED effect, on any voter, while no quorum exists.
    for (int round = 0; round < 400; ++round) {
        for (auto& node : cluster.nodes) node->tick(10);
        cluster.route();
    }
    for (const auto& node : cluster.nodes) {
        require(node->status().outbox_effect_count == before.outbox_effect_count,
                "new risk appeared while the cluster had no quorum");
        require(node->status().state_digest == before.state_digest,
                "state advanced on node " + std::to_string(node->status().node_id) +
                    " while the cluster had no quorum");
    }
    require(!isolated.take_committed_result(),
            "a command committed without a quorum");
    (void)outcome;

    // Healing the partition must let the same command through, or the test
    // proves only that it was malformed.
    //
    // It is RE-PROPOSED rather than waited for. An entry appended by a leader
    // that then lost quorum is uncommitted, and Raft may legitimately truncate
    // it when a new leader is chosen - losing it is correct, and a client
    // retries. Waiting for the original to appear would be asserting something
    // Raft never promised.
    cluster.partitioned.clear();
    const auto healed = cluster.commit(command(
        sequence + 1, RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                        SourceSequence{2}, digest(0x45)}));
    require(healed.result.code == ApplyCode::applied,
            "the command never committed even once quorum returned, so the "
            "refusal above proved nothing about quorum");
}

// --- G-040: restart and dedup ------------------------------------------------

void test_a_restarted_voter_recovers_state_outbox_and_dedup(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    const auto sequence = build_authorized_submit(cluster);
    cluster.drain_results();
    cluster.settle(200);

    const auto before = cluster.elect().status();
    std::int32_t follower_id = 0;
    ReplicaStatus follower_before{};
    for (auto& node : cluster.nodes) {
        if (node->is_leader()) continue;
        follower_id = node->status().node_id;
        follower_before = node->status();
    }
    require(follower_id != 0, "no follower to restart");

    cluster.restart(follower_id);
    cluster.settle(400);

    const auto after = cluster.node(follower_id).status();
    require(after.state_digest == follower_before.state_digest,
            "a restarted voter recovered a different state");
    require(after.outbox_effect_count == follower_before.outbox_effect_count,
            "a restarted voter lost outbox effects");
    require(after.applied_index == follower_before.applied_index,
            "a restarted voter did not replay its committed prefix");

    // Dedup survives too: an already-applied command re-presented after the
    // restart is still answered from the retained receipt.
    const auto replayed = cluster.commit(command(
        sequence, AuthorizeDecision{DecisionId{20}}));
    require(replayed.result.code == ApplyCode::already_applied ||
                replayed.result.code == ApplyCode::applied,
            "a replayed command after restart produced an unexpected code");
    for (const auto& node : cluster.nodes) {
        require(node->status().outbox_effect_count == before.outbox_effect_count,
                "a replayed authorization created a second effect after restart");
    }
}

// --- G-039: convergence ------------------------------------------------------

void test_all_voters_converge(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)build_authorized_submit(cluster);
    cluster.drain_results();
    cluster.settle(400);

    const auto reference = cluster.nodes.front()->status();
    for (const auto& node : cluster.nodes) {
        const auto now = node->status();
        const auto where = " on node " + std::to_string(now.node_id);
        require(now.state_digest == reference.state_digest,
                "voters disagree about state" + where);
        require(now.commit_index == reference.commit_index,
                "voters disagree about commit index" + where);
        require(now.applied_index == reference.applied_index,
                "voters disagree about applied index" + where);
        require(now.outbox_effect_count == reference.outbox_effect_count,
                "voters disagree about the outbox watermark" + where);
        require(now.state_version == reference.state_version,
                "voters disagree about state version" + where);
    }
    require(reference.outbox_effect_count == 1,
            "the convergence check ran on a cluster with nothing to converge on");
}


// --- Phase 6 leftover: the committed-outbox read contract -------------------

void test_a_committed_outbox_read_carries_its_own_authority(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)build_authorized_submit(cluster);
    cluster.drain_results();
    cluster.settle(200);

    auto& leader = cluster.elect();
    const auto from_leader = leader.committed_outbox();
    require(from_leader.authoritative,
            "a leader's own outbox read did not claim authority");
    require(from_leader.effect_count == 1, "the leader read no committed effect");
    require(from_leader.effects[0].effect_id.value() != 0,
            "the read returned an effect with no identity");
    require(from_leader.applied_index > 0,
            "an authoritative read carried no applied index");

    // A follower holds the same BYTES and must not present them as
    // authoritative. That distinction is the entire contract: without it a
    // caller cannot tell a current view from a stale one, and both look correct.
    for (const auto& node : cluster.nodes) {
        if (node->is_leader()) continue;
        const auto from_follower = node->committed_outbox();
        require(!from_follower.authoritative,
                "a follower presented its outbox as authoritative");
        require(from_follower.effect_count == from_leader.effect_count,
                "a follower disagreed about committed outbox contents");
        require(from_follower.leader_id == from_leader.leader_id,
                "a follower named a different leader");
    }
}

// --- Phase 7a: durable snapshots, compaction and install --------------------

void test_a_snapshot_is_durable_before_its_prefix_is_reclaimed(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)build_authorized_submit(cluster);
    cluster.settle(50);

    auto& only = cluster.elect();
    const auto before = only.status();
    const auto wal_before = WalSegment(root / "1").read_records().size();
    require(wal_before > 1, "the fixture did not produce a log worth compacting");

    const auto included = only.compact();
    require(included > 0, "compaction covered nothing");

    // The ORDER is the property. After compaction both must be true at once:
    // the snapshot exists and decodes, and the log no longer holds the prefix.
    // If reclamation could precede durability, a crash between them would leave
    // neither.
    const auto stored = SnapshotStore(root / "1").load();
    require(stored.has_value(), "compaction reclaimed a prefix with no snapshot");
    require(stored->last_included_index == included,
            "the snapshot did not cover the prefix that was reclaimed");
    require(stored->state_digest == before.state_digest,
            "the snapshot captured a different state than the replica held");

    const auto wal_after = WalSegment(root / "1").read_records().size();
    require(wal_after < wal_before, "compaction reclaimed nothing from the log");
    for (const auto& record : WalSegment(root / "1").read_records()) {
        require(record.index > included,
                "a reclaimed entry is still in the log");
    }
}

void test_a_restart_installs_the_snapshot_and_replays_the_rest(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    const auto sequence = build_authorized_submit(cluster);
    cluster.settle(50);
    const auto included = cluster.elect().compact();
    require(included > 0, "compaction covered nothing");

    // More commands AFTER the snapshot, so recovery has to do both halves:
    // install the snapshot, then replay the suffix the snapshot does not cover.
    (void)cluster.commit(command(sequence + 1,
                                 RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                                   SourceSequence{2}, digest(0x45)}));
    cluster.settle(50);
    const auto before = cluster.elect().status();
    require(before.applied_index > static_cast<std::int64_t>(included),
            "the fixture left nothing beyond the snapshot to replay");

    cluster.restart(1);
    cluster.settle(200);

    const auto after = cluster.node(1).status();
    require(after.state_digest == before.state_digest,
            "installing the snapshot and replaying the suffix produced a "
            "different state");
    require(after.applied_index == before.applied_index,
            "recovery did not reach the pre-restart applied index");
    require(after.outbox_effect_count == before.outbox_effect_count,
            "recovery lost outbox effects held only in the snapshot");
}

void test_a_corrupt_snapshot_degrades_to_replay_rather_than_refusing_to_start(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)build_authorized_submit(cluster);
    cluster.settle(50);
    const auto before = cluster.elect().status();

    // No compaction, so the log still holds everything. Corrupting the snapshot
    // must therefore cost nothing at all.
    SnapshotStore store(root / "1");
    store.save(cluster.elect().state(), 1, 1);
    require(store.load().has_value(), "the fixture snapshot did not round-trip");

    {
        std::fstream file(store.path(), std::ios::binary | std::ios::in | std::ios::out);
        require(file.good(), "could not open the snapshot to corrupt it");
        file.seekp(20);
        const char rubbish = 0x7F;
        file.write(&rubbish, 1);
    }
    require(!store.load().has_value(),
            "a corrupted snapshot still loaded, so the checksum proves nothing");

    // The node must still start. A snapshot is an optimisation over replay, and
    // an unreadable one has to degrade to the thing it optimised - refusing to
    // start would turn a recoverable disk fault into an outage.
    cluster.restart(1);
    cluster.settle(200);
    require(cluster.node(1).status().state_digest == before.state_digest,
            "a node with a corrupt snapshot did not recover by replay");
}

void test_recovery_is_never_behind_the_last_acknowledged_commit(
    const std::filesystem::path& root) {
    // The power-cut property, stated as the invariant rather than as a fault
    // injection: whatever a caller was told had committed must still be there
    // after a restart, at every point where the durable artefacts differ.
    Cluster cluster(root, 1);
    const auto sequence = build_authorized_submit(cluster);
    cluster.settle(50);

    struct Point {
        const char* name;
        bool compact_here;
        bool extra_command;
    };
    const std::array<Point, 3> points{{{"no snapshot", false, false},
                                       {"snapshot only", true, false},
                                       {"snapshot then more", true, true}}};

    std::uint64_t next = sequence;
    for (const auto& point : points) {
        if (point.compact_here) (void)cluster.elect().compact();
        if (point.extra_command) {
            (void)cluster.commit(command(
                ++next, RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                          SourceSequence{2}, digest(0x45)}));
        }
        cluster.settle(50);

        const auto acknowledged = cluster.elect().status();
        cluster.restart(1);
        cluster.settle(200);
        const auto recovered = cluster.node(1).status();

        const std::string where = std::string(" at ") + point.name;
        require(recovered.state_version >= acknowledged.state_version,
                "recovery went backwards" + where);
        require(recovered.state_digest == acknowledged.state_digest,
                "recovery produced a different state" + where);
        require(recovered.outbox_effect_count == acknowledged.outbox_effect_count,
                "recovery lost an acknowledged outbox effect" + where);
    }
}


// --- Phase 7b: durability is a separate index from commit -------------------

void test_an_entry_cannot_commit_before_its_bytes_are_durable(
    const std::filesystem::path& root) {
    // THE GATE, proved by withholding the disk.
    //
    // An earlier version of this check only asserted `commit <= durable` while
    // the harness flushed on every tick. That invariant held trivially, because
    // the gap it describes was closed before anything could observe it - and
    // removing the gate entirely still passed. The test proved nothing.
    //
    // Here the cluster ticks WITHOUT flushing, so the entry is staged and never
    // synced, and the leader's own vote is the only one available.
    Cluster cluster(root, 1);
    auto& node = cluster.elect();

    const auto value = command(
        1, SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}});
    const auto before_commit = node.status().commit_index;
    const auto before_durable = node.durable_index();

    require(node.propose(encoded(value)).status == ProposeStatus::admitted,
            "the leader refused the command");

    for (int round = 0; round < 50; ++round) {
        node.tick(10);  // no flush_durability: the disk never happens
        require(node.durable_index() == before_durable,
                "durability advanced with no flush");
        require(node.status().commit_index == before_commit,
                "an entry committed while its bytes were only staged");
        require(!node.take_committed_result(),
                "a result appeared for an entry that was never durable");
    }
    require(node.status().current_index > before_commit,
            "the entry never reached the log, so nothing was being withheld");

    // Now let the disk happen. The same entry must commit and apply.
    for (int round = 0; round < 50; ++round) {
        node.flush_durability();
        node.tick(10);
        if (node.status().commit_index > before_commit) break;
    }
    require(node.durable_index() > before_durable,
            "flushing did not advance durability");
    require(node.status().commit_index > before_commit,
            "the entry never committed even once durable, so the gate is a "
            "deadlock rather than a gate");
    require(node.take_committed_result().has_value(),
            "no result arrived after the entry became durable");
}

void test_a_writer_thread_carries_durability_off_the_owner_loop(
    const std::filesystem::path& root) {
    // The same cluster, with the I/O moved to another thread. Nothing about the
    // guarantees may change: the point of the thread is WHERE the fsync happens,
    // not whether it has to happen before an entry counts.
    Cluster cluster(root, 1);
    auto& node = cluster.elect();
    node.start_writer();

    std::uint64_t sequence = 0;
    const auto value = command(
        ++sequence,
        SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}});
    require(node.propose(encoded(value)).status == ProposeStatus::admitted,
            "the leader refused the command");

    std::optional<CommittedResult> committed;
    for (int round = 0; round < 4000 && !committed; ++round) {
        node.flush_durability();   // submits and collects; the thread writes
        node.tick(10);
        committed = node.take_committed_result();
        // The invariant still holds at every observation, now with a real
        // thread between the append and the sync.
        require(node.status().commit_index <=
                    static_cast<std::int64_t>(node.durable_index()),
                "a threaded writer let commit run ahead of durability");
    }
    require(committed.has_value(), "the command never committed with a writer thread");
    require(committed->result.code == ApplyCode::applied, "the command failed");

    const auto metrics = node.wal_metrics();
    require(metrics.blocks_submitted > 0,
            "no blocks were handed to the writer, so the thread did nothing");
    require(metrics.blocks_durable > 0, "the writer thread made nothing durable");
    require(metrics.backpressure_refusals == 0,
            "the arena refused a record under a trivial load");
    require(metrics.durable_index >= metrics.highest_staged_index,
            "durability never caught up with staging");
    require(metrics.durability_lag() == 0,
            "the writer thread left a durability gap after settling");

    node.stop_writer();

    // And the durable bytes are real: a restart with no thread reconstructs the
    // same state from what the thread put on disk.
    const auto before = node.status();
    cluster.restart(1);
    cluster.settle(200);
    require(cluster.node(1).status().state_digest == before.state_digest,
            "state written by the writer thread did not survive a restart");
}

void test_durability_is_tracked_separately_and_never_overstated(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)build_authorized_submit(cluster);
    cluster.settle(50);

    auto& node = cluster.elect();
    const auto durable = node.durable_index();
    const auto status = node.status();

    // Non-vacuity first: records really are going through the block segment, so
    // everything below is about the new path and not the legacy one.
    const auto staged = WalSegment(root / "1").read_records();
    require(!staged.empty(),
            "no records reached the block segment, so this test is measuring the "
            "old synchronous path");
    require(durable >= staged.back().index,
            "the durable index does not cover the records on disk");

    // The persisted hard state is a statement about BYTES ON DISK. willemt can
    // advance its in-memory commit index past what this node has synced, since
    // it counts the leader's own vote unconditionally; persisting that would
    // leave a restart reading a commit index past the end of its own log.
    const auto persisted = velocita::MetadataStore(root / "1").load();
    require(persisted.commit_index <= static_cast<std::int64_t>(durable),
            "the persisted commit index ran ahead of durability: commit " +
                std::to_string(persisted.commit_index) + " durable " +
                std::to_string(durable));
    require(persisted.commit_index <= status.current_index,
            "the persisted commit index is past the end of the log");

    // And the whole point: a restart reconstructs from those durable bytes.
    cluster.restart(1);
    cluster.settle(200);
    require(cluster.node(1).status().state_digest == status.state_digest,
            "recovery from the block segment produced a different state");
}

// --- the same suite on a single voter ----------------------------------------

void test_one_voter_reaches_the_same_state(const std::filesystem::path& root) {
    // The gate requires the deterministic tests to pass through ONE and three
    // voters. A single voter commits without any peer, so this isolates whether
    // anything in the economic path depended on replication rather than on
    // consensus being satisfied.
    Cluster single(root / "single", 1);
    (void)build_authorized_submit(single);
    single.settle(100);
    const auto one = single.elect().status();

    Cluster three(root / "three", 3);
    (void)build_authorized_submit(three);
    three.settle(400);
    const auto many = three.elect().status();

    require(one.outbox_effect_count == many.outbox_effect_count,
            "one voter and three voters produced different outbox contents");
    require(one.state_version == many.state_version,
            "one voter and three voters produced different state versions");
    require(one.state_digest == many.state_digest,
            "one voter and three voters produced different states from the same "
            "commands");
}

}  // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          "velocita-v2-replication-tests";
        std::filesystem::remove_all(root);
        const auto fresh = [&](const char* name) {
            const auto path = root / name;
            std::filesystem::create_directories(path);
            return path;
        };

        test_only_committed_commands_invoke_the_reducer(fresh("committed-only"));
        test_follower_routing_preserves_command_identity(fresh("routing"));
        test_a_leader_crash_before_commit_creates_no_effect(fresh("cut-before"));
        test_a_leader_crash_after_commit_reproduces_the_same_effect(fresh("cut-after"));
        test_a_retry_after_a_lost_response_returns_the_retained_result(fresh("retry"));
        test_quorum_loss_creates_no_new_risk_decision(fresh("quorum"));
        test_a_restarted_voter_recovers_state_outbox_and_dedup(fresh("restart"));
        test_all_voters_converge(fresh("converge"));
        test_a_committed_outbox_read_carries_its_own_authority(fresh("outbox-read"));
        test_a_snapshot_is_durable_before_its_prefix_is_reclaimed(fresh("compact"));
        test_a_restart_installs_the_snapshot_and_replays_the_rest(fresh("install"));
        test_a_corrupt_snapshot_degrades_to_replay_rather_than_refusing_to_start(
            fresh("corrupt-snapshot"));
        test_recovery_is_never_behind_the_last_acknowledged_commit(fresh("power-cut"));
        test_durability_is_tracked_separately_and_never_overstated(fresh("durable"));
        test_an_entry_cannot_commit_before_its_bytes_are_durable(fresh("gate"));
        test_a_writer_thread_carries_durability_off_the_owner_loop(fresh("writer"));
        test_one_voter_reaches_the_same_state(fresh("voter-count"));

        std::filesystem::remove_all(root);
        std::cout << "all V2 replication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 replication test failed: " << error.what() << "\n";
        return 1;
    }
}
