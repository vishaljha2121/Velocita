import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
F='v2-swap-failed-settlement-tests'
R='v2-swap-gateway-runtime-tests'
P='v2-swap-runtime-production-tests'
MUTANTS=[
 ('reducer_zero_transaction','src/v2_swap.cpp','x.evidence.is_zero() || x.transaction.is_zero() != (x.outcome == Outcome::expired_unexecuted)) return Code::invalid;','x.evidence.is_zero()) return Code::invalid;',F,['expired_reducer'],'expired with a transaction refused'),
 ('reducer_expired_branch','src/v2_swap.cpp','if (x.outcome == Outcome::failed_without_charge || x.outcome == Outcome::expired_unexecuted) {\n        if (x.base_debit','if (x.outcome == Outcome::failed_without_charge) {\n        if (x.base_debit',F,['expired_admission'],'kernel accepts expired settlement from unknown'),
 ('reducer_zero_dedupe','src/v2_swap.cpp','if (!x.transaction.is_zero() && x.transaction == s.settlements[n].transaction) return conflict(s, x);','if (x.transaction == s.settlements[n].transaction) return conflict(s, x);',F,['expired_reducer'],'window settlement'),
 ('snapshot_zero_transaction','src/v2_swap.cpp','x.custody != s.opening.binding.custody || x.payload.is_zero() || x.evidence.is_zero() ||\n            x.transaction.is_zero() != (x.outcome == Outcome::expired_unexecuted)) return false;','x.custody != s.opening.binding.custody || x.payload.is_zero() || x.evidence.is_zero()) return false;',F,['expired_reducer'],'snapshot with expired and a transaction refused'),
 ('snapshot_zero_dedupe','src/v2_swap.cpp','for (std::size_t k = 0; k < n; ++k) if (!x.transaction.is_zero() && x.transaction == s.settlements[k].transaction) return false;','for (std::size_t k = 0; k < n; ++k) if (x.transaction == s.settlements[k].transaction) return false;',F,['expired_reducer'],'evicted history with several expired entries revalidates'),
 ('snapshot_expired_replay','src/v2_swap.cpp','if (x.outcome == Outcome::failed_without_charge || x.outcome == Outcome::expired_unexecuted) {\n            if (x.base_debit','if (x.outcome == Outcome::failed_without_charge) {\n            if (x.base_debit',F,['expired_admission'],'only the reservation is released and the snapshot revalidates'),
 ('fence_old_rule','include/velocita/v2/swap_settlement_fence.hpp','settlement.transaction.is_zero()!=(settlement.outcome==swap::Outcome::expired_unexecuted))return false;','settlement.transaction.is_zero())return false;',F,['expired_admission'],'gateway and signer predecessor fence accepts the expired settlement'),
 ('fence_any_zero','include/velocita/v2/swap_settlement_fence.hpp','settlement.transaction.is_zero()!=(settlement.outcome==swap::Outcome::expired_unexecuted))return false;','false)return false;',F,['expired_admission'],'fence refuses a success without a transaction'),
 ('resolution_old_rule','src/v2_swap_submission_resolution.cpp','!candidate.finalized || candidate.transaction.is_zero()!=expired || candidate.evidence.is_zero() ||','!candidate.finalized || candidate.transaction.is_zero() || candidate.evidence.is_zero() ||',R,['19'],'FAIL'),
 ('prepare_predecessor_old_rule','src/v2_swap_prepare_session.cpp','settled.transaction.is_zero()!=(settled.outcome==swap::Outcome::expired_unexecuted))return finish(SwapPrepareSessionStatus::needs_reconciliation);','settled.transaction.is_zero())return finish(SwapPrepareSessionStatus::needs_reconciliation);',R,['18'],'FAIL'),
 ('prepare_store_old_rule','src/v2_swap_prepare_store.cpp','prior->settlement.transaction.is_zero()!=(prior->settlement.outcome==swap::Outcome::expired_unexecuted))return fail(Code::bad_transition);','prior->settlement.transaction.is_zero())return fail(Code::bad_transition);',R,['18'],'FAIL'),
 ('startup_old_rule','src/v2_swap_runtime_production.cpp','prior.transaction.is_zero()==(prior.outcome==swap::Outcome::expired_unexecuted) &&','!prior.transaction.is_zero() &&',P,[],'an expired unexecuted predecessor restarts normally'),
 ('startup_any_zero','src/v2_swap_runtime_production.cpp','prior.transaction.is_zero()==(prior.outcome==swap::Outcome::expired_unexecuted) &&','true &&',P,[],'a succeeded settlement without a transaction never counts as settled'),
 ('venue_map_failed','src/v2_swap_runtime_production.cpp','read.detail.status==truemarkets::RestOrderStatus::failed,!read.detail.tx_hash.empty(),','true,!read.detail.tx_hash.empty(),',P,[],'canceled is not failed'),
 ('venue_map_hash','src/v2_swap_runtime_production.cpp','read.detail.status==truemarkets::RestOrderStatus::failed,!read.detail.tx_hash.empty(),','read.detail.status==truemarkets::RestOrderStatus::failed,false,',P,[],'reported hash is never ignored'),
 ('admit_failed','src/v2_swap_settlement_admission.cpp','if(!order.terminal || !order.failed || order.transaction_reported','if(!order.terminal || order.transaction_reported',F,['expired_admission'],'keeps the reservation'),
 ('admit_executed','src/v2_swap_settlement_admission.cpp','order.executed_base_atoms!=std::int64_t{0} ||','false ||',F,['expired_admission'],'keeps the reservation'),
 ('admit_fee','src/v2_swap_settlement_admission.cpp','order.fee_quote_atoms!=std::int64_t{0} ||','false ||',F,['expired_admission'],'keeps the reservation'),
 ('admit_hash','src/v2_swap_settlement_admission.cpp','if(!order.terminal || !order.failed || order.transaction_reported','if(!order.terminal || !order.failed',F,['expired_admission'],'keeps the reservation'),
 ('admit_leaves','src/v2_swap_settlement_admission.cpp','!order.leaves_base_atoms || order.response_digest.is_zero())return result;','order.response_digest.is_zero())return result;',F,['expired_admission'],'keeps the reservation'),
 ('admit_attested','src/v2_swap_settlement_admission.cpp','!order.leaves_base_atoms || order.response_digest.is_zero())return result;','!order.leaves_base_atoms)return result;',F,['expired_admission'],'keeps the reservation'),
 ('admit_terminal','src/v2_swap_settlement_admission.cpp','if(!order.terminal || !order.failed','if(!order.failed',F,['expired_admission'],'keeps the reservation'),
 ('admit_known_transaction','src/v2_swap_settlement_admission.cpp','sent.transaction!=SwapSubmissionTransaction{} || !p.handle.length','!p.handle.length',F,['expired_admission'],'a known venue transaction must use finalized settlement'),
 ('admit_chain_required','src/v2_swap_settlement_admission.cpp','if(chain.code!=UnexecutedProofCode::proven || chain.evidence.is_zero() ||','if(chain.evidence.is_zero() && false ||',F,['expired_admission'],'venue failure alone never releases'),
 ('chain_blockhash_valid','src/v2_swap_settlement_evidence.cpp','if(*value!="false")return finish(Code::blockhash_still_valid);','',F,['chain_refusals'],'valid_blockhash_never_resolves'),
 ('chain_margin','src/v2_swap_settlement_evidence.cpp','|| *block_time*1000<stamp+kUnexecutedFinalityMarginMs)return finish(Code::not_final_long_enough);',')return finish(Code::not_final_long_enough);',F,['chain_refusals'],'finalized_view_too_close_to_creation_refused'),
 ('chain_landed','src/v2_swap_settlement_evidence.cpp','            return finish(Code::landed);','            {}',F,['chain_refusals'],'matching_landed_message_refused'),
 ('chain_incomplete','src/v2_swap_settlement_evidence.cpp','if(!complete)return finish(Code::history_incomplete);','',F,['chain_refusals'],'eight_full_pages_without_reaching_creation_refused'),
 ('chain_skip_after_expiry','src/v2_swap_settlement_evidence.cpp','if(*entry_slot>*slot)continue;','',F,['chain_proven'],'expired_blockhash_and_no_matching_history_proven'),
 ('chain_time_completion','src/v2_swap_settlement_evidence.cpp','if(*entry_time*1000<earliest){complete=true;break;}','',F,['chain_refusals'],'second_page_reaches_creation_and_completes'),
 ('chain_null_time','src/v2_swap_settlement_evidence.cpp','if(!signature || !entry_slot || !entry_time || *entry_time>maximum/1000)return finish(Code::malformed);','if(!signature || !entry_slot)return finish(Code::malformed);',F,['chain_refusals'],'missing_block_time_refused'),
 ('chain_provider','src/v2_swap_settlement_evidence.cpp','if(port.provider_identity()!=provider || !out.response.transport_complete','if(!out.response.transport_complete',F,['chain_refusals'],'provider_change_during_proof_refused'),
 ('chain_rpc_error','src/v2_swap_settlement_evidence.cpp','number(out.document,"id")!=id || json_member_raw(out.document,"error"))return false;','number(out.document,"id")!=id)return false;',F,['chain_refusals'],'rpc_error_refused'),
 ('chain_min_context','src/v2_swap_settlement_evidence.cpp','",\\"minContextSlot\\":"+std::to_string(*slot);','"";',F,['chain_proven'],'expired_blockhash_and_no_matching_history_proven'),
 ('template_finalized','src/v2_swap_runtime_https.cpp','constexpr std::string_view blockhash_suffix="\\",{\\"commitment\\":\\"finalized\\"}]}";','constexpr std::string_view blockhash_suffix="\\",{\\"commitment\\":\\"processed\\"}]}";',F,['rpc_templates'],'blockhash template'),
 ('template_history_bound','src/v2_swap_runtime_https.cpp','constexpr std::string_view history_options="\\",{\\"commitment\\":\\"finalized\\",\\"limit\\":64,\\"minContextSlot\\":";','constexpr std::string_view history_options="\\",{\\"commitment\\":\\"finalized\\",\\"limit\\":";',F,['rpc_templates'],'history template'),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else ''


def objects(build,relative):
    name=Path(relative).name+'.o'
    return {str(p):sha(p) for p in build.glob('CMakeFiles/*/'+str(Path(relative).parent)+'/'+name)}


def build_target(build,target):
    result=subprocess.run(['cmake','--build',str(build),'-j','8','--target',target],capture_output=True,text=True)
    if result.returncode:raise RuntimeError('build failed for '+target+': '+result.stdout[-2000:]+result.stderr[-2000:])
    return result.stdout


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--only')
    args=parser.parse_args()
    build=args.build.resolve()
    for target in (F,R,P):build_target(build,target)
    results=[]
    for name,relative,old,new,target,argv,assertion in MUTANTS:
        if args.only and name!=args.only:continue
        path=ROOT/relative;original=path.read_text()
        if original.count(old)!=1:raise RuntimeError(name+' anchor must be unique')
        before_objects=objects(build,relative);before_binary=sha(build/target)
        time.sleep(1.1)
        path.write_text(original.replace(old,new))
        try:
            time.sleep(1.1)
            log=build_target(build,target)
            after_objects=objects(build,relative);after_binary=sha(build/target)
            recompiled=(any(after_objects.get(k)!=v for k,v in before_objects.items()) if before_objects else 'Building CXX object' in log) and after_binary!=before_binary
            if not recompiled:raise RuntimeError(name+' did not recompile into '+target)
            tested=subprocess.run([str(build/target),*argv],capture_output=True,text=True,timeout=900)
            output=tested.stdout+tested.stderr
            killed=tested.returncode!=0 and assertion in output
            results.append(dict(name=name,file=relative,target=target,argv=argv,assertion=assertion,killed=killed,returncode=tested.returncode,
                output=output[-1500:],mutant_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),binary_sha256=after_binary,
                building_lines=log.count('Building CXX object')))
            print(name,'KILLED' if killed else 'SURVIVED',flush=True)
        finally:
            time.sleep(1.1)
            path.write_text(original)
        args.output.write_text(json.dumps(results,indent=2)+'\n')
    for target in (F,R,P):build_target(build,target)
    for target,argv in ((F,[]),(P,[])):
        if subprocess.run([str(build/target),*argv],capture_output=True,text=True,timeout=900).returncode:raise RuntimeError('restored build must pass: '+target)
    return 0 if results and all(row['killed'] for row in results) else 1


if __name__=='__main__':
    raise SystemExit(main())
