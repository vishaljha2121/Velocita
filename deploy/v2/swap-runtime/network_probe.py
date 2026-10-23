#!/usr/bin/env python3
"""Owner-run, bounded NUC1 network isolation drill. No credentials or trading code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

from service_profile import signer_network_properties

VOTERS = ['100.72.127.102', '100.84.151.116', '100.85.36.22']
EXTERNAL = '1.1.1.1'

CHILD = r'''
import errno, json, os, socket, sys
restricted = sys.argv[1] == 'restricted'
port = int(sys.argv[2])
nonce = sys.argv[3].encode()
voters = ['100.72.127.102', '100.84.151.116', '100.85.36.22']
result = {'role': sys.argv[1], 'uid': os.getuid(), 'checks': []}
ok = os.getuid() != 0
for address in voters + ['1.1.1.1', '127.0.0.1']:
    error = None
    try:
        target_port = 22 if address in voters else (port if address == '127.0.0.1' else 443)
        with socket.create_connection((address, target_port), timeout=2) as channel:
            if address == '127.0.0.1': channel.sendall(nonce)
    except OSError as exc:
        error = exc.errno if exc.errno is not None else 'timeout'
    expected_deny = restricted and address not in voters
    # A TCP timeout is provisional: the parent separately requires bracketing
    # control success and absence of any accepted restricted connection.
    passed = error in (errno.EPERM, errno.EACCES, 'timeout') if expected_deny else error is None
    result['checks'].append({'protocol': 'tcp', 'address': address, 'error': error, 'passed': passed})
    ok = ok and passed
for address in ['127.0.0.1', '1.1.1.1']:
    error = None
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as channel:
            channel.settimeout(2)
            channel.sendto(b'velocita-network-probe', (address, 59998))
    except OSError as exc:
        error = exc.errno if exc.errno is not None else 'timeout'
    passed = error in (errno.EPERM, errno.EACCES) if restricted else error is None
    result['checks'].append({'protocol': 'udp', 'address': address, 'error': error, 'passed': passed})
    ok = ok and passed
result['passed'] = bool(ok)
print(json.dumps(result), flush=True)
sys.exit(0 if ok else 1)
'''


def command(role, stamp, port, nonce, phase):
    if role not in ('control', 'restricted') or not stamp.isdigit() or phase not in ('before', 'restricted', 'after'):
        raise ValueError('fixed role and numeric run identifier required')
    if type(port) is not int or not 1024 <= port <= 65535 or len(nonce) != 32 or any(c not in '0123456789abcdef' for c in nonce):
        raise ValueError('bounded loopback endpoint and nonce required')
    props = ['DynamicUser=yes', 'NoNewPrivileges=yes', 'ProtectSystem=strict',
             'ProtectHome=yes', 'PrivateTmp=yes', 'PrivateDevices=yes',
             'ProtectKernelTunables=yes', 'ProtectKernelModules=yes',
             'ProtectControlGroups=yes', 'RestrictSUIDSGID=yes',
             'LockPersonality=yes', 'MemoryDenyWriteExecute=yes', 'LimitCORE=0',
             'CapabilityBoundingSet=', 'AmbientCapabilities=', 'UMask=0077',
             'RuntimeMaxSec=35', 'TimeoutStopSec=2', 'MemoryMax=64M', 'TasksMax=16',
             'Restart=no']
    props += signer_network_properties(VOTERS) if role == 'restricted' else [
        'PrivateNetwork=no', 'RestrictAddressFamilies=AF_UNIX AF_INET']
    args = ['/usr/bin/sudo', '-n', '/usr/bin/systemd-run', '--quiet', '--wait',
            '--pipe', '--collect', '--unit=velocita-signer-net-' + stamp + '-' + phase]
    args += ['--property=' + p for p in props]
    return args + ['/usr/bin/python3', '-I', '-B', '-c', CHILD, role, str(port), nonce]


def evaluate(role, completed):
    """Validate provisional results; TCP timeouts also need the parent bracket."""
    data = json.loads(completed.stdout)
    if not isinstance(data, dict) or data.get('role') != role or type(data.get('uid')) is not int or data['uid'] <= 0:
        raise ValueError('invalid probe identity')
    expected = [('tcp', x) for x in VOTERS + [EXTERNAL, '127.0.0.1']] + [('udp', x) for x in ['127.0.0.1', EXTERNAL]]
    if not isinstance(data.get('checks'), list) or len(data['checks']) != len(expected):
        raise ValueError('incomplete probe')
    import errno
    for check, (protocol, address) in zip(data['checks'], expected):
        if not isinstance(check, dict) or check.get('protocol') != protocol or check.get('address') != address:
            raise ValueError('probe coverage mismatch')
        deny = role == 'restricted' and address not in VOTERS
        error = check.get('error')
        good = ((type(error) is int and error in (errno.EPERM, errno.EACCES)) or
                (protocol == 'tcp' and error == 'timeout')) if deny else error is None
        if check.get('passed') is not True or not good:
            raise ValueError('connectivity or enforced-denial check failed')
    if completed.returncode != 0 or data.get('passed') is not True:
        raise ValueError('service failed')
    return data


def observe_listener(listener, expected_nonce):
    """Require one complete control nonce or zero restricted TCP handshakes."""
    received = []
    listener.settimeout(0.2)
    for _ in range(4):
        try:
            connection, peer = listener.accept()
        except socket.timeout:
            break
        with connection:
            connection.settimeout(1)
            payload = b''
            while len(payload) < 33:
                chunk = connection.recv(33 - len(payload))
                if not chunk: break
                payload += chunk
            received.append((peer[0], payload))
    expected = [] if expected_nonce is None else [('127.0.0.1', expected_nonce.encode())]
    if received != expected:
        raise ValueError('controlled endpoint did not match expected TCP reachability')
    return {'accepted_connections': len(received), 'expected_nonce_received': expected_nonce is not None}


def run(output, runner=subprocess.run):
    if sys.platform != 'linux' or socket.gethostname() != 'bulbazar-NUC' or os.geteuid() == 0:
        raise ValueError('run as the non-root owner account on NUC1, after sudo -v')
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    stamp = str(time.time_ns())
    summary = {'run': stamp, 'network_policy_passed': False,
               'aeron_qualified': False, 'production_uids_qualified': False,
               'live_admitted': False, 'commands': [], 'results': []}
    def save(name, value):
        fd = os.open(output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w') as stream: json.dump(value, stream, indent=2); stream.write('\n')
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(('127.0.0.1', 0)); listener.listen(4)
            port = listener.getsockname()[1]
            summary['controlled_endpoint'] = {'address': '127.0.0.1', 'port': port}
            for phase, role in (('before', 'control'), ('restricted', 'restricted'), ('after', 'control')):
                nonce = os.urandom(16).hex()
                args = command(role, stamp, port, nonce, phase)
                summary['commands'].append(args)
                completed = runner(args, capture_output=True, text=True, timeout=45, check=False)
                save(phase + '.json', {'returncode': completed.returncode,
                                     'stdout': completed.stdout, 'stderr': completed.stderr})
                checked = evaluate(role, completed)
                observed = observe_listener(listener, None if role == 'restricted' else nonce)
                summary['results'].append({'phase': phase, 'checks': checked, 'endpoint': observed})
            summary['network_policy_passed'] = True
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        summary['error_type'] = type(exc).__name__
    finally:
        summary['source_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (Path(__file__), Path(__file__).with_name('service_profile.py'))}
        save('result.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        print('Prepared: three sequential temporary non-root services, <=35s each, bracketed loopback TCP listener; no retries, keys, orders or cluster changes.')
        return 0
    result = run(args.output)
    print(json.dumps({k: result[k] for k in ('network_policy_passed', 'aeron_qualified',
                                            'production_uids_qualified', 'live_admitted')}))
    return 0 if result['network_policy_passed'] else 1


if __name__ == '__main__':
    try: raise SystemExit(main())
    except (ValueError, OSError):
        print('Network drill refused; preserve any partial evidence.', file=sys.stderr)
        raise SystemExit(1)
