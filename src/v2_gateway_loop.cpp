#include "velocita/v2/gateway_loop.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/gateway_dispatch.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace velocita::v2 {
namespace {

void append(std::vector<std::byte>& out, std::string_view text) {
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
}

template <typename T>
void append_le(std::vector<std::byte>& out, T value) {
    auto raw = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<std::byte>((raw >> (8 * i)) & 0xff));
}

// The payload digest binds every byte of the command except the digest field:
// encode with a constant placeholder there, then hash under a domain tag.
Command identified(CallerId caller, RequestSequence sequence, CommandBody body) {
    Command command{{caller, sequence, {}}, std::move(body)};
    command.identity.payload_digest.bytes.fill(std::byte{0x01});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    if (encoded.error != KernelCodecError::none) throw std::invalid_argument("gateway command does not encode");
    std::vector<std::byte> tagged;
    append(tagged, "velocita.v2.gateway-command.1");
    tagged.insert(tagged.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written));
    command.identity.payload_digest = sha256_digest(tagged);
    return command;
}

CommandBody evidence_body(const VenueObservation& observation) {
    if (observation.kind == VenueObservationKind::fill) return ObserveFill{observation};
    return ObserveOrderEvent{observation};
}

bool accepted_code(ApplyCode code) noexcept { return code == ApplyCode::applied || code == ApplyCode::already_applied; }

const OutboxEffectRecord* outbox(const TradingState& state, EffectId effect) noexcept {
    for (std::size_t i = 0; i < state.outbox_effect_count; ++i)
        if (state.outbox_effects[i].effect_id == effect) return &state.outbox_effects[i];
    return nullptr;
}

VenueQueryStatus status_of(sim::QueryOutcome outcome) noexcept {
    switch (outcome) {
    case sim::QueryOutcome::known: return VenueQueryStatus::known;
    case sim::QueryOutcome::not_found: return VenueQueryStatus::not_found;
    case sim::QueryOutcome::unavailable: return VenueQueryStatus::unavailable;
    }
    return VenueQueryStatus::unavailable;
}

template <typename Array, typename Enum>
std::uint64_t& at(Array& array, Enum value) noexcept {
    return array[static_cast<std::size_t>(value)];
}

}  // namespace

void GatewayStageHistogram::observe(double ms) noexcept {
    for (std::size_t i = 0; i < kGatewayStageBucketsMs.size(); ++i)
        if (ms <= kGatewayStageBucketsMs[i]) ++buckets[i];
    ++count;
    sum_ms += ms;
}

GatewayLoop::GatewayLoop(GatewayLoopConfig config, GatewayLink& link, const VerifiedExecutionCatalog& catalog,
                         sim::VenueTransport venue, GatewayClock clock)
    : config_(config), link_(link), catalog_(catalog), venue_(venue), clock_(clock),
      evidence_(std::make_unique<GatewayEvidenceState>()), quota_(config.quota_per_second),
      next_sequence_(config.first_sequence) {
    if (config_.caller.value() == 0 || is_operator_caller(config_.caller))
        throw std::invalid_argument("the gateway caller must be a non-operator CallerId");
    if (config_.account.value() == 0 || config_.instrument.value() == 0 ||
        config_.instrument_spec_version.value() == 0 || config_.venue.venue_id.value() == 0)
        throw std::invalid_argument("the gateway loop needs the catalog row it serves");
    if (config_.first_sequence == 0 || config_.sequence_limit < config_.first_sequence)
        throw std::invalid_argument("the gateway needs a reserved request-sequence range");
    if (!clock_.monotonic_ms || !clock_.wall_seconds || !venue_.send_fn || !venue_.poll_fn ||
        !venue_.acknowledge_fn || !venue_.query_fn || !venue_.session_fn)
        throw std::invalid_argument("the gateway loop needs a clock and a complete venue transport");
}

GatewayLoop::~GatewayLoop() = default;

bool GatewayLoop::new_risk_stopped() const noexcept { return !authority_ || authority_->new_risk_stopped(); }
bool GatewayLoop::all_transport_stopped() const noexcept { return !authority_ || authority_->all_transport_stopped(); }
void GatewayLoop::extend_sequence_limit(std::uint64_t limit) noexcept {
    config_.sequence_limit = std::max(config_.sequence_limit, limit);
}

bool GatewayLoop::serves(const BoundOrderEffect& binding) const noexcept {
    return binding.effect.venue_id == config_.venue.venue_id &&
           binding.venue_capabilities_version == config_.venue.capabilities_version &&
           binding.effect.account_id == config_.account && binding.effect.instrument_id == config_.instrument &&
           binding.instrument_spec_version == config_.instrument_spec_version;
}

void GatewayLoop::stop_new_risk() noexcept {
    if (authority_) authority_->stop_new_risk();
}

std::optional<RequestSequence> GatewayLoop::take_sequence() noexcept {
    if (next_sequence_ >= config_.sequence_limit) {
        stop_new_risk();
        return std::nullopt;
    }
    return RequestSequence{next_sequence_++};
}

GatewayLoop::Marks* GatewayLoop::marks(EffectId effect, bool create) noexcept {
    Marks* free = nullptr;
    for (auto& mark : marks_) {
        if (mark.effect_id == effect) return &mark;
        if (!free && mark.effect_id.value() == 0) free = &mark;
    }
    if (!create) return nullptr;
    if (!free) {  // bounded: the oldest diagnostic mark gives way
        std::rotate(marks_.begin(), marks_.begin() + 1, marks_.end());
        free = &marks_.back();
    }
    *free = {effect};
    return free;
}

VenueObservation GatewayLoop::synthetic(VenueObservationKind kind, ClusterOrderId order, EffectId effect,
                                        RequestSequence sequence) const {
    VenueObservation observation;
    observation.kind = kind;
    observation.venue_id = config_.venue.venue_id;
    observation.venue_session_id = venue_.session();
    // Gateway-authored identity: the command's own reserved sequence, which no
    // other gateway command of any process ever reuses.
    observation.venue_sequence = VenueSequence{sequence.value()};
    observation.cluster_order_id = order;
    observation.effect_id = effect;
    std::vector<std::byte> tagged;
    append(tagged, "velocita.v2.gateway-synthetic.1");
    append_le(tagged, static_cast<std::uint8_t>(kind));
    append_le(tagged, observation.venue_session_id.value());
    append_le(tagged, sequence.value());
    append_le(tagged, order.value());
    append_le(tagged, effect.value());
    observation.evidence_digest = sha256_digest(tagged);
    return observation;
}

bool GatewayLoop::record(const VenueObservation& observation, Tick now) {
    std::optional<RequestSequence> sequence;
    if (observation_origin(observation.kind) == ObservationOrigin::synthetic)
        sequence = RequestSequence{observation.venue_sequence.value()};
    else
        sequence = take_sequence();
    if (!sequence) return false;
    // The reducer requires an evidence command's digest to be the observation's
    // own fingerprint, so a copied digest can never hide changed evidence.
    const CommandIdentity identity{config_.caller, *sequence, observation_fingerprint(observation)};
    return record_evidence_obligation(*evidence_, identity, observation, observation.effect_id, now);
}

void GatewayLoop::step() {
    const auto now = clock_.monotonic_ms(clock_.context);
    metrics_.heartbeat_timestamp_seconds = clock_.wall_seconds(clock_.context);
    if (now >= quota_refilled_at_ + 1000) {
        quota_ = config_.quota_per_second;
        quota_refilled_at_ = now;
    }
    auto outcome = link_.pump(now);
    if (outcome.status != LinkStatus::pending && outcome.status != LinkStatus::idle) {
        const auto finished = in_flight_;
        in_flight_ = InFlight::none;
        switch (finished) {
        case InFlight::state: on_state(outcome, now); break;
        case InFlight::authority: on_authority(outcome, now); break;
        case InFlight::evidence:
        case InFlight::acknowledge: on_command(outcome, now); break;
        case InFlight::none: break;
        }
    }
    if (phase_ == GatewayPhase::running) poll_venue(now);
    if (!link_.busy() && phase_ != GatewayPhase::halted) choose(now);
    metrics_.new_risk_stopped = new_risk_stopped();
}

void GatewayLoop::on_state(const LinkOutcome& outcome, Tick now) {
    last_state_at_ = now;
    if (outcome.status != LinkStatus::done) {
        if (outcome.status == LinkStatus::refused && outcome.refusal_reason == 0) stop_new_risk();
        return;
    }
    const auto& state = link_.state();
    const auto& reading = outcome.reading;
    if (state.execution_catalog_root != catalog_.root() || reading.authority_domain.is_zero() ||
        (authority_ && reading.authority_domain != authority_domain_)) {
        // Not the cluster this process was configured for: send nothing more.
        if (authority_) authority_->stop_all_transport();
        phase_ = GatewayPhase::halted;
        return;
    }
    control_ = {state.executor_generation, state.kill_epoch, state.strategy_mode, state_requested_at_,
                config_.lease_bound, now};
    term_ = reading.term;
    if (!authority_) {
        authority_domain_ = reading.authority_domain;
        authority_ = std::make_unique<GatewayAuthority>(link_.incarnation(), authority_domain_,
                                                        config_.freshness_bound);
        authority_->observe_fence(control_, term_);
        const auto suspects = begin_gateway_recovery(delivery_, state, now);
        // Startup is the deliberate activation; a prior process may have sent
        // any unsettled effect, so their presence keeps new risk stopped.
        authority_->activate_after_reconciliation();
        if (suspects != 0 || !config_.allow_new_risk || config_.freshness_bound == 0) authority_->stop_new_risk();
        phase_ = GatewayPhase::running;
    } else {
        authority_->observe_fence(control_, term_);
    }
    if (delivery_.recovering && recovery_complete(delivery_, state)) delivery_.recovering = false;
    (void)reclaim_delivery_attempts(delivery_, state);
    (void)reclaim_reconciliation_attempts(reconciliation_, state);

    bool conflict = false;
    for (std::size_t i = 0; i < state.order_count; ++i)
        conflict |= state.orders[i].state == ClusterOrderState::conflict;
    for (std::size_t i = 0; i < state.outbox_effect_count; ++i)
        conflict |= state.outbox_effects[i].state == OutboxState::conflict;
    if (conflict && !authority_->new_risk_stopped()) ++at(metrics_.rejects, GatewayReject::reconciliation_conflict);
    if (conflict) stop_new_risk();

    metrics_.position_count = std::min(state.risk_account_count, metrics_.positions.size());
    for (std::size_t i = 0; i < metrics_.position_count; ++i)
        metrics_.positions[i] = {state.risk_accounts[i].account_id, state.risk_accounts[i].instrument_id,
                                 state.risk_accounts[i].position_quantity.atoms()};
    have_state_ = true;
    state_dirty_ = false;
}

void GatewayLoop::on_authority(LinkOutcome& outcome, Tick now) {
    state_dirty_ = true;
    if (outcome.status != LinkStatus::done || !outcome.proof) {
        authority_->abandon_request();
        ++at(metrics_.authority,
             outcome.status == LinkStatus::timed_out ? GatewayAuthorityResult::timed_out : GatewayAuthorityResult::refused);
        return;
    }
    if (!authority_->accept(*outcome.proof, now)) {
        authority_->abandon_request();
        ++at(metrics_.authority, GatewayAuthorityResult::rejected);
        return;
    }
    ++at(metrics_.authority, GatewayAuthorityResult::granted);
    if (auto* mark = marks(prepared_.effect.effect_id, true)) {
        if (mark->first_deliverable != 0 && now >= mark->first_deliverable)
            metrics_.stages[static_cast<std::size_t>(GatewayStage::authorized_to_proof)].observe(
                static_cast<double>(now - mark->first_deliverable));
        mark->proof_at = now;
    }
    handoff_outcome_.reset();
    const GatewayTransportBoundary boundary{&GatewayLoop::transport_clock, &GatewayLoop::transport_handoff, this};
    if (!dispatch_gateway_send(*authority_, link_.state(), prepared_, control_, term_, config_.venue, quota_,
                               delivery_, *evidence_, config_.timeouts, catalog_, boundary)) {
        ++at(metrics_.rejects, GatewayReject::dispatch_refused);
        return;
    }
    const auto sent_at = clock_.monotonic_ms(clock_.context);
    ++(prepared_.effect.side == Side::buy ? metrics_.orders_sent_buy : metrics_.orders_sent_sell);
    if (auto* mark = marks(prepared_.effect.effect_id, true)) {
        if (mark->proof_at != 0 && sent_at >= mark->proof_at)
            metrics_.stages[static_cast<std::size_t>(GatewayStage::proof_to_sent)].observe(
                static_cast<double>(sent_at - mark->proof_at));
        mark->sent_at = sent_at;
    }
    switch (handoff_outcome_.value_or(sim::SendOutcome::ambiguous)) {
    case sim::SendOutcome::accepted:
    case sim::SendOutcome::venue_duplicate:
        break;  // transport stays open until the venue's first observation
    case sim::SendOutcome::business_rejected:
        // Still open: a venue rejection observation closes it, silence becomes unknown.
        ++at(metrics_.rejects, GatewayReject::business_rejected);
        break;
    case sim::SendOutcome::pre_send_failure:
        (void)record_unsent(delivery_, prepared_.effect.effect_id);
        ++at(metrics_.rejects, GatewayReject::pre_send_failure);
        break;
    case sim::SendOutcome::rate_limited:
        (void)record_unsent(delivery_, prepared_.effect.effect_id);
        ++at(metrics_.rejects, GatewayReject::rate_limited);
        break;
    case sim::SendOutcome::ambiguous:
        stop_new_risk();  // halt on unknown; the deadline turns it into effect_unknown
        break;
    }
}

void GatewayLoop::on_command(const LinkOutcome& outcome, Tick now) {
    if (in_flight_identity_.request_sequence.value() == in_flight_ack_ && in_flight_ack_ != 0) {
        if (outcome.status == LinkStatus::done && accepted_code(outcome.result.code))
            acknowledged_through_ = std::max(acknowledged_through_, in_flight_ack_through_);
        in_flight_ack_ = 0;
        return;
    }
    const auto* obligation = find_obligation(*evidence_, in_flight_identity_);
    if (!obligation) return;
    if (outcome.status == LinkStatus::refused) {
        stop_new_risk();  // a final refusal of evidence is a configuration fault
        return;
    }
    if (outcome.status != LinkStatus::done) return;  // plan_evidence retries the same identity
    const auto observation = obligation->observation;
    (void)discharge_evidence(*evidence_, in_flight_identity_);
    state_dirty_ = true;
    if (!accepted_code(outcome.result.code)) {
        // Evidence the reducer refused stays owned by the venue (not acknowledged).
        ++at(metrics_.rejects, GatewayReject::kernel_refused);
        stop_new_risk();
        return;
    }
    venue_.acknowledge(observation);
    if (observation.kind != VenueObservationKind::fill) return;
    ++(observation.side == Side::buy ? metrics_.fills_buy : metrics_.fills_sell);
    metrics_.fee_micros_total += observation.fee.atoms();
    metrics_.last_fill_timestamp_seconds = clock_.wall_seconds(clock_.context);
    for (auto& mark : fill_marks_) {
        if (mark.sequence != in_flight_identity_.request_sequence) continue;
        if (now >= mark.polled_at)
            metrics_.stages[static_cast<std::size_t>(GatewayStage::fill_to_committed)].observe(
                static_cast<double>(now - mark.polled_at));
        mark = {};
    }
}

void GatewayLoop::poll_venue(Tick now) {
    const auto free = evidence_->obligations.size() - evidence_->obligation_count;
    const auto sequences = config_.sequence_limit - next_sequence_;
    const auto room = std::min({free, static_cast<std::size_t>(sim::kMaxVenueObservationsPerPoll),
                                static_cast<std::size_t>(std::min<std::uint64_t>(sequences, 64))});
    if (room == 0) return;  // the venue keeps what we cannot yet own
    std::array<VenueObservation, sim::kMaxVenueObservationsPerPoll> polled{};
    const auto count = venue_.poll(std::span(polled).first(room), now);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& observation = polled[i];
        const auto fingerprint = observation_fingerprint(observation);
        bool duplicate = false;
        for (std::size_t j = 0; j < evidence_->obligation_count; ++j)
            duplicate |= observation_fingerprint(evidence_->obligations[j].observation) == fingerprint;
        if (duplicate) continue;  // one obligation; acknowledging it releases every copy
        const auto sequence = next_sequence_;
        if (!record(observation, now)) {
            stop_new_risk();
            continue;
        }
        (void)record_response(delivery_, observation.effect_id);
        if (observation.kind == VenueObservationKind::order_rejected) ++at(metrics_.rejects, GatewayReject::venue_rejected);
        if (observation.kind != VenueObservationKind::fill) continue;
        if (auto* mark = marks(observation.effect_id, false); mark && !mark->fill_seen && mark->sent_at != 0) {
            mark->fill_seen = true;
            if (now >= mark->sent_at)
                metrics_.stages[static_cast<std::size_t>(GatewayStage::sent_to_fill)].observe(
                    static_cast<double>(now - mark->sent_at));
        }
        for (auto& mark : fill_marks_) {
            if (mark.sequence.value() != 0) continue;
            mark = {RequestSequence{sequence}, now};
            break;
        }
    }
}

void GatewayLoop::choose(Tick now) {
    if (phase_ == GatewayPhase::starting || state_dirty_ || now >= last_state_at_ + config_.state_interval) {
        if (phase_ != GatewayPhase::starting && propose_evidence(now)) return;
        if (phase_ == GatewayPhase::starting && now < last_state_at_ + config_.state_interval && last_state_at_ != 0)
            return;
        if (link_.request_state(now, config_.request_timeout)) {
            state_requested_at_ = now;
            in_flight_ = InFlight::state;
        }
        return;
    }
    if (propose_evidence(now)) return;

    std::uint64_t through = next_sequence_ - 1;
    for (std::size_t i = 0; i < evidence_->obligation_count; ++i)
        through = std::min(through, evidence_->obligations[i].identity.request_sequence.value() - 1);
    if (through >= config_.first_sequence && through > acknowledged_through_ && now >= last_ack_at_ + config_.ack_interval) {
        if (const auto sequence = take_sequence()) {
            const auto command = identified(config_.caller, *sequence, AcknowledgeGatewayResults{RequestSequence{through}});
            last_ack_at_ = now;
            if (link_.submit(command, ProposeLane::normal, now, config_.request_timeout)) {
                in_flight_ = InFlight::acknowledge;
                in_flight_identity_ = command.identity;
                in_flight_ack_ = sequence->value();
                in_flight_ack_through_ = through;
                return;
            }
        }
    }
    if (deliver(now)) return;
    (void)reconcile(now);
}

bool GatewayLoop::propose_evidence(Tick now) {
    const auto plan = plan_evidence(*evidence_, config_.timeouts, now);
    if (plan.proposal_count == 0) return false;
    const auto* obligation = find_obligation(*evidence_, plan.to_propose[0]);
    if (!obligation) return false;
    const Command command{obligation->identity, evidence_body(obligation->observation)};
    if (!link_.submit(command, ProposeLane::normal, now, config_.request_timeout)) return false;
    (void)record_proposal(*evidence_, command.identity, now);
    in_flight_ = InFlight::evidence;
    in_flight_identity_ = command.identity;
    return true;
}

bool GatewayLoop::deliver(Tick now) {
    if (!have_state_ || !authority_) return false;
    const auto& state = link_.state();
    control_.now = now;
    const auto plan = plan_delivery(state, delivery_, *evidence_, control_, config_.venue, quota_, config_.timeouts,
                                    now, &catalog_);
    for (std::size_t i = 0; i < plan.ambiguous_count; ++i) {
        const auto effect = plan.now_ambiguous[i];
        const auto* record_of = outbox(state, effect);
        // Only an effect the cluster still holds as unanswered can be declared
        // unknown; one it already settled or marked is merely closed here.
        if (record_of && record_of->state == OutboxState::pending) {
            const auto sequence = take_sequence();
            if (!sequence) return false;
            if (!record(synthetic(VenueObservationKind::effect_unknown, record_of->cluster_order_id, effect,
                                  *sequence), now))
                return false;
            ++metrics_.unknown_outcomes;
        }
        (void)record_response(delivery_, effect);
        stop_new_risk();
    }
    if (plan.ambiguous_count != 0) return false;

    for (std::size_t i = 0; i < plan.send_count; ++i) {
        const auto effect = plan.to_send[i];
        const auto binding = materialize_bound_order_effect(state, effect, catalog_);
        if (!binding || !serves(*binding)) continue;
        if (auto* mark = marks(effect, true); mark->first_deliverable == 0) mark->first_deliverable = now;
        if (binding->effect.operation == EffectOperation::cancel) {
            // Committed-cancel exception: no proof, still every fence and quota.
            handoff_outcome_.reset();
            const GatewayTransportBoundary boundary{&GatewayLoop::transport_clock, &GatewayLoop::transport_handoff,
                                                    this};
            if (!dispatch_gateway_send(*authority_, state, *binding, control_, term_, config_.venue, quota_,
                                       delivery_, *evidence_, config_.timeouts, catalog_, boundary)) {
                ++at(metrics_.rejects, GatewayReject::dispatch_refused);
                continue;
            }
            ++metrics_.orders_sent_cancel;
            const auto outcome = handoff_outcome_.value_or(sim::SendOutcome::ambiguous);
            if (outcome == sim::SendOutcome::pre_send_failure || outcome == sim::SendOutcome::rate_limited)
                (void)record_unsent(delivery_, effect);
            state_dirty_ = true;
            return true;
        }
        if (authority_->new_risk_stopped()) continue;
        const auto barrier = authority_->begin_request(*binding, control_, now);
        if (!barrier) continue;
        if (!link_.request_authority(*barrier, now, std::min(config_.request_timeout, config_.freshness_bound))) {
            authority_->abandon_request();
            continue;
        }
        prepared_ = *binding;
        in_flight_ = InFlight::authority;
        return true;
    }
    return false;
}

bool GatewayLoop::reconcile(Tick now) {
    if (!have_state_ || evidence_queue_saturated(*evidence_)) return false;
    const auto& state = link_.state();
    const auto plan = plan_reconciliation(state, reconciliation_, delivery_, control_, config_.venue,
                                          config_.timeouts, now);
    // Only this venue can answer for an order; another venue's "not found"
    // would release a reservation that is real somewhere else.
    std::optional<ReconciliationTarget> chosen;
    for (std::size_t i = 0; i < plan.query_count && !chosen; ++i) {
        const auto binding = materialize_bound_order_effect(state, plan.to_query[i].effect_id, catalog_);
        if (binding && serves(*binding)) chosen = plan.to_query[i];
    }
    if (!chosen) return false;
    const auto target = *chosen;
    const auto watermark = accepted_evidence_watermark(state, target.cluster_order_id);
    if (!record_query(reconciliation_, target.cluster_order_id, now, watermark)) return false;
    const auto answer = venue_.query(target.cluster_order_id, now);
    (void)record_answer(reconciliation_, target.cluster_order_id);
    const VenueQueryView view{status_of(answer.outcome), venue_.session(), answer.venue_order_id,
                              answer.filled_quantity, answer.remaining_quantity, answer.cancelled,
                              answer.window_complete, answer.ordered_after_relevant_sends};
    const auto disposition = classify_query(view, find_reconciliation(reconciliation_, target.cluster_order_id),
                                            watermark, config_.timeouts);
    if (answer.inconsistent) {
        ++at(metrics_.rejects, GatewayReject::reconciliation_conflict);
        stop_new_risk();
        return true;
    }
    switch (disposition) {
    case QueryDisposition::submit: {
        const auto sequence = take_sequence();
        if (!sequence) return true;
        const bool known = answer.outcome == sim::QueryOutcome::known;
        const auto kind = target.need == ReconciliationNeed::cancel_unconfirmed && known && answer.cancelled
                              ? VenueObservationKind::cancel_reconciled
                              : VenueObservationKind::order_reconciled;
        auto observation = synthetic(kind, target.cluster_order_id, target.effect_id, *sequence);
        observation.venue_order_id = known ? answer.venue_order_id : VenueOrderId{};
        observation.quantity = known ? answer.filled_quantity : QuantityLots{};
        if (!record(observation, now)) stop_new_risk();
        return true;
    }
    case QueryDisposition::exhausted:
        ++at(metrics_.rejects, GatewayReject::reconciliation_exhausted);
        stop_new_risk();
        return true;
    case QueryDisposition::incomplete:
    case QueryDisposition::unavailable:
        return true;
    }
    return true;
}

Tick GatewayLoop::transport_clock(void* self) noexcept {
    auto& loop = *static_cast<GatewayLoop*>(self);
    return loop.clock_.monotonic_ms(loop.clock_.context);
}

void GatewayLoop::transport_handoff(BoundOrderEffect binding, void* self) {
    auto& loop = *static_cast<GatewayLoop*>(self);
    try {
        loop.handoff_outcome_ = loop.venue_.send(binding.effect, loop.clock_.monotonic_ms(loop.clock_.context));
    } catch (...) {
        // Bytes may have left: possibly sent, never proof of unsent.
        loop.handoff_outcome_ = sim::SendOutcome::ambiguous;
    }
}

std::string render_gateway_metrics(const GatewayMetrics& m) {
    std::ostringstream out;
    const auto type = [&](const char* name, const char* kind, const char* help) {
        out << "# HELP " << name << ' ' << help << "\n# TYPE " << name << ' ' << kind << '\n';
    };
    const auto number = [](double value) {
        std::array<char, 40> text{};
        std::snprintf(text.data(), text.size(), "%.6f", value);
        return std::string(text.data());
    };
    type("velocita_gateway_up", "gauge", "1 while the gateway process publishes metrics.");
    out << "velocita_gateway_up 1\n";
    type("velocita_gateway_heartbeat_timestamp_seconds", "gauge", "Wall-clock time of the last loop iteration.");
    out << "velocita_gateway_heartbeat_timestamp_seconds " << number(m.heartbeat_timestamp_seconds) << '\n';
    type("velocita_gateway_new_risk_stopped", "gauge", "1 when the gateway will not send risk-increasing effects.");
    out << "velocita_gateway_new_risk_stopped " << (m.new_risk_stopped ? 1 : 0) << '\n';
    type("velocita_gateway_orders_sent_total", "counter", "Effects handed to the venue, by side (cancel for cancels).");
    out << "velocita_gateway_orders_sent_total{side=\"buy\"} " << m.orders_sent_buy << '\n'
        << "velocita_gateway_orders_sent_total{side=\"sell\"} " << m.orders_sent_sell << '\n'
        << "velocita_gateway_orders_sent_total{side=\"cancel\"} " << m.orders_sent_cancel << '\n';
    type("velocita_gateway_fills_total", "counter", "Fills committed to the cluster as ObserveFill, by side.");
    out << "velocita_gateway_fills_total{side=\"buy\"} " << m.fills_buy << '\n'
        << "velocita_gateway_fills_total{side=\"sell\"} " << m.fills_sell << '\n';
    static constexpr std::array<const char*, static_cast<std::size_t>(GatewayReject::count)> kReasons{
        "business_rejected", "pre_send_failure", "rate_limited", "dispatch_refused", "venue_rejected",
        "kernel_refused", "reconciliation_conflict", "reconciliation_exhausted"};
    type("velocita_gateway_rejects_total", "counter", "Refused sends, venue rejections and refused evidence, by reason.");
    for (std::size_t i = 0; i < kReasons.size(); ++i)
        out << "velocita_gateway_rejects_total{reason=\"" << kReasons[i] << "\"} " << m.rejects[i] << '\n';
    type("velocita_gateway_unknown_outcomes_total", "counter", "Sends declared effect_unknown to the cluster.");
    out << "velocita_gateway_unknown_outcomes_total " << m.unknown_outcomes << '\n';
    static constexpr std::array<const char*, static_cast<std::size_t>(GatewayAuthorityResult::count)> kResults{
        "granted", "refused", "timed_out", "rejected"};
    type("velocita_gateway_authority_requests_total", "counter", "Authority barrier requests, by result.");
    for (std::size_t i = 0; i < kResults.size(); ++i)
        out << "velocita_gateway_authority_requests_total{result=\"" << kResults[i] << "\"} " << m.authority[i] << '\n';
    static constexpr std::array<const char*, static_cast<std::size_t>(GatewayStage::count)> kStages{
        "authorized_to_proof", "proof_to_sent", "sent_to_fill", "fill_to_committed"};
    type("velocita_gateway_stage_ms", "histogram", "Gateway-clock milliseconds per delivery stage.");
    for (std::size_t s = 0; s < kStages.size(); ++s) {
        const auto& h = m.stages[s];
        for (std::size_t b = 0; b < kGatewayStageBucketsMs.size(); ++b)
            out << "velocita_gateway_stage_ms_bucket{stage=\"" << kStages[s] << "\",le=\""
                << kGatewayStageBucketsMs[b] << "\"} " << h.buckets[b] << '\n';
        out << "velocita_gateway_stage_ms_bucket{stage=\"" << kStages[s] << "\",le=\"+Inf\"} " << h.count << '\n'
            << "velocita_gateway_stage_ms_sum{stage=\"" << kStages[s] << "\"} " << number(h.sum_ms) << '\n'
            << "velocita_gateway_stage_ms_count{stage=\"" << kStages[s] << "\"} " << h.count << '\n';
    }
    type("velocita_gateway_position_lots", "gauge", "Position in lots from the committed kernel state last read.");
    for (std::size_t i = 0; i < m.position_count; ++i)
        out << "velocita_gateway_position_lots{account=\"" << m.positions[i].account_id.value() << "\",instrument=\""
            << m.positions[i].instrument_id.value() << "\"} " << m.positions[i].lots << '\n';
    type("velocita_gateway_fees_usd_total", "counter", "Venue-reported fees on committed fills (fee micros / 1e6).");
    out << "velocita_gateway_fees_usd_total " << number(static_cast<double>(m.fee_micros_total) / 1e6) << '\n';
    type("velocita_gateway_last_fill_timestamp_seconds", "gauge", "Wall-clock time the last fill committed (0: none).");
    out << "velocita_gateway_last_fill_timestamp_seconds " << number(m.last_fill_timestamp_seconds) << '\n';
    return out.str();
}

}  // namespace velocita::v2
