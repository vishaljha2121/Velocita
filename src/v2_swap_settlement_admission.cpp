#include "velocita/v2/swap_settlement_admission.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/swap_submission_store.hpp"
#include <utility>
#include <limits>

namespace velocita::v2 {
SwapSettlementAdmissionResult admit_retained_swap_settlement(
    const SwapPrepareStore& preparation, const SwapSubmissionStore& submission,
    const SwapPrepareBinding& binding, const swap::Intent& intent, SwapFinalizedReader& reader,
    std::uint64_t request_id, std::uint64_t minimum_slot, std::string_view organization,
    std::string_view signer, std::span<const truemarkets::SolanaLookupEvidence> lookups,
    const truemarkets::SolanaAccountKey& owner, SwapFailureAccountingPolicy failure_policy) {
    std::array<std::byte,swap::max_record_bytes> bytes{};
    const auto encoded=swap::encode({swap::Role::strategy,intent.source_time,intent},bytes);
    if (encoded.error!=swap::CodecError::none || intent.binding!=binding.allocation ||
        intent.effect!=preparation.latest().operation.effect ||
        preparation.latest().operation.intent_digest!=sha256_digest(std::span(bytes).first(encoded.size))) return {};
    auto observed=collect_retained_swap_settlement(preparation,submission,binding,reader,request_id,
        minimum_slot,organization,signer,lookups,owner);
    SwapSettlementAdmissionResult result;
    switch(observed.code) {
        case SwapSettlementConnectionCode::identity_refused:
            result.code=SwapSettlementAdmissionCode::retained_identity_refused;break;
        case SwapSettlementConnectionCode::rpc_unresolved:
            result.code=SwapSettlementAdmissionCode::rpc_unresolved;break;
        case SwapSettlementConnectionCode::evidence_refused:
            result.code=SwapSettlementAdmissionCode::evidence_refused;break;
        case SwapSettlementConnectionCode::observed_failed_needs_accounting:
            result.code=SwapSettlementAdmissionCode::economic_refused;
            if(observed.finalized_read){
                result.failure=account_failed_swap(*observed.finalized_read,preparation.latest().prepared.payload_view(),
                    organization,signer,lookups,owner,failure_policy);
                const auto failed_transaction=solana_signature_transaction_digest(observed.evidence.signature);
                if(result.failure.code==FailedSwapAccountingCode::observed_external_fee_only && failed_transaction){
                    swap::Settlement failure;failure.effect=intent.effect;failure.custody=intent.binding.custody;
                    failure.payload=observed.evidence.original_payload_digest;failure.transaction=*failed_transaction;
                    failure.evidence=observed.evidence.response_digest;failure.finalized=true;failure.outcome=swap::Outcome::failed_without_charge;
                    result.verified=VerifiedSwapSettlement(failure);result.code=SwapSettlementAdmissionCode::admitted;
                }
            }
            break;
        case SwapSettlementConnectionCode::observed_needs_account_lifecycle:
            result.code=SwapSettlementAdmissionCode::lifecycle_unproven;
            result.required_evidence=complete_lifecycle_trace|account_control_continuity|
                fee_and_principal_attribution|owner_net_receipt_and_costs;
            if(observed.finalized_read) {
                result.lifecycle=replay_swap_settlement_lifecycle(*observed.finalized_read,
                    preparation.latest().prepared.payload_view(),organization,signer,lookups,owner);
                if(result.lifecycle.code==SwapLifecycleCode::replayed_effects_only) {
                    OwnerPropertyMeasurement measured;
                    measured.base_change_excluding_deposits=result.lifecycle.owner_wallet_lamports;
                    measured.deposit_change=result.lifecycle.owner_new_refundable_deposits;
                    measured.usdc_change=result.lifecycle.owner_usdc_atoms;
                    if(measured.base_change_excluding_deposits<=std::numeric_limits<std::int64_t>::max()-measured.deposit_change) {
                        measured.sol_change=measured.base_change_excluding_deposits+measured.deposit_change;
                        measured.code=OwnerAccountingCode::measured_declared_scope_only;
                        result.bounds=compare_observed_swap_bounds(intent,measured);
                        result.required_evidence=fee_and_principal_attribution|owner_net_receipt_and_costs;
                        const auto& replay=result.lifecycle;
                        bool mapping=replay.fee_attribution==SwapFeeAttributionCode::observed_abi_bound_usdc_fee &&
                            replay.route_side==intent.side && replay.fee_atoms<=intent.fee_cap.value() &&
                            preparation.latest().operation.input.side==intent.side &&
                            preparation.latest().operation.input.quantity_atoms==replay.route_input;
                        swap::Settlement settlement;
                        if(mapping && intent.side==Side::buy) {
                            mapping=replay.owner_usdc_atoms<0 && replay.owner_wallet_lamports>0 &&
                                intent.quote_input.value()<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-replay.fee_atoms &&
                                replay.route_principal_debit==intent.quote_input.value() &&
                                static_cast<std::uint64_t>(-replay.owner_usdc_atoms)==intent.quote_input.value()+replay.fee_atoms &&
                                replay.route_input==static_cast<std::uint64_t>(-replay.owner_usdc_atoms) &&
                                static_cast<std::uint64_t>(replay.owner_wallet_lamports)==replay.route_destination_credit;
                            if(mapping) {
                                settlement.base_credit=swap::BaseAtoms{static_cast<std::uint64_t>(replay.owner_wallet_lamports)};
                                settlement.quote_debit=swap::QuoteAtoms{static_cast<std::uint64_t>(-replay.owner_usdc_atoms)};
                            }
                        } else if(mapping && intent.side==Side::sell) {
                            mapping=replay.owner_wallet_lamports<0 && replay.owner_usdc_atoms>0 &&
                                replay.route_input==intent.base_input.value() && replay.route_principal_debit==intent.base_input.value() &&
                                static_cast<std::uint64_t>(-replay.owner_wallet_lamports)==intent.base_input.value() &&
                                static_cast<std::uint64_t>(replay.owner_usdc_atoms)<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-replay.fee_atoms &&
                                static_cast<std::uint64_t>(replay.owner_usdc_atoms)+replay.fee_atoms==replay.route_destination_credit;
                            if(mapping) {
                                settlement.base_debit=intent.base_input;
                                settlement.quote_credit=swap::QuoteAtoms{static_cast<std::uint64_t>(replay.owner_usdc_atoms)};
                            }
                        } else mapping=false;
                        const auto transaction=solana_signature_transaction_digest(observed.evidence.signature);
                        if(mapping && transaction && replay.owner_new_refundable_deposits==0 &&
                            result.bounds==ObservedSwapBounds::within_observed_net_bounds_only) {
                            settlement.effect=intent.effect;settlement.custody=intent.binding.custody;
                            settlement.payload=observed.evidence.original_payload_digest;
                            settlement.transaction=*transaction;
                            settlement.evidence=observed.evidence.response_digest;settlement.finalized=true;
                            settlement.outcome=swap::Outcome::succeeded;settlement.fee=swap::QuoteAtoms{replay.fee_atoms};
                            result.verified=VerifiedSwapSettlement(settlement);
                            result.code=SwapSettlementAdmissionCode::admitted;result.required_evidence=0;
                        } else result.code=SwapSettlementAdmissionCode::economic_refused;
                    }
                }
            }
            break;
    }
    result.observation=std::move(observed);
    return result;
}
}
