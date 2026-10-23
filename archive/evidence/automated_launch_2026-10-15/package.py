"""Package qualified voter, exporter and client binaries over the previous qualified image; no network, no service change."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

BASE_IMAGE = 'localhost/velocita-v2:qualified-1a46fd8'
NAMES = ('velocita-v2-node', 'velocita-v2-exporter', 'velocita-v2-client')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def package(commit, source, pins, out, run=subprocess.run):
    require(re.fullmatch('[0-9a-f]{40}', commit), 'full commit required')
    require(set(pins) == set(NAMES) and all(re.fullmatch('[0-9a-f]{64}', v) for v in pins.values()), 'exact binary pins required')
    tag = 'localhost/velocita-v2:qualified-' + commit[:7]
    require(run(['podman', 'image', 'exists', tag]).returncode != 0, 'image tag already exists; preserve it')
    base = run(['podman', 'image', 'inspect', BASE_IMAGE, '--format', '{{.Id}}'], capture_output=True, text=True, check=True).stdout.strip()
    require(not out.exists() and shutil.disk_usage(out.parent).free > 2_000_000_000, 'fresh output with 2 GB free required')
    for name in NAMES:
        raw = (source/name).read_bytes()
        require(raw.startswith(b'\x7fELF') and hashlib.sha256(raw).hexdigest() == pins[name], 'binary differs from its qualified pin: ' + name)
    context = out/'image'
    context.mkdir(parents=True, mode=0o700)
    for name in NAMES:
        (context/name).write_bytes((source/name).read_bytes())
        (context/name).chmod(0o755)
    (context/'qualified-binary-pins.json').write_text(json.dumps(pins, indent=2, sort_keys=True) + '\n')
    (context/'Containerfile').write_text(f'FROM {base}\n' + ''.join(f'COPY {n} /usr/local/bin/{n}\n' for n in NAMES) +
        'COPY qualified-binary-pins.json /usr/share/velocita/qualified-binary-pins.json\n'
        f'LABEL org.velocita.source_commit="{commit}"\nENTRYPOINT ["/usr/local/bin/velocita-v2-node"]\n')
    with (out/'package.log').open('x') as log:
        run(['podman', 'build', '--pull-never', '--network=none', '--memory=1g', '-t', tag, str(context)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    image = run(['podman', 'image', 'inspect', tag, '--format', '{{.Id}}'], capture_output=True, text=True, check=True).stdout.strip()
    result = dict(source_commit=commit, image=tag, image_id=image, base_image=base, binary_sha256=pins, compiled=False)
    (out/'package-result.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--commit', required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--pins', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(package(a.commit, a.source, json.loads(a.pins.read_text()), a.output), sort_keys=True))


if __name__ == '__main__':
    main()
