"""One account setup GET and balance GET using an existing private session; no orders."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import time
import urllib.request

spec = importlib.util.spec_from_file_location('tm_session', Path(__file__).with_name('tm-account-session.py'))
TM = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TM)
PATHS = ('/v1/account/me/trading', '/v1/gateway/balances')


def read(path, bearer):
    if path not in PATHS:
        raise ValueError('endpoint refused')
    TM.MINT._validate_token(bearer)
    request = urllib.request.Request(TM.ORIGIN + path, method='GET', headers={
        'Authorization': 'Bearer ' + bearer, 'Accept': 'application/json',
        'User-Agent': 'velocita-account-read/1'})
    with urllib.request.build_opener(urllib.request.ProxyHandler({}), TM.NoRedirect()).open(request, timeout=20) as reply:
        raw = reply.read(32769)
        if reply.status != 200 or len(raw) > 32768:
            raise ValueError('response refused')
    return raw


def private_write(directory, name, raw):
    fd = os.open(directory / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    parent = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(parent)
    finally:
        os.close(parent)


def collect(session, output):
    output_parent, output = TM.MINT._open_private_parent(output)
    try:
        os.mkdir(output.name, mode=0o700, dir_fd=output_parent)
        os.fsync(output_parent)
    finally:
        os.close(output_parent)
    parent, session = TM.MINT._open_private_parent(session)
    try:
        lock = os.open(session.name + '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(lock, 'w') as handle:
        info = os.fstat(handle.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('unsafe session lock')
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        private_write(output, 'attempt.json', b'{"orders":false,"retries":false}\n')
        pair, operation = TM.session(session, None, False)
        started = time.time_ns()
        for path, name in zip(PATHS, ('setup.json', 'balances.json')):
            TM.token_pair(pair)
            raw = read(path, pair['access_token'])
            private_write(output, name, raw)
            document = json.loads(raw)
            if name == 'setup.json' and not isinstance(document, dict):
                raise ValueError('setup object required')
            if name == 'balances.json':
                TM.balances(document)
        report = {'authentication': operation, 'started_ns': started, 'finished_ns': time.time_ns(),
                  'requests': list(PATHS), 'orders_sent': False, 'live_admitted': False,
                  'raw_sha256': {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                                 for name in ('setup.json', 'balances.json')}}
        private_write(output, 'result.json', (json.dumps(report, indent=2) + '\n').encode())
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print('Prepared: at most one session refresh, then setup GET and balances GET; no retry or orders.')
        return
    print(json.dumps(collect(args.session, args.output), indent=2))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Account check stopped; preserve partial evidence. Error type: ' + type(error).__name__)
        raise SystemExit(1)
