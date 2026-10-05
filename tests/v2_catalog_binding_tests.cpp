#include "velocita/v2/kernel.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "v2_catalog_test_support.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

void run_catalog_replication_tests();

namespace {
using namespace velocita::v2;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Digest256 root(unsigned char marker) {
    Digest256 value;
    value.bytes[0] = std::byte{marker};
    return value;
}
Command install(std::uint64_t sequence, Digest256 catalog = catalog_test::standard_catalog().root()) {
    return {{CallerId{1}, RequestSequence{sequence}, root(static_cast<unsigned char>(sequence))},
            InstallExecutionCatalog{catalog, KillEpoch{1}, ExecutorGeneration{1}}};
}
std::vector<std::byte> snapshot(const TradingState& state) {
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none, "fixture snapshot refused");
    return {bytes.begin(), bytes.begin() + encoded.bytes_written};
}
void test_install_once() {
    TradingState state;
    state.kill_epoch = KillEpoch{1};
    state.executor_generation = ExecutorGeneration{1};
    ApplyScratch scratch;
    const auto& catalog = catalog_test::standard_catalog();
    const auto first = apply(state, install(1), scratch, &catalog);
    require(first.code == ApplyCode::applied && state.execution_catalog_root == catalog.root(),
            "valid installation did not persist catalog root");
    require(apply(state, install(1), scratch, &catalog) == first && state.state_version == 1,
            "exact installation retry changed state");
    require(apply(state, install(2), scratch, &catalog).code == ApplyCode::already_applied,
            "same-root installation was not idempotent");
    require(apply(state, install(3, root(2)), scratch, &catalog).code == ApplyCode::immutable_catalog_conflict &&
                state.execution_catalog_root == catalog.root(), "conflicting installation changed root");
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    require(encoded.error == StateSnapshotError::none, "installed state did not encode");
    TradingState restored;
    StateSnapshotScratch snapshot_scratch;
    require(decode_state_snapshot(std::span{bytes}.first(encoded.bytes_written), restored,
                snapshot_scratch).error == StateSnapshotError::none &&
                restored.execution_catalog_root == catalog.root(), "snapshot lost catalog root");
}
void test_installation_guards_and_missing_dependency() {
    const auto& catalog = catalog_test::standard_catalog();
    TradingState state;
    state.kill_epoch = KillEpoch{1};
    state.executor_generation = ExecutorGeneration{1};
    ApplyScratch scratch;
    const auto before = snapshot(state);
    bool failed = false;
    try { (void)apply(state, install(1), scratch); }
    catch (const CatalogDependencyUnavailable&) { failed = true; }
    require(failed && snapshot(state) == before,
            "missing artifact changed reducer state or produced a result");
    VerifiedExecutionCatalog empty;
    failed = false;
    try { (void)apply(state, install(1), scratch, &empty); }
    catch (const CatalogDependencyUnavailable&) { failed = true; }
    require(failed && snapshot(state) == before, "empty artifact allowed installation");

    auto unauthorized = install(1);
    unauthorized.identity.caller_id = CallerId{kMaxOperatorCallerId + 1};
    require(apply(state, unauthorized, scratch).code == ApplyCode::unauthorized_control &&
                snapshot(state) == before, "nonoperator installation was retained");
    require(apply(state, install(2, {}), scratch).code == ApplyCode::invalid_command &&
                state.execution_catalog_root.is_zero(), "zero catalog root installed");
    auto stale = install(3);
    std::get<InstallExecutionCatalog>(stale.body).expected_kill_epoch = KillEpoch{2};
    require(apply(state, stale, scratch).code == ApplyCode::stale_control_generation,
            "stale controls required artifact or installed root");
    state.strategy_mode = StrategyMode::normal;
    require(apply(state, install(4), scratch).code == ApplyCode::catalog_installation_blocked,
            "running strategy installed catalog");
    state.strategy_mode = StrategyMode::halted;
    for (unsigned arena = 0; arena < 8; ++arena) {
        TradingState occupied;
        occupied.kill_epoch = KillEpoch{1};
        occupied.executor_generation = ExecutorGeneration{1};
        switch (arena) {
            case 0: occupied.policy_count = 1; break;
            case 1: occupied.active_executable_policy.present = true; break;
            case 2: occupied.staged_executable_policy.present = true; break;
            case 3: occupied.decision_count = 1; break;
            case 4: occupied.risk_account_count = 1; break;
            case 5: occupied.reservation_count = 1; break;
            case 6: occupied.order_count = 1; break;
            case 7: occupied.outbox_effect_count = 1; break;
        }
        require(apply(occupied, install(1), scratch).code == ApplyCode::catalog_installation_blocked &&
                    occupied.execution_catalog_root.is_zero(), "occupied arena allowed first install");
    }
    require(apply(state, install(5), scratch, &catalog).code == ApplyCode::applied,
            "valid artifact did not clear local dependency");
    state.strategy_mode = StrategyMode::normal;
    require(apply(state, install(6), scratch, &catalog).code == ApplyCode::already_applied,
            "installed same-root retry incorrectly rechecked emptiness/mode");
    const auto installed = snapshot(state);
    failed = false;
    try { (void)apply(state, {{CallerId{1}, RequestSequence{7}, root(7)},
                         AdvanceKillEpoch{KillEpoch{2}}}, scratch); }
    catch (const CatalogDependencyUnavailable&) { failed = true; }
    require(failed && snapshot(state) == installed,
            "installed root missing dependency consumed even a control command");
}

void test_uninstalled_economic_commands_refuse() {
    TradingState state;
    ApplyScratch scratch;
    const std::array<CommandBody, 5> bodies{PublishPolicy{}, StageExecutablePolicy{},
        ConfigureRiskLimits{}, EvaluateDecisionFrame{}, AuthorizeDecision{DecisionId{1}}};
    std::uint64_t sequence{};
    for (const auto& body : bodies) {
        const Command command{{CallerId{1}, RequestSequence{++sequence}, root(9)}, body};
        require(apply(state, command, scratch).code == ApplyCode::catalog_not_installed,
                "rootless economic configuration or authorization succeeded");
    }
    require(state.policy_count == 0 && state.risk_account_count == 0 && state.decision_count == 0 &&
                state.reservation_count == 0 && state.order_count == 0 && state.outbox_effect_count == 0,
            "rootless refusal created economic state");
}

void test_refused_install_does_not_require_candidate_artifact() {
    const auto proposed = catalog_test::standard_catalog().root();
    ApplyScratch scratch;

    TradingState receipts_full;
    auto control = [&](const std::uint64_t sequence, CommandBody body) {
        return Command{{CallerId{1}, RequestSequence{sequence}, root(0x31)},
                       std::move(body)};
    };
    require(apply(receipts_full, control(1, AdvanceKillEpoch{KillEpoch{1}}),
                  scratch).code == ApplyCode::applied &&
                apply(receipts_full,
                      control(2, SetExecutorGeneration{ExecutorGeneration{1}}),
                      scratch).code == ApplyCode::applied &&
                apply(receipts_full,
                      control(3, AcknowledgeGatewayResults{RequestSequence{1}}),
                      scratch).code == ApplyCode::applied,
            "pinned receipt fixture controls did not initialize");
    std::uint64_t sequence = 3;
    std::uint64_t epoch = 1;
    while (receipts_full.receipt_count < receipts_full.receipts.size()) {
        require(apply(receipts_full,
                      control(++sequence, AdvanceKillEpoch{KillEpoch{++epoch}}),
                      scratch).code == ApplyCode::applied,
                "pinned receipt fixture did not fill through public commands");
    }
    require(apply(receipts_full,
                  control(++sequence, AdvanceKillEpoch{KillEpoch{++epoch}}),
                  scratch).code == ApplyCode::applied &&
                receipts_full.receipt_count == receipts_full.receipts.size() &&
                snapshot(receipts_full).size() != 0,
            "public pinned receipt fixture was not canonical");
    const Command receipt_blocked{
        {CallerId{1}, RequestSequence{++sequence}, root(0x33)},
        InstallExecutionCatalog{proposed, KillEpoch{epoch},
                                ExecutorGeneration{1}}};
    require(required_execution_catalog(receipts_full, receipt_blocked).is_zero(),
            "receipt-capacity refusal incorrectly required candidate artifact");
    require(apply(receipts_full, receipt_blocked, scratch).code ==
                ApplyCode::receipt_capacity_exceeded,
            "receipt-full install did not return its deterministic refusal");

    TradingState sweepable;
    require(apply(sweepable, control(1, AdvanceKillEpoch{KillEpoch{1}}), scratch).code ==
                ApplyCode::applied &&
                apply(sweepable,
                      control(2, SetExecutorGeneration{ExecutorGeneration{1}}),
                      scratch).code == ApplyCode::applied,
            "sweepable receipt fixture controls did not initialize");
    sequence = 2;
    while (sweepable.receipt_count < sweepable.receipts.size()) {
        require(apply(sweepable,
                      control(++sequence, AdvanceKillEpoch{KillEpoch{1}}), scratch).code ==
                    ApplyCode::non_monotonic_kill_epoch,
                "sweepable receipt fixture did not fill through public refusals");
    }
    const Command sweepable_install{
        {CallerId{1}, RequestSequence{++sequence}, root(0x34)},
        InstallExecutionCatalog{proposed, KillEpoch{1}, ExecutorGeneration{1}}};
    require(snapshot(sweepable).size() != 0 &&
                required_execution_catalog(sweepable, sweepable_install) == proposed,
            "sweepable receipts hid an admissible install dependency");
    bool dependency_failed = false;
    try {
        (void)apply(sweepable, sweepable_install, scratch);
    } catch (const CatalogDependencyUnavailable&) {
        dependency_failed = true;
    }
    require(dependency_failed,
            "sweepable receipt arena installed without its required artifact");

    TradingState callers_full;
    require(apply(callers_full,
                  {{CallerId{2}, RequestSequence{1}, root(0x41)},
                   AdvanceKillEpoch{KillEpoch{1}}}, scratch).code ==
                ApplyCode::applied &&
                apply(callers_full,
                      {{CallerId{3}, RequestSequence{1}, root(0x42)},
                       SetExecutorGeneration{ExecutorGeneration{1}}}, scratch).code ==
                    ApplyCode::applied,
            "caller fixture controls did not initialize");
    for (std::size_t index = 0; index < kMaxCallerCursors - 2; ++index) {
        const Command source{
            {CallerId{4 + index}, RequestSequence{1}, root(0x43)},
            RecordSourceEvent{SourceId{index + 1}, SourceGeneration{1},
                              SourceSequence{1}, root(0x44)}};
        require(apply(callers_full, source, scratch).code == ApplyCode::applied,
                "caller fixture did not register through public source command");
    }
    require(callers_full.caller_cursor_count == kMaxCallerCursors &&
                snapshot(callers_full).size() != 0,
            "public caller-capacity fixture was not canonical");
    require(required_execution_catalog(callers_full, install(1)).is_zero(),
            "caller-capacity refusal incorrectly required candidate artifact");
    require(apply(callers_full, install(1), scratch).code ==
                ApplyCode::receipt_capacity_exceeded,
            "caller-full install did not return its deterministic refusal");

    TradingState exhausted;
    exhausted.kill_epoch = KillEpoch{1};
    exhausted.executor_generation = ExecutorGeneration{1};
    exhausted.state_version = std::numeric_limits<std::uint64_t>::max();
    require(required_execution_catalog(exhausted, install(1)).is_zero(),
            "state-version exhaustion incorrectly required candidate artifact");
    require(apply(exhausted, install(1), scratch).code ==
                ApplyCode::state_version_exhausted,
            "version-exhausted install did not return its deterministic refusal");

    TradingState admissible;
    admissible.kill_epoch = KillEpoch{1};
    admissible.executor_generation = ExecutorGeneration{1};
    require(required_execution_catalog(admissible, install(1)) == proposed,
            "admissible installation did not require its candidate artifact");
}
}
int main() {
    try { test_install_once(); test_installation_guards_and_missing_dependency();
          test_uninstalled_economic_commands_refuse();
          test_refused_install_does_not_require_candidate_artifact();
          run_catalog_replication_tests(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "v2_catalog_binding_tests: PASS\n";
}
