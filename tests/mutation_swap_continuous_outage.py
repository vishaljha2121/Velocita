import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build, source, output = args.build.resolve(), args.source.resolve(), args.output.resolve()
    target = build / 'CMakeFiles/v2-swap-continuous-drill.dir'
    link = shlex.split((target / 'link.txt').read_text())
    settings = dict(line.split(' = ', 1) for line in (target / 'flags.make').read_text().splitlines()
                    if line.startswith(('CXX_DEFINES = ', 'CXX_INCLUDES = ', 'CXX_FLAGS = ')))
    flags = sum((shlex.split(settings[name]) for name in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS')), [])
    original = (source / 'tests/v2_swap_continuous_drill.cpp').read_text()
    variants = [
        ('baseline', None, None, None),
        ('disable_read_recovery', "cfg.read_outage=120'000;", 'cfg.read_outage=0;',
         'continuous runtime survives injected regular-state outage'),
        ('omit_injected_outage', 'if(inject_state_outage && decode_gateway_request',
         'if(false && decode_gateway_request', 'controlled state outage was exercised and restored'),
    ]
    if any(len(str(output / name / 'case/c0/s.sock').encode()) >= 104 for name, *_ in variants):
        raise SystemExit('use a shorter durable output path for Unix sockets')
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for name, old, new, assertion in variants:
        work = output / name
        work.mkdir()
        if old is not None and original.count(old) != 1:
            raise RuntimeError('mutation anchor must match exactly once: ' + name)
        candidate = work / 'candidate.cpp'
        candidate.write_text(original if old is None else original.replace(old, new))
        obj, binary = work / 'candidate.o', work / 'check'
        compile_command = [link[0], *flags, '-I' + str(source / 'tests'), '-c', str(candidate), '-o', str(obj)]
        command = [str(obj) if item.endswith('/v2_swap_continuous_drill.cpp.o') else item for item in link]
        command[command.index('-o') + 1] = str(binary)
        with (work / 'build.log').open('x') as log:
            subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        tested = subprocess.run([str(binary), '--case', '0', str(work / 'case')],
                                capture_output=True, text=True, timeout=100)
        text = tested.stdout + tested.stderr
        (work / 'test.log').write_text(text)
        accepted = tested.returncode == 0 if assertion is None else tested.returncode == 1 and assertion in text
        results.append(dict(name=name, accepted=accepted, assertion=assertion, exit=tested.returncode,
                            source_sha256=digest(candidate), object_sha256=digest(obj), binary_sha256=digest(binary),
                            compile_command=compile_command, link_command=command))
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(name, 'PASS' if accepted else 'FAIL', flush=True)
        if not accepted:
            raise SystemExit(1)


if __name__ == '__main__':
    main()
