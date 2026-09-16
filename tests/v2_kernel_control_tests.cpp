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

void test_control_authority_is_reserved_to_operator_callers() {
    using namespace velocita::v2;
    // G-056, the authorization half. The reducer CANNOT verify a signature:
    // G-066 keeps key material out of replicated state and `apply` performs no
    // I/O, so authentication is necessarily an edge property. What the cluster
    // can do - and what this enforces - is refuse a control action from a caller
    // the edge never authenticated, and record which operator did issue it.
    //
    // The reservation is ONE-DIRECTIONAL by design. A control command must come
    // from an operator caller; an operator caller is not forbidden ordinary
    // ones. The deployment obligation that makes this mean something is that the
    // edge never hands a reserved CallerId to a strategy.
    const auto control_from = [](const std::uint64_t caller,
                                 const std::uint64_t sequence,
                                 CommandBody body) {
        return Command{CommandIdentity{CallerId{caller}, RequestSequence{sequence},
                                       digest(std::byte{0x77})},
                       std::move(body)};
    };

    require(kMaxOperatorCallerId > 0,
            "the operator caller band is empty, so nothing can ever issue control");

    for (const auto& body : std::initializer_list<CommandBody>{
             AdvanceKillEpoch{KillEpoch{1}},
             SetExecutorGeneration{ExecutorGeneration{1}},
             SetStrategyMode{StrategyMode::normal, KillEpoch{}, ExecutorGeneration{}}}) {
        TradingState state;
        ApplyScratch scratch;
        const auto unauthorized =
            apply(state, control_from(kMaxOperatorCallerId + 1, 1, body), scratch);
        require(unauthorized.code == ApplyCode::unauthorized_control,
                "a control command from a non-operator caller was not refused");

        // Refused means NOTHING MOVED - not even the request receipt, or a
        // replay of the same command would come back as a completed action.
        TradingState untouched;
        require(std::memcmp(&state, &untouched, sizeof(TradingState)) == 0,
                "a refused control command still mutated authoritative state");
    }

    // The same command from inside the band is accepted, so the refusal above
    // is about the caller and not about the command being malformed.
    {
        TradingState state;
        ApplyScratch scratch;
        const auto allowed = apply(
            state, control_from(kMaxOperatorCallerId, 1, AdvanceKillEpoch{KillEpoch{1}}),
            scratch);
        require(allowed.code == ApplyCode::applied,
                "a control command from an operator caller was refused");
        require(state.kill_epoch.value() == 1, "the accepted kill epoch did not take");
    }

    // A REFUSAL THAT CANNOT BE ENCODED IS NOT A REFUSAL AN OPERATOR EVER SEES.
    //
    // `ApplyCode` is carried in the canonical 128-byte result and validated on
    // the way back in, so a new code has to be admitted by the codec and by the
    // snapshot or the result fails to decode. Adding the code and forgetting
    // those two lists is silent - it was caught here by a compiler warning
    // about an unhandled switch, which is luck rather than coverage.
    {
        TradingState state;
        ApplyScratch scratch;
        const auto refused = apply(
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

    // And an ordinary command from a high caller is untouched by any of this.
    {
        TradingState state;
        ApplyScratch scratch;
        const auto ordinary = apply(
            state,
            control_from(kMaxOperatorCallerId + 1, 1,
                         ConfigureRiskLimits{AccountId{5}, InstrumentId{4},
                                             RiskPolicyVersion{6}, RiskVersion{0},
                                             KillEpoch{}, ExecutorGeneration{},
                                             QuantityLots::from_atoms(4),
                                             QuantityLots::from_atoms(8),
                                             MoneyMicros::from_atoms(4'000),
                                             MoneyMicros::from_atoms(8'000),
                                             1'000'000, 2'500}),
            scratch);
        require(ordinary.code != ApplyCode::unauthorized_control,
                "the operator reservation leaked onto an ordinary command");
    }
}

void test_boot_and_kill_epoch_are_fail_safe() {
    using namespace velocita::v2;
    TradingState state;
    ApplyScratch scratch;
    require(state.strategy_mode == StrategyMode::halted && state.kill_epoch.value() == 0 &&
                state.executor_generation.value() == 0,
            "trading authority did not boot fail-safe");

    const auto normal = command(
        1,
        std::byte{0x11},
        SetStrategyMode{StrategyMode::normal, KillEpoch{0}, ExecutorGeneration{0}});
    const auto normal_result = apply(state, normal, scratch);
    require(normal_result.code == ApplyCode::applied &&
                normal_result.strategy_mode == StrategyMode::normal,
            "generation-matched mode transition was rejected");
    require(apply(state, normal, scratch) == normal_result && state.state_version == 1,
            "exact mode-command retry did not return its retained result");

    const auto killed = apply(
        state,
        command(2, std::byte{0x12}, AdvanceKillEpoch{KillEpoch{1}}),
        scratch);
    require(killed.code == ApplyCode::applied && killed.kill_epoch == KillEpoch{1} &&
                killed.strategy_mode == StrategyMode::halted &&
                state.strategy_mode == StrategyMode::halted,
            "kill-epoch advance did not atomically halt trading authority");

    const auto stale_resume = apply(
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

    const auto matched_resume = apply(
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
    TradingState state;
    ApplyScratch scratch;

    require(apply(state,
                  command(1, std::byte{0x21}, AdvanceKillEpoch{KillEpoch{4}}),
                  scratch).code == ApplyCode::applied,
            "initial kill-epoch advance failed");
    const auto equal = apply(
        state,
        command(2, std::byte{0x22}, AdvanceKillEpoch{KillEpoch{4}}),
        scratch);
    const auto regressed = apply(
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
    TradingState state;
    ApplyScratch scratch;

    require(apply(state,
                  command(1,
                          std::byte{0x31},
                          SetStrategyMode{StrategyMode::normal,
                                          KillEpoch{0},
                                          ExecutorGeneration{0}}),
                  scratch).code == ApplyCode::applied,
            "normal-mode setup failed");

    const auto promoted = apply(
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

    const auto stale_resume = apply(
        state,
        command(3,
                std::byte{0x33},
                SetStrategyMode{StrategyMode::normal,
                                KillEpoch{0},
                                ExecutorGeneration{0}}),
        scratch);
    require(stale_resume.code == ApplyCode::stale_control_generation,
            "pre-promotion mode command was accepted");

    const auto matched_resume = apply(
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

    const auto equal = apply(
        state,
        command(5,
                std::byte{0x35},
                SetExecutorGeneration{ExecutorGeneration{1}}),
        scratch);
    const auto regressed = apply(
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
    TradingState state;
    ApplyScratch scratch;
    const auto invalid = command(
        1,
        std::byte{0x41},
        SetStrategyMode{static_cast<StrategyMode>(255),
                        KillEpoch{0},
                        ExecutorGeneration{0}});
    const auto first = apply(state, invalid, scratch);
    require(first.code == ApplyCode::invalid_command &&
                first.contract_error == ContractError::invalid_enum &&
                state.strategy_mode == StrategyMode::halted,
            "unknown strategy mode was accepted or misclassified");
    require(apply(state, invalid, scratch) == first && state.state_version == 1,
            "invalid identified control command was not retained exactly");
}

}  // namespace

int main() {
    try {
        test_control_authority_is_reserved_to_operator_callers();
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
