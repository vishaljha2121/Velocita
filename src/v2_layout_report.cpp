#include "velocita/v2/contracts.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/rule_evaluator.hpp"

#include <iostream>

int main() {
    using namespace velocita::v2;

    std::cout << "target_cache_line_bytes=" << kTargetCacheLineBytes << '\n'
              << "decision_frame_size_bytes=" << sizeof(DecisionFrame) << '\n'
              << "decision_frame_alignment_bytes=" << alignof(DecisionFrame) << '\n'
              << "decision_frame_budget_bytes="
              << kDecisionFrameObjectBudgetBytes << '\n'
              << "decision_frame_arena_slot_stride_bytes="
              << kDecisionFrameArenaSlotStrideBytes << '\n'
              << "decision_frame_payload_budget_bytes="
              << kDecisionFramePayloadBudgetBytes << '\n'
              << "policy_artifact_size_bytes=" << sizeof(PolicyArtifact) << '\n'
              << "policy_artifact_alignment_bytes=" << alignof(PolicyArtifact) << '\n'
              << "policy_artifact_budget_bytes="
              << kPolicyArtifactObjectBudgetBytes << '\n'
              << "policy_artifact_payload_budget_bytes="
              << kPolicyArtifactPayloadBudgetBytes << '\n'
              << "rule_evaluation_size_bytes=" << sizeof(RuleEvaluation) << '\n'
              << "rule_evaluation_budget_bytes="
              << kRuleEvaluationObjectBudgetBytes << '\n'
              << "apply_result_size_bytes=" << sizeof(ApplyResult) << '\n'
              << "request_receipt_size_bytes=" << sizeof(RequestReceipt) << '\n'
              << "kernel_command_budget_bytes=" << kKernelCommandBudgetBytes << '\n'
              << "kernel_result_budget_bytes=" << kKernelResultBudgetBytes << '\n'
              << "trading_state_size_bytes=" << sizeof(TradingState) << '\n'
              << "trading_state_budget_bytes=" << kTradingStateObjectBudgetBytes
              << '\n'
              << "decision_record_hot_cold_split_status=pending_phase3c\n";
    return 0;
}
