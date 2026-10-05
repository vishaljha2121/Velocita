#include "v2_catalog_test_support.hpp"
#include "velocita/v2/kernel.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include "velocita/v2/kernel_codec.hpp"

#include <array>
#include <cstring>
#include <span>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::byte first) {
    velocita::v2::Digest256 result;
    result.bytes[0] = first;
    return result;
}

velocita::v2::Command command(const std::uint64_t request_sequence,
                              const std::byte payload_marker,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{9},
                                   RequestSequence{request_sequence},
                                   digest(payload_marker)},
                   std::move(body)};
}

void test_resuming_normal_requires_reconciliation() {
    using namespace velocita::v2;
    const auto with_order_in = [](const ClusterOrderState order_state) {
        TradingState state = catalog_test::state();
        state.strategy_mode = StrategyMode::halted;
        state.order_count = 1;
        state.orders[0].state = order_state;
        return state;
    };

    const auto resume = [](TradingState& state) {
        ApplyScratch scratch;
        return catalog_test::apply(state,
                     Command{CommandIdentity{CallerId{9}, RequestSequence{1},
                                             digest(std::byte{0x31})},
                             SetStrategyMode{StrategyMode::normal, state.kill_epoch,
                                             state.executor_generation}},
                     scratch);
    };

    for (const auto blocked : {ClusterOrderState::submission_unknown,
                               ClusterOrderState::reconciling,
                               ClusterOrderState::conflict}) {
        auto state = with_order_in(blocked);
        const auto outcome = resume(state);
        require(outcome.code == ApplyCode::reconciliation_required,
                "normal trading resumed with an order in state " +
                    std::to_string(static_cast<int>(blocked)));
        require(state.strategy_mode == StrategyMode::halted,
                "a refused resume still changed the strategy mode");
    }

    for (const auto allowed : {ClusterOrderState::live,
                               ClusterOrderState::partially_filled,
                               ClusterOrderState::filled,
                               ClusterOrderState::cancelled}) {
        auto state = with_order_in(allowed);
        require(resume(state).code == ApplyCode::applied,
                "a known order state blocked a resume: " +
                    std::to_string(static_cast<int>(allowed)));
        require(state.strategy_mode == StrategyMode::normal,
                "the accepted resume did not take");
    }

    for (const auto safer : {StrategyMode::halted, StrategyMode::no_new_risk,
                             StrategyMode::cancel_only, StrategyMode::reduce_only}) {
        auto state = with_order_in(ClusterOrderState::conflict);
        state.strategy_mode = StrategyMode::normal;
        ApplyScratch scratch;
        const auto outcome = catalog_test::apply(
            state,
            Command{CommandIdentity{CallerId{9}, RequestSequence{1},
                                    digest(std::byte{0x32})},
                    SetStrategyMode{safer, state.kill_epoch,
                                    state.executor_generation}},
            scratch);
        require(outcome.code == ApplyCode::applied,
                "an unreconciled cluster could not reduce its own authority to " +
                    std::to_string(static_cast<int>(safer)));
    }

    {
        auto state = with_order_in(ClusterOrderState::conflict);
        state.strategy_mode = StrategyMode::normal;
        ApplyScratch scratch;
        const auto outcome =
            catalog_test::apply(state,
                  Command{CommandIdentity{CallerId{9}, RequestSequence{1},
                                          digest(std::byte{0x33})},
                          AdvanceKillEpoch{KillEpoch{1}}},
                  scratch);
        require(outcome.code == ApplyCode::applied,
                "an unreconciled cluster could not be killed");
        require(state.strategy_mode == StrategyMode::halted,
                "the kill did not halt the cluster");
    }
}

void test_control_authority_is_reserved_to_operator_callers() {
    using namespace velocita::v2;
    const auto control_from = [](const std::uint64_t caller,
                                 const std::uint64_t sequence,
                                 CommandBody body) {
        return Command{CommandIdentity{CallerId{caller}, RequestSequence{sequence},
                                       digest(std::byte{0x77})},
                       std::move(body)};
    };

    require(kMaxOperatorCallerId > 0,
            "the operator caller band is empty, so nothing can ever issue control");

    const ConfigureRiskLimits limits{AccountId{5}, InstrumentId{4},
                                     RiskPolicyVersion{6}, RiskVersion{0},
                                     KillEpoch{}, ExecutorGeneration{},
                                     QuantityLots::from_atoms(4),
                                     QuantityLots::from_atoms(8),
                                     MoneyMicros::from_atoms(4'000),
                                     MoneyMicros::from_atoms(8'000),
                                     1'000'000, 2'500, QuantityLots::from_atoms(4),
                                     false, 1};
    for (const auto& body : std::initializer_list<CommandBody>{
             AdvanceKillEpoch{KillEpoch{1}},
             SetExecutorGeneration{ExecutorGeneration{1}},
             SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}},
             limits}) {
        TradingState state = catalog_test::state();
        ApplyScratch scratch;
        const auto unauthorized =
            catalog_test::apply(state, control_from(kMaxOperatorCallerId + 1, 1, body), scratch);
        require(unauthorized.code == ApplyCode::unauthorized_control,
                "a control command from a non-operator caller was not refused");

        TradingState untouched = catalog_test::state();
        require(std::memcmp(&state, &untouched, sizeof(TradingState)) == 0,
                "a refused control command still mutated authoritative state");
    }

    {
        TradingState state = catalog_test::state();
        ApplyScratch scratch;
        const auto allowed = catalog_test::apply(
            state, control_from(kMaxOperatorCallerId, 1, AdvanceKillEpoch{KillEpoch{1}}),
            scratch);
        require(allowed.code == ApplyCode::applied,
                "a control command from an operator caller was refused");
        require(state.kill_epoch.value() == 1, "the accepted kill epoch did not take");
    }
    {
        TradingState state = catalog_test::state();
        ApplyScratch scratch;
        const auto allowed = catalog_test::apply(
            state, control_from(kMaxOperatorCallerId, 1, limits), scratch);
        require(allowed.code == ApplyCode::applied && state.risk_account_count == 1 &&
                    state.risk_accounts[0].max_position_quantity == limits.max_position_quantity,
                "risk limits from an operator caller were refused");
    }

    {
        TradingState state = catalog_test::state();
        ApplyScratch scratch;
        const auto refused = catalog_test::apply(
            state,
            control_from(kMaxOperatorCallerId + 1, 1, AdvanceKillEpoch{KillEpoch{1}}),
            scratch);
        require(refused.code == ApplyCode::unauthorized_control, "fixture drifted");

        std::array<std::byte, kKernelResultBytes> bytes{};
        const auto encoded = encode_kernel_result(refused, bytes);
        require(encoded.error == KernelCodecError::none,
                "an unauthorized-control result could not be encoded");
        ApplyResult decoded{};
        const auto read = decode_kernel_result(
            std::span<const std::byte>(bytes).first(encoded.bytes_written), decoded);
        require(read.error == KernelCodecError::none,
                "an unauthorized-control result could not be decoded back");
        require(decoded.code == ApplyCode::unauthorized_control,
                "the refusal did not survive a canonical round trip");
    }

    {
        TradingState state = catalog_test::state();
        ApplyScratch scratch;
        const auto ordinary = catalog_test::apply(
            state,
            control_from(kMaxOperatorCallerId + 1, 1,
                         RecordSourceEvent{SourceId{12}, SourceGeneration{1},
                                           SourceSequence{1}, digest(std::byte{0x44})}),
            scratch);
        require(ordinary.code == ApplyCode::applied && state.source_count == 1,
                "the operator reservation leaked onto an ordinary command");
    }
}

void test_boot_and_kill_epoch_are_fail_safe() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    require(state.strategy_mode == StrategyMode::halted && state.kill_epoch.value() == 0 &&
                state.executor_generation.value() == 0,
            "trading authority did not boot fail-safe");

    const auto normal = command(
        1,
        std::byte{0x11},
        SetStrategyMode{StrategyMode::normal, KillEpoch{0}, ExecutorGeneration{0}});
    const auto normal_result = catalog_test::apply(state, normal, scratch);
    require(normal_result.code == ApplyCode::applied &&
                normal_result.strategy_mode == StrategyMode::normal,
            "generation-matched mode transition was rejected");
    require(catalog_test::apply(state, normal, scratch) == normal_result && state.state_version == 1,
            "exact mode-command retry did not return its retained result");

    const auto killed = catalog_test::apply(
        state,
        command(2, std::byte{0x12}, AdvanceKillEpoch{KillEpoch{1}}),
        scratch);
    require(killed.code == ApplyCode::applied && killed.kill_epoch == KillEpoch{1} &&
                killed.strategy_mode == StrategyMode::halted &&
                state.strategy_mode == StrategyMode::halted,
            "kill-epoch advance did not atomically halt trading authority");

    const auto stale_resume = catalog_test::apply(
        state,
        command(3,
                std::byte{0x13},
                SetStrategyMode{StrategyMode::normal,
                                KillEpoch{0},
                                ExecutorGeneration{0}}),
        scratch);
    require(stale_resume.code == ApplyCode::stale_control_generation &&
                state.strategy_mode == StrategyMode::halted,
            "stale pre-kill command reopened trading authority");

    const auto matched_resume = catalog_test::apply(
        state,
        command(4,
                std::byte{0x14},
                SetStrategyMode{StrategyMode::no_new_risk,
                                KillEpoch{1},
                                ExecutorGeneration{0}}),
        scratch);
    require(matched_resume.code == ApplyCode::applied &&
                state.strategy_mode == StrategyMode::no_new_risk,
            "generation-matched post-kill safe mode was rejected");
}

void test_kill_epoch_never_regresses() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    require(catalog_test::apply(state,
                  command(1, std::byte{0x21}, AdvanceKillEpoch{KillEpoch{4}}),
                  scratch).code == ApplyCode::applied,
            "initial kill-epoch advance failed");
    const auto equal = catalog_test::apply(
        state,
        command(2, std::byte{0x22}, AdvanceKillEpoch{KillEpoch{4}}),
        scratch);
    const auto regressed = catalog_test::apply(
        state,
        command(3, std::byte{0x23}, AdvanceKillEpoch{KillEpoch{3}}),
        scratch);
    require(equal.code == ApplyCode::non_monotonic_kill_epoch &&
                regressed.code == ApplyCode::non_monotonic_kill_epoch &&
                state.kill_epoch == KillEpoch{4},
            "kill epoch repeated or regressed");
}

void test_executor_generation_advance_removes_new_risk_authority() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;

    require(catalog_test::apply(state,
                  command(1,
                          std::byte{0x31},
                          SetStrategyMode{StrategyMode::normal,
                                          KillEpoch{0},
                                          ExecutorGeneration{0}}),
                  scratch).code == ApplyCode::applied,
            "normal-mode setup failed");

    const auto promoted = catalog_test::apply(
        state,
        command(2,
                std::byte{0x32},
                SetExecutorGeneration{ExecutorGeneration{1}}),
        scratch);
    require(promoted.code == ApplyCode::applied &&
                promoted.executor_generation == ExecutorGeneration{1} &&
                promoted.strategy_mode == StrategyMode::no_new_risk &&
                state.strategy_mode == StrategyMode::no_new_risk,
            "executor promotion retained unsafe new-risk authority");

    const auto stale_resume = catalog_test::apply(
        state,
        command(3,
                std::byte{0x33},
                SetStrategyMode{StrategyMode::normal,
                                KillEpoch{0},
                                ExecutorGeneration{0}}),
        scratch);
    require(stale_resume.code == ApplyCode::stale_control_generation,
            "pre-promotion mode command was accepted");

    const auto matched_resume = catalog_test::apply(
        state,
        command(4,
                std::byte{0x34},
                SetStrategyMode{StrategyMode::normal,
                                KillEpoch{0},
                                ExecutorGeneration{1}}),
        scratch);
    require(matched_resume.code == ApplyCode::applied &&
                state.strategy_mode == StrategyMode::normal,
            "current executor generation could not restore configured mode");

    const auto equal = catalog_test::apply(
        state,
        command(5,
                std::byte{0x35},
                SetExecutorGeneration{ExecutorGeneration{1}}),
        scratch);
    const auto regressed = catalog_test::apply(
        state,
        command(6,
                std::byte{0x36},
                SetExecutorGeneration{ExecutorGeneration{0}}),
        scratch);
    require(equal.code == ApplyCode::non_monotonic_executor_generation &&
                regressed.code == ApplyCode::non_monotonic_executor_generation &&
                state.executor_generation == ExecutorGeneration{1},
            "executor generation repeated or regressed");
}

void test_invalid_mode_is_typed_and_retained() {
    using namespace velocita::v2;
    TradingState state = catalog_test::state();
    ApplyScratch scratch;
    const auto invalid = command(
        1,
        std::byte{0x41},
        SetStrategyMode{static_cast<StrategyMode>(255),
                        KillEpoch{0},
                        ExecutorGeneration{0}});
    const auto first = catalog_test::apply(state, invalid, scratch);
    require(first.code == ApplyCode::invalid_command &&
                first.contract_error == ContractError::invalid_enum &&
                state.strategy_mode == StrategyMode::halted,
            "unknown strategy mode was accepted or misclassified");
    require(catalog_test::apply(state, invalid, scratch) == first && state.state_version == 1,
            "invalid identified control command was not retained exactly");
}

}

int main() {
    try {
        test_control_authority_is_reserved_to_operator_callers();
        test_resuming_normal_requires_reconciliation();
        test_boot_and_kill_epoch_are_fail_safe();
        test_kill_epoch_never_regresses();
        test_executor_generation_advance_removes_new_risk_authority();
        test_invalid_mode_is_typed_and_retained();
        std::cout << "all V2 kernel control tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel control test failed: " << error.what() << '\n';
        return 1;
    }
}
