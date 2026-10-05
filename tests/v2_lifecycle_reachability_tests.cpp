#include "v2_catalog_test_support.hpp"

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
    TradingState state = catalog_test::state();
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
         (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc, 1},
        1, 2};
    state.risk_account_count = 1;
    state.risk_accounts[0] = {
        AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6}, RiskVersion{1},
        PositionVersion{1}, QuantityLots::from_atoms(4),
        QuantityLots::from_atoms(8), {}, MoneyMicros::from_atoms(4'000),
        MoneyMicros::from_atoms(8'000), {}, {}, 1'000'000, 2'500, {},
        QuantityLots::from_atoms(1'000), true};
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

struct Signature {
    ClusterOrderState order{};
    OutboxState effect{};
    EffectOperation operation{};
    ReservationState reservation{};
    bool has_booked_fill{};
    bool group_settled{};

    auto operator<=>(const Signature&) const noexcept = default;

    [[nodiscard]] std::string text() const {
        return std::string(name_of(order)) + " / " + name_of(effect) + " / " +
               name_of(operation) + " / " +
               (reservation == ReservationState::released ? "released" : "held") +
               (has_booked_fill ? " / booked" : "") +
               (group_settled ? " / SETTLED" : "");
    }
};

[[nodiscard]] std::vector<std::byte> canonical_bytes(const TradingState& state) {
    std::array<std::byte, kMaxStateSnapshotBytes> buffer{};
    const auto encoded = encode_state_snapshot(state, buffer);
    if (encoded.error != StateSnapshotError::none) return {};
    require(execution_catalog_bindings_valid(state, catalog_test::standard_catalog()),
            "reachable lifecycle state contradicted its execution catalog");
    return {buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

[[nodiscard]] bool group_is_settled(const TradingState& state) {
    if (state.order_count != 1) return false;
    const auto& order = state.orders[0];
    const bool terminal = order.state == ClusterOrderState::filled ||
                          order.state == ClusterOrderState::cancelled ||
                          order.state == ClusterOrderState::rejected;
    if (!terminal) return false;
    if (order.reservation_index >= state.reservation_count) return false;
    if (state.reservations[order.reservation_index].state !=
        ReservationState::released) {
        return false;
    }
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        if (effect.cluster_order_id != order.cluster_order_id) continue;
        const bool settled = effect.state == OutboxState::acknowledged ||
                             effect.state == OutboxState::rejected ||
                             effect.state == OutboxState::conflict ||
                             effect.state == OutboxState::incomplete;
        if (!settled) return false;
    }
    return true;
}

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
           reservation.filled_quantity.atoms() > 0, group_is_settled(state)};
    return true;
}

struct Step {
    Command command;
    const char* label;
};

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

struct Reachable {
    std::map<Signature, std::string> first_path;
    std::map<Signature, std::set<std::string>> accepted;
    std::map<Signature, std::set<Signature>> transitions;
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
    require(catalog_test::apply(seed, evaluate, scratch).code == ApplyCode::applied,
            "reachability seed could not evaluate");
    const Command authorize{{CallerId{1}, RequestSequence{++counter}, digest(0x52)},
                            AuthorizeDecision{DecisionId{20}}};
    require(catalog_test::apply(seed, authorize, scratch).code == ApplyCode::applied,
            "reachability seed could not authorize");

    struct Node {
        TradingState state = catalog_test::state();
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
            const auto before = canonical_bytes(next);
            const auto result = catalog_test::apply(next, step.command, step_scratch);
            const auto after = canonical_bytes(next);
            if (after == before) continue;
            (void)result;
            Signature signature{};
            if (!signature_of(next, signature)) continue;

            Signature from{};
            if (signature_of(node.state, from)) {
                found.accepted[from].insert(step.label);
                if (!(signature == from)) found.transitions[from].insert(signature);
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

}

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

        for (const auto& [signature, destinations] : found.transitions) {
            if (signature.order != ClusterOrderState::conflict) continue;
            for (const auto& next : destinations) {
                require(next.order == ClusterOrderState::conflict,
                        "a conflicted order changed lifecycle state, so a "
                        "recorded disagreement can be erased: " +
                            signature.text() + " -> " + next.text());
            }
        }

        std::map<Signature, bool> resolves;
        const auto reaches_rest = [&](const Signature& start) {
            std::set<Signature> seen{start};
            std::deque<Signature> queue{start};
            while (!queue.empty()) {
                const auto here = queue.front();
                queue.pop_front();
                if (here.group_settled ||
                    here.order == ClusterOrderState::conflict) {
                    return true;
                }
                const auto edges = found.transitions.find(here);
                if (edges == found.transitions.end()) continue;
                for (const auto& next : edges->second) {
                    if (seen.insert(next).second) queue.push_back(next);
                }
            }
            return false;
        };
        for (const auto& [signature, path] : found.first_path) {
            (void)path;
            if (signature.order == ClusterOrderState::conflict) continue;
            if (signature.group_settled) continue;
            resolves[signature] = reaches_rest(signature);
            require(resolves[signature],
                    "a recoverable position cannot reach settlement or "
                    "escalation, so its order group is pinned forever: " +
                        signature.text());
        }

        require(resolves.size() >= 8,
                "too few recoverable positions were checked for reachability (" +
                    std::to_string(resolves.size()) + "), so the property is "
                    "close to vacuous");
        std::size_t multi_hop = 0;
        for (const auto& [signature, ok] : resolves) {
            (void)ok;
            const auto edges = found.transitions.find(signature);
            if (edges == found.transitions.end()) continue;
            bool direct = false;
            for (const auto& next : edges->second) {
                direct = direct || next.group_settled ||
                         next.order == ClusterOrderState::conflict;
            }
            if (!direct) ++multi_hop;
        }
        require(multi_hop > 0,
                "every checked position settles in one step, so the reachability "
                "search never traverses the graph and adds nothing over the "
                "weaker has-a-successor property it replaced");

        require(!found.first_path.empty(), "exploration reached nothing");
        std::cout << "all V2 lifecycle reachability checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 lifecycle reachability failed: " << error.what() << '\n';
        return 1;
    }
}
