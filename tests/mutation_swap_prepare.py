import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root-build', type=Path, required=True)
    parser.add_argument('--raft-include', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    origin = root
    root = out/'source'
    root.mkdir()
    shutil.copytree(origin/'include', root/'include')
    (root/'tests').mkdir()
    for header in (origin/'tests').rglob('*.hpp'):
        target = root/header.relative_to(origin)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(header, target)
    raft_include = out/'raft-include'
    shutil.copytree(args.raft_include.resolve(), raft_include)
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-DVELOCITA_SWAP_PREPARE_SESSION_TEST_ONLY=1',
              '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I'+str(root/'include'),
              '-I'+str(root/'tests'), '-I'+str(raft_include)]
    sources = ['src/v2_swap_rest_codec.cpp', 'src/v2_swap_signing_stamp.cpp', 'src/v2_truemarkets_wire.cpp',
               'src/v2_digest.cpp', 'src/v2_swap_prepare_store.cpp', 'src/v2_gateway_link.cpp',
               'src/v2_auth_envelope.cpp', 'src/v2_node_protocol.cpp', 'src/v2_session_fence.cpp',
               'tests/v2_swap_prepare_store_tests.cpp', 'tests/v2_swap_prepare_session_tests.cpp']
    copied_sources = sources+['src/v2_swap_prepare_session.cpp', 'tests/mutation_swap_prepare.py']
    for source in copied_sources:
        target = root/source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin/source, target)
    library_names = ['velocita_v2_node', 'velocita_v2_replication', 'velocita_v2_kernel',
                     'velocita_v2_evaluator', 'velocita_core', 'willemt_raft', 'velocita_v2_contracts']
    (out/'libraries').mkdir()
    libraries = []
    for name in library_names:
        target = out/'libraries'/('lib'+name+'.a')
        shutil.copy2(args.root_build.resolve()/target.name, target)
        libraries.append(target)
    snapshots = [p for base in (root, raft_include, out/'libraries') for p in base.rglob('*') if p.is_file()]
    initial_hashes = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in snapshots}
    (out/'snapshot-sha256.json').write_text(json.dumps(initial_hashes, indent=2)+'\n')
    objects = {}
    for source in sources:
        obj = out/(Path(source).stem+'.o')
        subprocess.run(common+['-c', str(root/source), '-o', str(obj)], check=True)
        objects[source] = str(obj)
    shared = [objects[s] for s in sources[:4]]
    store_link = shared+[objects['tests/v2_swap_prepare_store_tests.cpp']]
    session_link = shared+[objects[s] for s in sources[4:9]]+[objects['tests/v2_swap_prepare_session_tests.cpp']]+[str(p) for p in libraries]+['-pthread']
    links = {'store': store_link, 'session': session_link}
    paths = {'store': root/'src/v2_swap_prepare_store.cpp', 'session': root/'src/v2_swap_prepare_session.cpp'}
    texts = {kind: path.read_text() for kind,path in paths.items()}
    for kind in links:
        exe = out/('baseline-'+kind)
        subprocess.run(common+[str(paths[kind])]+links[kind]+['-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], text=True, capture_output=True)
        (out/('green-'+kind+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
        print(result.stdout, end='')
    mutations = [
        ('session', 'inclusive_mapping_identity', 'config_.binding.mapping_identity!=fee_inclusive_prepare_mapping_identity()', 'false',
         'fee_inclusive_input_mapping', 'inclusive_mapping_identity_mismatch_refuses'),
        ('session', 'inclusive_legacy_recovery', 'config_.binding.mapping_identity==fee_inclusive_prepare_mapping_identity()', 'false',
         'fee_inclusive_input_mapping', 'inclusive_journal_cannot_recover_under_legacy_mapping'),
        ('session', 'inclusive_fee_omitted', 'input.quantity_atoms=intent.quote_input.value()+intent.fee_cap.value();',
         'input.quantity_atoms=intent.quote_input.value();',
         'fee_inclusive_input_mapping', '998000_principal_plus_2000_fee_requests_one_usdc_not_0998'),
        ('session', 'inclusive_principal_mismatch', 'prepared.quoted_input-prepared.quoted_fee!=intent.quote_input.value()', 'false',
         'fee_inclusive_input_mapping', 'fee_mismatch_never_reinterprets_principal_or_retries'),
        ('session', 'inclusive_sell_fee_cap', 'prepared.quoted_fee>intent.fee_cap.value()', 'false',
         'fee_inclusive_input_mapping', 'inclusive_sell_rejects_fee_above_cap'),
        ('store', 'skip_checksum', '!std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+8736) ||', '',
         'corrupt_and_missing_recovery', 'torn trailing checksum custody mapping payload and phase corruption refuse'),
        ('store', 'skip_binding', 'r.binding!=binding ||', '(r.binding!=binding && false) ||',
         'corrupt_and_missing_recovery', 'torn trailing checksum custody mapping payload and phase corruption refuse'),
        ('store', 'skip_payload_hash', 'p.payload_digest==sha256_digest(std::as_bytes(std::span(p.payload.data(),p.payload_size)))', 'true',
         'payload_and_request_guards', 'malformed payload quote handle length padding or digest cannot publish prepared state'),
        ('store', 'skip_request_hash', 'operation.request_digest==sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size)))', 'true',
         'payload_and_request_guards', 'create request digest must match canonical exact request'),
        ('store', 'skip_unknown_fence', 'latest_.phase==SwapPreparePhase::create_intent ||', '',
         'one_shot_and_prior_settlement', 'unknown create cannot be replaced even with caller predecessor claim'),
        ('store', 'skip_higher_effect', 'operation.effect.value()<=latest_.operation.effect.value() ||', '',
         'one_shot_and_prior_settlement', 'new prepare requires higher effect and exact finalized predecessor at authenticated index'),
        ('store', 'skip_prior_payload', 'prior->settlement.payload!=latest_.prepared.payload_digest ||', '',
         'one_shot_and_prior_settlement', 'new prepare requires higher effect and exact finalized predecessor at authenticated index'),
        ('store', 'skip_prior_custody', 'prior->settlement.custody!=binding_.allocation.custody ||', '',
         'one_shot_and_prior_settlement', 'new prepare requires higher effect and exact finalized predecessor at authenticated index'),
        ('store', 'skip_prior_finality', '!prior->settlement.finalized ||', '',
         'one_shot_and_prior_settlement', 'new prepare requires higher effect and exact finalized predecessor at authenticated index'),
        ('store', 'publish_before_sync', 'const auto bytes=encode(record);', 'const auto bytes=encode(record);latest_=record;',
         'durability_and_single_writer', 'failed durability never publishes usable prepared bytes'),
        ('store', 'skip_file_sync', '!checkpoint(Stage::temporary_sync) || !sync_fd(output.value)', 'false',
         'durability_and_single_writer', 'failed durability never publishes usable prepared bytes'),
        ('store', 'skip_directory_sync', '!checkpoint(Stage::directory_sync) || !sync_fd(directory_)', 'false',
         'durability_and_single_writer', 'failed durability never publishes usable prepared bytes'),
        ('store', 'skip_temporary', 'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::temporary_present;',
         'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::none;',
         'durability_and_single_writer', 'partial prepare response recovery stays closed'),
        ('session', 'skip_authority', 'outcome.reading.authority_domain!=config_.binding.authority_domain ||', '',
         'state_and_timing_fences', 'wrong domain index authentication phase or effect never creates order'),
        ('session', 'skip_index', 'outcome.reading.applied_index<minimum_index_ ||', '',
         'state_and_timing_fences', 'wrong domain index authentication phase or effect never creates order'),
        ('session', 'skip_phase', 'pending.phase!=swap::Phase::reserved ||', '',
         'state_and_timing_fences', 'wrong domain index authentication phase or effect never creates order'),
        ('session', 'skip_effect', 'intent.effect!=effect_ ||', '',
         'state_and_timing_fences', 'wrong domain index authentication phase or effect never creates order'),
        ('session', 'skip_kill_epoch', 'intent.kill_epoch!=swap.kill_epoch ||', '',
         'sell_input_and_expired_admission', 'stale reserved kill or executor generation never reaches create port'),
        ('session', 'skip_executor_generation', 'intent.executor_generation!=swap.executor_generation ||', '',
         'sell_input_and_expired_admission', 'stale reserved kill or executor generation never reaches create port'),
        ('session', 'skip_signing_expiry', 'event_time>=intent.sign_by', 'false',
         'sell_input_and_expired_admission', 'signing deadline admission refuses unsigned create at expiry'),
        ('session', 'skip_deadline', 'now>=deadline_', 'false',
         'state_and_timing_fences', 'late queued response or backward clock abandons prepare read without POST'),
        ('session', 'skip_backward_tick', 'now<last_tick_', 'false',
         'state_and_timing_fences', 'late queued response or backward clock abandons prepare read without POST'),
        ('session', 'skip_journal_freeze', 'if(store_.latest().ordinal!=ordinal_ || store_.latest().checksum!=checksum_)', 'if(false)',
         'lifecycle_and_mapping_guards', 'changed prepare journal invalidates outstanding state read'),
        ('session', 'skip_input_mapping', 'config_.mapping!=SwapPrepareInputMapping::fixture_principal_and_base_input', 'false',
         'lifecycle_and_mapping_guards', 'unadmitted input mapping refuses prepare session'),
        ('session', 'create_before_durable', 'if(store_.begin({effect_,intent_digest,request_digest,input},outcome.reading.applied_index,outcome.reading.state_digest,prior)!=SwapPrepareStoreCode::none)',
         '(void)port_.create(request);\n    if(store_.begin({effect_,intent_digest,request_digest,input},outcome.reading.applied_index,outcome.reading.state_digest,prior)!=SwapPrepareStoreCode::none)',
         'one_post_and_durable_payload', 'prepare intent is durable before exactly one create invocation'),
        ('session', 'ignore_initial_sync_failure', 'if(store_.begin({effect_,intent_digest,request_digest,input},outcome.reading.applied_index,outcome.reading.state_digest,prior)!=SwapPrepareStoreCode::none)',
         'if(((void)store_.begin({effect_,intent_digest,request_digest,input},outcome.reading.applied_index,outcome.reading.state_digest,prior),false))',
         'durable_write_failures', 'failed initial journal sync prevents create request'),
        ('session', 'ignore_payload_sync_failure', 'if(store_.complete(prepared,response_digest)!=SwapPrepareStoreCode::none)',
         'if(((void)store_.complete(prepared,response_digest),false))',
         'durable_write_failures', 'failed response journal sync never exposes usable payload'),
        ('session', 'ignore_reply_exposure', 'response.exposure!=transport::SwapHttpsExposure::response_received ||', '',
         'unknown_and_bad_response_never_retry', 'lost timeout malformed or unconfirmed create response remains unresolved'),
    ]
    results=[]
    for kind,name,old,new,case,assertion in mutations:
        source=texts[kind]
        if source.count(old)!=1:
            raise RuntimeError(name+': expected exactly one mutation anchor')
        changed=out/(name+'.cpp');changed.write_text(source.replace(old,new,1))
        exe=out/name
        compiled=subprocess.run(common+[str(changed)]+links[kind]+['-o',str(exe)],text=True,capture_output=True)
        if compiled.returncode:
            raise RuntimeError(name+': compile failure is not a kill: '+compiled.stderr)
        result=subprocess.run([str(exe),case],text=True,capture_output=True)
        killed=result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stdout
        results.append(dict(component=kind,mutation=name,case=case,assertion=assertion,killed=killed,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
        (out/'partial-results.json').write_text(json.dumps(results,indent=2)+'\n')
        if not killed:
            raise RuntimeError(name+': survived or failed elsewhere: '+result.stdout+result.stderr)
    final_hashes = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in snapshots}
    if initial_hashes != final_hashes:
        raise RuntimeError('snapshot changed during mutation run')
    report=dict(source_sha256={str(p.relative_to(root)):initial_hashes[str(p.relative_to(out))]
                               for p in snapshots if p.is_relative_to(root)},
                snapshot_sha256=initial_hashes, snapshot_unchanged=True,
                linked_library_sha256={str(p):initial_hashes[str(p.relative_to(out))] for p in libraries},results=results)
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'compile-args.json').write_text(json.dumps(common,indent=2)+'\n')
    print(f'Killed {len(results)} mutants at designated assertions')


if __name__=='__main__':
    main()
