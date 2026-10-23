"""Build the arm and seal transition bundles from the adopted documents; documents unchanged, only the two launch flags move."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('review_transition', ROOT/'deploy/v2/swap-runtime/review_transition.py')
T = importlib.util.module_from_spec(spec)
import sys
sys.path.insert(0, str(ROOT/'deploy/v2/swap-runtime'))
spec.loader.exec_module(T)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def build(adoption, output):
    documents = {name: (adoption/name).read_bytes() for name in T.A.DOCUMENTS}
    adopted = json.loads((adoption/'manifest.json').read_bytes())
    armed = dict(adopted, launch_authorized=True, initialize_swap_recovery=True)
    sealed = dict(armed, initialize_swap_recovery=False)
    output.mkdir(mode=0o700)
    pins = {}
    for mode, old, new in (('arm', adopted, armed), ('seal', armed, sealed)):
        files = {'old-manifest.json': T.P.manifest_bytes(old), 'new-manifest.json': T.P.manifest_bytes(new)}
        for name, raw in documents.items():
            files['old-' + name] = files['new-' + name] = raw
        base = output/mode
        base.mkdir(mode=0o700)
        for name, raw in files.items():
            (base/name).write_bytes(raw)
        spec_raw = (json.dumps(dict(schema=1, mode=mode, files={n: sha(r) for n, r in files.items()}), sort_keys=True) + '\n').encode()
        (base/'transition.json').write_bytes(spec_raw)
        pins[mode] = sha(spec_raw)
        assert T.bundle(base, pins[mode])[0] == mode
    pins['sealed_manifest_sha256'] = sha(T.P.manifest_bytes(sealed))
    pins['armed_manifest_sha256'] = sha(T.P.manifest_bytes(armed))
    return pins


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adoption', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(build(a.adoption, a.output), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
