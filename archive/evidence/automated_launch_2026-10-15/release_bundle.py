"""Build the dormant install bundle for the new incarnation from the current manifest; writes only a fresh output directory."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

LAUNCHER_FILES = ('service_launcher.py', 'service_profile.py', 'fd_handoff.py')
CATALOG_SHA256 = '8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def manifest(previous, commit, binary_sha256, incarnation, profile):
    require(previous['incarnation'] == 'b79e65be1ea14e219016ec48ea02efef' and previous['catalog_sha256'] == CATALOG_SHA256, 'current b79e65be manifest required')
    require(re.fullmatch('[0-9a-f]{40}', commit) and commit != previous['source_commit'], 'new full commit required')
    require(re.fullmatch('[0-9a-f]{32}', incarnation) and incarnation != previous['incarnation'], 'new incarnation required')
    m = {k: v for k, v in previous.items() if k not in ('initialize_swap_recovery', 'recovery_only')}
    m.update(source_commit=commit, binary=f'/opt/velocita/releases/{commit}/velocita-v2-gateway', binary_sha256=binary_sha256,
             incarnation=incarnation, launch_authorized=False, runtime_profile_sha256=None, account_review_sha256=None, account_history_sha256=None)
    profile.validate(m)
    return m


def build(previous_path, source, binary, binary_sha256, catalog, commit, incarnation, output):
    spec = importlib.util.spec_from_file_location('service_profile', source/'service_profile.py')
    profile = importlib.util.module_from_spec(spec)
    import sys
    sys.path.insert(0, str(source))
    spec.loader.exec_module(profile)
    raw_binary, raw_catalog = binary.read_bytes(), catalog.read_bytes()
    require(raw_binary.startswith(b'\x7fELF') and sha(raw_binary) == binary_sha256, 'gateway binary differs from its qualified pin')
    require(sha(raw_catalog) == CATALOG_SHA256, 'catalog differs from the installed catalog')
    m = manifest(json.loads(previous_path.read_bytes()), commit, binary_sha256, incarnation, profile)
    rendered = profile.render(m)
    files = {name: (source/name).read_bytes() for name in LAUNCHER_FILES}
    files.update({'velocita-v2-gateway': raw_binary, 'catalog.bin': raw_catalog, 'manifest.json': profile.manifest_bytes(m)})
    files.update({f'velocita-swap-{role}.service': rendered[f'velocita-swap-{role}.service.review'].encode() for role in ('gateway', 'signer')})
    output.mkdir(mode=0o700)
    for name, raw in files.items():
        (output/name).write_bytes(raw)
    pins = json.dumps(dict(schema=1, files={name: sha(raw) for name, raw in files.items()}), indent=2, sort_keys=True).encode() + b'\n'
    (output/'install-bundle.json').write_bytes(pins)
    return dict(bundle_sha256=sha(pins), manifest_sha256=sha(files['manifest.json']), incarnation=incarnation, source_commit=commit,
                launch_authorized=False, installed=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('previous-manifest', 'source', 'binary', 'catalog', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    for name in ('binary-sha256', 'commit', 'incarnation'):
        p.add_argument('--' + name, required=True)
    a = p.parse_args()
    print(json.dumps(build(a.previous_manifest, a.source, a.binary, a.binary_sha256, a.catalog, a.commit, a.incarnation, a.output), sort_keys=True))


if __name__ == '__main__':
    main()
