// Generated Phase 4 property corpus.
//
// The Phase 4 gate is stated as a property, not as agreement with a second
// implementation: fault/property tests must not be able to produce an unreserved
// effect, a duplicate economic mutation, a negative reservation, or a replay
// mismatch. This corpus drives randomized but well-formed command histories and
// checks those properties after every single command.
//
// Independence argument. Most checks are contract-derived invariants rather
// than a transcription of reducer branches. A deliberately tiny execution
// ledger adds the one independent model Phase 4 needs most: accepted buys add
// position, accepted sells subtract it, and duplicate identities add nothing.
// It does not model order states, holds, reclamation, or return-code selection,
// so it can fail differently from the implementation without becoming a second
// copy of it. The Phase 1-3 mirror remains in v2_model_replay_tests.cpp.
//
// Leverage worth knowing about: encode_state_snapshot runs the full structural
// validator, which already enforces per-account conservation - reserved totals
// must equal the sum over that account's reservations - plus reservation
// ownership, non-negativity, and index bounds. So snapshotting after every
// command is itself a dense invariant check, and this corpus adds the properties
// the validator cannot see: cross-command monotonicity, reclamation being
// economically inert, and byte-identical canonical replay of the full history.

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/replay.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint64_t marker) {
    Digest256 value;
    for (std::size_t index = 0; index < 8; ++index) {
        value.bytes[index] = static_cast<std::byte>((marker >> (index * 8)) & 0xFF);
    }
    return value;
}

class Generator final {
public:
    explicit Generator(const std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        state_ += 0x9e3779b97f4a7c15ULL;
        auto value = state_;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    std::uint64_t bounded(const std::uint64_t upper_exclusive) noexcept {
        return next() % upper_exclusive;
    }

private:
    std::uint64_t state_;
};

// --- properties ------------------------------------------------------------

// Every retained effect must belong to a retained order, and that order must own
// the reservation it points at. Reclamation removes a group whole, so a surviving
// effect naming a missing order means compaction dropped records unevenly - the
// exact failure that would strand a pending external effect.
void require_no_unreserved_effect(const TradingState& state,
                                  const std::string& where) {
    for (std::size_t index = 0; index < state.outbox_effect_count; ++index) {
        const auto& effect = state.outbox_effects[index];
        bool owned = false;
        for (std::size_t order = 0; order < state.order_count; ++order) {
            if (state.orders[order].cluster_order_id != effect.cluster_order_id) {
                continue;
            }
            owned = true;
            const auto reservation_index = state.orders[order].reservation_index;
            require(reservation_index < state.reservation_count,
                    where + ": order points outside the reservation arena");
            require(state.reservations[reservation_index].cluster_order_id ==
                        effect.cluster_order_id,
                    where + ": order does not own the reservation it points at");
        }
        require(owned, where + ": an outbox effect outlived its cluster order");
    }
}

// Quantities cannot go negative or exceed what was originally reserved. The
// snapshot validator checks the per-account sums; this checks the per-record
// arithmetic that feeds them.
void require_reservation_arithmetic(const TradingState& state,
                                    const std::string& where) {
    for (std::size_t index = 0; index < state.reservation_count; ++index) {
        const auto& reservation = state.reservations[index];
        require(reservation.original_quantity.atoms() >= 0 &&
                    reservation.filled_quantity.atoms() >= 0 &&
                    reservation.remaining_quantity.atoms() >= 0 &&
                    reservation.remaining_notional.atoms() >= 0 &&
                    reservation.margin_hold.atoms() >= 0 &&
                    reservation.fee_hold.atoms() >= 0,
                where + ": a reservation went negative");
        require(reservation.filled_quantity <= reservation.original_quantity,
                where + ": filled quantity exceeded the original reservation");
        const auto sum = reservation.filled_quantity.checked_add(
            reservation.remaining_quantity);
        require(sum.has_value() && *sum <= reservation.original_quantity,
                where + ": filled plus remaining exceeded the original reservation");
        if (reservation.state == ReservationState::released) {
            require(reservation.remaining_quantity.atoms() == 0 &&
                        reservation.remaining_notional.atoms() == 0 &&
                        reservation.margin_hold.atoms() == 0 &&
                        reservation.fee_hold.atoms() == 0,
                    where + ": a released reservation still holds economics");
        }
    }
}

// Snapshot encode runs the structural validator, including per-account
// conservation; decode must reproduce the exact state.
void require_snapshot_round_trip(const TradingState& state,
                                 const std::string& where) {
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none,
            where + ": state failed canonical snapshot invariants");
    TradingState restored;
    StateSnapshotScratch scratch;
    require(decode_state_snapshot({bytes.data(), encoded.bytes_written}, restored,
                                  scratch)
                    .error == StateSnapshotError::none,
            where + ": canonical snapshot did not decode");
    std::array<std::byte, kMaxStateSnapshotBytes> again{};
    const auto re_encoded = encode_state_snapshot(restored, again);
    require(re_encoded.error == StateSnapshotError::none &&
                re_encoded.bytes_written == encoded.bytes_written &&
                std::equal(bytes.begin(),
                           bytes.begin() +
                               static_cast<std::ptrdiff_t>(encoded.bytes_written),
                           again.begin()),
            where + ": snapshot round trip was not byte-identical");
}


// --- fixture ---------------------------------------------------------------
//
// Phase 1-3 scaffolding (policy catalog, executable-policy bundle, source
// cursor) is built directly: it is already covered by the Phase 1-3 mirror model
// and is not what this corpus is testing. Every piece of PHASE 4 state - risk
// accounts, decisions, orders, reservations, outbox effects - is created only by
// commands, so the corpus exercises the real admission paths rather than a
// hand-assembled state that may be unreachable in practice.

PolicyArtifact corpus_policy() {
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
    value.rule_count = 2;
    value.rules[0] = {0, PredicateOperator::greater_than_or_equal,
                      FeatureMicros::from_atoms(15), Directive::place_limit};
    value.rules[1] = {0, PredicateOperator::greater_than,
                      FeatureMicros::from_atoms(5), Directive::place_limit};
    return value;
}

FeatureSchemaArtifact corpus_schema() {
    FeatureSchemaArtifact value;
    value.schema_version = kFeatureSchemaArtifactSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.feature_count = 1;
    value.features[0] = {FeatureEconomicKind::price, 1};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

ActionTemplate corpus_template(const Side side, const std::uint16_t slot) {
    ActionTemplate value;
    value.schema_version = kActionTemplateSchemaVersion;
    value.directive = Directive::place_limit;
    value.side = side;
    value.time_in_force = TimeInForce::good_til_cancel;
    value.post_only = PostOnlyMode::required;
    value.order_slot = LogicalOrderSlot{slot};
    value.instrument_spec_version = InstrumentSpecVersion{7};
    value.venue_capabilities_version = VenueCapabilitiesVersion{8};
    value.price_source = {EconomicValueSourceKind::feature, 0,
                          RoundingMode::toward_zero, {}, 1, {}};
    value.quantity_source = {EconomicValueSourceKind::fixed, 0,
                             RoundingMode::toward_zero,
                             QuantityLots::from_atoms(2), 0};
    value.safety_caps = {QuantityLots::from_atoms(2),
                         MoneyMicros::from_atoms(10'000),
                         PriceTicks::from_atoms(2), PriceTicks::from_atoms(1),
                         100, value.price_source};
    return value;
}

ExecutablePolicyArtifact corpus_executable_policy(
    const FeatureSchemaArtifact& schema) {
    ExecutablePolicyArtifact value;
    value.schema_version = kExecutablePolicyArtifactSchemaVersion;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.policy_artifact_digest = corpus_policy().artifact_digest;
    value.feature_schema_id = schema.feature_schema_id;
    value.feature_schema_digest = schema.artifact_digest;
    value.template_count = 2;
    value.templates[0] = corpus_template(Side::buy, 1);
    value.templates[1] = corpus_template(Side::sell, 2);
    value.rule_binding_count = 2;
    value.rule_template_slots[0] = ActionTemplateSlot{1};
    value.rule_template_slots[1] = ActionTemplateSlot{2};
    value.artifact_digest = compute_artifact_digest(value).digest;
    return value;
}

TradingState bootstrap_state() {
    TradingState state;
    // The bundle records the StateVersions at which it was staged and activated,
    // and validation requires both to be at or below the current version. A
    // bootstrap that declares them while sitting at version zero describes a
    // history that never happened.
    state.state_version = 2;
    state.strategy_mode = StrategyMode::normal;
    state.kill_epoch = KillEpoch{2};
    state.executor_generation = ExecutorGeneration{3};
    state.policy_count = 1;
    state.policies[0] = {corpus_policy(), PolicyLifecycle::active};
    const auto schema = corpus_schema();
    state.active_executable_policy = {
        true,
        schema,
        corpus_executable_policy(schema),
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9},
         VenueCapabilitiesVersion{8}, kVenueDirectivePlaceLimit, kVenueTifGtc, 1},
        1,
        2};
    // The source cursor is deliberately NOT pre-registered: a zero sequence is
    // an invalid cursor, and letting the first RecordSourceEvent establish it
    // keeps the corpus honest about admission rather than hand-seeding state the
    // kernel would never produce.
    return state;
}

DecisionFrame corpus_frame(const std::int64_t feature,
                           const std::uint64_t sequence,
                           const Digest256& evidence,
                           const TradingState& state) {
    DecisionFrame value;
    value.policy_id = PolicyId{10};
    value.policy_version = PolicyVersion{1};
    value.feature_schema_id = FeatureSchemaId{3};
    value.venue_id = VenueId{9};
    value.venue_session_id = VenueSessionId{1};
    value.venue_sequence = VenueSequence{sequence};
    value.source_id = SourceId{12};
    value.source_generation = SourceGeneration{1};
    value.source_sequence = SourceSequence{sequence};
    value.event_time_ns = 150;
    value.receive_time_ns = 151;
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.feature_count = 1;
    value.features[0] = FeatureMicros::from_atoms(feature);
    value.market_data_status = MarketDataStatus::ready;
    value.market_data_age_ns = 1;
    value.position_version = state.risk_account_count > 0
                                 ? state.risk_accounts[0].position_version
                                 : PositionVersion{1};
    value.risk_version = state.risk_account_count > 0
                             ? state.risk_accounts[0].risk_version
                             : RiskVersion{1};
    value.raw_evidence_digest = evidence;
    return value;
}

struct Coverage {
    bool authorized{};
    bool partial_fill{};
    bool full_fill{};
    bool cancelled{};
    bool reconciled{};
    bool unknown{};
    bool configured{};
    bool reclaimed{};
    bool settled_incomplete{};
    bool refused{};
    bool sell_fill{};
    bool same_request_duplicate{};
    bool cross_request_duplicate{};
    bool observation_identity_conflict{};
    std::size_t applied{};
    std::size_t total{};

    [[nodiscard]] bool sufficient() const noexcept {
        return authorized && partial_fill && full_fill && cancelled && reconciled &&
               unknown && configured && reclaimed && settled_incomplete && refused &&
               sell_fill && same_request_duplicate && cross_request_duplicate &&
               observation_identity_conflict;
    }
};


// --- corpus driver ---------------------------------------------------------

void run_seed(const std::uint64_t seed, Coverage& coverage) {
    Generator generator(seed);
    auto state = bootstrap_state();
    ApplyScratch scratch;
    std::vector<Command> history;

    std::uint64_t request = 1;
    std::uint64_t sequence = 0;
    std::uint64_t decision_id = 100;
    std::uint64_t execution_id = 1'000;
    std::uint64_t venue_sequence = 5'000;
    std::vector<DecisionId> unauthorized;
    std::vector<Command> applied_observations;
    std::int64_t model_position = 0;
    std::uint64_t trace_step = 0;

    const auto admit = [&](const Command& command, const std::string& where) {
        // Every command is recorded, refusals included: an admitted refusal
        // retains a receipt and advances StateVersion, and an effect's canonical
        // committed_directive_index is that StateVersion at authorization time.
        // A replay that skipped refusals would authorize at a different version
        // and diverge on bytes for reasons unrelated to the reducer.
        const auto outcome = apply(state, command, scratch);
        history.push_back(command);
        ++coverage.total;
        if (outcome.code == ApplyCode::applied) ++coverage.applied;
        if (outcome.code != ApplyCode::applied &&
            outcome.code != ApplyCode::already_applied) {
            coverage.refused = true;
        }
        const auto location = where + " seed=" + std::to_string(seed) +
                              " step=" + std::to_string(trace_step);
        require_no_unreserved_effect(state, location);
        require_reservation_arithmetic(state, location);
        require_snapshot_round_trip(state, location);
        return outcome;
    };

    // Create the risk account through B1 rather than by hand.
    {
        const Command create{
            {CallerId{19}, RequestSequence{request++}, digest(0xe2)},
            ConfigureRiskLimits{AccountId{5}, InstrumentId{4}, RiskPolicyVersion{6},
                                RiskVersion{}, state.kill_epoch,
                                state.executor_generation,
                                QuantityLots::from_atoms(4),
                                QuantityLots::from_atoms(8),
                                MoneyMicros::from_atoms(4'000),
                                MoneyMicros::from_atoms(8'000), 1'000'000, 2'500}};
        require(admit(create, "risk account creation").code == ApplyCode::applied,
                "corpus could not create its risk account");
        coverage.configured = true;
    }

    for (std::uint64_t step = 0; step < 220; ++step) {
        trace_step = step + 1;
        const auto previous_reclaimed = state.reclaimed_group_count;
        const auto previous_settled = state.settled_incomplete_effect_count;
        const auto totals_before = state.risk_accounts[0];

        switch (generator.bounded(10)) {
            case 0:
            case 1: {  // evaluate a decision, steering the slot by feature value
                ++sequence;
                const auto evidence = digest(0x400 + sequence);
                admit({{CallerId{7}, RequestSequence{request}, digest(0x21)},
                       RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                         SourceSequence{sequence}, evidence}},
                      "source admission");
                const std::int64_t feature =
                    generator.bounded(2) == 0 ? 16 : 11;  // slot 1 buy / slot 2 sell
                const DecisionId id{decision_id++};
                const auto outcome = admit(
                    {{CallerId{8}, RequestSequence{request++},
                      digest(0x800 + id.value())},
                     EvaluateDecisionFrame{id, EvaluationMode::authoritative,
                                           corpus_frame(feature, sequence, evidence,
                                                        state)}},
                    "decision evaluation");
                if (outcome.code == ApplyCode::applied) unauthorized.push_back(id);
                break;
            }
            case 2: {  // authorize a retained decision
                if (unauthorized.empty()) break;
                const auto pick = generator.bounded(unauthorized.size());
                const auto id = unauthorized[pick];
                const auto outcome = admit(
                    {{CallerId{1}, RequestSequence{request++}, digest(0x900 + id.value())},
                     AuthorizeDecision{id}},
                    "authorization");
                if (outcome.code == ApplyCode::applied) {
                    coverage.authorized = true;
                    unauthorized.erase(unauthorized.begin() +
                                       static_cast<std::ptrdiff_t>(pick));
                }
                break;
            }
            case 3:
            case 4: {  // fill an existing order, sometimes partially
                if (state.order_count == 0) break;
                const auto& order =
                    state.orders[generator.bounded(state.order_count)];
                if (order.reservation_index >= state.reservation_count) break;
                const auto remaining =
                    state.reservations[order.reservation_index].remaining_quantity;
                if (remaining.atoms() <= 0) break;
                const EconomicActionProposal* proposal = nullptr;
                for (std::size_t index = 0; index < state.decision_count; ++index) {
                    if (state.decision_hot[index].decision_id == order.decision_id) {
                        proposal = &state.decision_cold[index].proposal;
                        break;
                    }
                }
                if (proposal == nullptr) break;
                EffectId submit_effect{};
                for (std::size_t index = 0; index < state.outbox_effect_count;
                     ++index) {
                    const auto& effect = state.outbox_effects[index];
                    if (effect.cluster_order_id == order.cluster_order_id &&
                        effect.operation == EffectOperation::submit) {
                        submit_effect = effect.effect_id;
                        break;
                    }
                }
                if (submit_effect.value() == 0) break;
                const std::int64_t quantity =
                    (remaining.atoms() > 1 && generator.bounded(2) == 0)
                        ? 1
                        : remaining.atoms();
                VenueObservation fill;
                fill.kind = VenueObservationKind::fill;
                fill.venue_id = VenueId{9};
                fill.venue_session_id = VenueSessionId{2};
                fill.venue_sequence = VenueSequence{venue_sequence++};
                fill.cluster_order_id = order.cluster_order_id;
                fill.effect_id = submit_effect;
                fill.venue_order_id = VenueOrderId{70};
                fill.execution_id = ExecutionId{execution_id++};
                fill.side = proposal->side;
                fill.price = proposal->price;
                fill.quantity = QuantityLots::from_atoms(quantity);
                fill.fee = FeeMicros::from_atoms(3);
                fill.evidence_digest = digest(0xA00 + fill.execution_id.value());
                const Command fill_command{
                    {CallerId{2}, RequestSequence{request++},
                     observation_fingerprint(fill)},
                    ObserveFill{fill}};
                const auto outcome = admit(fill_command, "fill");
                if (outcome.code == ApplyCode::applied) {
                    applied_observations.push_back(fill_command);
                    if (fill.side == Side::sell) {
                        coverage.sell_fill = true;
                        model_position -= quantity;
                    } else {
                        model_position += quantity;
                    }
                    if (quantity < remaining.atoms()) coverage.partial_fill = true;
                    else coverage.full_fill = true;
                }
                break;
            }
            case 5: {  // cancel an order
                if (state.order_count == 0) break;
                const auto& order =
                    state.orders[generator.bounded(state.order_count)];
                const auto outcome = admit(
                    {{CallerId{3}, RequestSequence{request++}, digest(0xB00 + request)},
                     RequestCancel{order.cluster_order_id}},
                    "cancel request");
                if (outcome.code == ApplyCode::applied) coverage.cancelled = true;
                break;
            }
            case 6: {  // venue order event against the current effect
                if (state.outbox_effect_count == 0) break;
                const auto& effect =
                    state.outbox_effects[generator.bounded(state.outbox_effect_count)];
                static constexpr std::array kinds{
                    VenueObservationKind::order_acknowledged,
                    VenueObservationKind::cancelled,
                    VenueObservationKind::effect_unknown,
                    VenueObservationKind::cancel_reconciled};
                const auto kind = kinds[generator.bounded(kinds.size())];
                VenueObservation event;
                event.kind = kind;
                event.venue_id = VenueId{9};
                event.venue_session_id = VenueSessionId{2};
                event.venue_sequence = VenueSequence{venue_sequence++};
                event.cluster_order_id = effect.cluster_order_id;
                event.effect_id = effect.effect_id;
                if (kind == VenueObservationKind::order_acknowledged ||
                    kind == VenueObservationKind::cancelled ||
                    kind == VenueObservationKind::cancel_reconciled) {
                    event.venue_order_id = VenueOrderId{70};
                }
                if (kind == VenueObservationKind::cancel_reconciled) {
                    for (std::size_t i = 0; i < state.order_count; ++i) {
                        if (state.orders[i].cluster_order_id !=
                            effect.cluster_order_id) continue;
                        if (state.orders[i].reservation_index <
                            state.reservation_count) {
                            event.quantity =
                                state.reservations[state.orders[i].reservation_index]
                                    .filled_quantity;
                        }
                    }
                }
                event.evidence_digest = digest(0xC00 + event.venue_sequence.value());
                const Command event_command{
                    {CallerId{4}, RequestSequence{request++},
                     observation_fingerprint(event)},
                    ObserveOrderEvent{event}};
                const auto outcome = admit(event_command, "venue order event");
                if (outcome.code == ApplyCode::applied) {
                    applied_observations.push_back(event_command);
                    if (kind == VenueObservationKind::effect_unknown) {
                        coverage.unknown = true;
                    }
                    if (kind == VenueObservationKind::cancel_reconciled) {
                        coverage.reconciled = true;
                    }
                }
                break;
            }
            case 7: {  // duplicate and conflicting observation identities
                if (applied_observations.empty()) break;
                auto command = applied_observations[
                    generator.bounded(applied_observations.size())];
                const auto retained = std::find_if(
                    state.receipts.begin(),
                    state.receipts.begin() +
                        static_cast<std::ptrdiff_t>(state.receipt_count),
                    [&](const RequestReceipt& receipt) {
                        return receipt.identity.caller_id ==
                                   command.identity.caller_id &&
                               receipt.identity.request_sequence ==
                                   command.identity.request_sequence &&
                               receipt.result.code == ApplyCode::applied;
                    });
                if (retained == state.receipts.begin() +
                                    static_cast<std::ptrdiff_t>(state.receipt_count)) {
                    break;
                }
                const auto risk_before = state.risk_accounts[0];
                const auto duplicate_kind = generator.bounded(3);

                // Only a NATIVE observation can be judged by venue sequence
                // identity. A synthetic one - a gateway-declared timeout, or a
                // reconciliation answer - carries a number the gateway chose, so
                // a repeat of it is re-adjudicated rather than refused on
                // identity. What must hold either way is the economic invariant
                // asserted below, and that is the property this case exists for.
                auto origin = ObservationOrigin::native;
                if (const auto* event = std::get_if<ObserveOrderEvent>(&command.body)) {
                    origin = observation_origin(event->observation.kind);
                }
                const bool synthetic = origin == ObservationOrigin::synthetic;

                ApplyCode expected = ApplyCode::applied;
                if (duplicate_kind == 1) {
                    command.identity.request_sequence = RequestSequence{request++};
                    expected = ApplyCode::already_applied;
                } else if (duplicate_kind == 2) {
                    command.identity.request_sequence = RequestSequence{request++};
                    if (auto* fill = std::get_if<ObserveFill>(&command.body)) {
                        fill->observation.evidence_digest =
                            digest(0xF00 + request);
                        command.identity.payload_digest =
                            observation_fingerprint(fill->observation);
                        expected = ApplyCode::fill_identity_conflict;
                    } else if (auto* event =
                                   std::get_if<ObserveOrderEvent>(&command.body)) {
                        event->observation.evidence_digest =
                            digest(0xF00 + request);
                        command.identity.payload_digest =
                            observation_fingerprint(event->observation);
                        expected = ApplyCode::venue_event_identity_conflict;
                    }
                }
                const auto outcome = admit(command, "duplicate observation");
                // For a synthetic repeat the code is a function of the ORDER's
                // current state rather than of identity, so pinning one value
                // here would pin the fixture's timing instead of the contract.
                // What is pinned is the thing that would be wrong: the venue's
                // sequence domain must never claim a number the gateway chose.
                const bool code_matches =
                    synthetic && duplicate_kind != 0
                        ? outcome.code != ApplyCode::venue_event_identity_conflict
                        : outcome.code == expected;
                require(code_matches,
                        "duplicate observation returned the wrong code seed=" +
                            std::to_string(seed) + " step=" +
                            std::to_string(trace_step) + " code=" +
                            std::to_string(static_cast<int>(outcome.code)));
                if (duplicate_kind == 0) coverage.same_request_duplicate = true;
                if (duplicate_kind == 1) coverage.cross_request_duplicate = true;
                if (duplicate_kind == 2) {
                    coverage.observation_identity_conflict = true;
                }
                require(state.risk_accounts[0].reserved_quantity ==
                                risk_before.reserved_quantity &&
                            state.risk_accounts[0].reserved_margin ==
                                risk_before.reserved_margin &&
                            state.risk_accounts[0].reserved_fee ==
                                risk_before.reserved_fee &&
                            state.risk_accounts[0].position_quantity ==
                                risk_before.position_quantity,
                        "duplicate observation changed economics seed=" +
                            std::to_string(seed) + " step=" +
                            std::to_string(trace_step));
                break;
            }
            default: {  // refusals and control traffic
                if (generator.bounded(2) == 0 && state.order_count > 0) {
                    // Native replacement is disabled and must stay refused.
                    const auto& order = state.orders[0];
                    const auto outcome = admit(
                        {{CallerId{5}, RequestSequence{request++}, digest(0xD00 + request)},
                         RequestReplace{order.cluster_order_id, DecisionId{decision_id}}},
                        "replacement request");
                    // The property is that replacement NEVER succeeds, not that
                    // it always fails for the same reason: once the corpus
                    // exhausts the 256-receipt arena, admission refuses on
                    // capacity before dispatch ever reaches the replacement
                    // handler. Both are correct fail-closed outcomes.
                    require(outcome.code != ApplyCode::applied,
                            "native replacement was admitted by the corpus");
                } else {
                    const auto& risk = state.risk_accounts[0];
                    admit({{CallerId{19}, RequestSequence{request++}, digest(0xE00 + request)},
                           ConfigureRiskLimits{risk.account_id, risk.instrument_id,
                                               risk.risk_policy_version,
                                               risk.risk_version, state.kill_epoch,
                                               state.executor_generation,
                                               risk.max_order_quantity,
                                               risk.max_reserved_quantity,
                                               risk.max_order_gross_notional,
                                               risk.max_reserved_exposure,
                                               risk.initial_margin_ppm,
                                               risk.maximum_fee_ppm}},
                          "risk reconfiguration");
                }
                break;
            }
        }

        require(state.risk_accounts[0].position_quantity.atoms() == model_position,
                "independent execution ledger diverged seed=" +
                    std::to_string(seed) + " step=" +
                    std::to_string(trace_step));

        // Monotonic accounting: neither counter may ever decrease.
        require(state.reclaimed_group_count >= previous_reclaimed &&
                    state.settled_incomplete_effect_count >= previous_settled,
                "a Phase 4 accounting counter went backwards");
        if (state.reclaimed_group_count > previous_reclaimed) {
            coverage.reclaimed = true;
            // Reclamation must be economically inert: it only ever removes
            // groups whose reservations are already released and therefore hold
            // nothing, so no account total may move because of a sweep.
            require(state.risk_accounts[0].reserved_quantity ==
                            totals_before.reserved_quantity &&
                        state.risk_accounts[0].reserved_margin ==
                            totals_before.reserved_margin &&
                        state.risk_accounts[0].reserved_fee ==
                            totals_before.reserved_fee,
                    "reclamation changed an economic total");
        }
        if (state.settled_incomplete_effect_count > previous_settled) {
            coverage.settled_incomplete = true;
        }
    }

    // Canonical replay of the whole history, refusals included, must converge to
    // byte-identical state.
    auto replayed = bootstrap_state();
    ApplyScratch replay_scratch;
    KernelCommandCodecScratch codec_scratch;
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    for (const auto& command : history) {
        const auto encoded = encode_kernel_command(command, command_bytes);
        require(encoded.error == KernelCodecError::none,
                "corpus command did not encode canonically");
        require(apply_encoded_kernel_command(
                    replayed, {command_bytes.data(), encoded.bytes_written},
                    replay_scratch, codec_scratch, result_bytes)
                        .error == ReplayError::none,
                "corpus command failed canonical replay");
    }
    std::array<std::byte, kMaxStateSnapshotBytes> direct_bytes{};
    std::array<std::byte, kMaxStateSnapshotBytes> replay_bytes{};
    const auto direct = encode_state_snapshot(state, direct_bytes);
    const auto replay = encode_state_snapshot(replayed, replay_bytes);
    require(direct.error == StateSnapshotError::none &&
                replay.error == StateSnapshotError::none &&
                direct.bytes_written == replay.bytes_written &&
                std::equal(direct_bytes.begin(),
                           direct_bytes.begin() +
                               static_cast<std::ptrdiff_t>(direct.bytes_written),
                           replay_bytes.begin()),
            "canonical replay diverged from direct apply for seed " +
                std::to_string(seed));
}

void test_generated_phase4_properties() {
    Coverage coverage;
    for (std::uint64_t seed = 1; seed <= 24; ++seed) {
        run_seed(seed, coverage);
    }
    require(coverage.sufficient(),
            "the generated corpus did not exercise every required Phase 4 outcome");
    // Guard against a future change quietly making the corpus vacuous - for
    // example by exhausting an arena early so every later step is a no-op.
    //
    // A high refusal rate is expected and wanted: the generator deliberately
    // aims commands at stale decisions, released reservations, occupied slots
    // and closed orders, which is how the refusal paths get exercised at all.
    // The threshold only has to catch "almost nothing was admitted", so it is
    // set at a third rather than tuned to the current ratio, which would make it
    // a change detector instead of a vacuity guard.
    require(coverage.applied * 3 > coverage.total,
            "fewer than one third of corpus commands were admitted: " +
                std::to_string(coverage.applied) + "/" +
                std::to_string(coverage.total));
    std::cout << "phase 4 property corpus: 24 seeds, 220 steps each, "
              << coverage.applied << "/" << coverage.total
              << " commands admitted\n";
}

}  // namespace

int main() {
    try {
        test_generated_phase4_properties();
        std::cout << "all V2 Phase 4 property tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 Phase 4 property test failed: " << error.what() << '\n';
        return 1;
    }
}
