#include "velocita/v2/replication.hpp"

#include "velocita/v2/artifact_digest.hpp"
#include "velocita/v2/digest.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <filesystem>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using namespace velocita::v2;
using velocita::PeerMessage;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Content digest of a file, for asserting a file was NOT touched.
std::string file_digest(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "<missing>";
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    return std::to_string(bytes.size()) + ":" +
           std::to_string(std::hash<std::string>{}(bytes));
}

using DirectoryImage = std::map<std::string, std::vector<std::byte>>;

DirectoryImage directory_image(const std::filesystem::path& root) {
    DirectoryImage image;
    if (!std::filesystem::exists(root)) return image;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        require(input.good(), "could not capture persisted directory bytes");
        const std::string raw((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            bytes[i] = std::byte{static_cast<unsigned char>(raw[i])};
        }
        image.emplace(std::filesystem::relative(entry.path(), root).generic_string(),
                      std::move(bytes));
    }
    return image;
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "could not read fixture bytes");
    const std::string raw((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        bytes[i] = std::byte{static_cast<unsigned char>(raw[i])};
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path,
                 const std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "could not write fixture bytes");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(output.good(), "could not finish fixture bytes");
}

void write_le(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint64_t value, const std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

void reseal_bound(std::vector<std::byte>& bytes) {
    const std::string domain = "velocita.v2.bound-frame.v1";
    std::vector<std::byte> covered;
    for (const auto c : domain) covered.push_back(static_cast<std::byte>(c));
    covered.insert(covered.end(), bytes.begin(), bytes.end() - 32);
    const auto digest = sha256_digest(covered);
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.end() - 32);
}

std::uint64_t read_le(const std::span<const std::byte> bytes,
                      const std::size_t offset, const std::size_t width) {
    std::uint64_t value{};
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<unsigned char>(bytes[offset + i]))
                 << (i * 8U);
    }
    return value;
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
                   VenueCapabilitiesVersion{8}, (kVenueDirectivePlaceLimit | kVenueDirectiveCancel),
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

// Same, with the caller spelled out, for the tests that care which one it is.
Command command_from(const std::uint64_t caller, const std::uint64_t sequence,
                     CommandBody body) {
    return {{CallerId{caller}, RequestSequence{sequence},
             digest(static_cast<std::uint8_t>(0x60 + (sequence % 150)))},
            std::move(body)};
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

std::vector<std::byte> unsupported_command(
    Command value, const std::uint16_t schema = kKernelCodecSchemaVersion + 1) {
    auto bytes = encoded(value);
    write_le(bytes, kKernelCommandSchemaOffset,
             schema, 2);
    const auto digest = sha256_digest(std::span<const std::byte>(bytes).subspan(
        kKernelCommandDigestInputOffset,
        bytes.size() - kKernelCommandDigestInputOffset - kDigestBytes));
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.end() - kDigestBytes);
    return bytes;
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
    // Sees every message before any filtering; `drop` then filters further.
    std::function<void(const PeerMessage&)> observe;
    std::function<bool(const PeerMessage&)> drop;
    // Election safety: at most one leader per term, over the whole run.
    std::map<std::int64_t, std::int32_t> leader_of_term;
    std::uint8_t lineage_tag{};

    Cluster(std::filesystem::path directory, const std::size_t voters,
            const std::uint8_t tag = 0xAB, const bool recover_existing = false)
        : root(std::move(directory)), lineage_tag(tag) {
        for (std::int32_t id = 1; id <= static_cast<std::int32_t>(voters); ++id) {
            members.push_back(id);
        }
        for (const auto id : members) open(id, !recover_existing);
    }

    void open(const std::int32_t id, const bool bootstrap = false) {
        auto startup = bootstrap
            ? identity_test::bootstrap(root / std::to_string(id), id, members,
                                       catalog_test::standard_catalog(), lineage_tag)
            : identity_test::recover(root / std::to_string(id), id, members,
                                     catalog_test::standard_catalog(), lineage_tag);
        nodes.push_back(std::make_unique<ReplicatedKernel>(
            std::move(startup), StorageCheckpoints{}));
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
        open(id, false);
    }

    void route() {
        std::vector<PeerMessage> messages;
        for (auto& sender : nodes) {
            while (auto message = identity_test::take_outbound_message(*sender)) {
                messages.push_back(std::move(*message));
            }
        }
        for (const auto& message : messages) {
            if (observe) observe(message);
            if (partitioned.count(message.sender_id) != 0) continue;
            if (partitioned.count(message.recipient_id) != 0) continue;
            if (drop && drop(message)) continue;
            for (auto& recipient : nodes) {
                if (recipient->status().node_id != message.recipient_id) continue;
                // Through the wire codec, so a message that could not survive
                // encoding cannot pass here either.
                identity_test::deliver_peer(*recipient, message,
                    node(message.sender_id).static_identity());
                break;
            }
        }
        check_one_leader_per_term();
    }

    void check_one_leader_per_term() {
        for (const auto& candidate : nodes) {
            if (!candidate->is_leader()) continue;
            const auto status = candidate->status();
            const auto [seen, fresh] = leader_of_term.emplace(status.term, status.node_id);
            require(fresh || seen->second == status.node_id,
                    "two leaders in term " + std::to_string(status.term) + ": " +
                        std::to_string(seen->second) + " and " + std::to_string(status.node_id));
        }
    }

    void step() {
        for (auto& candidate : nodes) candidate->flush_durability();
        for (auto& candidate : nodes) candidate->tick(10);
        route();
        check_durability_invariant();
    }

    // THE DURABILITY INVARIANT, checked after every single tick of every test
    // rather than in one place that could be skipped: whatever ANY voter
    // believes is committed is durable on a MAJORITY of voters.
    //
    // Not "commit <= durable on every node": a follower learns commitment from
    // its leader and may hold (and apply) it before its own copy is on disk; it
    // only ever persists what it holds (see
    // test_zero_durable_follower_does_not_persist_volatile_commit). With three
    // voters two durable followers can also commit before the leader's own
    // sync. For one or two voters the majority form is at least as strict.
    // Every scenario - elections, partitions, restarts, compaction - is a
    // witness for it.
    void check_durability_invariant() const {
        std::int64_t highest = 0;
        std::int32_t claimant = 0;
        for (const auto& candidate : nodes) {
            const auto status = candidate->status();
            if (status.commit_index > highest) {
                highest = status.commit_index;
                claimant = status.node_id;
            }
        }
        std::size_t durable = 0;
        std::string positions;
        for (const auto& candidate : nodes) {
            if (static_cast<std::int64_t>(candidate->durable_index()) >= highest) ++durable;
            positions += " " + std::to_string(candidate->status().node_id) + ":" +
                         std::to_string(candidate->durable_index());
        }
        require(durable >= members.size() / 2 + 1,
                "node " + std::to_string(claimant) + " believes index " + std::to_string(highest) +
                    " committed but only " + std::to_string(durable) + " of " +
                    std::to_string(members.size()) + " voters hold it durably (durable:" + positions + ")");
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

    (void)cluster.commit(command(++sequence, AdvanceKillEpoch{KillEpoch{1}}));
    (void)cluster.commit(command(
        ++sequence, SetExecutorGeneration{ExecutorGeneration{1}}));
    require(cluster.commit(command(++sequence, InstallExecutionCatalog{
                catalog_test::standard_catalog().root(), KillEpoch{1},
                ExecutorGeneration{1}})).result.code == ApplyCode::applied,
            "replicated catalog installation failed");
    (void)cluster.commit(command(
        ++sequence, SetStrategyMode{StrategyMode::normal, KillEpoch{1},
                                    ExecutorGeneration{1}}));
    (void)cluster.commit(command(++sequence,
                                 ConfigureRiskLimits{
                                     AccountId{5}, InstrumentId{4},
                                     RiskPolicyVersion{6}, RiskVersion{},
                                     KillEpoch{1}, ExecutorGeneration{1},
                                     QuantityLots::from_atoms(4),
                                     QuantityLots::from_atoms(8),
                                     MoneyMicros::from_atoms(4'000),
                                     MoneyMicros::from_atoms(8'000), 1'000'000,
                                     2'500, QuantityLots::from_atoms(1'000), true, 1}));
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
    const auto effect = materialize_order_effect(
        cluster.elect().state(), authorized.result.effect_id());
    require(effect.has_value() && effect->kill_epoch == KillEpoch{1} &&
                effect->executor_generation == ExecutorGeneration{1},
            "replicated authorization did not produce fully materializable bytes");
    return sequence;
}

// The kill path end to end: a mass cancel committed through consensus must be
// answerable to its operator (the node encodes every committed result it
// delivers) and must leave every replica able to report and recover its state.
// Status and snapshots both run the canonical snapshot encoder over the retained
// receipt, so an unadmitted result detail stops every replica, not just one.
void test_a_committed_mass_cancel_is_answerable_and_recoverable(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto sequence = build_authorized_submit(cluster);
    const auto flattened = cluster.commit(command(
        ++sequence, RequestMassCancel{MassCancelScope::all, {}, {}, KillEpoch{1},
                                      ExecutorGeneration{1}}));
    // build_authorized_submit leaves exactly one live, cancellable order.
    require(flattened.result.code == ApplyCode::applied &&
                flattened.result.detail_kind == ApplyResultDetailKind::mass_cancel &&
                flattened.result.mass_in_scope() == 1 && flattened.result.mass_cancelled() == 1 &&
                flattened.result.mass_skipped() == 0,
            "the replicated mass cancel did not cancel the one live order");
    std::array<std::byte, kKernelResultBytes> answer{};
    require(encode_kernel_result(flattened.result, answer).error == KernelCodecError::none,
            "a committed mass-cancel result could not be encoded for its operator");
    cluster.settle();
    const auto digest = cluster.elect().status().state_digest;
    for (auto& node : cluster.nodes)
        require(node->status().state_digest == digest,
                "replicas disagree after a committed mass cancel");
    cluster.restart(2);
    cluster.settle(200);
    require(cluster.node(2).status().state_digest == digest,
            "a replica restarted over a committed mass cancel did not recover the same state");
}

void test_zero_authority_bootstrap_cannot_authorize_an_effect(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    std::uint64_t sequence = 0;
    const auto bundle = executable_bundle();

    require(cluster.commit(command(
                ++sequence, SetStrategyMode{StrategyMode::normal, KillEpoch{},
                                            ExecutorGeneration{}})).result.code ==
                ApplyCode::applied,
            "zero-authority bootstrap did not enter normal mode");
    (void)cluster.commit(command(
        ++sequence, ConfigureRiskLimits{AccountId{5}, InstrumentId{4},
            RiskPolicyVersion{6}, RiskVersion{}, KillEpoch{}, ExecutorGeneration{},
            QuantityLots::from_atoms(4), QuantityLots::from_atoms(8),
            MoneyMicros::from_atoms(4'000), MoneyMicros::from_atoms(8'000),
            1'000'000, 2'500, QuantityLots::from_atoms(1'000), true, 1}));
    (void)cluster.commit(command(++sequence, PublishPolicy{policy_artifact()}));
    (void)cluster.commit(command(
        ++sequence, ValidatePolicy{PolicyId{10}, PolicyVersion{1}}));
    (void)cluster.commit(command(++sequence, bundle));
    (void)cluster.commit(command(
        ++sequence, ActivateExecutablePolicy{PolicyId{10}, PolicyVersion{1},
                                             bundle.executable_policy.artifact_digest}));
    (void)cluster.commit(command(
        ++sequence, RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                      SourceSequence{1}, digest(0x44)}));
    const auto evaluated = cluster.commit(command(
        ++sequence, EvaluateDecisionFrame{DecisionId{20},
                                          EvaluationMode::authoritative,
                                          decision_frame()}));
    require(evaluated.result.code == ApplyCode::catalog_not_installed,
            "rootless bootstrap reached decision evaluation");

    const auto refused = cluster.commit(command(
        ++sequence, AuthorizeDecision{DecisionId{20}}));
    require(refused.result.code == ApplyCode::catalog_not_installed,
            "public zero-authority bootstrap authorized an outbound effect");
    const auto& state = cluster.elect().state();
    require(state.reservation_count == 0 && state.order_count == 0 &&
                state.outbox_effect_count == 0,
            "public zero-authority refusal partially created economics");
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
    const auto wal_before = identity_test::wal_segment(root / "1").read_records().size();
    require(wal_before > 1, "the fixture did not produce a log worth compacting");

    const auto included = identity_test::complete_compaction(only);
    require(included > 0, "compaction covered nothing");

    // The ORDER is the property. After compaction both must be true at once:
    // the snapshot exists and decodes, and the log no longer holds the prefix.
    // If reclamation could precede durability, a crash between them would leave
    // neither.
    const auto stored = identity_test::snapshot_store(root / "1").load();
    require(stored.has_value(), "compaction reclaimed a prefix with no snapshot");
    require(stored->last_included_index == included,
            "the snapshot did not cover the prefix that was reclaimed");
    require(stored->state_digest == before.state_digest,
            "the snapshot captured a different state than the replica held");

    const auto wal_after = identity_test::wal_segment(root / "1").read_records().size();
    require(wal_after < wal_before, "compaction reclaimed nothing from the log");
    for (const auto& record : identity_test::wal_segment(root / "1").read_records()) {
        require(record.index > included,
                "a reclaimed entry is still in the log");
    }
}

void test_a_restart_installs_the_snapshot_and_replays_the_rest(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    const auto sequence = build_authorized_submit(cluster);
    cluster.settle(50);
    const auto included = identity_test::complete_compaction(cluster.elect());
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

void test_membership_drift_refuses_before_recovery_and_preserves_history(
    const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)cluster.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
    (void)cluster.commit(command(2, SetExecutorGeneration{ExecutorGeneration{1}}));
    cluster.settle(100);

    auto& original = cluster.elect();
    const auto id = original.status().node_id;
    const auto expected = original.status();
    require(identity_test::complete_compaction(original) > 0, "membership fixture did not create a snapshot");
    for (auto& candidate : cluster.nodes) {
        if (candidate->status().node_id == id) {
            candidate.reset();
            break;
        }
    }

    const auto directory = root / std::to_string(id);
    const auto before = directory_image(directory);
    bool refused = false;
    try {
        ReplicatedKernel mismatched(
            identity_test::recover(directory, id, {id}));
    } catch (const IdentityStartupError& error) {
        refused = error.code() == IdentityStartupErrorCode::identity_mismatch;
    }
    require(refused, "membership drift lacked typed identity_mismatch refusal");
    require(directory_image(directory) == before,
            "membership drift refusal changed persisted filenames or bytes");

    ReplicatedKernel healthy(identity_test::recover(directory, id, cluster.members));
    require(healthy.status().state_digest == expected.state_digest &&
                healthy.status().applied_index == expected.applied_index,
            "same-identity restart did not recover the original state");
}

void test_a_corrupt_snapshot_degrades_to_replay_rather_than_refusing_to_start(
    const std::filesystem::path& root) {
    Cluster cluster(root, 1);
    (void)build_authorized_submit(cluster);
    cluster.settle(50);
    const auto before = cluster.elect().status();

    // No compaction, so the log still holds everything. Corrupting the snapshot
    // must therefore cost nothing at all.
    auto store = identity_test::snapshot_store(root / "1");
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

void test_an_unsupported_snapshot_schema_refuses_without_touching_storage(
    const std::filesystem::path& root) {
    const auto directory = root / "1";
    {
        Cluster cluster(root, 1);
        (void)cluster.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
        cluster.settle(50);
        identity_test::snapshot_store(directory).save(cluster.elect().state(), 1, 1);
    }

    // Bound header plus coordinates is 72 bytes. Its payload is a canonical
    // state frame whose schema sits at byte 8 and whose final 32 bytes are the
    // inner SHA-256. Change ONLY that schema and repair both integrity layers.
    constexpr std::size_t outer_header = 72;
    constexpr std::size_t outer_payload_length_offset = 12;
    const auto snapshot_path = identity_test::snapshot_store(directory).path();
    auto bytes = read_bytes(snapshot_path);
    const auto payload_size = static_cast<std::size_t>(
        read_le(bytes, outer_payload_length_offset, 4)) - 16;
    require(bytes.size() == outer_header + payload_size + 32,
            "snapshot fixture did not have the expected valid outer framing");
    auto payload = std::span<std::byte>(bytes).subspan(outer_header, payload_size);
    write_le(payload, kStateSnapshotSchemaOffset,
             static_cast<std::uint16_t>(kStateSnapshotSchemaVersion - 1), 2);
    const auto inner = sha256_digest(std::span<const std::byte>(payload).subspan(
        kStateSnapshotDigestInputOffset,
        payload.size() - kStateSnapshotDigestInputOffset - kDigestBytes));
    std::copy(inner.bytes.begin(), inner.bytes.end(), payload.end() - kDigestBytes);
    reseal_bound(bytes);
    write_bytes(snapshot_path, bytes);

    TradingState candidate;
    StateSnapshotScratch scratch;
    const auto decoded = decode_state_snapshot(payload, candidate, scratch);
    require(decoded.error == StateSnapshotError::unsupported_schema,
            "unsupported snapshot fixture failed for an integrity error instead");

    const auto before = directory_image(directory);
    bool refused = false;
    try { ReplicatedKernel restarted(identity_test::recover(directory, 1, {1})); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused,
            "unsupported snapshot schema fell through to full-WAL replay");
    require(directory_image(directory) == before,
            "unsupported snapshot recovery changed persisted filenames or bytes");
}

void test_an_oversized_future_snapshot_is_incompatible_not_corrupt(
    const std::filesystem::path& root) {
    const auto directory = root / "1";
    {
        Cluster cluster(root, 1);
        (void)cluster.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
        cluster.settle(50);
        identity_test::snapshot_store(directory).save(cluster.elect().state(), 1, 1);
    }

    constexpr std::size_t outer_header = 72;
    constexpr std::size_t outer_payload_length_offset = 12;
    const auto snapshot_path = identity_test::snapshot_store(directory).path();
    const auto original = read_bytes(snapshot_path);
    std::vector<std::byte> future(
        kStateSnapshotPayloadOffset + kMaxStateSnapshotPayloadBytes + 1 +
            kDigestBytes,
        std::byte{0});
    future[0] = std::byte{'V'};
    future[1] = std::byte{'2'};
    future[2] = std::byte{'S'};
    future[3] = std::byte{'T'};
    write_le(future, kStateSnapshotMagicBytes,
             kMaxStateSnapshotPayloadBytes + 1, 4);
    write_le(future, kStateSnapshotSchemaOffset,
             static_cast<std::uint16_t>(kStateSnapshotSchemaVersion + 1), 2);
    const auto inner = sha256_digest(std::span<const std::byte>(future).subspan(
        kStateSnapshotDigestInputOffset,
        future.size() - kStateSnapshotDigestInputOffset - kDigestBytes));
    std::copy(inner.bytes.begin(), inner.bytes.end(), future.end() - kDigestBytes);

    std::vector<std::byte> framed(outer_header + future.size() + 32, std::byte{0});
    std::copy_n(original.begin(), outer_header, framed.begin());
    write_le(framed, outer_payload_length_offset, 16 + future.size(), 4);
    write_le(framed, 8, framed.size(), 4);
    std::copy(future.begin(), future.end(), framed.begin() + outer_header);
    reseal_bound(framed);
    write_bytes(snapshot_path, framed);

    TradingState candidate;
    StateSnapshotScratch scratch;
    const auto decoded = decode_state_snapshot(future, candidate, scratch);
    require(decoded.error == StateSnapshotError::unsupported_schema,
            "oversized future snapshot was classified as current-schema corruption");

    const auto before = directory_image(directory);
    bool refused = false;
    try { ReplicatedKernel restarted(identity_test::recover(directory, 1, {1})); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused, "oversized unsupported snapshot fell through to WAL replay");
    require(directory_image(directory) == before,
            "oversized snapshot refusal changed persisted filenames or bytes");
}

void test_snapshot_incompatibility_is_checked_before_opening_writable_storage(
    const std::filesystem::path& root) {
    for (const bool outer_version : {false, true}) {
        const auto directory = root / (outer_version ? "outer" : "canonical");
        identity_test::provision(directory, 1, {1});
        identity_test::snapshot_store(directory).save(TradingState{}, 1, 1);
        const auto snapshot_path = identity_test::snapshot_store(directory).path();
        auto bytes = read_bytes(snapshot_path);
        if (outer_version) {
            write_le(bytes, 4, 2, 2);
        } else {
            constexpr std::size_t outer_header = 72;
            const auto payload_size = static_cast<std::size_t>(read_le(bytes, 12, 4)) - 16;
            auto payload = std::span<std::byte>(bytes).subspan(
                outer_header, payload_size);
            write_le(payload, kStateSnapshotSchemaOffset,
                     static_cast<std::uint16_t>(kStateSnapshotSchemaVersion + 1), 2);
            const auto inner = sha256_digest(
                std::span<const std::byte>(payload).subspan(
                    kStateSnapshotDigestInputOffset,
                    payload.size() - kStateSnapshotDigestInputOffset - kDigestBytes));
            std::copy(inner.bytes.begin(), inner.bytes.end(),
                      payload.end() - kDigestBytes);
        }
        reseal_bound(bytes);
        write_bytes(snapshot_path, bytes);

        const auto before = directory_image(directory);
        require(!std::filesystem::exists(
                    identity_test::runtime_directory(directory) / "v2-wal-blocks.bin"),
                "snapshot-only incompatibility fixture already contained WAL files");
        bool refused = false;
        try { ReplicatedKernel restarted(identity_test::recover(directory, 1, {1})); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused,
                outer_version
                    ? "unsupported snapshot frame version started"
                    : "snapshot-only unsupported canonical schema started");
        require(directory_image(directory) == before,
                "snapshot incompatibility opened or changed writable storage");
    }
}

void test_an_incompatible_uncommitted_segment_suffix_refuses_without_touching_storage(
    const std::filesystem::path& root) {
    const auto directory = root / "1";
    std::uint64_t suffix_index{};
    {
        Cluster cluster(root, 1);
        (void)cluster.commit(command(1, AdvanceKillEpoch{KillEpoch{1}}));
        cluster.settle(50);
        suffix_index = static_cast<std::uint64_t>(cluster.elect().status().current_index + 1);
    }

    WalBlockArena arena;
    const auto payload = unsupported_command(
        command(2, SetStrategyMode{StrategyMode::normal, {}, {}}));
    const auto record = identity_test::raw_record(
        {suffix_index, 1, 2, 0, payload}, identity_test::stored_identity(directory));
    require(arena.stage(record, suffix_index) == StageOutcome::staged,
            "incompatible segment suffix fixture did not stage");
    arena.seal_open_block();
    {
        auto segment = identity_test::wal_segment(directory);
        require(segment.flush(arena) == 1,
                "incompatible segment suffix fixture did not flush");
    }
    // Preserve a diagnostic torn tail after the complete incompatible record.
    // Startup must reject compatibility before any writable recovery path can
    // normalize, truncate, or otherwise erase either piece of evidence.
    const auto segment_path =
        identity_test::runtime_directory(directory) / "v2-wal-blocks.bin";
    const auto segment_bytes = read_bytes(segment_path);
    std::size_t complete_blocks = 0;
    while ((complete_blocks + 1) * kWalBlockBytes <= segment_bytes.size()) {
        const auto block = std::span<const std::byte>(segment_bytes).subspan(
            complete_blocks * kWalBlockBytes, kWalBlockBytes);
        if (!decode_block(block).has_value()) break;
        ++complete_blocks;
    }
    require(complete_blocks > 0,
            "incompatible segment suffix fixture had no complete block");
    std::filesystem::resize_file(
        segment_path, complete_blocks * kWalBlockBytes + 3);

    const auto before = directory_image(directory);
    bool refused = false;
    try { ReplicatedKernel restarted(identity_test::recover(directory, 1, {1})); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused, "unsupported command schema in uncommitted segment suffix started");
    require(directory_image(directory) == before,
            "segment incompatibility refusal changed persisted filenames or bytes");
}

void test_an_incompatible_uncommitted_legacy_suffix_refuses_without_touching_storage(
    const std::filesystem::path& root) {
    identity_test::provision(root, 1, {1});
    velocita::PersistentMetadata metadata;
    metadata.term = 1;
    metadata.voted_for = 1;
    metadata.commit_index = 1;
    identity_test::metadata_store(root).save(metadata);
    velocita::WriteAheadLog wal(identity_test::runtime_directory(root),
                                velocita::kV2WalFileName);
    wal.append({1, 1, 1, 0,
                encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))});
    wal.append({2, 1, 2, 0,
                unsupported_command(
                    command(2, SetStrategyMode{StrategyMode::normal, {}, {}}),
                    kKernelCodecSchemaVersion - 1)});

    const auto before = directory_image(root);
    bool refused = false;
    try { ReplicatedKernel restarted(identity_test::recover(root, 1, {1})); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused, "unsupported command schema in uncommitted legacy suffix started");
    require(directory_image(root) == before,
            "legacy incompatibility refusal changed persisted filenames or bytes");
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
        if (point.compact_here) (void)identity_test::complete_compaction(cluster.elect());
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

void test_a_follower_ack_never_covers_merely_staged_entries(
    const std::filesystem::path& root) {
    // The leader turns a follower's reported current_idx straight into that
    // follower's match index and counts it toward quorum. A follower that acks
    // entries it has only staged is therefore the same defect as a leader
    // counting its own undurable copy - and the Raft patch cannot fix it,
    // because this number is produced on our side of the wire.
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    const auto before = leader.status().commit_index;

    const auto value = command(
        1, SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}});
    require(leader.propose(encoded(value)).status == ProposeStatus::admitted,
            "the leader refused the command");

    // ONLY THE LEADER FLUSHES. Followers receive, append and stage, and reply -
    // but their bytes never reach the disk, so their acks must not be worth a
    // quorum.
    for (int round = 0; round < 200; ++round) {
        leader.flush_durability();
        for (auto& node : cluster.nodes) node->tick(10);
        cluster.route();
        require(leader.status().commit_index == before,
                "the leader committed on follower acks that covered only staged "
                "entries");
        require(!leader.take_committed_result(),
                "a result appeared while no follower had synced");
    }

    // Non-vacuity: the followers really did receive and stage it, so something
    // was genuinely being withheld rather than never sent.
    bool a_follower_holds_it = false;
    for (const auto& node : cluster.nodes) {
        if (node->status().node_id == leader_id) continue;
        if (node->status().current_index > before) a_follower_holds_it = true;
        require(node->durable_index() == 0,
                "a follower synced despite never being flushed");
    }
    require(a_follower_holds_it,
            "no follower ever received the entry, so nothing was being withheld");

    // Let the followers reach the disk. The same entry must now commit.
    cluster.settle(400);
    require(cluster.elect().status().commit_index > before,
            "the entry never committed once followers were durable, so the clamp "
            "is a deadlock rather than a gate");
}

// ---------------------------------------------------------------------------
// EARLY DURABLE ACKS. A follower's reply may only claim what is on disk (the
// clamp above), and today it says nothing more until the leader's next
// heartbeat, so every commit waits up to one heartbeat interval for news the
// follower already has. These tests pin down both halves: the follower speaks
// up as soon as it is durable, and it never claims more than a normal reply
// in the same term could.

std::vector<velocita::AppendEntriesResponse> drain_acks(ReplicatedKernel& node,
                                                        std::vector<std::int32_t>* to = nullptr) {
    std::vector<velocita::AppendEntriesResponse> acks;
    while (auto message = identity_test::take_outbound_message(node)) {
        if (const auto* ack = std::get_if<velocita::AppendEntriesResponse>(&message->payload)) {
            acks.push_back(*ack);
            if (to != nullptr) to->push_back(message->recipient_id);
        }
    }
    return acks;
}

velocita::AppendEntries three_entries(const std::int64_t term) {
    velocita::AppendEntries append{};
    append.term = term;
    for (std::int32_t index = 1; index <= 3; ++index) {
        append.entries.push_back({term, index, RAFT_LOGTYPE_NORMAL,
            encoded(command(static_cast<std::uint64_t>(index),
                            SetStrategyMode{StrategyMode::normal, {}, {}}))});
    }
    return append;
}

void test_a_durable_follower_acks_without_waiting_to_be_asked(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, three_entries(1)});
    // Staged, not durable: the immediate reply may claim nothing (the clamp).
    const auto first = drain_acks(node);
    require(first.size() == 1 && first[0].success && first[0].current_index == 0,
            "the immediate reply claimed entries that were only staged");

    node.flush_durability();  // entries 1..3 reach the disk; no message arrives
    std::vector<std::int32_t> to;
    const auto early = drain_acks(node, &to);
    require(early.size() == 1, "a durable follower waited to be asked before acknowledging (" +
                                   std::to_string(early.size()) + " acks)");
    require(to[0] == 1 && early[0].term == 1 && early[0].success && early[0].current_index == 3,
            "the early ack went to the wrong leader, term or index: to " + std::to_string(to[0]) +
                " term " + std::to_string(early[0].term) + " index " +
                std::to_string(early[0].current_index));
    node.flush_durability();
    require(drain_acks(node).empty(), "the early ack was repeated with nothing new to say");
}

void test_an_early_ack_never_covers_entries_the_leader_has_not_verified(
    const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, three_entries(1)});
    node.flush_durability();
    (void)drain_acks(node);
    require(node.durable_index() == 3, "fixture: term-1 entries 1..3 are not durable");

    // A term-2 leader checks only index 1. Entries 2..3 are durable here but
    // may differ from that leader's log: claiming them would let it count a
    // copy of entries it never sent.
    velocita::AppendEntries heartbeat{};
    heartbeat.term = 2;
    heartbeat.previous_log_index = 1;
    heartbeat.previous_log_term = 1;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, heartbeat});
    std::vector<velocita::AppendEntriesResponse> acks;
    for (int pass = 0; pass < 5; ++pass) {
        node.flush_durability();
        for (const auto& ack : drain_acks(node)) acks.push_back(ack);
    }
    bool verified_reply = false;
    for (const auto& ack : acks) {
        require(ack.term == 2, "a reply spoke for term " + std::to_string(ack.term) + " after term 2 began");
        require(!ack.success || ack.current_index <= 1,
                "an ack claimed durable entries the term-2 leader never verified: " +
                    std::to_string(ack.current_index));
        verified_reply = verified_reply || (ack.success && ack.current_index == 1);
    }
    require(verified_reply, "the verified prefix itself was never acknowledged (vacuous test)");
}

void test_an_early_ack_never_speaks_for_a_term_already_left(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, three_entries(1)});
    (void)drain_acks(node);  // staged only: the clamped reply claimed nothing

    // Term 2 begins before the term-1 entries reach the disk. Helping the
    // term-1 leader commit them now could lose them: the term-2 leader may
    // overwrite entries it never had.
    velocita::AppendEntries heartbeat{};
    heartbeat.term = 2;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, heartbeat});
    std::vector<std::int32_t> to;
    std::vector<velocita::AppendEntriesResponse> acks;
    for (int pass = 0; pass < 5; ++pass) {
        node.flush_durability();
        for (const auto& ack : drain_acks(node, &to)) acks.push_back(ack);
    }
    require(node.durable_index() == 3, "fixture: the term-1 entries never became durable");
    for (std::size_t i = 0; i < acks.size(); ++i) {
        require(to[i] == 3 && acks[i].term == 2,
                "a reply went to the term-1 leader or carried term " + std::to_string(acks[i].term));
        require(!acks[i].success || acks[i].current_index == 0,
                "a term-1 prefix was acknowledged in term 2: " + std::to_string(acks[i].current_index));
    }
}

// Parks the WAL writer thread just before its fsync: the entries are written
// and staged but NOT durable, for as long as the test needs.
class WalSyncFence {
public:
    StorageCheckpoints checkpoints() { return {&WalSyncFence::observe, this}; }
    void arm() { std::lock_guard lock(mutex_); armed_ = true; }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
    bool parked() {
        std::lock_guard lock(mutex_);
        return parked_;
    }

private:
    static void observe(const StorageCheckpoint checkpoint, void* raw) {
        static_cast<WalSyncFence*>(raw)->at(checkpoint);
    }
    void at(const StorageCheckpoint checkpoint) {
        if (checkpoint != StorageCheckpoint::before_file_sync) return;
        std::unique_lock lock(mutex_);
        if (!armed_ || std::this_thread::get_id() == owner_) return;
        parked_ = true;
        changed_.wait(lock, [&] { return released_; });
    }
    const std::thread::id owner_{std::this_thread::get_id()};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool armed_{};
    bool parked_{};
    bool released_{};
};

void test_an_early_ack_waits_for_the_writer_thread_to_sync(const std::filesystem::path& root) {
    WalSyncFence fence;
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}), fence.checkpoints());
    struct Release { WalSyncFence& fence; ~Release() { fence.release(); } } release{fence};
    node.start_writer();
    fence.arm();
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, three_entries(1)});
    std::vector<velocita::AppendEntriesResponse> acks;
    const auto park_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fence.parked() && std::chrono::steady_clock::now() < park_deadline) {
        node.flush_durability();
        for (const auto& ack : drain_acks(node)) acks.push_back(ack);
        std::this_thread::yield();
    }
    require(fence.parked(), "fixture: the writer never reached its fsync");
    // Written and staged, not synced: the owner keeps flushing and must still
    // claim nothing, however far staging has run ahead.
    for (int pass = 0; pass < 20; ++pass) {
        node.flush_durability();
        for (const auto& ack : drain_acks(node)) acks.push_back(ack);
    }
    require(node.durable_index() == 0, "fixture: entries became durable while the writer was parked");
    for (const auto& ack : acks)
        require(!ack.success || ack.current_index == 0,
                "an ack claimed entries the writer had not synced: " + std::to_string(ack.current_index));

    fence.release();
    bool acknowledged = false;
    const auto ack_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!acknowledged && std::chrono::steady_clock::now() < ack_deadline) {
        node.flush_durability();
        for (const auto& ack : drain_acks(node))
            acknowledged = acknowledged || (ack.success && ack.term == 1 && ack.current_index == 3);
        std::this_thread::yield();
    }
    require(acknowledged && node.durable_index() == 3,
            "the synced entries were not acknowledged without being asked");
    node.stop_writer();
}

void test_a_commit_does_not_wait_for_a_heartbeat(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    cluster.drain_results();
    const auto before = leader.status().commit_index;
    require(leader.propose(encoded(command(1, SetStrategyMode{StrategyMode::normal, {}, {}}))).status ==
                ProposeStatus::admitted,
            "the leader refused the command");
    // Time does not move: tick(0) can never fire a heartbeat. Bytes still reach
    // the disk and messages still flow, which is all a commit should need.
    std::optional<CommittedResult> committed;
    for (int round = 0; round < 20 && !committed; ++round) {
        for (auto& node : cluster.nodes) node->flush_durability();
        for (auto& node : cluster.nodes) node->tick(0);
        cluster.route();
        cluster.check_durability_invariant();
        committed = leader.take_committed_result();
    }
    require(leader.status().commit_index > before && committed.has_value(),
            "the entry waited for a heartbeat: commit stayed at " +
                std::to_string(leader.status().commit_index));
}

// The retained log never evicts: once its ordinary pool is full, normal
// commands are refused until compaction reclaims a prefix. Operators must be
// able to see that coming, so status reports each pool's use.
void test_status_reports_retained_log_use_by_pool(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    const auto base = cluster.elect().status();
    require(base.retained_ordinary + base.retained_protected == static_cast<std::uint64_t>(base.current_index),
            "the pools do not account for every retained entry");
    // Ordinary: a caller outside the operator band. (An operator's kill-path
    // command draws on the protected pool whichever lane it arrives on.)
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    auto& leader = cluster.elect();
    const auto normal = leader.status();
    require(normal.retained_ordinary == base.retained_ordinary + 3 &&
                normal.retained_protected == base.retained_protected,
            "three normal-lane commands were not counted as three ordinary entries: ordinary " +
                std::to_string(normal.retained_ordinary) + " protected " +
                std::to_string(normal.retained_protected));
    require(leader.propose(encoded(command_from(kMaxOperatorCallerId, 1, AdvanceKillEpoch{KillEpoch{1}})),
                           ProposeLane::control).status == ProposeStatus::admitted,
            "the operator control command was refused");
    cluster.settle(200);
    const auto control = cluster.elect().status();
    require(control.retained_protected == normal.retained_protected + 1 &&
                control.retained_ordinary == normal.retained_ordinary,
            "a control-lane command did not draw on the protected pool");
}

// A voter behind the leader's compacted prefix cannot be served by
// AppendEntries, and runtime snapshot install does not exist yet. That voter
// is stranded until repaired - but it must not take the leader, or the rest of
// the cluster, down with it.
void test_a_voter_behind_the_leaders_snapshot_does_not_stop_the_cluster(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    // The lowest-numbered follower is served first in the leader's send loop,
    // so a stranded voter there would also starve the other follower.
    std::int32_t stranded = 0, healthy = 0;
    for (const auto id : cluster.members) {
        if (id == leader_id) continue;
        if (stranded == 0) stranded = id; else healthy = id;
    }
    cluster.partitioned.insert(stranded);
    for (std::uint64_t sequence = 1; sequence <= 5; ++sequence)
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    require(identity_test::complete_compaction(leader) > 0, "fixture: the leader did not compact");
    cluster.partitioned.erase(stranded);

    const auto term = leader.status().term;
    // 4 s of simulated time: longer than any election timeout (1-2 s), so a
    // follower starved of heartbeats would start an election.
    cluster.settle(400);
    // Every voter, not just the leader: a stranded voter that hears nothing
    // campaigns, and its higher terms depose a healthy leader.
    for (const auto id : cluster.members) {
        const auto status = cluster.node(id).status();
        require(status.term == term && status.role != "candidate",
                "voter " + std::to_string(id) + " left term " + std::to_string(term) + ": now " +
                    std::string(status.role) + " in term " + std::to_string(status.term));
    }
    require(cluster.leader() == &leader, "leadership moved while a voter was stranded");
    require(leader.status().stranded_voters == 1,
            "the leader did not report the voter behind its snapshot");
    // The other two keep committing.
    const auto result = cluster.commit(command_from(kMaxOperatorCallerId + 1, 6,
                                                    SetStrategyMode{StrategyMode::normal, {}, {}}));
    (void)result;
    cluster.settle(30);  // a follower learns the commit index with the next append
    require(cluster.node(healthy).status().commit_index == leader.status().commit_index,
            "the healthy follower stopped following");
    require(cluster.node(stranded).status().commit_index < leader.status().commit_index,
            "fixture: the stranded voter somehow caught up without a snapshot");
}

// The compaction floor is the lowest index EVERY voter holds durably: below
// it any voter may compact without stranding another. The leader computes it
// and sends it with every append; a silent voter pins it.
void test_the_compaction_floor_is_the_slowest_voters_position(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence)
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    cluster.settle(30);  // a heartbeat carries the floor to the followers
    const auto all = leader.status().commit_index;
    for (const auto id : cluster.members)
        require(cluster.node(id).status().compaction_floor == all,
                "voter " + std::to_string(id) + " floor " +
                    std::to_string(cluster.node(id).status().compaction_floor) + " with every voter at " +
                    std::to_string(all));

    // One voter falls silent: the floor stays where that voter is.
    std::int32_t silent = 0;
    for (const auto id : cluster.members) if (id != leader.status().node_id) { silent = id; break; }
    cluster.partitioned.insert(silent);
    for (std::uint64_t sequence = 5; sequence <= 8; ++sequence)
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    cluster.settle(30);
    require(leader.status().commit_index > all && leader.status().compaction_floor == all,
            "the floor moved past a voter that did not have the entries: floor " +
                std::to_string(leader.status().compaction_floor) + " silent voter at " + std::to_string(all));
    for (const auto id : cluster.members)
        if (id != silent)
            require(cluster.node(id).status().compaction_floor <= all,
                    "voter " + std::to_string(id) + " was told a floor above the silent voter");

    // Entries every voter holds can never be truncated, so the floor is
    // permanent: a new leader (whose match counts start at zero) keeps it.
    // (The silent voter campaigned while cut off, so healing may itself move
    // leadership: take whoever leads once the cluster settles.)
    cluster.partitioned.clear();
    cluster.settle(400);
    auto& current = cluster.elect();
    (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, 9, SetStrategyMode{StrategyMode::normal, {}, {}}));
    cluster.settle(30);
    const auto healed = current.status().compaction_floor;
    require(healed == current.status().commit_index,
            "the healed voter did not bring the floor back up: floor " + std::to_string(healed) + " commit " +
                std::to_string(current.status().commit_index));
    const auto cut_off = current.status().node_id;
    cluster.partitioned.insert(cut_off);
    cluster.settle(400);  // the other two elect a new leader
    // The cut-off leader still believes it leads; look among the others.
    ReplicatedKernel* successor = nullptr;
    for (const auto id : cluster.members)
        if (id != cut_off && cluster.node(id).is_leader()) successor = &cluster.node(id);
    require(successor != nullptr, "fixture: no new leader was elected");
    require(successor->status().compaction_floor >= healed,
            "the floor dropped when leadership moved: " + std::to_string(successor->status().compaction_floor) +
                " < " + std::to_string(healed));
}

void test_a_follower_never_lowers_its_known_floor(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    velocita::AppendEntries heartbeat{};
    heartbeat.term = 1;
    heartbeat.compaction_floor = 5;
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, heartbeat});
    require(node.status().compaction_floor == 5, "the floor from the leader was not recorded");
    // A later append with a lower floor (another leader that learned less)
    // cannot undo it: entries every voter held stay held.
    heartbeat.term = 2;
    heartbeat.compaction_floor = 3;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, heartbeat});
    node.flush_durability();
    require(node.status().compaction_floor == 5,
            "a lower floor replaced what the voter already knew: " + std::to_string(node.status().compaction_floor));
}

// With three voters two durable followers can commit an entry before the
// leader's own sync. The floor must still wait for the leader's disk: a leader
// that crashed would come back without those entries and need them from peers.
void test_the_floor_waits_for_the_leaders_own_disk(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    cluster.settle(30);
    const auto durable_before = static_cast<std::int64_t>(leader.durable_index());
    require(leader.propose(encoded(command_from(kMaxOperatorCallerId + 1, 1,
                                                SetStrategyMode{StrategyMode::normal, {}, {}})))
                    .status == ProposeStatus::admitted,
            "the leader refused the command");
    for (int round = 0; round < 200; ++round) {
        for (auto& node : cluster.nodes)
            if (node.get() != &leader) node->flush_durability();  // ONLY THE FOLLOWERS SYNC
        for (auto& node : cluster.nodes) node->tick(10);
        cluster.route();
        cluster.check_durability_invariant();
    }
    require(leader.status().commit_index > durable_before &&
                static_cast<std::int64_t>(leader.durable_index()) == durable_before,
            "fixture: the followers alone did not commit the entry");
    for (const auto id : cluster.members)
        require(cluster.node(id).status().compaction_floor <= durable_before,
                "voter " + std::to_string(id) + " floor " +
                    std::to_string(cluster.node(id).status().compaction_floor) +
                    " is above what the leader holds on disk (" + std::to_string(durable_before) + ")");
}

// A candidate whose whole log is compacted must still advertise the term of
// its last entry (now only in the snapshot). Advertising 0 makes every voter
// refuse it as out of date, so after the leader is lost nobody can win.
void test_voters_compacted_to_their_end_can_still_elect(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence)
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    cluster.settle(30);
    for (const auto id : cluster.members)
        require(identity_test::complete_compaction(cluster.node(id)) ==
                    static_cast<std::uint64_t>(cluster.node(id).status().current_index),
                "fixture: voter " + std::to_string(id) + " did not compact to its end");
    const auto old = leader.status().node_id;
    cluster.partitioned.insert(old);
    cluster.settle(600);
    // The cut-off old leader still believes it leads; look among the others.
    ReplicatedKernel* successor = nullptr;
    for (const auto id : cluster.members)
        if (id != old && cluster.node(id).is_leader()) successor = &cluster.node(id);
    require(successor != nullptr, "no voter could win an election after the log was compacted to its end");
    const auto before = successor->status().commit_index;
    require(successor->propose(encoded(command_from(kMaxOperatorCallerId + 1, 5,
                                                    SetStrategyMode{StrategyMode::normal, {}, {}})))
                    .status == ProposeStatus::admitted,
            "the new leader refused a command");
    cluster.settle(100);
    require(successor->status().commit_index > before, "the new leader could not commit after compaction");
}

// Drive the floor-gated compaction for up to LIMIT; 0 means it never started.
std::uint64_t compact_to_floor(ReplicatedKernel& node, const std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();
        if (const auto cut = node.compact_within_floor()) {
            node.stop_writer();
            return cut;
        }
        std::this_thread::yield();
    }
    require(!node.compaction_pending(), "a gated compaction started but never finished");
    return 0;
}

void test_compaction_never_cuts_above_the_floor(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    std::int32_t silent = 0, other = 0;
    for (const auto id : cluster.members) {
        if (id == leader_id) continue;
        if (silent == 0) silent = id; else other = id;
    }
    std::uint64_t sequence = 0;
    const auto commit = [&] {
        (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, ++sequence,
                                          SetStrategyMode{StrategyMode::normal, {}, {}}));
    };
    cluster.partitioned.insert(silent);
    for (int i = 0; i < 4; ++i) commit();
    cluster.settle(30);
    // The silent voter holds nothing yet: nobody may reclaim anything.
    require(compact_to_floor(leader, std::chrono::milliseconds(300)) == 0,
            "the leader compacted above a voter that lacks the entries");
    require(compact_to_floor(cluster.node(other), std::chrono::milliseconds(300)) == 0,
            "a follower compacted above a voter that lacks the entries");

    // Heal: the floor rises to the commit index and every voter may compact.
    cluster.partitioned.clear();
    cluster.settle(400);
    auto& current = cluster.elect();
    commit();
    cluster.settle(30);
    for (const auto id : cluster.members)
        require(compact_to_floor(cluster.node(id), std::chrono::seconds(10)) > 0,
                "voter " + std::to_string(id) + " could not compact up to a floor every voter holds");

    // A voter that falls behind now catches up by ordinary appends: nothing it
    // needs was reclaimed.
    cluster.partitioned.insert(silent);
    for (int i = 0; i < 3; ++i) commit();
    cluster.partitioned.clear();
    cluster.settle(400);
    auto& after = cluster.elect();
    commit();
    cluster.settle(30);
    require(after.status().stranded_voters == 0, "a voter was stranded behind a gated compaction");
    for (const auto id : cluster.members)
        require(cluster.node(id).status().commit_index == after.status().commit_index,
                "voter " + std::to_string(id) + " did not catch up after gated compaction");
    (void)current;
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

    // A wall-clock bound, not a round count: 4000 spins last ~3 ms, less than
    // one fsync on the Pi's SD card while its live voter is also syncing.
    std::optional<CommittedResult> committed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!committed && std::chrono::steady_clock::now() < deadline) {
        node.flush_durability();   // submits and collects; the thread writes
        node.tick(10);
        committed = node.take_committed_result();
        std::this_thread::yield();
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
    const auto staged = identity_test::wal_segment(root / "1").read_records();
    require(!staged.empty(),
            "no records reached the block segment, so this test is measuring the "
            "old synchronous path");
    require(durable >= staged.back().index,
            "the durable index does not cover the records on disk");

    // The persisted hard state is a statement about BYTES ON DISK. willemt can
    // advance its in-memory commit index past what this node has synced, since
    // it counts the leader's own vote unconditionally; persisting that would
    // leave a restart reading a commit index past the end of its own log.
    const auto persisted = identity_test::metadata_store(root / "1").load();
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

// --- Phase 7: the power-cut matrix -------------------------------------------

void test_truncation_does_not_race_the_writer(const std::filesystem::path& root) {
    // A suffix replacement REWRITES the segment. If the writer thread is still
    // draining while that happens, blocks belonging to the rejected suffix are
    // written to a file that is about to be replaced, and their completions are
    // published afterwards - so the owner raises the durable index for entries
    // the segment no longer holds. The node then reports as durable a suffix
    // that a restart cannot find, which is the exact thing the durability gate
    // exists to make impossible.
    //
    // Timing alone cannot pin this: the drain usually finishes first, which is
    // why running both threads and hoping produced a green test over a broken
    // truncation. The writer is therefore PARKED inside its drain, at the point
    // where its bytes are written but not yet synced, until the owner has
    // reached the replacement. The park releases itself after a bounded wait so
    // that the correct implementation - which joins the writer before
    // rewriting - cannot deadlock against it.
    struct Park {
        std::mutex mutex;
        std::condition_variable changed;
        bool parked{false};
        bool armed{false};
    };
    static Park park;
    park.parked = false;
    park.armed = false;

    const auto dir = root / "node";
    const StorageCheckpoints checkpoints{
        [](const StorageCheckpoint at, void*) {
            if (at != StorageCheckpoint::before_file_sync) return;
            std::unique_lock<std::mutex> lock(park.mutex);
            if (!park.armed) return;
            park.armed = false;
            park.parked = true;
            park.changed.notify_all();
            // Bounded: the fix joins this thread, so an unbounded park would
            // hang the passing case rather than fail the broken one.
            park.changed.wait_for(lock, std::chrono::milliseconds(300),
                                  [] { return !park.parked; });
            park.parked = false;
        },
        nullptr};

    const auto first_payload = encoded(command(1, SetStrategyMode{StrategyMode::normal, {}, {}}));
    const auto second_payload = encoded(command(2, SetStrategyMode{StrategyMode::normal, {}, {}}));
    const auto replacement = encoded(command(3, SetStrategyMode{StrategyMode::normal, {}, {}}));

    {
        ReplicatedKernel node(identity_test::bootstrap(dir, 2, {1, 2, 3}), checkpoints);
        node.start_writer();

        // Two entries, so the replacement below removes index 2 and keeps
        // index 1. A single entry would hide the defect: the replacement reuses
        // the same index, so a stale completion and a correct one name the same
        // number and the invariant cannot tell them apart.
        velocita::AppendEntries append{};
        append.term = 1;
        // `PeerLogEntry` is {term, id, type, payload}; the index comes from
        // `previous_log_index` plus position.
        append.entries.push_back({1, 1, RAFT_LOGTYPE_NORMAL, first_payload});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, append});
        node.flush_durability();
        // Settle the first entry before arming, so the park catches the drain
        // that carries index 2 and not this one.
        // The owner applies completions; the writer only publishes them. So
        // draining is a loop of `flush_durability`, not a sleep.
        for (int i = 0; i < 500 && node.durable_index() < 1; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            node.flush_durability();
        }
        require(node.durable_index() == 1, "the writer never made the first entry durable");

        {
            std::lock_guard<std::mutex> lock(park.mutex);
            park.armed = true;
        }
        append.entries[0] = {1, 2, RAFT_LOGTYPE_NORMAL, second_payload};
        append.previous_log_index = 1;
        append.previous_log_term = 1;
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, append});
        node.flush_durability();
        while (identity_test::take_outbound_message(node)) {}

        {
            std::unique_lock<std::mutex> lock(park.mutex);
            const auto caught = park.changed.wait_for(
                lock, std::chrono::seconds(2), [] { return park.parked; });
            require(caught,
                    "the writer was never parked mid-drain, so the race was "
                    "never set up and this test proves nothing");
        }

        // The writer now holds index 2's bytes written but unsynced. Replace the
        // suffix from index 2 while it sits there.
        append.term = 2;
        append.previous_log_index = 1;
        append.previous_log_term = 1;
        append.entries[0] = {2, 3, RAFT_LOGTYPE_NORMAL, replacement};
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, append});

        {
            std::lock_guard<std::mutex> lock(park.mutex);
            park.parked = false;
            park.armed = false;
        }
        park.changed.notify_all();

        for (int i = 0; i < 200; ++i) {
            node.flush_durability();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        node.stop_writer();
        node.flush_durability();

        // THE INVARIANT. The durable index may never name an index the segment
        // does not hold - "durable" is a claim about bytes on disk, and a claim
        // that outruns the file is the failure this whole phase is about.
        const auto records = identity_test::wal_segment(dir).read_records();
        std::uint64_t highest_on_disk = 0;
        for (const auto& record : records) {
            highest_on_disk = std::max(highest_on_disk, record.index);
        }
        require(node.durable_index() <= highest_on_disk,
                "the node reports durable index " + std::to_string(node.durable_index()) +
                    " but the segment holds only up to " + std::to_string(highest_on_disk) +
                    ", so a rewritten segment was credited with the suffix it dropped");
        require(records.size() == 2 && records[1].index == 2 && records[1].term == 2 &&
                    records[1].payload == replacement,
                "the segment does not hold the replacement suffix after the rewrite");
    }

    // And the claim must survive the restart it is about.
    ReplicatedKernel restarted(identity_test::recover(dir, 2, {1, 2, 3}));
    require(restarted.status().current_index == 2,
            "the restart did not recover the replaced suffix");
}

void test_corruption_of_acknowledged_history_fails_closed(
    const std::filesystem::path& root) {
    // THE POWER-CUT PROPERTY, as a sweep rather than a handful of points.
    //
    // A power cut can land anywhere: between two blocks, inside a block header,
    // inside a record, inside the checksum. The guarantee is not that every cut
    // preserves everything - that is impossible - but that recovery never comes
    // back holding LESS than the last state a caller was told about, and never
    // comes back holding something the cluster never agreed.
    //
    // Each iteration builds a node, records what it acknowledged, truncates its
    // segment at a chosen offset, and restarts.
    const auto build = [&](const std::filesystem::path& dir) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        Cluster cluster(dir, 1);
        (void)build_authorized_submit(cluster);
        cluster.settle(50);
        return cluster.elect().status();
    };

    const auto segment_path = [](const std::filesystem::path& dir) {
        return identity_test::runtime_directory(dir / "1") / "v2-wal-blocks.bin";
    };

    // Establish the shape once: how big the segment is, and what a clean restart
    // recovers, so the sweep below has something to compare against.
    const auto reference_dir = root / "reference";
    const auto acknowledged = build(reference_dir);
    const auto full_size = std::filesystem::file_size(segment_path(reference_dir));
    require(full_size >= kWalBlockBytes,
            "the fixture produced no segment to cut");

    // Offsets chosen to land in every structurally distinct place: nothing, a
    // few bytes (inside the magic), inside the header, just short of a block,
    // exactly a block, and past the written frontier into reserved space.
    // The written frontier, not the file size: preallocation leaves reserved
    // zeros past the data, and cutting inside those changes nothing.
    const auto written = identity_test::wal_segment(reference_dir / "1").read_records().size();
    require(written > 1, "the fixture wrote too few blocks to sweep");
    const auto data_end = written * kWalBlockBytes;

    std::vector<std::uint64_t> cuts{
        0, 3, kWalBlockHeaderBytes / 2, kWalBlockHeaderBytes,
        kWalBlockHeaderBytes + 7, kWalBlockBytes - 1,
    };
    // Every block boundary, and a byte either side of each, so a cut lands in a
    // header, in a payload, and exactly between blocks.
    for (std::uint64_t block = 1; block <= written; ++block) {
        const auto boundary = block * kWalBlockBytes;
        cuts.push_back(boundary - 1);
        cuts.push_back(boundary);
        cuts.push_back(boundary + kWalBlockHeaderBytes / 2);
    }
    cuts.push_back(data_end);

    std::size_t recovered_full = 0;
    std::size_t recovered_partial = 0;
    for (const auto cut : cuts) {
        if (cut > full_size) continue;
        const auto dir = root / ("cut-" + std::to_string(cut));
        const auto before = build(dir);
        require(before.state_digest == acknowledged.state_digest,
                "the fixture was not reproducible at cut " + std::to_string(cut));

        {
            // Truncate: the bytes past the cut simply never reached the platter.
            const auto path = segment_path(dir);
            std::filesystem::resize_file(path, cut);
        }

        if (cut < data_end) {
            // Refusing is necessary but not sufficient. The reversal from
            // clamping to refusing (ADR 0006's amendment) was made so an
            // operator is TOLD what happened and the files are left intact, and
            // a refusal that surfaces as a downstream replay failure with an
            // empty cause serves neither. So the diagnostic is part of the
            // contract and is asserted as one: without this, deleting the
            // explicit check still "refuses", because `raft_apply_all` then
            // fails on the entries that are not there - which is how a
            // mutation of that check survived this sweep.
            std::string reason;
            try { Cluster restarted(dir, 1, 0xAB, true); }
            catch (const std::runtime_error& error) { reason = error.what(); }
            require(!reason.empty(),
                    "lost acknowledged WAL history was accepted at cut " +
                        std::to_string(cut));
            require(reason.find("committed WAL history is missing") != std::string::npos,
                    "recovery refused at cut " + std::to_string(cut) +
                        " without naming the missing committed history: " + reason);
            ++recovered_partial;
            continue;
        }
        Cluster restarted(dir, 1, 0xAB, true);
        restarted.settle(200);
        const auto after = restarted.elect().status();

        const std::string where = " at cut " + std::to_string(cut);
        require(after.state_digest == acknowledged.state_digest,
                "intact acknowledged prefix changed" + where);
        ++recovered_full;

        // And whatever it recovered, it must be able to keep going: a node that
        // starts but cannot make progress has traded an outage for a slower one.
        const auto resumed = restarted.elect().status();
        require(resumed.node_id == 1, "the node did not resume after a cut" + where);
    }

    require(recovered_full > 0,
            "no cut recovered the acknowledged state, so the sweep never "
            "exercised a clean boundary");
    require(recovered_partial > 0,
            "every cut recovered everything, so the sweep never actually "
            "truncated anything meaningful");
}

void test_winning_leadership_is_not_promotion(const std::filesystem::path& root) {
    // G-053. The dangerous reading of "the leader is the gateway" is that
    // whoever wins Raft has thereby become the execution authority. It is
    // dangerous because Raft leadership changes on its own - a timeout, a slow
    // link, a restart - and the old gateway may still be alive and sending.
    // Promotion has to be a decision somebody makes, not a consequence of an
    // election nobody watched.
    //
    // Executor generation is what the pre-send guard fences on, so this asserts
    // the only thing that matters: no leadership event moves it. The generation
    // moves when an operator says so, and at no other time.
    Cluster cluster(root, 3);
    auto& first = cluster.elect();
    const auto original = first.state().executor_generation;
    const auto first_leader = first.status().node_id;

    // Promote deliberately once, so the test is not merely observing a value
    // that nothing in this fixture could ever change.
    const auto promote = encoded(command_from(kMaxOperatorCallerId, 1,
        SetExecutorGeneration{ExecutorGeneration{original.value() + 1}}));
    require(first.propose(promote, ProposeLane::control).status ==
                ProposeStatus::admitted,
            "a deliberate promotion was refused");
    cluster.settle(200);
    const auto promoted = cluster.elect().state().executor_generation;
    require(promoted.value() == original.value() + 1,
            "the deliberate promotion did not take");

    // Now take leadership away and give it to somebody else, repeatedly. Each
    // of these is exactly the event a standby might mistake for its cue.
    std::set<std::int32_t> leaders_seen{first_leader};
    for (int round = 0; round < 3; ++round) {
        const auto leader = cluster.elect().status().node_id;
        cluster.partitioned.insert(leader);
        cluster.settle(400);
        cluster.partitioned.erase(leader);
        cluster.settle(400);
        leaders_seen.insert(cluster.elect().status().node_id);

        for (auto& node : cluster.nodes) {
            require(node->state().executor_generation == promoted,
                    "a leadership change moved the executor generation on node " +
                        std::to_string(node->status().node_id));
        }
    }

    // Without this the loop above could have re-elected the same voter every
    // time, and "leadership changed and nothing promoted" would be a claim about
    // an event that never happened.
    require(leaders_seen.size() > 1,
            "leadership never actually moved, so this test never observed the "
            "event it exists to rule out");

    const auto final_leader = cluster.elect().status().node_id;

    // And a restart - a voter coming back with no memory of who it was - is not
    // a promotion either. It recovers the generation, it does not mint one.
    cluster.restart(final_leader);
    cluster.settle(400);
    for (auto& node : cluster.nodes) {
        require(node->state().executor_generation == promoted,
                "a restart moved the executor generation on node " +
                    std::to_string(node->status().node_id));
    }
}

void test_malformed_normal_commands_never_enter_durable_history(
    const std::filesystem::path& root) {
    auto valid = encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}));
    auto truncated = valid;
    truncated.pop_back();
    auto trailing = valid;
    trailing.push_back(std::byte{0});
    auto corrupt = valid;
    corrupt.back() ^= std::byte{1};
    const std::vector<std::vector<std::byte>> invalid{
        {}, {std::byte{0xff}}, truncated, trailing, corrupt};
    {
        ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}));
        node.tick(1000);
        require(node.is_leader(), "malformed-ingress fixture did not elect");
        const auto before = node.status();
        const auto durable = node.durable_index();
        for (const auto& bytes : invalid) {
            require(node.propose(bytes).status == ProposeStatus::refused,
                    "malformed normal command entered the replicated log");
            node.flush_durability();
            node.tick(0);
            const auto after = node.status();
            require(after.current_index == before.current_index &&
                        after.commit_index == before.commit_index &&
                        after.applied_index == before.applied_index &&
                        after.state_digest == before.state_digest &&
                        node.durable_index() == durable &&
                        !node.take_committed_result().has_value(),
                    "malformed refusal mutated log, durability or reducer state");
        }
        require(node.propose(valid).status == ProposeStatus::admitted,
                "malformed refusal prevented subsequent valid admission");
        node.flush_durability();
        node.tick(0);
        require(node.state().kill_epoch == KillEpoch{1},
                "valid command after malformed input did not apply");
    }
    ReplicatedKernel restarted(identity_test::recover(root, 1, {1}));
    require(restarted.state().kill_epoch == KillEpoch{1},
            "malformed input poisoned durable replay of the valid command");
    restarted.tick(1000);
    require(restarted.propose(encoded(command(2, AdvanceKillEpoch{KillEpoch{2}}))).status ==
                ProposeStatus::admitted,
            "restarted node could not admit the next valid command");
    restarted.flush_durability();
    restarted.tick(0);
    require(restarted.state().kill_epoch == KillEpoch{2},
            "restarted node could not apply the next valid command");
}

void test_the_control_lane_admits_only_authenticated_control(
    const std::filesystem::path& root) {
    // THE RESERVE IS ONLY A RESERVE IF NOT EVERYONE CAN SPEND IT.
    //
    // `ProposeLane` is chosen by the caller, so on its own it is a label, not a
    // permission. Two ways to make the reservation meaningless follow from that,
    // and both are closed here:
    //
    //   - label an ordinary command "control" and spend the kill path's blocks
    //     on strategy traffic;
    //   - send a real control command from an unauthenticated caller, which the
    //     reducer will refuse - AFTER the entry has already consumed reserved
    //     capacity, committed, and replicated. A refusal that costs the resource
    //     it was protecting is a denial of service on the kill path.
    //
    // Both are refused BEFORE the entry exists, which is the only point at which
    // refusing is free.
    Cluster cluster(root, 1);
    auto& node = cluster.elect();

    const auto ordinary = encoded(command(1, ConfigureRiskLimits{
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{0},
        KillEpoch{}, ExecutorGeneration{}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), MoneyMicros::from_atoms(4'000),
        MoneyMicros::from_atoms(8'000), 1'000'000, 2'500,
        QuantityLots::from_atoms(1'000), true, 1}));
    require(node.propose(ordinary, ProposeLane::control).status ==
                ProposeStatus::not_a_control_command,
            "an ordinary command was admitted to the control lane");

    // The same command on its own lane is fine, so the refusal is about the
    // lane and not about the command.
    require(node.propose(ordinary).status == ProposeStatus::admitted,
            "the ordinary command was not admissible at all");

    const auto unauthenticated =
        encoded(command_from(kMaxOperatorCallerId + 1, 2,
                             AdvanceKillEpoch{KillEpoch{1}}));
    require(node.propose(unauthenticated, ProposeLane::control).status ==
                ProposeStatus::not_a_control_command,
            "an unauthenticated caller was allowed to spend the kill reserve");

    // And the real thing still gets in.
    const auto authentic = encoded(command_from(kMaxOperatorCallerId, 3,
                                                AdvanceKillEpoch{KillEpoch{1}}));
    require(node.propose(authentic, ProposeLane::control).status ==
                ProposeStatus::admitted,
            "an authenticated control command was refused by its own lane");

    // The lane must carry the operation it was built for. ADR 0007 notes this
    // explicitly, because adding a mass cancel and forgetting the lane would
    // leave the emergency command refused by the emergency path.
    const auto flatten = encoded(command_from(
        kMaxOperatorCallerId, 4,
        RequestMassCancel{MassCancelScope::all, {}, {}, KillEpoch{1},
                          ExecutorGeneration{}}));
    require(node.propose(flatten, ProposeLane::control).status ==
                ProposeStatus::admitted,
            "the control lane refused a mass cancel, which is the operation it "
            "exists to carry");

    // Undecodable bytes are not a control command either. The lane cannot take
    // the caller's word for what it is carrying, so anything it cannot read is
    // not something it may reserve capacity for.
    const std::vector<std::byte> garbage(64, std::byte{0xEE});
    require(node.propose(garbage, ProposeLane::control).status ==
                ProposeStatus::not_a_control_command,
            "the control lane reserved capacity for bytes it could not read");
}

void test_a_flood_of_normal_commands_cannot_starve_a_kill(
    const std::filesystem::path& root) {
    // G-056, the independence half. The kill command already exists and is now
    // authorized; what was missing is that it had to queue behind everything
    // else. A strategy that floods the cluster fills the staging arena, and
    // without a reservation the command that stops the flood is precisely the
    // one that cannot get in.
    //
    // Driven by NOT FLUSHING. Staging fills, nothing drains it, and the arena
    // reaches the reserved margin - which is the same shape as a stalled disk or
    // a writer that cannot keep up, and is why the margin is in blocks rather
    // than in a rate.
    Cluster cluster(root, 1);
    auto& node = cluster.elect();

    std::size_t admitted = 0;
    bool normal_refused = false;
    for (std::size_t i = 0; i < 20'000 && !normal_refused; ++i) {
        // Large ordinary records reach WAL pressure before the independent
        // result budget; small control records now saturate that budget first.
        const auto bytes = encoded(command(static_cast<std::uint64_t>(i + 1),
                                           PublishPolicy{policy_artifact()}));
        const auto outcome = node.propose(bytes);
        if (outcome.status == ProposeStatus::normal_lane_exhausted) {
            normal_refused = true;
            break;
        }
        require(outcome.status == ProposeStatus::admitted,
                "a normal command was refused for an unexpected reason");
        ++admitted;
        node.tick(1);
    }
    require(normal_refused,
            "the normal lane never filled, so this test never reached the "
            "condition it exists to check");
    require(admitted > 0, "the fixture admitted nothing at all");

    // THE POINT, and it has to be measured rather than sampled. A single
    // control command getting through proves almost nothing: whatever block was
    // open when the normal lane closed still had room in it, so one more record
    // fits whether or not any blocks were actually reserved. Shrinking the
    // margin to zero left a one-command assertion green, which is how that was
    // found.
    //
    // So count how much the control lane can absorb, and require it to exceed
    // what one block could hold. Only real reserved BLOCKS can supply that.
    std::size_t control_admitted = 0;
    std::size_t control_bytes = 0;
    bool control_refused = false;
    for (std::size_t i = 0; i < 20'000 && !control_refused; ++i) {
        const auto more = encoded(command(900'001 + static_cast<std::uint64_t>(i),
                                          AdvanceKillEpoch{KillEpoch{i + 1}}));
        const auto outcome = node.propose(more, ProposeLane::control);
        if (outcome.status != ProposeStatus::admitted) {
            control_refused = true;
            break;
        }
        ++control_admitted;
        control_bytes += more.size();
        node.tick(1);
    }
    require(control_admitted > 0,
            "a control command was starved by a normal-lane flood");
    require(control_bytes > kWalBlockPayloadBytes,
            "the control lane absorbed only " + std::to_string(control_bytes) +
                " bytes, which one already-open block could have supplied - so "
                "nothing proves any capacity was reserved for it");

    // And the reservation is a margin, not an exemption: drain the reserve and
    // control is refused too, because unbounded admission is what the arena's
    // backpressure exists to prevent.
    require(control_refused,
            "the control lane admitted without bound, which is the failure the "
            "arena's backpressure exists to prevent");
    require(node.reserved_result_count() == static_cast<std::size_t>(node.status().current_index) &&
                node.ready_result_count() == 0,
            "failed WAL append leaked an unaccepted result reservation");

    // Draining restores the normal lane, so the refusal was backpressure and not
    // a latch that needs clearing.
    node.flush_durability();
    for (int i = 0; i < 50; ++i) {
        node.tick(1);
        node.flush_durability();
    }
    const auto after = encoded(command(950'001,
                                       SetStrategyMode{StrategyMode::normal, {}, {}}));
    require(node.propose(after).status == ProposeStatus::admitted,
            "the normal lane did not recover after the arena drained");
}

void test_v2_storage_is_not_v1_storage(const std::filesystem::path& root) {
    // G-051. V2 reuses V1's MetadataStore and WriteAheadLog - they are
    // payload-generic and duplicating them would have been worse - but that
    // made both milestones write the same filenames with the same magic and
    // version constants. A V2 node aimed at a V1 directory therefore ACCEPTED
    // V1's hard state, because the values it validates are literally the values
    // V1 wrote, and failed only later and by accident when a V1 payload reached
    // the V2 codec.
    //
    // The generation layout reserves a closed root namespace. A selected V2
    // root polluted with V1 files must now fail closed and preserve both
    // histories; startup must not silently treat that root as a fresh node.
    const auto dir = root / "shared";
    identity_test::provision(dir, 1, {1});

    // A V1 data directory: hard state a V1 node would have written, plus a log
    // record whose payload means nothing to V2.
    velocita::PersistentMetadata v1_metadata;
    v1_metadata.term = 9;
    v1_metadata.voted_for = 3;
    v1_metadata.commit_index = 4;
    velocita::MetadataStore(dir).save(v1_metadata);
    velocita::WalRecord v1_record;
    v1_record.index = 1;
    v1_record.term = 9;
    v1_record.entry_id = 1;
    v1_record.entry_type = 0;
    v1_record.payload = std::vector<std::byte>(16, std::byte{0xA5});
    velocita::WriteAheadLog(dir).append(v1_record);

    const auto v1_metadata_digest = file_digest(dir / velocita::kV1MetadataFileName);
    const auto v1_wal_digest = file_digest(dir / velocita::kV1WalFileName);
    const auto v2_metadata = identity_test::runtime_directory(dir) /
        velocita::kV2MetadataFileName;
    const auto v2_metadata_digest = file_digest(v2_metadata);
    bool refused{};
    try { (void)identity_test::recover(dir, 1, {1}); }
    catch (const IdentityStartupError& error) {
        refused = error.code() == IdentityStartupErrorCode::unexpected_contents;
    }
    require(refused, "a selected V2 root accepted foreign V1 root contents");

    // And it must not have written over what V1 left. Storage isolation that
    // works by starting fresh and clobbering the other milestone's files is not
    // isolation; it is the destructive migration G-051 names.
    require(file_digest(dir / velocita::kV1MetadataFileName) == v1_metadata_digest,
            "the V2 node rewrote V1's metadata");
    require(file_digest(dir / velocita::kV1WalFileName) == v1_wal_digest,
            "the V2 node rewrote V1's write-ahead log");
    require(file_digest(v2_metadata) == v2_metadata_digest,
            "V1 root pollution changed selected V2 metadata during refusal");
}

void test_a_torn_hard_state_write_never_produces_a_corrupt_hard_state(
    const std::filesystem::path& root) {
    // The plan asks for "hard-state two-slot recovery chooses newest valid
    // sequence". Two slots are one way to get the property; this implementation
    // gets the same property from a temp file plus an atomic rename, so what is
    // tested here is the PROPERTY rather than the mechanism: recovery never
    // observes a partially written hard state.
    std::filesystem::create_directories(root / "1");
    Cluster cluster(root, 1);
    (void)build_authorized_submit(cluster);
    cluster.settle(50);
    const auto before = cluster.elect().status();

    const auto metadata_path = identity_test::runtime_directory(root / "1") /
        velocita::kV2MetadataFileName;
    require(std::filesystem::exists(metadata_path),
            "the fixture wrote no metadata to tear");
    const auto size = std::filesystem::file_size(metadata_path);

    // A torn hard state cannot be produced by truncating the real file, because
    // rename is atomic - which IS the property. So the test asserts the two
    // halves that make it true: a partial file is refused rather than
    // misinterpreted, and the real path never leaves one behind.
    const auto torn = root / "1" / "metadata.torn";
    std::filesystem::copy_file(metadata_path, torn);
    std::filesystem::resize_file(torn, size / 2);
    bool refused = false;
    try {
        (void)identity_test::metadata_store(root / "1").load();
    } catch (const std::exception&) {
        refused = true;
    }
    require(!refused, "the intact metadata was refused");

    // Now tear the real one and require a refusal rather than a plausible
    // misreading - a hard state read wrong is worse than one that fails loudly.
    std::filesystem::resize_file(metadata_path, size / 2);
    bool threw = false;
    try {
        (void)identity_test::metadata_store(root / "1").load();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw,
            "a truncated hard state was accepted, so a torn write could be read "
            "as a valid term or vote");
    (void)before;
}

struct MetadataCut {
    velocita::MetadataCheckpoint cut;
};

void exit_at_metadata_cut(const velocita::MetadataCheckpoint point, void* context) {
    if (point == static_cast<MetadataCut*>(context)->cut) _exit(73);
}

void test_hard_state_atomic_replace_cut_matrix(const std::filesystem::path& root) {
    using Checkpoint = velocita::MetadataCheckpoint;
    const velocita::PersistentMetadata old_state{7, 2, 19};
    const velocita::PersistentMetadata new_state{8, 3, 23};
    const auto is_old = [&](const velocita::PersistentMetadata& value) {
        return value.term == old_state.term && value.voted_for == old_state.voted_for &&
               value.commit_index == old_state.commit_index;
    };
    const auto is_new = [&](const velocita::PersistentMetadata& value) {
        return value.term == new_state.term && value.voted_for == new_state.voted_for &&
               value.commit_index == new_state.commit_index;
    };

    // `_exit` models a process crash: dirty kernel page-cache contents remain
    // available. Before rename the named file must therefore remain old; after
    // atomic rename it must be the complete new frame, never a torn mixture.
    for (const auto cut : {Checkpoint::before_write, Checkpoint::partial_write,
                           Checkpoint::file_written, Checkpoint::file_synced,
                           Checkpoint::file_renamed, Checkpoint::directory_synced}) {
        const auto directory = root / std::to_string(static_cast<int>(cut));
        identity_test::provision(directory, 1, {1, 2, 3});
        identity_test::metadata_store(directory).save(old_state);

        const auto child = ::fork();
        require(child >= 0, "could not fork hard-state cut fixture");
        if (child == 0) {
            MetadataCut context{cut};
            identity_test::metadata_store(
                directory,
                {exit_at_metadata_cut, &context}).save(new_state);
            _exit(74);
        }
        int status{};
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                    WEXITSTATUS(status) == 73,
                "hard-state cut did not reach its protocol checkpoint");

        // A partial temp is preserved and refuses serving recovery. Inspect the
        // selected FINAL separately to prove atomicity without blessing the temp.
        const auto runtime = identity_test::runtime_directory(directory);
        const int directory_fd = ::open(runtime.c_str(), O_RDONLY | O_DIRECTORY);
        require(directory_fd >= 0, "metadata cut directory open failed");
        const auto recovered = read_bound_metadata_at(directory_fd, velocita::kV2MetadataFileName,
                                                       identity_test::stored_identity(directory));
        ::close(directory_fd);
        const bool rename_completed =
            cut == Checkpoint::file_renamed || cut == Checkpoint::directory_synced;
        require(rename_completed ? is_new(recovered) : is_old(recovered),
                "process crash exposed the wrong hard-state generation");

        const auto temporary = runtime /
                               (std::string(velocita::kV2MetadataFileName) + ".tmp");
        if (cut == Checkpoint::before_write)
            require(!std::filesystem::exists(temporary),
                    "before-write cut was not at the intended boundary");
        if (cut == Checkpoint::partial_write)
            require(std::filesystem::file_size(temporary) == 56,
                    "partial-write cut did not leave a demonstrably torn temporary");
        if (cut == Checkpoint::file_written || cut == Checkpoint::file_synced)
            require(std::filesystem::file_size(temporary) == 112,
                    "pre-rename cut did not leave the complete temporary frame");
        if (rename_completed)
            require(!std::filesystem::exists(temporary),
                    "post-rename cut retained the temporary pathname");
    }

    // This observes callback ordering only: save reaches directory_synced before
    // returning. It neither proves that fsync executed nor emulates power loss.
    const auto completed = root / "completion-checkpoint";
    identity_test::provision(completed, 1, {1, 2, 3});
    identity_test::metadata_store(completed).save(old_state);
    bool observed = false;
    const velocita::MetadataCheckpoints checkpoints{
        [](const Checkpoint point, void* raw) {
            if (point == Checkpoint::directory_synced) *static_cast<bool*>(raw) = true;
        }, &observed};
    identity_test::metadata_store(completed, checkpoints)
        .save(new_state);
    require(observed && is_new(identity_test::metadata_store(completed).load()),
            "save returned without observing its completion checkpoint");
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

void test_suffix_replacement_invalidates_old_durability(
    const std::filesystem::path& root, const bool flushed, const bool threaded) {
    const auto dir = root / "node";
    const auto replacement = encoded(command(2, SetStrategyMode{StrategyMode::normal, {}, {}}));
    {
        ReplicatedKernel node(identity_test::bootstrap(dir, 2, {1, 2, 3}));
        velocita::AppendEntries first{};
        first.term = 1;
        first.entries.push_back({1, 1, RAFT_LOGTYPE_NORMAL,
            encoded(command(1, SetStrategyMode{StrategyMode::normal, {}, {}}))});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, first});
        if (flushed) node.flush_durability();
        if (threaded) {
            node.start_writer();
            node.flush_durability();
        }
        while (identity_test::take_outbound_message(node)) {}
        first.term = 2;
        first.entries[0] = {2, 2, RAFT_LOGTYPE_NORMAL, replacement};
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, first});
        // Without a writer thread the reply exists at once: the clamp must
        // claim nothing, whatever the replaced entry's durability was.
        if (!threaded) {
            const auto immediate = identity_test::take_outbound_message(node);
            require(immediate.has_value() &&
                        std::get<velocita::AppendEntriesResponse>(immediate->payload).current_index == 0,
                    "replacement acknowledged using obsolete history durability");
        }
        // Later replies (an early ack once the writer catches up) may claim
        // index 1 only if the REPLACEMENT is what is on disk at that moment; an
        // ack built from the old entry's durability would find term 1 there.
        bool claimed = false;
        const auto reply_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(5);
        while (!claimed && std::chrono::steady_clock::now() < reply_deadline) {
            node.flush_durability();
            if (const auto reply = identity_test::take_outbound_message(node)) {
                const auto ack = std::get<velocita::AppendEntriesResponse>(reply->payload);
                if (ack.success && ack.current_index > 0) {
                    const auto on_disk = identity_test::wal_segment(dir).read_records();
                    require(ack.term == 2 && ack.current_index == 1 && on_disk.size() == 1 &&
                                on_disk[0].term == 2 && on_disk[0].payload == replacement,
                            "replacement acknowledged using obsolete history durability");
                    claimed = true;
                }
            }
            std::this_thread::yield();
        }
        require(claimed, "the durable replacement was never acknowledged");
        node.stop_writer();
        node.flush_durability();
        const auto records = identity_test::wal_segment(dir).read_records();
        require(records.size() == 1 && records[0].index == 1 &&
                    records[0].term == 2 && records[0].payload == replacement,
                "rejected staged suffix reappeared on disk");
    }
    ReplicatedKernel restarted(identity_test::recover(dir, 2, {1, 2, 3}));
    require(restarted.status().current_index == 1 && restarted.durable_index() == 1,
            "restart changed the replacement log's index");
}

void test_compacted_corrupt_snapshot_refuses_empty_recovery(
    const std::filesystem::path& root) {
    {
        Cluster cluster(root, 1);
        (void)build_authorized_submit(cluster);
        cluster.settle();
        require(identity_test::complete_compaction(cluster.elect()) > 0, "fixture must compact");
    }
    const auto snapshot = identity_test::snapshot_store(root / "1").path();
    std::filesystem::resize_file(snapshot, 3);
    bool refused = false;
    try { ReplicatedKernel restarted(identity_test::recover(root / "1", 1, {1})); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused, "corrupt compacted snapshot allowed empty recovery");
    require(std::filesystem::file_size(snapshot) == 3,
            "refused recovery overwrote the diagnostic evidence");
}

void test_snapshot_directory_failure_is_not_success(const std::filesystem::path& root) {
    auto directory = root / "original";
    identity_test::provision(directory, 1, {1});
    auto store = identity_test::snapshot_store(directory, {[](StorageCheckpoint point, void* context) {
        if (point != StorageCheckpoint::file_renamed) return;
        const auto& dir = *static_cast<std::filesystem::path*>(context);
        std::filesystem::rename(dir, dir.string() + ".moved");
    }, &directory});
    bool refused = false;
    try { store.save(TradingState{}, 1, 1); }
    catch (const std::runtime_error&) { refused = true; }
    require(refused, "snapshot reported success without syncing its directory");
}

void test_snapshot_catalog_metadata_is_validated_before_publication(
    const std::filesystem::path& root) {
    TradingState recovered;
    ReplicaStatus status;
    {
        Cluster cluster(root, 1);
        (void)build_authorized_submit(cluster);
        cluster.settle();
        recovered = cluster.elect().state();
        status = cluster.elect().status();
    }
    // This is structurally valid and hashable, but contradicts immutable spec 7.
    recovered.active_executable_policy.instrument.maximum_price = PriceTicks::from_atoms(101);
    auto store = identity_test::snapshot_store(root / "1");
    store.save(recovered, static_cast<std::uint64_t>(status.applied_index), status.term);
    const auto before = directory_image(root / "1");
    bool refused = false;
    try {
        ReplicatedKernel restarted(identity_test::recover(root / "1", 1, {1}));
    } catch (const CatalogDependencyUnavailable&) { refused = true; }
    require(refused, "snapshot published metadata that contradicts its installed catalog");
    require(directory_image(root / "1") == before,
            "catalog-inconsistent snapshot refusal rewrote evidence");
}

AuthorityBarrier authority_request(const std::uint64_t sequence = 1) {
    AuthorityBarrier value;
    value.incarnation[0] = std::byte{1};
    value.request_sequence = sequence;
    value.effect_id = EffectId{1};
    value.expected_effect_payload_digest = digest(2);
    value.expected_kill_epoch = KillEpoch{1};
    value.expected_executor_generation = ExecutorGeneration{1};
    value.catalog_root = catalog_test::standard_catalog().root();
    value.authority_domain = identity_test::authority_domain({1, 2, 3});
    return value;
}

AuthorityBarrier live_authority_request(const ReplicatedKernel& leader,
                                        const std::uint64_t sequence = 1) {
    require(leader.state().outbox_effect_count != 0, "authority fixture has no effect");
    const auto& effect = leader.state().outbox_effects[0];
    auto request = authority_request(sequence);
    request.effect_id = effect.effect_id;
    request.expected_effect_payload_digest = effect.payload_digest;
    request.expected_kill_epoch = leader.state().kill_epoch;
    request.expected_executor_generation = leader.state().executor_generation;
    request.catalog_root = leader.state().execution_catalog_root;
    request.authority_domain = leader.static_identity().authority_domain();
    return request;
}

void test_live_authority_proof_is_exact_and_one_shot(const std::filesystem::path& root) {
    Cluster cluster(root, 3, 0xAB);
    (void)build_authorized_submit(cluster);
    auto& leader = cluster.elect();
    const auto request = live_authority_request(leader);
    const auto marker = leader.propose_authority_barrier(request);
    require(marker.status == ProposeStatus::admitted && !leader.take_authority_proof(),
            "authority escaped before durable quorum apply");
    cluster.settle();
    auto proof = leader.take_authority_proof();
    require(proof && proof->view().request == request &&
                proof->view().term == marker.term && proof->view().barrier_index == marker.index &&
                proof->view().applied_index >= marker.index &&
                proof->view().binding.effect.effect_id == request.effect_id &&
                proof->view().binding.effect.payload_digest == request.expected_effect_payload_digest &&
                proof->view().binding.catalog_root == request.catalog_root &&
                !leader.take_authority_proof(),
            "committed marker did not yield one exact one-shot proof");
}

void test_authority_abandonment_drops_subscriber_not_drain(const std::filesystem::path& root) {
    Cluster cluster(root, 3, 0xAB);
    (void)build_authorized_submit(cluster);
    auto& leader = cluster.elect();
    cluster.partitioned.insert(leader.status().node_id);
    const auto pending = live_authority_request(leader);
    const auto marker = leader.propose_authority_barrier(pending);
    leader.abandon_authority_request(pending);
    require(!leader.take_authority_proof() && leader.authority_drain_index() == marker.index,
            "pending abandonment erased retained drain or invented a proof");

    cluster.partitioned.erase(leader.status().node_id);
    cluster.settle();
    require(!leader.take_authority_proof(), "abandoned pending subscriber received a late proof");

    const auto ready = live_authority_request(leader, 2);
    require(leader.propose_authority_barrier(ready).status == ProposeStatus::admitted,
            "ready abandonment setup failed");
    cluster.settle();
    leader.abandon_authority_request(ready);
    require(!leader.take_authority_proof(), "ready abandonment retained permission");
}

void test_authority_result_never_blocks_kill_and_controls_fence_it(const std::filesystem::path& root) {
    Cluster cluster(root, 1, 0xAB);
    (void)build_authorized_submit(cluster);
    auto& leader = cluster.elect();
    const auto request = live_authority_request(leader);
    require(leader.propose_authority_barrier(request).status == ProposeStatus::admitted,
            "proof fixture marker refused");
    cluster.settle();
    require(leader.propose_authority_barrier(live_authority_request(leader, 2)).status ==
                ProposeStatus::authority_busy,
            "full result slot admitted an overwriting marker");
    const auto kill = leader.propose(encoded(command(100, AdvanceKillEpoch{KillEpoch{2}})),
                                     ProposeLane::control);
    require(kill.status == ProposeStatus::admitted,
            "full authority result blocked the emergency control lane");
    cluster.settle();
    require(leader.state().kill_epoch == KillEpoch{2} && !leader.take_authority_proof(),
            "new controls failed to revoke the ready proof");
}

void test_higher_term_revokes_queued_authority(const std::filesystem::path& root) {
    Cluster cluster(root, 3, 0xAB);
    (void)build_authorized_submit(cluster);
    auto& leader = cluster.elect();
    const auto marker = leader.propose_authority_barrier(live_authority_request(leader));
    require(marker.status == ProposeStatus::admitted, "queued-term proof marker refused");
    cluster.settle();
    const auto before = leader.status();
    const auto challenger = before.node_id == 1 ? 2 : 1;
    identity_test::deliver_declared_peer(leader, PeerMessage{challenger, before.node_id,
        velocita::AppendEntries{before.term + 1, before.current_index, marker.term,
                                before.commit_index, {}}});
    require(leader.status().term > before.term, "higher-term fixture did not advance term");
    require(!leader.take_authority_proof(), "higher term left queued authority consumable");
}

void test_kill_committed_before_marker_apply_yields_no_proof(const std::filesystem::path& root) {
    Cluster cluster(root, 3, 0xAB);
    (void)build_authorized_submit(cluster);
    auto& old = cluster.elect();
    const auto old_id = old.status().node_id;
    cluster.partitioned.insert(old_id);
    require(old.propose_authority_barrier(live_authority_request(old)).status ==
                ProposeStatus::admitted, "isolated marker fixture refused");

    ReplicatedKernel* successor = nullptr;
    for (int round = 0; round < 2000 && successor == nullptr; ++round) {
        for (auto& node : cluster.nodes) node->flush_durability();
        for (auto& node : cluster.nodes) node->tick(10);
        cluster.route();
        for (auto& node : cluster.nodes) {
            if (node->status().node_id != old_id && node->is_leader()) {
                successor = node.get();
                break;
            }
        }
    }
    require(successor != nullptr, "majority did not elect marker successor");
    const auto kill = successor->propose(
        encoded(command(100, AdvanceKillEpoch{KillEpoch{2}})), ProposeLane::control);
    require(kill.status == ProposeStatus::admitted, "successor kill refused");
    for (int round = 0; round < 200; ++round) {
        for (auto& node : cluster.nodes) node->flush_durability();
        for (auto& node : cluster.nodes) node->tick(10);
        cluster.route();
    }
    require(successor->state().kill_epoch == KillEpoch{2}, "majority kill did not commit");
    cluster.partitioned.clear();
    for (int round = 0; round < 200; ++round) {
        cluster.route();
        for (auto& node : cluster.nodes) node->flush_durability();
        for (auto& node : cluster.nodes) node->tick(10);
    }
    cluster.check_durability_invariant();
    require(old.state().kill_epoch == KillEpoch{2} && !old.take_authority_proof(),
            "stale marker subscriber gained authority after the newer kill");
}

void test_nonpositive_issuer_refuses_marker_admission(const std::filesystem::path& root) {
    bool refused = false;
    try {
        auto startup = identity_test::bootstrap(root, 0, {0});
        (void)startup;
    } catch (const IdentityStartupError& error) {
        refused = error.code() == IdentityStartupErrorCode::invalid_identity;
    }
    require(refused, "non-positive issuer was not rejected as invalid identity");
}

void test_authority_replay_never_restores_permission(const std::filesystem::path& root) {
    {
        Cluster cluster(root, 1, 0xAB);
        (void)build_authorized_submit(cluster);
        auto& leader = cluster.elect();
        require(leader.propose_authority_barrier(live_authority_request(leader)).status ==
                    ProposeStatus::admitted, "restart proof setup failed");
        cluster.settle();
    }
    ReplicatedKernel restarted(identity_test::recover(root / "1", 1, {1}));
    require(!restarted.take_authority_proof(), "restart replay resurrected send permission");
}

void test_authority_marker_is_not_an_economic_command(const std::filesystem::path& root) {
    Cluster cluster(root, 1, 0xAB);
    auto& leader = cluster.elect();
    const auto before = leader.status();
    auto first_request = authority_request();
    first_request.authority_domain = leader.static_identity().authority_domain();
    const auto marker = leader.propose_authority_barrier(first_request);
    require(marker.status == ProposeStatus::admitted,
            "authority marker was not admitted on the normal lane");
    require(leader.authority_drain_index() == marker.index &&
                leader.status().applied_index == before.applied_index,
            "undurable marker was not retained as a drain obligation");
    require(!leader.take_committed_result(), "marker emitted an economic result before durability");
    cluster.settle();
    const auto after = leader.status();
    require(after.applied_index == marker.index && after.commit_index == marker.index &&
                after.state_version == before.state_version && after.state_digest == before.state_digest &&
                leader.authority_drain_index() == 0 && !leader.take_committed_result(),
            "applied marker changed economic state or emitted a business receipt");
    cluster.restart(1);
    require(cluster.node(1).status().state_digest == before.state_digest &&
                cluster.node(1).status().applied_index == marker.index &&
                !cluster.node(1).take_committed_result(),
            "marker replay changed state or resurrected a result");
    auto& restarted = cluster.elect();
    auto second_request = authority_request(2);
    second_request.authority_domain = restarted.static_identity().authority_domain();
    const auto second = restarted.propose_authority_barrier(second_request);
    require(second.status == ProposeStatus::admitted, "second resolved marker refused");
    cluster.settle();
    // The pinned library snapshots only when at least two log entries exist.
    require(identity_test::complete_compaction(cluster.node(1)) == static_cast<std::uint64_t>(second.index),
            "marker-ended snapshot could not compact");
    cluster.restart(1);
    require(cluster.node(1).status().applied_index == second.index &&
                cluster.node(1).status().state_digest == before.state_digest,
            "marker-ended snapshot did not recover its replication coordinate");
}

void test_old_term_marker_waits_for_current_term_control(const std::filesystem::path& root,
                                                       const ProposeLane lane = ProposeLane::control) {
    Cluster cluster(root, 3, 0xAB);
    auto& old = cluster.elect();
    const auto old_term = old.status().term;
    const auto marker = old.propose_authority_barrier(authority_request());
    require(marker.status == ProposeStatus::admitted, "old-term marker setup failed");
    old.flush_durability();
    // Deliver the append but never deliver the durable acknowledgement back.
    while (auto message = identity_test::take_outbound_message(old)) {
        identity_test::deliver_peer(cluster.node(message->recipient_id), *message,
            cluster.node(message->sender_id).static_identity());
    }
    for (auto& node : cluster.nodes) node->flush_durability();
    for (const auto id : cluster.members) cluster.restart(id);
    auto& successor = cluster.elect();
    cluster.settle(100);
    require(successor.status().term > old_term && successor.authority_drain_index() == marker.index &&
                successor.status().applied_index < marker.index &&
                successor.status().current_index == marker.index,
            "old-term marker unexpectedly committed without a current-term entry");
    const auto end = successor.status().current_index;
    for (std::uint64_t retry = 2; retry != 102; ++retry)
        require(successor.propose_authority_barrier(authority_request(retry)).status ==
                    ProposeStatus::authority_busy,
                "retry bypassed the old-term drain obligation");
    require(successor.status().current_index == end,
            "blocked authority retries appended more log entries");
    const auto kill = successor.propose(encoded(command(1, AdvanceKillEpoch{KillEpoch{1}})),
                                        lane);
    require(kill.status == ProposeStatus::admitted,
            "authority drain obligation blocked the current-term control lane");
    cluster.settle();
    require(successor.status().applied_index == kill.index && successor.authority_drain_index() == 0 &&
                successor.state().kill_epoch == KillEpoch{1},
            "current-term kill failed to commit and drain the old marker");
    const auto result = successor.take_committed_result();
    require(result && result->index == kill.index && !successor.take_committed_result(),
            "draining an old marker resurrected an economic result");
    require(successor.propose_authority_barrier(authority_request(102)).status == ProposeStatus::admitted,
            "resolved old-term obligation permanently wedged marker admission");
    cluster.settle();
}

void test_authority_pending_retry_is_bounded(const std::filesystem::path& root) {
    Cluster cluster(root, 3, 0xAB);
    auto& leader = cluster.elect();
    cluster.partitioned.insert(leader.status().node_id);
    const auto first = leader.propose_authority_barrier(authority_request());
    require(first.status == ProposeStatus::admitted, "isolated leader marker setup failed");
    for (int i = 0; i < 100; ++i) {
        const auto same = leader.propose_authority_barrier(authority_request());
        require(same.status == ProposeStatus::admitted && same.index == first.index,
                "identical pending retry did not reuse its marker");
        require(leader.propose_authority_barrier(authority_request(2)).status == ProposeStatus::authority_busy,
                "new request appended around a pending marker");
    }
    auto changed = authority_request();
    changed.effect_id = EffectId{2};
    require(leader.propose_authority_barrier(changed).status == ProposeStatus::refused,
            "same correlation accepted changed intent");
    require(leader.status().current_index == first.index && leader.status().applied_index < first.index,
            "isolated retries enlarged or applied the authority log");
}

velocita::PeerLogEntry marker_entry(const std::uint64_t sequence, const std::int64_t term = 1) {
    std::array<std::byte, kAuthorityBarrierBytes> bytes{};
    require(encode_authority_barrier(kAuthorityBarrierEntryType, authority_request(sequence), bytes).error ==
                AuthorityBarrierCodecError::none, "marker fixture encoding failed");
    return {term, static_cast<std::int32_t>(sequence), kAuthorityBarrierEntryType,
            std::vector<std::byte>(bytes.begin(), bytes.end())};
}

void test_lagging_follower_marker_history_and_suffix_replacement(const std::filesystem::path& root) {
    const auto open = [&] {
        const auto identity_path = root / kStaticIdentityFileName;
        auto startup = std::filesystem::exists(identity_path)
            ? identity_test::recover(root, 2, {1, 2, 3})
            : identity_test::bootstrap(root, 2, {1, 2, 3});
        return std::make_unique<ReplicatedKernel>(std::move(startup));
    };
    auto follower = open();
    // A leader may have applied A/B before emitting C while this follower lagged.
    // A held append can still carry leader_commit=0; replay cannot reject this backlog.
    velocita::AppendEntries append{1, 0, 0, 0,
        {marker_entry(1), marker_entry(2), marker_entry(3)}};
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, append});
    follower->flush_durability();
    require(follower->authority_drain_index() == 3, "follower refused or forgot multiple retained markers");
    follower.reset();
    follower = open();
    require(follower->authority_drain_index() == 3 && follower->status().applied_index == 0 &&
                !follower->take_committed_result(), "restart lost marker backlog or resurrected results");

    const auto ordinary = encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}));
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2,
        velocita::AppendEntries{2, 2, 1, 0, {{2, 10, RAFT_LOGTYPE_NORMAL, ordinary}}}});
    require(follower->authority_drain_index() == 2,
            "removing C cleared surviving B or retained removed C");
    follower->flush_durability();
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, velocita::AppendEntries{2, 3, 2, 1, {}}});
    require(follower->status().applied_index == 1 && follower->authority_drain_index() == 2,
            "applying A prematurely cleared B");
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, velocita::AppendEntries{2, 3, 2, 2, {}}});
    require(follower->status().applied_index == 2 && follower->authority_drain_index() == 0 &&
                follower->state().state_version == 0 && !follower->take_committed_result(),
            "applying through the final marker did not clear the drain without economic results");

    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, velocita::AppendEntries{2, 3, 2, 2, {marker_entry(4, 2)}}});
    require(follower->authority_drain_index() == 4, "new incoming marker not tracked");
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2,
        velocita::AppendEntries{3, 3, 2, 2, {{3, 11, RAFT_LOGTYPE_NORMAL, ordinary}}}});
    require(follower->authority_drain_index() == 0, "removing last unapplied marker left a stale drain");
}

void test_marker_domain_and_type_refusal_preserves_history(const std::filesystem::path& root) {
    Cluster cluster(root / "foreign-domain", 1);
    auto& derived = cluster.elect();
    // The request helper carries the three-voter domain; this one-voter identity
    // has a different nonzero derived domain and must reject it without append.
    require(derived.propose_authority_barrier(authority_request()).status == ProposeStatus::refused &&
                derived.status().current_index == 0,
            "foreign authority domain admitted a marker");

    auto follower = std::make_unique<ReplicatedKernel>(
        identity_test::bootstrap(root / "follower", 2, {1, 2, 3}));
    const auto before = follower->status();
    const auto storage = directory_image(root / "follower");
    auto valid = marker_entry(1);
    for (int mutation = 0; mutation < 4; ++mutation) {
        auto bad = valid;
        if (mutation == 0) bad.type = 102;
        if (mutation == 1) bad.type = RAFT_LOGTYPE_NORMAL;
        if (mutation == 2) bad.payload.back() ^= std::byte{1};
        if (mutation == 3) {
            auto request = authority_request();
            request.authority_domain = digest(0xAC);
            require(encode_authority_barrier(kAuthorityBarrierEntryType, request, bad.payload).error ==
                        AuthorityBarrierCodecError::none, "wrong-domain fixture invalid");
        }
        bool refused = false;
        try { identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, velocita::AppendEntries{1, 0, 0, 0, {bad}}}); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused && follower->status().current_index == before.current_index &&
                    follower->status().applied_index == before.applied_index &&
                    directory_image(root / "follower") == storage,
                "invalid marker entered history or mutated recovery evidence");
    }
    identity_test::deliver_declared_peer(*follower, PeerMessage{1, 2, velocita::AppendEntries{1, 0, 0, 0, {valid}}});
    follower->flush_durability();
    const auto valid_storage = directory_image(root / "follower");
    follower.reset();
    bool refused = false;
    try {
        ReplicatedKernel wrong_domain(
            identity_test::recover(root / "follower", 2, {1, 2, 3},
                                   catalog_test::standard_catalog(), 0xAC));
    } catch (const IdentityStartupError& error) {
        refused = error.code() == IdentityStartupErrorCode::identity_mismatch;
    }
    require(refused && directory_image(root / "follower") == valid_storage,
            "wrong-domain recovery accepted or rewrote history");
}

void test_zero_durable_follower_does_not_persist_volatile_commit(const std::filesystem::path& root) {
    {
        ReplicatedKernel follower(identity_test::bootstrap(root, 2, {1, 2, 3}));
        const auto bytes = encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}));
        identity_test::deliver_declared_peer(follower, PeerMessage{1, 2,
            velocita::AppendEntries{1, 0, 0, 1, {{1, 1, RAFT_LOGTYPE_NORMAL, bytes}}}});
        require(follower.durable_index() == 0 && follower.state().kill_epoch == KillEpoch{1},
                "zero-durable follower fixture did not apply known committed volatile state");
        const auto metadata = identity_test::metadata_store(root).load();
        require(metadata.commit_index == 0, "follower persisted committed index beyond zero local durability");
    }
    ReplicatedKernel recovered(identity_test::recover(root, 2, {1, 2, 3}));
    require(recovered.status().current_index == 0 && recovered.status().applied_index == 0 &&
                recovered.state().kill_epoch == KillEpoch{},
            "restart invented locally lost state instead of requiring leader catch-up");
}

void test_marker_snapshot_covers_volatile_committed_prefix(const std::filesystem::path& root) {
    {
        ReplicatedKernel follower(identity_test::bootstrap(root, 2, {1, 2, 3}));
        identity_test::deliver_declared_peer(follower, PeerMessage{1, 2,
            velocita::AppendEntries{1, 0, 0, 2, {marker_entry(1), marker_entry(2)}}});
        require(follower.durable_index() == 0 && follower.status().applied_index == 2,
                "volatile marker snapshot fixture not reached");
        require(follower.compact() == 0 && !follower.compaction_pending() && follower.durable_index() == 0,
                "compaction archived volatile entries without durable WAL evidence");
        require(identity_test::complete_compaction(follower) == 2 && follower.durable_index() >= 2,
                "durable marker snapshot left the voter behind its compacted prefix");
        while (identity_test::take_outbound_message(follower)) {}
        identity_test::deliver_declared_peer(follower, PeerMessage{1, 2, velocita::AppendEntries{1, 2, 1, 2, {}}});
        const auto ack = identity_test::take_outbound_message(follower);
        const auto* response = ack ? std::get_if<velocita::AppendEntriesResponse>(&ack->payload) : nullptr;
        require(response && response->success && response->current_index >= 2,
                "snapshot-covered marker prefix remained unacknowledgeable");
    }
    ReplicatedKernel recovered(identity_test::recover(root, 2, {1, 2, 3}));
    require(recovered.status().applied_index == 2 && recovered.authority_drain_index() == 0 &&
                recovered.state().state_version == 0 && !recovered.take_committed_result(),
            "marker snapshot recovery lost its prefix or recreated an economic result");
}

}  // namespace

// --- pre-vote (Raft thesis 9.6; M3 log findings 43 and 47) -------------------
//
// A voter that times out first asks whether it COULD win the next term. Only a
// majority of yes answers lets it raise its term, and a voter that still hears
// its leader says no. An isolated voter therefore cannot climb terms and depose
// a healthy leader when it returns.

bool is_prevote_request(const PeerMessage& message) {
    const auto* vote = std::get_if<velocita::RequestVote>(&message.payload);
    return vote != nullptr && vote->prevote;
}

std::vector<velocita::RequestVoteResponse> drain_vote_replies(
    ReplicatedKernel& node, std::vector<velocita::RequestVote>* requests = nullptr) {
    std::vector<velocita::RequestVoteResponse> replies;
    while (auto message = identity_test::take_outbound_message(node)) {
        if (const auto* reply = std::get_if<velocita::RequestVoteResponse>(&message->payload))
            replies.push_back(*reply);
        else if (const auto* request = std::get_if<velocita::RequestVote>(&message->payload);
                 request != nullptr && requests != nullptr)
            requests->push_back(*request);
    }
    return replies;
}

velocita::RequestVote vote_request(const std::int32_t candidate, const std::int64_t term,
                                   const std::int64_t last_index, const std::int64_t last_term,
                                   const bool prevote) {
    velocita::RequestVote vote{term, candidate, last_index, last_term};
    vote.prevote = prevote;
    return vote;
}

// Voter 2 of {1,2,3} holding entries 1..3 of term 1 from leader 1. With
// `silence`, it then hears nothing for 2.1 s, longer than any randomized
// election timeout (1-2 s), so it no longer has a leader.
std::vector<velocita::RequestVote> follower_of_term_one(ReplicatedKernel& node, const bool silence) {
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, three_entries(1)});
    node.flush_durability();
    std::vector<velocita::RequestVote> sent;
    (void)drain_vote_replies(node, &sent);
    for (int elapsed = 0; silence && elapsed < 2100; elapsed += 10) {
        node.tick(10);
        node.flush_durability();
        (void)drain_vote_replies(node, &sent);
    }
    return sent;
}

velocita::RequestVoteResponse only_reply(ReplicatedKernel& node, const char* what,
                                        const bool prevote = true) {
    node.flush_durability();
    const auto replies = drain_vote_replies(node);
    require(replies.size() == 1, std::string(what) + ": expected one vote reply, got " +
                                     std::to_string(replies.size()));
    require(replies[0].prevote == prevote, std::string(what) + ": the reply's pre-vote flag is wrong");
    return replies[0];
}

void test_a_timed_out_follower_only_asks_and_a_prevote_grant_changes_nothing(
    const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    const auto sent = follower_of_term_one(node, true);
    require(!sent.empty(), "fixture: the follower never timed out");
    for (const auto& request : sent) {
        require(request.prevote, "a timed-out follower sent a real vote request");
        require(request.term == 2 && request.candidate_id == 2 && request.last_log_index == 3 &&
                    request.last_log_term == 1,
                "a timed-out follower's pre-vote did not propose term 2 for its own log");
    }
    require(node.status().term == 1 && node.status().role == "follower",
            "asking for pre-votes changed the asker's term or role");

    const auto before = directory_image(root / "node");
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, vote_request(3, 2, 3, 1, true)});
    const auto granted = only_reply(node, "up-to-date pre-vote");
    require(granted.vote_granted && granted.term == 2,
            "a leaderless voter refused an up-to-date pre-vote");
    require(node.status().term == 1 && node.status().role == "follower",
            "granting a pre-vote changed the grantor's term or role");
    require(directory_image(root / "node") == before, "granting a pre-vote wrote the grantor's storage");

    // The vote itself is still free: a real term-2 candidate gets it, and then
    // the pre-vote's candidate cannot.
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, vote_request(1, 2, 3, 1, false)});
    const auto real = only_reply(node, "real vote", false);
    require(real.vote_granted && real.term == 2,
            "a pre-vote grant consumed the grantor's real vote");
    require(node.status().term == 2 && directory_image(root / "node") != before,
            "fixture: a real vote did not reach hard state, so the image proves nothing");
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, vote_request(3, 2, 3, 1, false)});
    require(!only_reply(node, "second real vote", false).vote_granted, "a voter voted twice in term 2");
}

void test_a_voter_that_hears_its_leader_refuses_prevotes(const std::filesystem::path& root) {
    {
        ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
        (void)follower_of_term_one(node, false);
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, vote_request(3, 2, 3, 1, true)});
        require(!only_reply(node, "pre-vote while led").vote_granted,
                "a follower that just heard its leader granted a pre-vote");
        require(node.status().term == 1 && node.status().leader_id == 1,
                "a refused pre-vote changed the follower's term or leader");
    }
    {
        // Its own expired timer is the voter's verdict on the leader: from the
        // moment it asks, it grants others who have also lost that leader.
        ReplicatedKernel node(identity_test::bootstrap(root / "asker", 2, {1, 2, 3}));
        (void)follower_of_term_one(node, false);
        std::vector<velocita::RequestVote> asked;
        for (int elapsed = 0; elapsed < 2100 && asked.empty(); elapsed += 10) {
            node.tick(10);
            node.flush_durability();
            (void)drain_vote_replies(node, &asked);
        }
        require(!asked.empty(), "fixture: the follower never timed out");
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, vote_request(3, 2, 3, 1, true)});
        require(only_reply(node, "pre-vote after own timeout").vote_granted,
                "a voter that gave up on its leader still refused a pre-vote in its leader's name");
    }
    Cluster cluster(root / "cluster", 3);
    auto& leader = cluster.elect();
    const auto status = leader.status();
    const auto asker = status.node_id == 1 ? 2 : 1;
    const auto log = cluster.node(asker).status();
    identity_test::deliver_peer(leader,
        PeerMessage{asker, status.node_id,
                    vote_request(asker, status.term + 1, log.current_index, status.term, true)},
        cluster.node(asker).static_identity());
    std::vector<velocita::RequestVoteResponse> replies;
    while (auto message = identity_test::take_outbound_message(leader))
        if (const auto* reply = std::get_if<velocita::RequestVoteResponse>(&message->payload))
            replies.push_back(*reply);
    require(replies.size() == 1 && replies[0].prevote, "the leader did not answer the pre-vote as one");
    require(!replies[0].vote_granted, "the leader granted a pre-vote");
    require(leader.is_leader() && leader.status().term == status.term,
            "a pre-vote request unseated the leader or changed its term");
}

void test_a_stale_log_cannot_win_a_prevote(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    (void)follower_of_term_one(node, true);
    // The voter holds 1..3 of term 1 (the fixture delivered them): a log ending
    // earlier, or in an older term, is behind it.
    // The last one proposes the voter's own term: no newer term to win.
    for (const auto& stale : {vote_request(3, 2, 2, 1, true), vote_request(3, 2, 0, 0, true),
                              vote_request(3, 2, 9, 0, true), vote_request(3, 1, 3, 1, true)}) {
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2, stale});
        require(!only_reply(node, "stale pre-vote").vote_granted,
                "a pre-vote from a log behind the voter's was granted (last " +
                    std::to_string(stale.last_log_index) + "/" + std::to_string(stale.last_log_term) + ")");
    }
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, vote_request(3, 2, 3, 1, true)});
    require(only_reply(node, "equal log").vote_granted, "fixture: an equal log was refused too");
    require(node.status().term == 1, "a pre-vote changed the voter's term");
}

// A pre-candidate counts a grant only for the term it proposed and only while
// its round is open; a refusal from a newer term teaches it that term.
void test_a_prevote_round_counts_only_its_own_grants(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root / "node", 2, {1, 2, 3}));
    require(!follower_of_term_one(node, true).empty(), "fixture: the follower never timed out");
    const auto grant = [](const std::int64_t term) {
        velocita::RequestVoteResponse value{term, true};
        value.prevote = true;
        return value;
    };
    const auto expect = [&](const std::string_view role, const std::int64_t term, const char* what) {
        node.flush_durability();
        (void)drain_vote_replies(node);
        const auto status = node.status();
        require(status.role == role && status.term == term,
                std::string(what) + ": now " + std::string(status.role) + " in term " +
                    std::to_string(status.term));
    };
    // One grant is a majority of three with our own: only term 2 may count.
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, grant(3)});
    expect("follower", 1, "a grant for a term the node never proposed started an election");

    velocita::AppendEntries heartbeat{};
    heartbeat.term = 1;
    heartbeat.previous_log_index = 3;
    heartbeat.previous_log_term = 1;
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, heartbeat});
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, grant(2)});
    expect("follower", 1, "a grant arriving after the leader returned started an election");

    (void)follower_of_term_one(node, true);
    velocita::RequestVoteResponse refusal{5, false};
    refusal.prevote = true;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, refusal});
    expect("follower", 5, "a refusal from term 5 did not bring the pre-candidate to term 5");

    for (int elapsed = 0; elapsed < 2100; elapsed += 10) {
        node.tick(10);
        node.flush_durability();
        (void)drain_vote_replies(node);
    }
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2, grant(6)});
    expect("candidate", 6, "fixture: a grant for the proposed term did not start the election");
}

void test_an_isolated_voter_rejoins_without_raising_the_term(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    const auto term = leader.status().term;
    const auto isolated = leader_id == 1 ? 2 : 1;
    std::size_t prevotes = 0;
    cluster.observe = [&](const PeerMessage& message) {
        if (message.sender_id == isolated && is_prevote_request(message)) ++prevotes;
    };
    // 10 s alone: five or more election timeouts, like the wifi drops of finding 43.
    cluster.partitioned.insert(isolated);
    for (int round = 0; round < 1000; ++round) {
        cluster.step();
        const auto status = cluster.node(isolated).status();
        require(status.term == term && status.role == "follower",
                "the isolated voter became " + std::string(status.role) + " in term " +
                    std::to_string(status.term) + " (cluster term " + std::to_string(term) + ")");
    }
    require(prevotes >= 10, "fixture: the isolated voter did not keep timing out (" +
                                std::to_string(prevotes) + " pre-votes)");
    cluster.partitioned.clear();
    cluster.settle(400);
    for (const auto id : cluster.members)
        require(cluster.node(id).status().term == term,
                "voter " + std::to_string(id) + " left term " + std::to_string(term) + " after the rejoin");
    require(cluster.leader() == &leader, "the rejoining voter unseated the healthy leader");
    require(cluster.node(isolated).status().leader_id == leader_id,
            "the rejoined voter does not follow the leader");
    const auto result = cluster.commit(command_from(kMaxOperatorCallerId + 1, 1,
                                                    SetStrategyMode{StrategyMode::normal, {}, {}}));
    require(result.term == term, "the command committed in another term");
    cluster.settle(30);
    require(cluster.node(isolated).status().commit_index == result.index,
            "the rejoined voter did not catch up");
}

void test_a_lost_leader_is_replaced_within_bounded_time(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& old = cluster.elect();
    const auto old_id = old.status().node_id;
    const auto term = old.status().term;
    cluster.partitioned.insert(old_id);
    // A follower times out within 2 s of the last heartbeat; each split
    // pre-vote or vote costs at most another 2 s. 8 s allows three splits.
    ReplicatedKernel* successor = nullptr;
    int elapsed = 0;
    for (; elapsed < 8000 && successor == nullptr; elapsed += 10) {
        cluster.step();
        for (const auto id : cluster.members)
            if (id != old_id && cluster.node(id).is_leader()) successor = &cluster.node(id);
    }
    require(successor != nullptr, "no new leader within 8 s of losing the old one");
    if (std::getenv("VELOCITA_TEST_SEED") != nullptr)
        std::cout << "prevote-leader-loss: new leader after " << elapsed << " ms, term "
                  << successor->status().term << "\n";
    require(successor->status().term > term, "the new leader did not raise the term");
    const auto before = successor->status().commit_index;
    require(successor->propose(encoded(command_from(kMaxOperatorCallerId + 1, 1,
                                                    SetStrategyMode{StrategyMode::normal, {}, {}})))
                    .status == ProposeStatus::admitted,
            "the new leader refused a command");
    cluster.settle(100);
    require(successor->status().commit_index > before, "the new leader could not commit");
    cluster.partitioned.clear();
    cluster.settle(300);
    require(cluster.leader() == successor && !old.is_leader() &&
                old.status().leader_id == successor->status().node_id,
            "the old leader did not step down and follow after the partition healed");
}

void test_a_stale_voter_never_campaigns_and_the_complete_log_wins(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto leader_id = leader.status().node_id;
    const auto term = leader.status().term;
    const auto stale = leader_id == 1 ? 2 : 1;
    const auto fresh = 6 - leader_id - stale;
    cluster.partitioned.insert(stale);
    std::int64_t committed = 0;
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
        committed = cluster.commit(command_from(kMaxOperatorCallerId + 1, sequence,
                                                SetStrategyMode{StrategyMode::normal, {}, {}})).index;
    require(cluster.node(stale).status().current_index < committed,
            "fixture: the stale voter is not behind");

    // The leader is lost and the stale voter returns. For 6 s the complete
    // voter's own requests are dropped, so only the stale one can campaign.
    cluster.partitioned = {leader_id};
    std::size_t asked = 0;
    cluster.observe = [&](const PeerMessage& message) {
        if (message.sender_id == stale && message.recipient_id == fresh && is_prevote_request(message))
            ++asked;
    };
    cluster.drop = [&](const PeerMessage& message) {
        return message.sender_id == fresh &&
            std::holds_alternative<velocita::RequestVote>(message.payload);
    };
    for (int round = 0; round < 600; ++round) {
        cluster.step();
        const auto status = cluster.node(stale).status();
        require(status.role == "follower" && status.term == term,
                "a voter missing committed entries won a pre-vote: now " + std::string(status.role) +
                    " in term " + std::to_string(status.term));
        require(cluster.node(fresh).status().term == term, "a pre-vote raised the complete voter's term");
    }
    require(asked >= 3, "fixture: the stale voter never asked (" + std::to_string(asked) + ")");

    cluster.drop = nullptr;
    ReplicatedKernel* successor = nullptr;
    for (int round = 0; round < 800 && successor == nullptr; ++round) {
        cluster.step();
        for (const auto id : {stale, fresh})
            if (cluster.node(id).is_leader()) successor = &cluster.node(id);
    }
    require(successor == &cluster.node(fresh), "the voter holding every committed entry did not win");
    // It may not know they are committed until it commits in its own term.
    require(successor->status().current_index >= committed, "the new leader lacks a committed entry");
    require(successor->propose(encoded(command_from(kMaxOperatorCallerId + 1, 4,
                                                    SetStrategyMode{StrategyMode::normal, {}, {}})))
                    .status == ProposeStatus::admitted,
            "the new leader refused a command");
    cluster.settle(100);
    require(successor->status().commit_index > committed, "the new leader could not commit");
    require(cluster.node(stale).status().state_digest == successor->status().state_digest &&
                cluster.node(stale).status().commit_index == successor->status().commit_index,
            "the stale voter did not converge on the new leader");
}

// Random single-voter partitions (the leader included) while commands flow.
// Checked every step: one leader per term, and the majority-durable invariant.
// Checked here: every voter applies the same term at every committed index.
void test_prevote_keeps_elections_and_commits_safe_under_partitions(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    (void)cluster.elect();
    std::map<std::int64_t, std::int64_t> committed;
    const auto record = [&] {
        for (auto& node : cluster.nodes)
            while (auto result = node->take_committed_result()) {
                const auto [seen, fresh] = committed.emplace(result->index, result->term);
                require(fresh || seen->second == result->term,
                        "voters committed different entries at index " + std::to_string(result->index));
            }
    };
    std::uint64_t sequence = 0;
    for (int phase = 0; phase < 16; ++phase) {
        cluster.partitioned.clear();
        if (const auto cut = std::rand() % 4; cut != 0) cluster.partitioned.insert(cut);
        const auto rounds = 50 + std::rand() % 300;
        for (int round = 0; round < rounds; ++round) {
            if (round % 25 == 0)
                for (auto& node : cluster.nodes)
                    if (node->is_leader())
                        (void)node->propose(encoded(command_from(kMaxOperatorCallerId + 1, ++sequence,
                            SetStrategyMode{StrategyMode::normal, {}, {}})));
            cluster.step();
            record();
        }
    }
    cluster.partitioned.clear();
    cluster.settle(600);
    record();
    (void)cluster.commit(command_from(kMaxOperatorCallerId + 1, ++sequence,
                                      SetStrategyMode{StrategyMode::normal, {}, {}}));
    cluster.settle(50);
    record();
    require(committed.size() >= 5,
            "fixture: only " + std::to_string(committed.size()) + " entries committed");
    const auto reference = cluster.nodes.front()->status();
    for (const auto& node : cluster.nodes)
        require(node->status().commit_index == reference.commit_index &&
                    node->status().state_digest == reference.state_digest,
                "voters did not converge after the partitions healed");
}

int main(int argc, char** argv) {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          "velocita-v2-replication-tests";
        std::filesystem::remove_all(root);
        const auto fresh = [&](const char* name) {
            const auto path = root / name;
            std::filesystem::create_directories(path);
            return path;
        };

        const auto corrections = [&](const std::string& name) {
            if (name == "authority-noop")
                test_authority_marker_is_not_an_economic_command(fresh("authority-noop"));
            else if (name == "authority-old-term")
                test_old_term_marker_waits_for_current_term_control(fresh("authority-old-term"));
            else if (name == "authority-retry")
                test_authority_pending_retry_is_bounded(fresh("authority-retry"));
            else if (name == "authority-proof")
                test_live_authority_proof_is_exact_and_one_shot(fresh("authority-proof"));
            else if (name == "authority-abandon")
                test_authority_abandonment_drops_subscriber_not_drain(fresh("authority-abandon"));
            else if (name == "authority-result-fence")
                test_authority_result_never_blocks_kill_and_controls_fence_it(fresh("authority-result-fence"));
            else if (name == "authority-term-fence")
                test_higher_term_revokes_queued_authority(fresh("authority-term-fence"));
            else if (name == "authority-kill-before-apply")
                test_kill_committed_before_marker_apply_yields_no_proof(fresh("authority-kill-before-apply"));
            else if (name == "authority-issuer")
                test_nonpositive_issuer_refuses_marker_admission(fresh("authority-issuer"));
            else if (name == "authority-no-replay")
                test_authority_replay_never_restores_permission(fresh("authority-no-replay"));
            else if (name == "authority-follower")
                test_lagging_follower_marker_history_and_suffix_replacement(fresh("authority-follower"));
            else if (name == "authority-domain")
                test_marker_domain_and_type_refusal_preserves_history(fresh("authority-domain"));
            else if (name == "zero-durable-commit")
                test_zero_durable_follower_does_not_persist_volatile_commit(fresh("zero-durable-commit"));
            else if (name == "authority-volatile-snapshot")
                test_marker_snapshot_covers_volatile_committed_prefix(fresh("authority-volatile-snapshot"));
            else if (name == "catalog-snapshot-binding")
                test_snapshot_catalog_metadata_is_validated_before_publication(fresh("catalog-snapshot-binding"));
            else if (name == "suffix-staged")
                test_suffix_replacement_invalidates_old_durability(fresh("suffix-staged"), false, false);
            else if (name == "suffix-durable")
                test_suffix_replacement_invalidates_old_durability(fresh("suffix-durable"), true, false);
            else if (name == "suffix-threaded")
                test_suffix_replacement_invalidates_old_durability(fresh("suffix-threaded"), false, true);
            else if (name == "writer-truncation-race")
                test_truncation_does_not_race_the_writer(fresh("writer-truncation-race"));
            else if (name == "compacted-corrupt")
                test_compacted_corrupt_snapshot_refuses_empty_recovery(fresh("compacted-corrupt"));
            else if (name == "corrupt-wal-sweep")
                test_corruption_of_acknowledged_history_fails_closed(fresh("corrupt-wal-sweep"));
            else if (name == "no-auto-promotion")
                test_winning_leadership_is_not_promotion(fresh("no-auto-promotion"));
            else if (name == "control-lane-admission")
                test_the_control_lane_admits_only_authenticated_control(
                    fresh("control-lane-admission"));
            else if (name == "control-lane")
                test_a_flood_of_normal_commands_cannot_starve_a_kill(fresh("control-lane"));
            else if (name == "mass-cancel-commit")
                test_a_committed_mass_cancel_is_answerable_and_recoverable(
                    fresh("mass-cancel-commit"));
            else if (name == "v1-v2-isolation")
                test_v2_storage_is_not_v1_storage(fresh("v1-v2-isolation"));
            else if (name == "snapshot-directory")
                test_snapshot_directory_failure_is_not_success(fresh("snapshot-directory"));
            else if (name == "hard-state-cuts")
                test_hard_state_atomic_replace_cut_matrix(fresh("hard-state-cuts"));
            else if (name == "unsupported-snapshot")
                test_an_unsupported_snapshot_schema_refuses_without_touching_storage(
                    fresh("unsupported-snapshot"));
            else if (name == "unsupported-segment-suffix")
                test_an_incompatible_uncommitted_segment_suffix_refuses_without_touching_storage(
                    fresh("unsupported-segment-suffix"));
            else if (name == "unsupported-oversized-snapshot")
                test_an_oversized_future_snapshot_is_incompatible_not_corrupt(
                    fresh("unsupported-oversized-snapshot"));
            else if (name == "snapshot-preflight-readonly")
                test_snapshot_incompatibility_is_checked_before_opening_writable_storage(
                    fresh("snapshot-preflight-readonly"));
            else if (name == "unsupported-legacy-suffix")
                test_an_incompatible_uncommitted_legacy_suffix_refuses_without_touching_storage(
                    fresh("unsupported-legacy-suffix"));
            else if (name == "stranded-voter")
                test_a_voter_behind_the_leaders_snapshot_does_not_stop_the_cluster(fresh("stranded-voter"));
            else if (name == "prevote-grant")
                test_a_timed_out_follower_only_asks_and_a_prevote_grant_changes_nothing(fresh("prevote-grant"));
            else if (name == "prevote-sticky")
                test_a_voter_that_hears_its_leader_refuses_prevotes(fresh("prevote-sticky"));
            else if (name == "prevote-stale-log")
                test_a_stale_log_cannot_win_a_prevote(fresh("prevote-stale-log"));
            else if (name == "prevote-round")
                test_a_prevote_round_counts_only_its_own_grants(fresh("prevote-round"));
            else if (name == "prevote-isolated")
                test_an_isolated_voter_rejoins_without_raising_the_term(fresh("prevote-isolated"));
            else if (name == "prevote-leader-loss")
                test_a_lost_leader_is_replaced_within_bounded_time(fresh("prevote-leader-loss"));
            else if (name == "prevote-stale-voter")
                test_a_stale_voter_never_campaigns_and_the_complete_log_wins(fresh("prevote-stale-voter"));
            else if (name == "prevote-safety")
                test_prevote_keeps_elections_and_commits_safe_under_partitions(fresh("prevote-safety"));
            else if (name == "identity-membership")
                test_membership_drift_refuses_before_recovery_and_preserves_history(
                    fresh("identity-membership"));
            else throw std::runtime_error("unknown correction test: " + name);
        };
        // Election timeouts come from rand(); a sweep reruns a test per seed.
        if (const char* seed = std::getenv("VELOCITA_TEST_SEED"))
            std::srand(static_cast<unsigned>(std::stoul(seed)));
        if (argc == 2) {
            corrections(argv[1]);
            std::cout << argv[1] << " passed\n";
            return 0;
        }
        for (const auto* name : {"suffix-staged", "suffix-durable", "suffix-threaded",
                                 "writer-truncation-race", "compacted-corrupt",
                                 "snapshot-directory"})
            corrections(name);

        test_only_committed_commands_invoke_the_reducer(fresh("committed-only"));
        test_authority_marker_is_not_an_economic_command(fresh("authority-noop"));
        test_old_term_marker_waits_for_current_term_control(fresh("authority-old-term"));
        test_authority_pending_retry_is_bounded(fresh("authority-retry"));
        test_live_authority_proof_is_exact_and_one_shot(fresh("authority-proof"));
        test_authority_abandonment_drops_subscriber_not_drain(fresh("authority-abandon"));
        test_authority_result_never_blocks_kill_and_controls_fence_it(fresh("authority-result-fence"));
        test_higher_term_revokes_queued_authority(fresh("authority-term-fence"));
        test_kill_committed_before_marker_apply_yields_no_proof(fresh("authority-kill-before-apply"));
        test_nonpositive_issuer_refuses_marker_admission(fresh("authority-issuer"));
        test_authority_replay_never_restores_permission(fresh("authority-no-replay"));
        test_old_term_marker_waits_for_current_term_control(fresh("authority-old-term-normal"), ProposeLane::normal);
        test_lagging_follower_marker_history_and_suffix_replacement(fresh("authority-follower"));
        test_marker_domain_and_type_refusal_preserves_history(fresh("authority-domain"));
        test_zero_durable_follower_does_not_persist_volatile_commit(fresh("zero-durable-commit"));
        test_marker_snapshot_covers_volatile_committed_prefix(fresh("authority-volatile-snapshot"));
        test_follower_routing_preserves_command_identity(fresh("routing"));
        test_a_leader_crash_before_commit_creates_no_effect(fresh("cut-before"));
        test_a_leader_crash_after_commit_reproduces_the_same_effect(fresh("cut-after"));
        test_a_retry_after_a_lost_response_returns_the_retained_result(fresh("retry"));
        test_quorum_loss_creates_no_new_risk_decision(fresh("quorum"));
        test_a_restarted_voter_recovers_state_outbox_and_dedup(fresh("restart"));
        test_all_voters_converge(fresh("converge"));
        test_a_committed_outbox_read_carries_its_own_authority(fresh("outbox-read"));
        test_malformed_normal_commands_never_enter_durable_history(fresh("malformed-normal"));
        test_zero_authority_bootstrap_cannot_authorize_an_effect(
            fresh("zero-authority-bootstrap"));
        test_a_snapshot_is_durable_before_its_prefix_is_reclaimed(fresh("compact"));
        test_snapshot_catalog_metadata_is_validated_before_publication(fresh("catalog-snapshot-binding"));
        test_a_restart_installs_the_snapshot_and_replays_the_rest(fresh("install"));
        test_membership_drift_refuses_before_recovery_and_preserves_history(
            fresh("identity-membership"));
        test_a_corrupt_snapshot_degrades_to_replay_rather_than_refusing_to_start(
            fresh("corrupt-snapshot"));
        test_an_unsupported_snapshot_schema_refuses_without_touching_storage(
            fresh("unsupported-snapshot"));
        test_an_oversized_future_snapshot_is_incompatible_not_corrupt(
            fresh("unsupported-oversized-snapshot"));
        test_snapshot_incompatibility_is_checked_before_opening_writable_storage(
            fresh("snapshot-preflight-readonly"));
        test_an_incompatible_uncommitted_segment_suffix_refuses_without_touching_storage(
            fresh("unsupported-segment-suffix"));
        test_an_incompatible_uncommitted_legacy_suffix_refuses_without_touching_storage(
            fresh("unsupported-legacy-suffix"));
        test_recovery_is_never_behind_the_last_acknowledged_commit(fresh("power-cut"));
        test_durability_is_tracked_separately_and_never_overstated(fresh("durable"));
        test_corruption_of_acknowledged_history_fails_closed(fresh("corrupt-wal-sweep"));
        test_a_torn_hard_state_write_never_produces_a_corrupt_hard_state(
            fresh("torn-hard-state"));
        test_hard_state_atomic_replace_cut_matrix(fresh("hard-state-cuts"));
        test_v2_storage_is_not_v1_storage(fresh("v1-v2-isolation"));
        test_a_flood_of_normal_commands_cannot_starve_a_kill(fresh("control-lane"));
        test_the_control_lane_admits_only_authenticated_control(
            fresh("control-lane-admission"));
        test_winning_leadership_is_not_promotion(fresh("no-auto-promotion"));
        test_an_entry_cannot_commit_before_its_bytes_are_durable(fresh("gate"));
        test_a_follower_ack_never_covers_merely_staged_entries(fresh("follower-ack"));
        test_a_durable_follower_acks_without_waiting_to_be_asked(fresh("early-ack"));
        test_an_early_ack_never_covers_entries_the_leader_has_not_verified(fresh("early-ack-verified"));
        test_an_early_ack_never_speaks_for_a_term_already_left(fresh("early-ack-term"));
        test_a_commit_does_not_wait_for_a_heartbeat(fresh("no-heartbeat-wait"));
        test_status_reports_retained_log_use_by_pool(fresh("retained-pools"));
        test_a_voter_behind_the_leaders_snapshot_does_not_stop_the_cluster(fresh("stranded-voter"));
        test_the_compaction_floor_is_the_slowest_voters_position(fresh("compaction-floor"));
        test_a_follower_never_lowers_its_known_floor(fresh("floor-monotonic"));
        test_compaction_never_cuts_above_the_floor(fresh("gated-compaction"));
        test_the_floor_waits_for_the_leaders_own_disk(fresh("floor-leader-disk"));
        test_voters_compacted_to_their_end_can_still_elect(fresh("compacted-election"));
        test_an_early_ack_waits_for_the_writer_thread_to_sync(fresh("early-ack-writer"));
        test_a_writer_thread_carries_durability_off_the_owner_loop(fresh("writer"));
        test_one_voter_reaches_the_same_state(fresh("voter-count"));

        // Last on purpose: willemt draws election timeouts from the process-wide
        // rand(), so a test added earlier would reseed every later test's timing.
        test_a_committed_mass_cancel_is_answerable_and_recoverable(fresh("mass-cancel-commit"));

        test_a_timed_out_follower_only_asks_and_a_prevote_grant_changes_nothing(fresh("prevote-grant"));
        test_a_voter_that_hears_its_leader_refuses_prevotes(fresh("prevote-sticky"));
        test_a_stale_log_cannot_win_a_prevote(fresh("prevote-stale-log"));
        test_a_prevote_round_counts_only_its_own_grants(fresh("prevote-round"));
        test_an_isolated_voter_rejoins_without_raising_the_term(fresh("prevote-isolated"));
        test_a_lost_leader_is_replaced_within_bounded_time(fresh("prevote-leader-loss"));
        test_a_stale_voter_never_campaigns_and_the_complete_log_wins(fresh("prevote-stale-voter"));
        test_prevote_keeps_elections_and_commits_safe_under_partitions(fresh("prevote-safety"));

        std::filesystem::remove_all(root);
        std::cout << "all V2 replication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 replication test failed: " << error.what() << "\n";
        return 1;
    }
}
