"""Isolated leader-read barrier mutations; only named assertion failures count."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root, build = Path(__file__).resolve().parents[1], args.root_build.resolve()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    selected = ['src/v2_replication.cpp', 'src/v2_bound_wal.cpp',
                'include/velocita/v2/leader_read_barrier.hpp',
                'tests/v2_replication_tests.cpp', 'tests/v2_swap_authority_storage_tests.cpp']
    before = {name: digest(root/name) for name in selected}
    for _ in range(10):
        time.sleep(0.2)
        current = {name: digest(root/name) for name in selected}
        if current == before:
            break
        before = current
    else:
        raise RuntimeError('source snapshot did not stabilize')
    snapshot = out/'snapshot'
    shutil.copytree(root/'include', snapshot/'include')
    (snapshot/'tests').mkdir()
    for source in (root/'tests').glob('*.hpp'):
        shutil.copy2(source, snapshot/'tests'/source.name)
    for name in selected:
        (snapshot/name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, snapshot/name)
    if any(digest(snapshot/name) != expected or digest(root/name) != expected for name, expected in before.items()):
        raise RuntimeError('source changed during snapshot')
    library_dir = out/'libraries'
    library_dir.mkdir()
    library_hashes, links = {}, {}
    for target in ('v2-swap-authority-storage-tests', 'v2-replication-tests'):
        link_path = build/'CMakeFiles'/(target+'.dir')/'link.txt'
        link_raw = link_path.read_text()
        (out/(target+'.link.txt')).write_text(link_raw)
        paths = [Path(token) if Path(token).is_absolute() else build/token
                 for token in shlex.split(link_raw) if token.endswith('.a')]
        if not paths:
            raise RuntimeError('no static library link metadata')
        copied = []
        for path in paths:
            target_path = library_dir/path.name
            expected = digest(path)
            if path.name not in library_hashes:
                shutil.copy2(path, target_path)
                library_hashes[path.name] = expected
            if digest(target_path) != expected or digest(path) != expected:
                raise RuntimeError('library changed during snapshot')
            copied.append(str(target_path))
        links[target] = copied
    flags_path = build/'CMakeFiles/v2-replication-tests.dir/flags.make'
    flags = {}
    for line in flags_path.read_text().splitlines():
        if ' = ' in line:
            name, value = line.split(' = ', 1)
            flags[name] = shlex.split(value)
    includes = [flag.replace(str(root/'include'), str(snapshot/'include'))
                for flag in flags['CXX_INCLUDES']]
    common = ['c++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror'] + flags['CXX_DEFINES'] + includes + ['-I'+str(snapshot/'tests')]
    commands = []

    def command(argv, name, env=None, timeout=180):
        commands.append(argv)
        result = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        return result

    objects = {}
    for group, source in [('storage','tests/v2_swap_authority_storage_tests.cpp'), ('replication','tests/v2_replication_tests.cpp')]:
        obj = out/(group+'-tests.o')
        result = command(common+['-c',str(snapshot/source),'-o',str(obj)], group+'-test-build')
        if result.returncode:
            raise RuntimeError('baseline test compilation failed: '+group)
        objects[group] = str(obj)

    def build_case(name, group, source, include_override=None):
        binary = out/name
        target = 'v2-swap-authority-storage-tests' if group == 'storage' else 'v2-replication-tests'
        flags = common[:]
        if include_override:
            flags.insert(1, '-I'+str(include_override))
        result = command(flags + [objects[group], str(source)] + links[target] + ['-pthread', '-o', str(binary)], name+'-build')
        if result.returncode:
            raise RuntimeError('compilation is not a mutation kill: '+name)
        return binary

    def run_case(binary, case, name):
        scratch = out/(name+'-scratch')
        scratch.mkdir()
        return command([str(binary), case], name, env=dict(os.environ, TMPDIR=str(scratch), VELOCITA_TEST_SEED='1'), timeout=90)

    baselines = {}
    for group, source, case in [('storage','src/v2_bound_wal.cpp','leader_wal_roundtrip_and_refusal'), ('replication','src/v2_replication.cpp','leader-read-quorum')]:
        binary = build_case('baseline-'+group, group, snapshot/source)
        result = run_case(binary, case, 'baseline-'+group+'-run')
        expected = 'PASS '+case+'\n' if group == 'storage' else case+' passed\n'
        if result.returncode != 0 or expected not in result.stdout:
            raise RuntimeError('baseline failed: '+group)
        baselines[group] = dict(exit=result.returncode, case=case, stdout=result.stdout)
    mutations = [
        ('ignore_marker_domain','storage','include/velocita/v2/leader_read_barrier.hpp',
         'std::equal(payload.begin(), payload.end(), domain.bytes.begin())', 'true',
         'leader_wal_roundtrip_and_refusal', 'FAIL leader_wal_roundtrip_and_refusal: leader103 WAL refuses wrong type domain size and authority-shaped payload\n'),
        ('ordinary_log_class','replication','src/v2_replication.cpp',
         'bool control = entry->type == kLeaderReadBarrierEntryType;', 'bool control = false;',
         'leader-read-quorum', 'V2 replication test failed: read barrier appends one protected record without result reservation\n'),
        ('admit_uncommitted_tail','replication','src/v2_replication.cpp',
         'if (tail && tail->term == term) return ProposeStatus::persistence_busy;',
         'if (tail && tail->term == term) return ProposeStatus::admitted;',
         'leader-read-quorum', 'V2 replication test failed: isolated read barrier stays uncommitted\n')]
    results = []
    for name, group, source, old, new, case, expected in mutations:
        original = (snapshot/source).read_text()
        if original.count(old) != 1:
            raise RuntimeError('nonunique mutation: '+name)
        override = None
        if source.endswith('.hpp'):
            override = out/(name+'-include')
            changed = override/'velocita/v2/leader_read_barrier.hpp'
            changed.parent.mkdir(parents=True)
            changed.write_text(original.replace(old,new,1))
            production = snapshot/'src/v2_bound_wal.cpp'
        else:
            changed = out/(name+'.cpp')
            changed.write_text(original.replace(old,new,1))
            production = changed
        binary = build_case(name,group,production,override)
        result = run_case(binary,case,name+'-run')
        killed = result.returncode == 1 and expected in (result.stdout+result.stderr)
        results.append(dict(name=name,case=case,expected_assertion=expected.strip(),exit=result.returncode,killed=killed,stdout=result.stdout,stderr=result.stderr))
    unchanged = all(digest(root/name) == expected for name,expected in before.items())
    snapshot_hashes = {str(path.relative_to(snapshot)):digest(path) for path in snapshot.rglob('*') if path.is_file()}
    report = dict(baselines=baselines, mutations=results, source_sha256=before, snapshot_sha256=snapshot_hashes,
                  copied_library_sha256=library_hashes, source_unchanged=unchanged,
                  qualification='local isolated release-library linkage; not native deployment or full-library sanitizer qualification')
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    if not unchanged or not all(row['killed'] for row in results):
        raise RuntimeError('mutation survived, failed elsewhere, or source changed')
    print('2 named baselines passed; 3/3 designated mutation assertions killed; workspace sources unchanged')


if __name__ == '__main__':
    main()
