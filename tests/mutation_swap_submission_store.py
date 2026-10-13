import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = ['tests/v2_swap_submission_store_tests.cpp', 'src/v2_digest.cpp']
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128', '-fsanitize=address,undefined',
              '-fno-omit-frame-pointer', '-I'+str(root/'include')]
    objects = []
    for source in sources:
        obj = out/(Path(source).stem+'.o')
        subprocess.run(common+['-c', str(root/source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    path = root/'src/v2_swap_submission_store.cpp'
    source = path.read_text()
    baseline = out/'baseline'
    subprocess.run(common+[str(path)]+objects+['-o', str(baseline)], check=True)
    green = subprocess.run([str(baseline)], capture_output=True, text=True)
    (out/'green.log').write_text(green.stdout+green.stderr)
    if green.returncode:
        raise RuntimeError(green.stdout+green.stderr)
    identity_case = 'no_resend_and_identity_guards'
    identity_assert = 'replay active overlap wrong identity empty evidence stale index refuse without rewrite'
    corruption_case = 'recovery_corruption_and_binding'
    corruption_assert = 'short trailing checksum binding phase padding index and handle corruption refuse'
    durability_assert = 'durability failure never grants execution or publishes intent'
    mutations = [
        ('active_may_resend', 'latest_.phase==SwapSubmissionPhase::execute_intent || identity.effect.value()',
         'false || identity.effect.value()', identity_case, identity_assert),
        ('wrong_resolution_identity', 'resolution.identity!=latest_.identity ||', '', identity_case, identity_assert),
        ('stale_resolution', 'resolution.committed_index<=latest_.committed_index ||', '', identity_case, identity_assert),
        ('invalid_effect_floor', 'identity.effect.value()<=latest_.identity.effect.value() ||', '', identity_case,
         'invalid effect index handle padding length and execution digest cannot authorize send'),
        ('invalid_attempt_index', 'committed_attempt_index<=latest_.committed_index', 'false', identity_case,
         'invalid effect index handle padding length and execution digest cannot authorize send'),
        ('conflicting_transaction', 'if(latest_.transaction!=transaction)return fail(Code::bad_transition);', '', identity_case,
         'conflicting transaction cannot overwrite first identity'),
        ('skip_checksum', '!std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+432) ||', '', corruption_case, corruption_assert),
        ('skip_incarnation', '!std::equal(binding.incarnation.bytes.begin(),binding.incarnation.bytes.end(),bytes.begin()+40) ||', '', corruption_case, corruption_assert),
        ('skip_authority', '!std::equal(binding.authority_domain.bytes.begin(),binding.authority_domain.bytes.end(),bytes.begin()+72) ||', '', corruption_case, corruption_assert),
        ('skip_handle_digest', 'sha256_digest(std::span(identity.handle).first(identity.handle_size))==identity.handle_digest',
         'true', corruption_case, corruption_assert),
        ('ignore_temporary', 'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::temporary_present;',
         'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::none;', 'durability_failures',
         'partial intent refuses ambiguous recovery'),
        ('early_publish', 'const auto bytes=encode(record);', 'const auto bytes=encode(record); latest_=record;', 'durability_failures', durability_assert),
        ('skip_file_sync', '!checkpoint(Stage::temporary_sync) || !sync_fd(output.value)', 'false', 'durability_failures', durability_assert),
        ('skip_directory_sync', '!checkpoint(Stage::directory_sync) || !sync_fd(directory_)', 'false', 'durability_failures', durability_assert),
        ('skip_parent_sync', 'if(create && (!checkpoint(Stage::parent_sync) || !sync_fd(parent_)))',
         'if(false && create && (!checkpoint(Stage::parent_sync) || !sync_fd(parent_)))', 'durability_failures', 'bootstrap parent sync failure refuses'),
        ('skip_recovery_sync', '!checkpoint(Stage::recovery_sync) || !sync_fd(file_) || !sync_fd(directory_)',
         'false', 'durability_failures', 'recovery sync failure refuses'),
        ('allow_public_file', '(st.st_mode&07777)==0600 &&', '', 'filesystem_guards',
         'public linked symlink fifo directory records refuse'),
        ('allow_hardlinks', ' && st.st_nlink==1', '', 'filesystem_guards',
         'public linked symlink fifo directory records refuse'),
        ('ignore_inode', 'named.st_dev==opened.st_dev && named.st_ino==opened.st_ino &&', '', 'filesystem_guards',
         'named record replacement refuses'),
        ('retry_failed_bootstrap', 'status_=code;started_=true;', 'status_=code;', corruption_case,
         'invalid bootstrap cannot retry implicitly'),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old) != 1:
            raise RuntimeError(f'{name}: requires exactly one match')
        changed = out/(name+'.cpp')
        changed.write_text(source.replace(old, new, 1))
        binary = out/name
        built = subprocess.run(common+[str(changed)]+objects+['-o', str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f'{name}: compilation is not a kill: {built.stderr}')
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode == 1 and f'FAIL {case}: {assertion}\n' in result.stdout
        results.append(dict(mutation=name, case=case, assertion=assertion, killed=killed,
                            exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out/'results.json').write_text(json.dumps({'results': results}, indent=2)+'\n')
        if not killed:
            raise RuntimeError(f'{name}: survived or failed elsewhere: {result.stdout} {result.stderr}')
    paths = [root/p for p in sources]+[path, root/'include/velocita/v2/swap_submission_store.hpp']
    report = dict(source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, results=results)
    (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'compile-args.json').write_text(json.dumps(common, indent=2)+'\n')
    print(green.stdout, end='')
    print(f'Killed {len(results)} mutants at designated assertions')


if __name__ == '__main__':
    main()
