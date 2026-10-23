"""Owner sudo: undo a recovery attempt refused before the bearer rotation; restore the archived manifest, profile and units, set the attempt archive and release aside and clear the gateway failed state."""
import argparse
import importlib.util
import json
import os
from pathlib import Path

spec = importlib.util.spec_from_file_location('owner_recover', Path(__file__).with_name('owner_recover.py'))
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)
U, S = R.U, R.S


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--binary-sha256', required=True)
    a = p.parse_args()
    R.configure(a.binary_sha256)
    data, profile = U.payload(a.bundle)
    U.require_host()
    S.driver_stopped()
    U.verify_guards()
    U.stopped()
    report = json.loads(U.read(R.ARCHIVE/'preflight.json', True))
    if (R.ARCHIVE/'result.json').exists():
        raise ValueError('attempt completed; nothing to roll back')
    archived = {'manifest.json': U.read(R.ARCHIVE/'manifest.json', True), 'runtime.profile': U.read(R.ARCHIVE/'runtime.profile', True)}
    archived_units = {u: U.read(R.ARCHIVE/u, True) for u in U.UNITS}
    if U.digest(archived['manifest.json']) != R.OLD_MANIFEST:
        raise ValueError('archived manifest is not the reviewed ba299ff manifest')
    current = {n: U.read(U.PUBLIC/n, True) for n in archived}
    current_units = {u: U.read(U.SYSTEMD/u, True) for u in U.UNITS}
    if U.digest(current['manifest.json']) != report['new_manifest'] or U.digest(current['runtime.profile']) != report['new_profile']:
        raise ValueError('installed documents are not the attempt documents')
    new = json.loads(current['manifest.json'])
    if any(current_units[u] != profile.render(new)[u+'.review'].encode() for u in U.UNITS):
        raise ValueError('installed units are not the attempt units')
    old = json.loads(archived['manifest.json'])
    if any(archived_units[u] != profile.render(old)[u+'.review'].encode() for u in U.UNITS):
        raise ValueError('archived units do not match the archived manifest')
    for name, raw in archived.items():
        if name != 'manifest.json':
            U.replace(U.PUBLIC/name, current[name], raw)
    for unit, raw in archived_units.items():
        U.replace(U.SYSTEMD/unit, current_units[unit], raw)
    U.replace(U.PUBLIC/'manifest.json', current['manifest.json'], archived['manifest.json'])
    U.run(['systemctl', 'daemon-reload'])
    U.verify_guards()
    archive_aside = R.ARCHIVE.with_name(R.ARCHIVE.name+'-attempt1')
    release_aside = U.RELEASE.with_name(U.RELEASE.name+'.attempt1')
    if archive_aside.exists() or release_aside.exists():
        raise ValueError('attempt set-aside paths already exist')
    os.rename(R.ARCHIVE, archive_aside)
    if U.RELEASE.exists():
        os.rename(U.RELEASE, release_aside)
    U.run(['systemctl', 'reset-failed', 'velocita-swap-gateway.service'])
    states = {u: U.run(['systemctl', 'show', u, '-p', 'ActiveState', '--value']).strip() for u in U.UNITS}
    if set(states.values()) != {'inactive'}:
        raise ValueError('services not inactive: '+json.dumps(states))
    if U.digest(U.read(U.PUBLIC/'manifest.json', True)) != R.OLD_MANIFEST:
        raise ValueError('manifest not restored')
    print(json.dumps(dict(restored_manifest=R.OLD_MANIFEST, archive=str(archive_aside), release=str(release_aside), states=states), sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps(dict(error_type=type(error).__name__, error=str(error)[:300], stop_and_inspect=True)))
        raise SystemExit(1)
