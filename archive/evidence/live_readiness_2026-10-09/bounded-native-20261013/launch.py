"""Launch offline native qualification from an exact supplied bundle; no service changes."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

IMAGES = {'x86_64': '076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f',
          'aarch64': '3f3cfb3403a2644b27bf21351e26683e62f6ad90fde1f1b1567c8d2279d4fb43'}


def output(argv):
    return subprocess.check_output(argv, text=True).strip()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--runner', type=Path, default=Path(__file__).with_name('native-focused.py'))
    args = parser.parse_args()
    require(re.fullmatch(r'[0-9a-f]{40}', args.commit), 'exact lowercase 40-character commit required')
    os.umask(0o077)
    old = Path.home()/'.cache/velocita-build/integrated-a890307-20261013'
    source, work = old/'source', old/'qualification'
    out = Path.home()/('.cache/velocita-build/bounded-native-20261013-'+args.commit)
    name = 'velocita-bounded-qualification-'+args.commit[:12]
    arch = os.uname().machine
    require(arch in IMAGES and os.getuid() != 0 and os.getuid() == os.geteuid(), 'supported unprivileged host required')
    require(all(p.is_dir() and not p.is_symlink() for p in (old, source, work, old/'ccache')), 'existing real cache directories required')
    require(args.bundle.is_file() and not args.bundle.is_symlink() and args.runner.is_file() and not args.runner.is_symlink(), 'regular supplied bundle and driver required')
    bundle, runner = args.bundle.resolve(), args.runner.resolve()
    with (old/'bounded-qualification.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        git = lambda *tail: output(['git', '-C', str(source), *tail])
        require(not out.exists(), 'evidence already exists; preserve it and do not retry automatically')
        require(not git('status', '--porcelain=v1', '--untracked-files=all'), 'qualification source is dirty')
        require(shutil.disk_usage(old).free > 2_500_000_000, 'need more than 2.5GB free before qualification')
        require(output(['podman', 'image', 'inspect', IMAGES[arch], '--format', '{{.Id}}']) == IMAGES[arch], 'pinned TLS image missing or different')
        running = output(['podman', 'ps', '--format', '{{.ID}}']).splitlines()
        for container in running:
            details = json.loads(output(['podman', 'inspect', container]))[0]
            container_name = details['Name'].lstrip('/')
            mounts = {str(Path(m['Source']).resolve()) for m in details.get('Mounts', []) if m.get('Source')}
            require('qualification' not in container_name and not mounts.intersection({str(source.resolve()), str(work.resolve())}),
                    'running qualification/cache user found; source must not be switched')
        existing_names = output(['podman', 'ps', '-a', '--format', '{{.Names}}']).splitlines()
        require(name not in existing_names, 'qualification container already exists; preserve it')
        subprocess.run(['git', '-C', str(source), 'bundle', 'verify', str(bundle)], check=True)
        heads = output(['git', 'bundle', 'list-heads', str(bundle), 'HEAD']).splitlines()
        require(heads == [args.commit+' HEAD'], 'bundle HEAD differs from supplied commit')
        before = dict(previous_commit=git('rev-parse', 'HEAD'), commit=args.commit, arch=arch,
            toolchain=IMAGES[arch], free_bytes=shutil.disk_usage(old).free,
            bundle_sha256=hashlib.sha256(bundle.read_bytes()).hexdigest(),
            runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
            launcher_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        out.mkdir(exist_ok=False)
        shutil.copyfile(runner, out/'native-focused.py')
        (out/'before.json').write_text(json.dumps(before, indent=2)+'\n')
        subprocess.run(['git', '-C', str(source), 'fetch', '--no-tags', str(bundle), 'HEAD'], check=True)
        require(git('rev-parse', 'FETCH_HEAD') == args.commit, 'fetched revision differs')
        subprocess.run(['git', '-C', str(source), 'checkout', '--detach', args.commit], check=True)
        require(git('rev-parse', 'HEAD') == args.commit and not git('status', '--porcelain=v1', '--untracked-files=all'), 'detached source is not exact and clean')
        argv = ['podman', 'run', '-d', '--name', name, '--network', 'none', '--userns', 'keep-id',
            '--user', f'{os.getuid()}:{os.getgid()}', '--cpus', '1', '--memory', '2g', '--memory-swap', '2g',
            '--pids-limit', '512', '--shm-size', '256m', '-v', f'{source}:/source:ro',
            '-v', f'{work}:/work:rw', '-v', f'{old / "ccache"}:/ccache:rw', '-v', f'{out}:/evidence:rw',
            '--entrypoint', '/bin/sh', IMAGES[arch], '-c',
            'exec taskset -c 3 python3 -B /evidence/native-focused.py '+args.commit+' '+arch+' > /evidence/driver.log 2>&1']
        (out/'launch-command.json').write_text(json.dumps(argv, indent=2)+'\n')
        container = output(argv)
        print(json.dumps(dict(container=container, container_name=name, commit=args.commit, arch=arch,
            evidence=str(out), live_admitted=False)))


if __name__ == '__main__':
    main()
