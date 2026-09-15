#include "velocita/v2/replication.hpp"

#include "velocita/v2/artifact_digest.hpp"

#include <array>
#include <filesystem>
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

    void settle(const int rounds = 40) {
        for (int round = 0; round < rounds; ++round) {
            for (auto& candidate : nodes) candidate->tick(10);
            route();
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
            for (auto& candidate : nodes) candidate->tick(10);
            route();
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
            for (auto& candidate : nodes) candidate->tick(10);
            route();
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
        test_one_voter_reaches_the_same_state(fresh("voter-count"));

        std::filesystem::remove_all(root);
        std::cout << "all V2 replication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 replication test failed: " << error.what() << "\n";
        return 1;
    }
}
