"""Instrument the new connections; freeze and hash their Release link dependencies."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess


MUTATIONS = [
    ('signing', 'skip_durable_intent', 'if(journal_.begin(identity,index)!=SwapSubmissionCode::none)', 'if(false)',
     'durability', 'gateway fsync failure prevents signing IPC'),
    ('signing', 'changed_recovery_index', 'saved.committed_index,get64', 'saved.committed_index+1,get64',
     'restart', 'restart retrieves original durable request'),
    ('signing', 'skip_signer_domain', 'j.latest().binding!=swap_signing_connection_binding(c) ||', '',
     'bounds', 'different signer pin cannot reuse request journal'),
    ('signing', 'wrong_execute_hash', 'result.identity.execution_request=sha256_digest(std::as_bytes(std::span(result.request.body.data(),result.request.body_size)));',
     'result.identity.execution_request=result.identity.payload;', 'restart', 'exact signed bytes feed canonical execute identity'),
    ('settlement', 'skip_effect', 'sent.identity.effect!=retained.operation.effect ||', '',
     'retained_connection', 'wrong retained identity or missing transaction never reaches RPC'),
    ('settlement', 'skip_payload', 'sent.identity.payload!=p.payload_digest ||', '',
     'retained_connection', 'wrong retained identity or missing transaction never reaches RPC'),
    ('settlement', 'skip_ordering', 'sent.committed_index<=retained.committed_index ||', '',
     'retained_connection', 'wrong retained identity or missing transaction never reaches RPC'),
    ('settlement', 'skip_changed_journal', ' || submission.latest().checksum!=sent.checksum', '',
     'retained_connection', 'journal changing during RPC invalidates evidence handoff'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.root_build.resolve()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(root/'include', out/'include')
    shutil.copytree(root/'tests', out/'tests', ignore=shutil.ignore_patterns('__pycache__'))
    (out/'objects').mkdir()
    dependencies = {}
    configurations = {}
    sources = {}
    for component in ('signing', 'settlement'):
        target = f'v2-swap-{component}-connection-tests'
        directory = build/'CMakeFiles'/f'{target}.dir'
        link = shlex.split((directory/'link.txt').read_text())
        copied = []
        object_index = None
        for token in link:
            path = build/token
            if path.is_file() and path.suffix in ('.o', '.a'):
                name = hashlib.sha256(str(path).encode()).hexdigest()[:16]+path.suffix
                dest = out/'objects'/name
                if not dest.exists():
                    shutil.copyfile(path, dest)
                dependencies[str(path)] = hashlib.sha256(dest.read_bytes()).hexdigest()
                if token.endswith(f'/src/v2_swap_{component}_connection.cpp.o'):
                    object_index = len(copied)
                copied.append(str(dest))
            else:
                copied.append(token)
        assert object_index is not None
        flags = []
        for line in (directory/'flags.make').read_text().splitlines():
            if line.startswith(('CXX_DEFINES =', 'CXX_INCLUDES =', 'CXX_FLAGS =')):
                flags += shlex.split(line.split('=', 1)[1])
        flags = [v.replace(str(root/'include'), str(out/'include')) for v in flags if v != '-O3']
        flags += ['-O1', '-g', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        text = (root/f'src/v2_swap_{component}_connection.cpp').read_text()
        sources[component] = text
        (out/f'original-{component}.cpp').write_text(text)
        configurations[component] = (copied, flags, object_index)

    report = dict(dependencies=dependencies, source_sha256={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}, results=[])

    def run(component, name, source, case=None):
        link, flags, at = configurations[component]
        changed = out/f'{name}.cpp'
        changed.write_text(source)
        obj, exe = out/f'{name}.o', out/name
        subprocess.run([link[0], *flags, '-c', str(changed), '-o', str(obj)], check=True, capture_output=True)
        command = list(link)
        command[at] = str(obj)
        command[command.index('-o')+1] = str(exe)
        command += ['-fsanitize=address,undefined']
        subprocess.run(command, check=True, capture_output=True)
        result = subprocess.run([str(exe), *([case] if case else [])], capture_output=True, text=True, timeout=20)
        (out/f'{name}.log').write_text(result.stdout+result.stderr)
        return result

    for component, source in sources.items():
        result = run(component, 'baseline-'+component, source)
        if result.returncode:
            raise RuntimeError(result.stdout+result.stderr)
    for component, name, old, new, case, assertion in MUTATIONS:
        source = sources[component]
        if source.count(old) != 1:
            raise RuntimeError(name+': mutation anchor is not unique')
        result = run(component, name, source.replace(old, new, 1), case)
        killed = result.returncode == 1 and f'FAIL {case}: {assertion}\n' in result.stdout
        report['results'].append(dict(name=name, killed=killed, assertion=assertion, stdout=result.stdout, stderr=result.stderr))
        (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
        if not killed:
            raise RuntimeError(name+': not a named assertion kill: '+result.stdout+result.stderr)
    print(f'{len(MUTATIONS)}/{len(MUTATIONS)} named connection mutation kills')


if __name__ == '__main__':
    main()
