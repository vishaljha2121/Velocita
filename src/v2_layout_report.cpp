#include "velocita/v2/action_template.hpp"
#include "velocita/v2/action_resolver.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/executable_policy.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/payload.hpp"
#include "velocita/v2/rule_evaluator.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <iostream>

int main() {
    using namespace velocita::v2;

    std::cout << "target_cache_line_bytes=" << kTargetCacheLineBytes << '\n'
              << "execution_catalog_context_size_bytes="
              << sizeof(VerifiedExecutionCatalog) << '\n'
              << "execution_catalog_context_alignment_bytes="
              << alignof(VerifiedExecutionCatalog) << '\n'
              << "execution_catalog_context_budget_bytes=16384\n"
              << "execution_catalog_payload_budget_bytes="
              << kMaxExecutionCatalogBytes << '\n'
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
              << "price_source_size_bytes=" << sizeof(PriceSource) << '\n'
              << "price_source_budget_bytes="
              << kPriceSourceObjectBudgetBytes << '\n'
              << "quantity_source_size_bytes=" << sizeof(QuantitySource) << '\n'
              << "quantity_source_budget_bytes="
              << kQuantitySourceObjectBudgetBytes << '\n'
              << "action_safety_caps_size_bytes=" << sizeof(ActionSafetyCaps)
              << '\n'
              << "action_safety_caps_budget_bytes="
              << kActionSafetyCapsObjectBudgetBytes << '\n'
              << "action_template_size_bytes=" << sizeof(ActionTemplate) << '\n'
              << "action_template_alignment_bytes=" << alignof(ActionTemplate)
              << '\n'
              << "action_template_budget_bytes="
              << kActionTemplateObjectBudgetBytes << '\n'
              << "economic_action_proposal_size_bytes="
              << sizeof(EconomicActionProposal) << '\n'
              << "economic_action_proposal_budget_bytes="
              << kEconomicActionProposalObjectBudgetBytes << '\n'
              << "instrument_execution_profile_size_bytes="
              << sizeof(InstrumentExecutionProfile) << '\n'
              << "instrument_execution_profile_budget_bytes="
              << kInstrumentExecutionProfileObjectBudgetBytes << '\n'
              << "venue_capabilities_size_bytes=" << sizeof(VenueCapabilities)
              << '\n'
              << "venue_capabilities_budget_bytes="
              << kVenueCapabilitiesObjectBudgetBytes << '\n'
              << "action_resolution_size_bytes=" << sizeof(ActionResolution)
              << '\n'
              << "action_resolution_budget_bytes="
              << kActionResolutionObjectBudgetBytes << '\n'
              << "feature_descriptor_size_bytes="
              << sizeof(FeatureDescriptor) << '\n'
              << "feature_descriptor_budget_bytes="
              << kFeatureDescriptorObjectBudgetBytes << '\n'
              << "feature_schema_artifact_size_bytes="
              << sizeof(FeatureSchemaArtifact) << '\n'
              << "feature_schema_artifact_budget_bytes="
              << kFeatureSchemaArtifactObjectBudgetBytes << '\n'
              << "feature_schema_artifact_max_canonical_bytes="
              << kMaxFeatureSchemaPayloadBytes << '\n'
              << "feature_schema_artifact_payload_budget_bytes="
              << kFeatureSchemaArtifactPayloadBudgetBytes << '\n'
              << "executable_policy_artifact_size_bytes="
              << sizeof(ExecutablePolicyArtifact) << '\n'
              << "executable_policy_artifact_budget_bytes="
              << kExecutablePolicyArtifactObjectBudgetBytes << '\n'
              << "executable_policy_artifact_max_canonical_bytes="
              << kMaxExecutablePolicyPayloadBytes << '\n'
              << "executable_policy_artifact_payload_budget_bytes="
              << kExecutablePolicyArtifactPayloadBudgetBytes << '\n'
              << "candidate_executable_policy_artifact_count="
              << kMaxExecutablePolicyArtifacts << '\n'
              << "candidate_executable_action_template_count="
              << kMaxExecutableActionTemplates << '\n'
              << "executable_policy_bundle_size_bytes="
              << sizeof(ExecutablePolicyBundle) << '\n'
              << "integrated_active_staged_resident_bytes="
              << kMaxExecutablePolicyArtifacts * sizeof(ExecutablePolicyBundle)
              << '\n'
              << "candidate_active_staged_max_canonical_bytes="
              << kMaxExecutablePolicyArtifacts *
                     (kMaxExecutablePolicyPayloadBytes +
                      kMaxFeatureSchemaPayloadBytes)
              << '\n'
              << "current_trading_state_headroom_bytes="
              << kTradingStateObjectBudgetBytes - sizeof(TradingState) << '\n'
              << "integrated_trading_state_ceiling_bytes="
              << kTradingStateObjectBudgetBytes << '\n'
              << "apply_result_size_bytes=" << sizeof(ApplyResult) << '\n'
              << "request_receipt_size_bytes=" << sizeof(RequestReceipt) << '\n'
              << "risk_account_capacity=" << kMaxRiskAccounts << '\n'
              << "risk_account_size_bytes=" << sizeof(RiskAccount) << '\n'
              << "risk_reservation_capacity=" << kMaxRiskReservations << '\n'
              << "risk_reservation_size_bytes=" << sizeof(RiskReservation)
              << '\n'
              << "cluster_order_capacity=" << kMaxClusterOrders << '\n'
              << "cluster_order_size_bytes=" << sizeof(ClusterOrder) << '\n'
              << "outbox_effect_capacity=" << kMaxOutboxEffects << '\n'
              << "outbox_effect_record_size_bytes="
              << sizeof(OutboxEffectRecord) << '\n'
              << "decision_record_capacity=" << kMaxDecisionRecords << '\n'
              << "decision_record_hot_size_bytes="
              << sizeof(DecisionRecordHot) << '\n'
              << "decision_record_hot_budget_bytes="
              << kDecisionRecordHotBudgetBytes << '\n'
              << "decision_record_hot_arena_slot_stride_bytes="
              << kDecisionRecordHotArenaSlotStrideBytes << '\n'
              << "decision_record_cold_size_bytes="
              << sizeof(DecisionRecordCold) << '\n'
              << "decision_record_cold_budget_bytes="
              << kDecisionRecordColdBudgetBytes << '\n'
              << "decision_record_cold_arena_slot_stride_bytes="
              << kDecisionRecordColdArenaSlotStrideBytes << '\n'
              << "decision_record_packed_object_footprint_bytes="
              << kMaxDecisionRecords *
                     (sizeof(DecisionRecordHot) + sizeof(DecisionRecordCold))
              << '\n'
              << "decision_record_arena_footprint_bytes="
              << kMaxDecisionRecords *
                     (kDecisionRecordHotArenaSlotStrideBytes +
                      kDecisionRecordColdArenaSlotStrideBytes)
              << '\n'
              << "kernel_command_budget_bytes=" << kKernelCommandBudgetBytes << '\n'
              << "kernel_command_max_canonical_bytes="
              << kMaxKernelCommandBytes << '\n'
              << "kernel_result_budget_bytes=" << kKernelResultBudgetBytes << '\n'
              << "kernel_result_canonical_bytes=" << kKernelResultBytes << '\n'
              << "state_snapshot_max_canonical_bytes="
              << kMaxStateSnapshotBytes << '\n'
              << "trading_state_size_bytes=" << sizeof(TradingState) << '\n'
              << "trading_state_budget_bytes=" << kTradingStateObjectBudgetBytes
              << '\n'
              << "decision_record_hot_cold_split_status="
                 "phase4_authorization_in_memory_ready\n";
    return 0;
}
