// An exhaustive inventory of the order lifecycle states the reducer can actually
// reach, and what every observation does in each one.
//
// WHY THIS EXISTS. Phase 5 grew `apply_observe_order_event` into a large set of
// interacting predicates - reconciled_submit, reconciled_cancel,
// cancel_reconcilable, cancel_agrees, cancel_contradicts, late_cancel_completion,
// reconciliation, terminal, effect_resolved - feeding a guard disjunction with
// negations. Reading that code is how it was written, so reading it again is a
// poor way to find what is wrong with it. This instead DRIVES the kernel through
// every command sequence it accepts, breadth first, and records the truth.
//
// Two things fall out that review-by-reading does not give:
//
//   THE REACHABLE SET. Phase 5 already shipped one branch that cannot be reached
//   and two fixtures that built states the kernel cannot produce. An enumerated
//   inventory makes both kinds of mistake visible instead of plausible.
//
//   A CHANGE DETECTOR. The inventory is asserted. A future edit that creates a
//   new reachable lifecycle state, or that makes an existing one unreachable,
//   fails this test and forces the author to say why.
//
// The fixture here is built independently of the one in the delivery tests, on
// purpose. Phase 4 used the same discipline for its property corpus: a second
// construction of the same setup is a cross-check, and a shared helper would hide
// exactly the kind of fixture error that has already bitten this phase twice.

#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/metrics.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t marker) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

const char* name_of(const ClusterOrderState state) {
    switch (state) {
        case ClusterOrderState::dispatch_authorized: return "dispatch_authorized";
        case ClusterOrderState::acknowledged:        return "acknowledged";
        case ClusterOrderState::live:                return "live";
        case ClusterOrderState::partially_filled:    return "partially_filled";
        case ClusterOrderState::cancel_pending:      return "cancel_pending";
        case ClusterOrderState::replace_pending:     return "replace_pending";
        case ClusterOrderState::submission_unknown:  return "submission_unknown";
        case ClusterOrderState::reconciling:         return "reconciling";
        case ClusterOrderState::filled:              return "filled";
        case ClusterOrderState::cancelled:           return "cancelled";
        case ClusterOrderState::rejected:            return "rejected";
        case ClusterOrderState::conflict:            return "conflict";
    }
    return "?";
}

const char* name_of(const OutboxState state) {
    switch (state) {
        case OutboxState::pending:      return "pending";
        case OutboxState::claimed:      return "claimed";
        case OutboxState::attempted:    return "attempted";
        case OutboxState::acknowledged: return "acknowledged";
        case OutboxState::rejected:     return "rejected";
        case OutboxState::unknown:      return "unknown";
        case OutboxState::reconciling:  return "reconciling";
        case OutboxState::conflict:     return "conflict";
        case OutboxState::incomplete:   return "incomplete";
    }
    return "?";
}

const char* name_of(const EffectOperation operation) {
    switch (operation) {
        case EffectOperation::submit:  return "submit";
        case EffectOperation::cancel:  return "cancel";
        case EffectOperation::replace: return "replace";
    }
    return "?";
}

// --- an independently built fixture ----------------------------------------

PolicyArtifact policy() {
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

FeatureSchemaArtifact feature_schema() {
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_count = 1;
    value.features[0] = {FeatureEconomicKind::price, 1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

ExecutablePolicyArtifact executable_policy(const FeatureSchemaArtifact& schema) {
    ActionTemplate action;
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

    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.policy_artifact_digest = policy().artifact_digest;
    value.feature_schema_id = schema.feature_schema_id;
    value.feature_schema_digest = schema.artifact_digest;
    value.template_count = 1;
    value.templates[0] = action;
    value.rule_binding_count = 1;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

TradingState origin() {
    TradingState state;
    state.state_version = 2;
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {policy(), PolicyLifecycle::active};
    const auto schema = feature_schema();
    state.active_executable_policy = {
        true, schema, executable_policy(schema),
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         kVenueDirectivePlaceLimit, kVenueTifGtc, 1},
        1, 2};
    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{1},
        PositionVersion{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), {}, MoneyMicros::from_atoms(4'000),
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500};
    state.source_count = 1;
    state.sources[0] = {SourceId{12}, SourceGeneration{1}, SourceSequence{1}};
    state.source_event_count = 1;
    state.source_events[0] = {SourceId{12}, SourceGeneration{1},
                              SourceSequence{1}, digest(0x44)};
    return state;
}

DecisionFrame frame(const TradingState& state) {
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
    value.position_version = state.risk_accounts[0].position_version;
    value.risk_version = state.risk_accounts[0].risk_version;
    value.raw_evidence_digest = digest(0x44);
    return value;
}

// --- the lifecycle signature -----------------------------------------------

// What distinguishes one lifecycle position from another for review purposes.
// Deliberately coarse: request sequences, digests and state versions differ on
// every path and would make the search never converge without telling anyone
// anything.
struct Signature {
    ClusterOrderState order{};
    OutboxState effect{};
    EffectOperation operation{};
    ReservationState reservation{};
    bool has_booked_fill{};

    auto operator<=>(const Signature&) const noexcept = default;

    [[nodiscard]] std::string text() const {
        return std::string(name_of(order)) + " / " + name_of(effect) + " / " +
               name_of(operation) + " / " +
               (reservation == ReservationState::released ? "released" : "held") +
               (has_booked_fill ? " / booked" : "");
    }
};

[[nodiscard]] bool signature_of(const TradingState& state, Signature& out) {
    if (state.order_count != 1) return false;
    const auto& order = state.orders[0];
    const OutboxEffectRecord* current = nullptr;
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].effect_id == order.effect_id) {
            current = &state.outbox_effects[index];
        }
    }
    if (current == nullptr) return false;
    if (order.reservation_index >= state.reservation_count) return false;
    const auto& reservation = state.reservations[order.reservation_index];
    out = {order.state, current->state, current->operation, reservation.state,
           reservation.filled_quantity.atoms() > 0};
    return true;
}

// --- command generation ----------------------------------------------------

struct Step {
    Command command;
    const char* label;
};

// Every command the kernel accepts that can move one order's lifecycle. Each is
// offered in every state; the kernel decides what is applicable, which is the
// entire point - a generator that pre-filtered would only ever confirm what its
// author already believed.
std::vector<Step> successors(const TradingState& state, std::uint64_t& counter) {
    std::vector<Step> steps;
    if (state.order_count != 1) return steps;
    const auto& order = state.orders[0];
    const auto cluster_order = order.cluster_order_id;
    const auto current_effect = order.effect_id;
    EffectId submit_effect{};
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        if (state.outbox_effects[index].cluster_order_id == cluster_order &&
            state.outbox_effects[index].operation == EffectOperation::submit) {
            submit_effect = state.outbox_effects[index].effect_id;
        }
    }
    const auto booked =
        order.reservation_index < state.reservation_count
            ? state.reservations[order.reservation_index].filled_quantity.atoms()
            : 0;

    const auto identity = [&counter](const Digest256& payload) {
        return CommandIdentity{CallerId{1}, RequestSequence{++counter}, payload};
    };

    steps.push_back({{identity(digest(0x91)), RequestCancel{cluster_order}},
                     "RequestCancel"});

    const auto observation = [&](const VenueObservationKind kind,
                                 const EffectId effect_id,
                                 const std::uint64_t venue_order,
                                 const std::int64_t quantity,
                                 const char* label) {
        VenueObservation value;
        value.kind = kind;
        value.venue_id = VenueId{9};
        value.venue_session_id = VenueSessionId{1};
        value.venue_sequence = VenueSequence{++counter + 1'000};
        value.cluster_order_id = cluster_order;
        value.effect_id = effect_id;
        value.venue_order_id = VenueOrderId{venue_order};
        value.quantity = QuantityLots::from_atoms(quantity);
        value.evidence_digest = digest(static_cast<std::uint8_t>(1 + counter % 255));
        if (kind == VenueObservationKind::fill) {
            value.execution_id = ExecutionId{counter + 2'000};
            value.side = Side::buy;
            value.price = PriceTicks::from_atoms(11);
        }
        if (validate(value) != ContractError::none) return;
        steps.push_back({{identity(observation_fingerprint(value)),
                          kind == VenueObservationKind::fill
                              ? CommandBody{ObserveFill{value}}
                              : CommandBody{ObserveOrderEvent{value}}},
                         label});
    };

    for (const auto effect_id : {current_effect, submit_effect}) {
        if (effect_id.value() == 0) continue;
        const bool is_current = effect_id == current_effect;
        observation(VenueObservationKind::order_acknowledged, effect_id, 88, 0,
                    is_current ? "ack(current)" : "ack(submit)");
        observation(VenueObservationKind::order_rejected, effect_id, 0, 0,
                    is_current ? "reject(current)" : "reject(submit)");
        observation(VenueObservationKind::cancelled, effect_id, 88, 0,
                    is_current ? "cancelled(current)" : "cancelled(submit)");
        observation(VenueObservationKind::effect_unknown, effect_id, 0, 0,
                    is_current ? "unknown(current)" : "unknown(submit)");
        // Quantity variants around what the cluster has booked: the three
        // relations are the whole of the reconciliation adjudication.
        for (const auto quantity : {booked, booked + 1, booked - 1}) {
            if (quantity < 0) continue;
            observation(VenueObservationKind::cancel_reconciled, effect_id, 88,
                        quantity,
                        quantity == booked   ? "cancel_final(=booked)"
                        : quantity > booked  ? "cancel_final(>booked)"
                                             : "cancel_final(<booked)");
            observation(VenueObservationKind::order_reconciled, effect_id, 88,
                        quantity,
                        quantity == booked   ? "reconciled(=booked)"
                        : quantity > booked  ? "reconciled(>booked)"
                                             : "reconciled(<booked)");
        }
        observation(VenueObservationKind::order_reconciled, effect_id, 0, 0,
                    "reconciled(venue denies)");
        observation(VenueObservationKind::fill, effect_id, 88, 1, "fill(1)");
        observation(VenueObservationKind::fill, effect_id, 88, 2, "fill(2)");
    }
    return steps;
}

// --- the exploration -------------------------------------------------------

struct Reachable {
    std::map<Signature, std::string> first_path;
    // For every reachable signature, which labelled steps were APPLIED there.
    std::map<Signature, std::set<std::string>> accepted;
};

Reachable explore(const std::size_t depth_limit) {
    ApplyScratch scratch;
    Reachable found;

    auto seed = origin();
    std::uint64_t counter = 0;
    const Command evaluate{{CallerId{1}, RequestSequence{++counter}, digest(0x51)},
                           EvaluateDecisionFrame{DecisionId{20},
                                                 EvaluationMode::authoritative,
                                                 frame(seed)}};
    require(apply(seed, evaluate, scratch).code == ApplyCode::applied,
            "reachability seed could not evaluate");
    const Command authorize{{CallerId{1}, RequestSequence{++counter}, digest(0x52)},
                            AuthorizeDecision{DecisionId{20}}};
    require(apply(seed, authorize, scratch).code == ApplyCode::applied,
            "reachability seed could not authorize");

    struct Node {
        TradingState state;
        std::uint64_t counter;
        std::string path;
        std::size_t depth;
    };
    std::deque<Node> queue;
    Signature start{};
    require(signature_of(seed, start), "reachability seed has no signature");
    found.first_path[start] = "(authorized)";
    queue.push_back({seed, counter, "", 0});

    std::set<Signature> visited{start};
    while (!queue.empty()) {
        const auto node = queue.front();
        queue.pop_front();
        if (node.depth >= depth_limit) continue;

        auto local = node.counter;
        for (const auto& step : successors(node.state, local)) {
            auto next = node.state;
            ApplyScratch step_scratch;
            const auto result = apply(next, step.command, step_scratch);
            if (result.code != ApplyCode::applied) continue;
            Signature signature{};
            if (!signature_of(next, signature)) continue;

            Signature from{};
            if (signature_of(node.state, from)) {
                found.accepted[from].insert(step.label);
            }
            if (visited.count(signature) != 0) continue;
            visited.insert(signature);
            found.first_path[signature] =
                node.path.empty() ? step.label : node.path + " -> " + step.label;
            queue.push_back({next, local, found.first_path[signature],
                             node.depth + 1});
        }
    }
    return found;
}

// The reviewed inventory of reachable lifecycle positions. Every entry here was
// looked at and judged correct; the assertions below make that judgement sticky.
const std::array<const char*, 27> kReviewedInventory{{
    "cancel_pending / pending / cancel / held",
    "cancel_pending / pending / cancel / held / booked",
    "cancelled / acknowledged / cancel / released",
    "cancelled / acknowledged / cancel / released / booked",
    "cancelled / rejected / cancel / released",
    "cancelled / rejected / cancel / released / booked",
    "conflict / conflict / cancel / held / booked",
    "dispatch_authorized / pending / submit / held",
    "filled / acknowledged / cancel / released / booked",
    "filled / acknowledged / submit / released / booked",
    "filled / pending / cancel / released / booked",
    "filled / reconciling / cancel / released / booked",
    "filled / rejected / cancel / released / booked",
    "filled / unknown / cancel / released / booked",
    "live / acknowledged / submit / held",
    "live / rejected / cancel / held",
    "partially_filled / acknowledged / submit / held / booked",
    "partially_filled / rejected / cancel / held / booked",
    "reconciling / reconciling / cancel / held",
    "reconciling / reconciling / cancel / held / booked",
    "reconciling / rejected / cancel / held",
    "reconciling / rejected / cancel / held / booked",
    "rejected / rejected / cancel / released",
    "rejected / rejected / submit / released",
    "submission_unknown / unknown / cancel / held",
    "submission_unknown / unknown / cancel / held / booked",
    "submission_unknown / unknown / submit / held",
}};

}  // namespace

int main() {
    try {
        const auto limit = std::getenv("VELOCITA_LIFECYCLE_DEPTH");
        const auto found = explore(limit != nullptr ? std::strtoul(limit, nullptr, 10) : 7);

        if (std::getenv("VELOCITA_LIFECYCLE_REPORT") != nullptr) {
            std::cout << "\nreachable lifecycle signatures: "
                      << found.first_path.size() << "\n\n";
            for (const auto& [signature, path] : found.first_path) {
                std::printf("  %-62s  %s\n", signature.text().c_str(), path.c_str());
            }
            std::cout << "\naccepted transitions by signature:\n\n";
            for (const auto& [signature, labels] : found.accepted) {
                std::printf("  %s\n", signature.text().c_str());
                for (const auto& label : labels) {
                    std::printf("      %s\n", label.c_str());
                }
            }
            std::cout << '\n';
        }

        // 1. THE INVENTORY. Reviewed once, asserted from then on. A change here
        //    means an edit created or removed a reachable lifecycle position, and
        //    whoever made it has to say why rather than discover it later.
        std::set<std::string> reached;
        for (const auto& [signature, path] : found.first_path) {
            (void)path;
            reached.insert(signature.text());
        }
        std::set<std::string> expected(kReviewedInventory.begin(),
                                       kReviewedInventory.end());
        for (const auto& signature : reached) {
            require(expected.count(signature) != 0,
                    "a lifecycle position became reachable that no one has "
                    "reviewed: " + signature);
        }
        for (const auto& signature : expected) {
            require(reached.count(signature) != 0,
                    "a reviewed lifecycle position is no longer reachable, so "
                    "either a transition was lost or the inventory is stale: " +
                        signature);
        }

        // 2. CONFLICT IS A SINK. Terminal AND unresolved: the cluster and the
        //    venue disagree and it cannot tell which is right, so nothing may
        //    carry the order out of it. A later fill used to do exactly that -
        //    moving it to `filled`, releasing the reservation and erasing the
        //    disagreement from the projection - which is ADR 0001's "never
        //    overwrites conflicting evidence to make a dashboard green".
        for (const auto& [signature, labels] : found.accepted) {
            require(signature.order != ClusterOrderState::conflict,
                    "a conflicted order accepted a transition, so a recorded "
                    "disagreement can be erased: " + signature.text() + " <- " +
                        *labels.begin());
        }

        // 3. NO UNSETTLED EFFECT IS A DEAD END. An effect that can never be
        //    settled pins its whole order group in the bounded arenas forever,
        //    and does it while the order looks finished. This caught a cancel
        //    left `pending` when its order filled: the gateway redelivered until
        //    its attempts ran out, every rejection was refused because the order
        //    was terminal, and reconciliation never targets a finished order.
        for (const auto& signature : found.first_path) {
            const auto effect = signature.first.effect;
            const bool unsettled = effect != OutboxState::acknowledged &&
                                   effect != OutboxState::rejected &&
                                   effect != OutboxState::conflict &&
                                   effect != OutboxState::incomplete;
            if (!unsettled) continue;
            require(found.accepted.count(signature.first) != 0,
                    "an unsettled effect has no way forward, so its order group "
                    "is pinned in the arenas permanently: " +
                        signature.first.text());
        }

        require(!found.first_path.empty(), "exploration reached nothing");
        std::cout << "all V2 lifecycle reachability checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 lifecycle reachability failed: " << error.what() << '\n';
        return 1;
    }
}
