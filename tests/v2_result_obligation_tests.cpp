#include "velocita/v2/result_ledger.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace velocita::v2 {
struct ResultLedgerTestAccess {
    static void ready_head(ResultLedger& ledger, std::uint16_t value) { ledger.header_.ready_head = value; }
    static void ready_count(ResultLedger& ledger, std::uint16_t value) { ledger.header_.ready_count = value; }
    static void wrong_pool(ResultLedger& ledger, ResultHandle handle) {
        ledger.slots_[handle.slot].pool = ResultLedger::SlotPool::protected_control;
    }
    static void cycle(ResultLedger& ledger, ResultHandle handle) {
        ledger.slots_[handle.slot].next = handle.slot;
    }
    static void zero_generation(ResultLedger& ledger, ResultHandle handle) {
        ledger.slots_[handle.slot].generation = 0;
    }
};
}

namespace {
using namespace velocita::v2;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

ResultBinding binding(std::int64_t index) {
    ResultBinding value;
    value.identity.caller_id = CallerId{17};
    value.identity.request_sequence = RequestSequence{static_cast<std::uint64_t>(index)};
    value.identity.payload_digest.bytes[3] = static_cast<std::byte>(index);
    value.command_digest.bytes[5] = static_cast<std::byte>(index + 1);
    value.term = 9;
    value.index = index;
    return value;
}

CommittedResult ready(const ResultBinding& binding, std::uint64_t seed) {
    CommittedResult value{binding.identity, {}, binding.term, binding.index};
    auto& result = value.result;
    result.code = ApplyCode::applied;
    result.contract_error = ContractError::invalid_capability;
    result.metadata_words = {seed + 1, seed + 2};
    result.policy_lifecycle = PolicyLifecycle::superseded;
    result.state_version = seed + 3;
    result.strategy_mode = StrategyMode::normal;
    result.kill_epoch = KillEpoch{seed + 4};
    result.executor_generation = ExecutorGeneration{seed + 5};
    result.detail = ApplyResultDetail::mass_cancel(seed + 6, seed + 7, seed + 8);
    result.decision_id = DecisionId{seed + 9};
    result.decision_code = DecisionCode::action;
    result.proposed_directive = Directive::replace;
    result.effective_directive = Directive::cancel;
    result.evaluation_mode = EvaluationMode::shadow;
    result.matched_rule_index = 11;
    result.rules_visited = 12;
    result.action_resolution_code = ActionResolutionCode::quantity_cap_exceeded;
    result.detail_kind = ApplyResultDetailKind::mass_cancel;
    return value;
}

bool same_result(const CommittedResult& a, const CommittedResult& b) {
    return a.identity == b.identity && a.term == b.term && a.index == b.index &&
           a.result.code == b.result.code && a.result.contract_error == b.result.contract_error &&
           a.result.metadata_words == b.result.metadata_words &&
           a.result.policy_lifecycle == b.result.policy_lifecycle &&
           a.result.state_version == b.result.state_version &&
           a.result.strategy_mode == b.result.strategy_mode &&
           a.result.kill_epoch == b.result.kill_epoch &&
           a.result.executor_generation == b.result.executor_generation &&
           a.result.detail == b.result.detail && a.result.decision_id == b.result.decision_id &&
           a.result.decision_code == b.result.decision_code &&
           a.result.proposed_directive == b.result.proposed_directive &&
           a.result.effective_directive == b.result.effective_directive &&
           a.result.evaluation_mode == b.result.evaluation_mode &&
           a.result.matched_rule_index == b.result.matched_rule_index &&
           a.result.rules_visited == b.result.rules_visited &&
           a.result.action_resolution_code == b.result.action_resolution_code &&
           a.result.detail_kind == b.result.detail_kind;
}

struct Capture { std::array<ResultObligationView, kResultLedgerSlots> values{}; std::size_t count{}; std::size_t stop{kResultLedgerSlots}; };
bool capture(void* opaque, ResultObligationView value) noexcept {
    auto& out = *static_cast<Capture*>(opaque);
    if (out.count == out.stop) return false;
    out.values[out.count++] = value;
    return true;
}

void test_order_values_and_reuse() {
    ResultLedger ledger;
    const auto a = binding(1), b = binding(2), c = binding(3);
    const auto ah = ledger.reserve(a, false), bh = ledger.reserve(b, false), ch = ledger.reserve(c, false);
    require(ah && bh && ch, "fixture reserve");
    const auto ar = ready(a, 100), cr = ready(c, 200);
    require(ledger.publish(*ch, cr) && ledger.publish(*ah, ar), "fixture publish");
    Capture seen;
    require(ledger.visit_obligations(capture, &seen) && seen.count == 3, "visit failed");
    require(seen.values[0].state == ResultObligationState::ready &&
                seen.values[1].state == ResultObligationState::ready &&
                seen.values[0].handle == *ch && seen.values[1].handle == *ah,
            "READY followed slot order instead of publish FIFO");
    require(seen.values[0].binding == c && seen.values[1].binding == a,
            "READY binding lost command identity, digest, term or index");
    require(same_result(seen.values[0].ready_result, cr) && same_result(seen.values[1].ready_result, ar),
            "READY lost an ApplyResult field");
    require(seen.values[2].state == ResultObligationState::reserved && seen.values[2].handle == *bh &&
                seen.values[2].binding == b && same_result(seen.values[2].ready_result, {}),
            "RESERVED view was not exact and default-valued");
    require(ledger.ready_count() == 2 && ledger.reserved_count() == 1 &&
                ledger.available_ordinary() == kResultLedgerOrdinarySlots - 3 &&
                ledger.available_protected() == kResultLedgerProtectedSlots,
            "visit changed ownership or admission capacity");
    require(ledger.take()->index == 3 && ledger.take()->index == 1, "visit mutated FIFO");
    require(ledger.release_reserved(*bh), "release failed");
    const auto reused = ledger.reserve(binding(4), false);
    require(reused && reused->slot == bh->slot && reused->generation != bh->generation, "slot reuse fixture");
    seen = {};
    require(ledger.visit_obligations(capture, &seen) && seen.count == 1 && seen.values[0].handle == *reused,
            "stale generation appeared");
}

void test_bounds_refusal_and_no_allocation() {
    ResultLedger empty;
    Capture seen;
    require(empty.visit_obligations(capture, &seen) && seen.count == 0, "empty visit");
    require(!empty.visit_obligations(nullptr, &seen), "null visitor accepted");

    ResultLedger full;
    for (std::size_t i = 0; i < kResultLedgerSlots; ++i)
        require(full.reserve(binding(static_cast<std::int64_t>(i + 1)), i >= kResultLedgerOrdinarySlots).has_value(), "full reserve");
    allocation_probe::count = 0; allocation_probe::enabled = true;
    const bool visited = full.visit_obligations(capture, &seen);
    allocation_probe::enabled = false;
    require(visited && seen.count == kResultLedgerSlots && allocation_probe::count == 0, "bounded visit allocated or omitted slots");
    Capture stopped; stopped.stop = 7;
    require(!full.visit_obligations(capture, &stopped) && stopped.count == 7, "visitor refusal did not stop at prefix");

    ResultLedger corrupt;
    const auto handle = corrupt.reserve(binding(1), false);
    require(handle && corrupt.publish(*handle, ready(binding(1), 1)), "corrupt fixture");
    ResultLedgerTestAccess::ready_head(corrupt, static_cast<std::uint16_t>(kResultLedgerSlots));
    seen = {};
    require(!corrupt.visit_obligations(capture, &seen) && seen.count == 0, "malformed ledger emitted partial view");
    ResultLedger corrupt_count;
    ResultLedgerTestAccess::ready_count(corrupt_count, 1);
    require(!corrupt_count.visit_obligations(capture, &seen), "invalid ready count accepted");

    for (int fault = 0; fault != 3; ++fault) {
        ResultLedger malformed;
        const auto owned = malformed.reserve(binding(9), false);
        require(owned.has_value(), "malformed fixture reserve");
        if (fault == 0) ResultLedgerTestAccess::wrong_pool(malformed, *owned);
        if (fault == 1) {
            require(malformed.publish(*owned, ready(binding(9), 2)), "cycle fixture publish");
            ResultLedgerTestAccess::cycle(malformed, *owned);
        }
        if (fault == 2) ResultLedgerTestAccess::zero_generation(malformed, *owned);
        seen = {};
        require(!malformed.visit_obligations(capture, &seen) && seen.count == 0,
                "malformed occupied pool/link/generation emitted a view");
    }
}

static_assert(std::is_trivially_copyable_v<ResultObligationView>);
static_assert(noexcept(std::declval<const ResultLedger&>().visit_obligations(nullptr, nullptr)));
static_assert(sizeof(ResultLedger) == 229400);
}

int main() {
    try {
        test_order_values_and_reuse();
        test_bounds_refusal_and_no_allocation();
        std::cout << "result obligation tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "result obligation tests failed: " << error.what() << '\n';
        return 1;
    }
}
