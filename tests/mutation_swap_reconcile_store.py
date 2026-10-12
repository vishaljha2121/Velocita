import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root-build', type=Path)
    parser.add_argument('--raft-include', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = ['tests/v2_swap_reconcile_store_tests.cpp', 'src/v2_swap_result_watermark.cpp', 'src/v2_digest.cpp']
    common = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
              '-DVELOCITA_TARGET_CACHE_LINE_SIZE=128',
              '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I'+str(root/'include')]
    objects = []
    for source in sources:
        obj = out/(Path(source).stem+'.o')
        subprocess.run(common+['-c', str(root/source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    libraries = []
    link = objects
    path = root/'src/v2_swap_reconcile_store.cpp'
    source = path.read_text()
    baseline = out/'baseline'
    subprocess.run(common+[str(path)]+link+['-o', str(baseline)], check=True)
    green = subprocess.run([str(baseline)], capture_output=True, text=True)
    (out/'green.log').write_text(green.stdout+green.stderr)
    if green.returncode:
        raise RuntimeError(green.stdout+green.stderr)
    mutations = [
        ('skip_checksum', '!std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+120) ||', '',
         'missing_torn_corrupt_and_binding', 'torn corrupt wrong binding invalid index and trailing bytes refuse'),
        ('skip_incarnation', '!std::equal(binding.incarnation.bytes.begin(),binding.incarnation.bytes.end(),bytes.begin()+24) ||', '',
         'missing_torn_corrupt_and_binding', 'torn corrupt wrong binding invalid index and trailing bytes refuse'),
        ('skip_authority_domain', '!std::equal(binding.authority_domain.bytes.begin(),binding.authority_domain.bytes.end(),bytes.begin()+56)', 'false',
         'missing_torn_corrupt_and_binding', 'torn corrupt wrong binding invalid index and trailing bytes refuse'),
        ('allow_conflicting_duplicate', '(result.index==latest_.result.index && result.result_digest!=latest_.result.result_digest)', 'false',
         'duplicate_monotonic_and_exhaustion', 'same index different digest closes without overwrite'),
        ('allow_decreasing_index', 'result.index<latest_.result.index ||', '',
         'duplicate_monotonic_and_exhaustion', 'lower committed index closes'),
        ('ignore_temporary', 'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::temporary_present;',
         'if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::none;',
         'fault_boundaries_never_publish', 'leftover temporary refuses recovery without cleanup'),
        ('leave_watermark_live_on_error', '(void)watermark_.publish(0);', '(void)watermark_.index();',
         'fault_boundaries_never_publish', 'failed durability never publishes higher index and closes'),
        ('publish_before_durable', 'const auto bytes=encode(result,ordinal);',
         'const auto bytes=encode(result,ordinal); if(latest_.ordinal!=0)(void)watermark_.publish(result.index);',
         'fault_boundaries_never_publish', 'failed durability never publishes higher index and closes'),
        ('skip_file_sync_gate', '!checkpoint(Stage::temporary_sync) || !sync_fd(output.value)', 'false',
         'fault_boundaries_never_publish', 'failed durability never publishes higher index and closes'),
        ('skip_directory_sync_gate', '!checkpoint(Stage::directory_sync) || !sync_fd(directory_)', 'false',
         'fault_boundaries_never_publish', 'failed durability never publishes higher index and closes'),
        ('skip_parent_sync_gate', 'if(create && (!checkpoint(Stage::parent_sync) || !sync_fd(parent_)))',
         'if(false && create && (!checkpoint(Stage::parent_sync) || !sync_fd(parent_)))',
         'fault_boundaries_never_publish', 'new directory parent durability failure closes'),
        ('skip_recovery_sync_gate', '!checkpoint(Stage::recovery_sync) || !sync_fd(file_) || !sync_fd(directory_)', 'false',
         'fault_boundaries_never_publish', 'recovery fsync failure never restores watermark'),
        ('allow_hardlinks', ' && st.st_nlink==1', '',
         'filesystem_safety_and_single_writer', 'insecure symlink fifo hardlink and permissions refuse'),
        ('allow_public_record', '(st.st_mode&07777)==0600 &&', '',
         'filesystem_safety_and_single_writer', 'insecure symlink fifo hardlink and permissions refuse'),
        ('ignore_inode_replacement', 'named.st_dev==opened.st_dev && named.st_ino==opened.st_ino &&', '',
         'invalid_input_and_replaced_file', 'file replacement while owned refuses'),
        ('ignore_external_publish', 'if(watermark_.index()!=latest_.result.index)', 'if(false)',
         'invalid_input_and_replaced_file', 'external watermark update cannot bypass durable owner'),
    ]
    results = []
    for name, old, new, case, assertion in mutations:
        if source.count(old)!=1:
            raise RuntimeError(f'{name}: requires exactly one source match')
        changed = out/(name+'.cpp')
        changed.write_text(source.replace(old, new, 1))
        binary = out/name
        built = subprocess.run(common+[str(changed)]+link+['-o', str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f'{name}: compilation is not a kill: {built.stderr}')
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        killed = result.returncode==1 and f'FAIL {case}: {assertion}\n' in result.stdout
        results.append(dict(mutation=name, case=case, assertion=assertion, killed=killed,
                            exit=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out/'results.json').write_text(json.dumps({'results': results}, indent=2)+'\n')
        if not killed:
            raise RuntimeError(f'{name}: survived or failed elsewhere: {result.stdout} {result.stderr}')
    paths = [root/p for p in sources]+[path, root/'include/velocita/v2/swap_reconcile_store.hpp']
    report = dict(source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                  linked_library_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in libraries},
                  results=results)
    (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(green.stdout, end='')
    print(f'Killed {len(results)} mutants at their designated assertions')


if __name__ == '__main__':
    main()
