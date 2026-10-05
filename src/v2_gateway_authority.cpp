#include "velocita/v2/gateway_authority.hpp"

namespace velocita::v2 {
namespace {

bool nonzero(const std::array<std::byte, 16>& value) noexcept {
    for (const auto byte : value) {
        if (byte != std::byte{0}) return true;
    }
    return false;
}

bool same_binding(const BoundOrderEffect& a,
                  const BoundOrderEffect& b) noexcept {
    return a == b;
}

bool valid_mode(const StrategyMode mode) noexcept {
    return mode >= StrategyMode::normal && mode <= StrategyMode::halted;
}

bool valid_control(const GatewayControlView& control) noexcept {
    return control.executor_generation.value() != 0 &&
           control.kill_epoch.value() != 0 && valid_mode(control.strategy_mode);
}

}

GatewayAuthority::GatewayAuthority(std::array<std::byte, 16> incarnation,
                                   Digest256 authority_domain,
                                   const Tick freshness_bound) noexcept
    : incarnation_(incarnation),
      authority_domain_(authority_domain),
      freshness_bound_(freshness_bound) {}

bool GatewayAuthority::observe_time(const Tick now) noexcept {
    if (have_time_ && now < last_now_) {
        drop_permission();
        stop_new_risk_ = true;
        return false;
    }
    last_now_ = now;
    have_time_ = true;
    return true;
}

void GatewayAuthority::drop_permission() noexcept {
    pending_.reset();
    accepted_.reset();
    accepted_started_at_ = 0;
}

std::optional<AuthorityBarrier> GatewayAuthority::begin_request(
    const BoundOrderEffect& binding, const GatewayControlView& control,
    const Tick now) noexcept {
    if (!observe_time(now) || stop_new_risk_ || stop_all_transport_ ||
        freshness_bound_ == 0 || !nonzero(incarnation_) ||
        authority_domain_.is_zero() || pending_ || accepted_ ||
        !detail::next_authority_sequence(next_sequence_) ||
        !valid_control(control) || control.strategy_mode != StrategyMode::normal ||
        binding.effect.effect_id.value() == 0 ||
        binding.effect.payload_digest.is_zero() || binding.catalog_root.is_zero() ||
        binding.instrument_spec_version.value() == 0 ||
        binding.venue_capabilities_version.value() == 0 ||
        binding.effect.executor_generation != control.executor_generation ||
        binding.effect.kill_epoch != control.kill_epoch) {
        return std::nullopt;
    }

    if (have_control_ &&
        (control.executor_generation.value() < generation_highwater_.value() ||
         control.kill_epoch.value() < kill_highwater_.value())) {
        stop_new_risk_ = true;
        drop_permission();
        return std::nullopt;
    }

    next_sequence_ = *detail::next_authority_sequence(next_sequence_);
    AuthorityBarrier request{incarnation_, next_sequence_, binding.effect.effect_id,
                             binding.effect.payload_digest,
                             control.kill_epoch, control.executor_generation,
                             binding.catalog_root, authority_domain_};
    pending_ = Pending{request, binding, control, now};
    generation_highwater_ = control.executor_generation;
    kill_highwater_ = control.kill_epoch;
    observed_mode_ = control.strategy_mode;
    have_control_ = true;
    return request;
}

bool GatewayAuthority::accept(const AuthorityProof& proof,
                              const Tick now) noexcept {
    if (!observe_time(now) || !pending_ || accepted_ || stop_new_risk_ ||
        stop_all_transport_) {
        return false;
    }
    const Pending expected = *pending_;
    const auto& value = proof.view();
    const bool fresh = now >= expected.started_at &&
                       now - expected.started_at <= freshness_bound_;
    const bool coordinates = value.issuer_id > 0 && value.term > 0 &&
                             value.barrier_index > 0 &&
                             value.applied_index >= value.barrier_index &&
                             value.state_version > 0 &&
                             value.term >= term_highwater_ &&
                             value.barrier_index >= index_highwater_ &&
                             value.applied_index >= applied_highwater_;
    const bool controls = value.request.expected_executor_generation ==
                              expected.control.executor_generation &&
                          value.request.expected_kill_epoch ==
                              expected.control.kill_epoch &&
                          value.binding.effect.executor_generation ==
                              expected.control.executor_generation &&
                          value.binding.effect.kill_epoch == expected.control.kill_epoch &&
                          value.strategy_mode == expected.control.strategy_mode;
    if (!fresh || !coordinates || !controls ||
        value.request != expected.request ||
        !same_binding(value.binding, expected.binding)) {
        pending_.reset();
        return false;
    }

    term_highwater_ = value.term;
    index_highwater_ = value.barrier_index;
    applied_highwater_ = value.applied_index;
    accepted_ = value;
    accepted_started_at_ = expected.started_at;
    pending_.reset();
    return true;
}

bool GatewayAuthority::consume(const BoundOrderEffect& binding,
                               const GatewayControlView& current,
                               const Tick now) noexcept {
    if (!observe_time(now) || !accepted_ || stop_new_risk_ ||
        stop_all_transport_) {
        return false;
    }
    const AuthorityProofView permission = *accepted_;
    const Tick started_at = accepted_started_at_;
    accepted_.reset();
    accepted_started_at_ = 0;

    if (now < started_at || now - started_at > freshness_bound_ ||
        !valid_control(current) || current.strategy_mode != StrategyMode::normal ||
        current.executor_generation != permission.request.expected_executor_generation ||
        current.kill_epoch != permission.request.expected_kill_epoch ||
        current.strategy_mode != permission.strategy_mode ||
        !same_binding(binding, permission.binding)) {
        return false;
    }
    return true;
}

bool GatewayAuthority::permits_committed_cancel(
    const BoundOrderEffect& binding,
    const GatewayControlView& current) const noexcept {
    return !stop_all_transport_ && valid_control(current) &&
        binding.effect.operation == EffectOperation::cancel &&
        binding.effect.effect_id.value() != 0 &&
        !binding.effect.payload_digest.is_zero() && !binding.catalog_root.is_zero() &&
        binding.instrument_spec_version.value() != 0 &&
        binding.venue_capabilities_version.value() != 0 &&
        binding.effect.executor_generation == current.executor_generation &&
        binding.effect.kill_epoch == current.kill_epoch &&
        current.executor_generation.value() >= generation_highwater_.value() &&
        current.kill_epoch.value() >= kill_highwater_.value();
}

void GatewayAuthority::observe_fence(const GatewayControlView& control,
                                     const std::int64_t term) noexcept {
    bool invalidate = false;
    if (term <= 0 || (term_highwater_ > 0 && term < term_highwater_)) {
        invalidate = true;
    } else if (term > term_highwater_) {
        invalidate = term_highwater_ != 0 || pending_.has_value() ||
            accepted_.has_value();
        term_highwater_ = term;
    }
    if (!valid_control(control)) {
        invalidate = true;
    } else if (!have_control_) {
        generation_highwater_ = control.executor_generation;
        kill_highwater_ = control.kill_epoch;
        observed_mode_ = control.strategy_mode;
        have_control_ = true;
    } else {
        if (control.executor_generation != generation_highwater_ ||
            control.kill_epoch != kill_highwater_ ||
            control.strategy_mode != observed_mode_) {
            invalidate = true;
        }
        if (control.executor_generation.value() >= generation_highwater_.value())
            generation_highwater_ = control.executor_generation;
        if (control.kill_epoch.value() >= kill_highwater_.value())
            kill_highwater_ = control.kill_epoch;
        observed_mode_ = control.strategy_mode;
    }
    if (invalidate) {
        drop_permission();
        stop_new_risk_ = true;
    }
}

void GatewayAuthority::abandon_request() noexcept { drop_permission(); }

void GatewayAuthority::stop_new_risk() noexcept {
    stop_new_risk_ = true;
    drop_permission();
}

void GatewayAuthority::stop_all_transport() noexcept {
    stop_all_transport_ = true;
    stop_new_risk_ = true;
    drop_permission();
}

void GatewayAuthority::activate_after_reconciliation() noexcept {
    drop_permission();
    stop_all_transport_ = false;
    stop_new_risk_ = false;
}

}
