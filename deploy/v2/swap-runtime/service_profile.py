#!/usr/bin/env python3
"""Render DISABLED service review artifacts from public deployment metadata only.

This tool neither opens credentials nor installs/starts units. The launch contracts
are data, not executable shell scripts. There is deliberately no enable-live flag.
"""
import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import re

FIELDS = frozenset('schema launch_authorized source_commit binary_sha256 runtime_image_sha256 binary incarnation gateway_uid gateway_gid signer_uid socket_gid gateway_caller signer_caller gateway_key_id signer_key_id gateway_response_port signer_response_port host_ip nodes runtime_profile_sha256 account_review_sha256 account_history_sha256 catalog_sha256'.split())
PENDING_ACCOUNT_PINS = frozenset(('runtime_profile_sha256', 'account_review_sha256', 'account_history_sha256'))
BLOCKERS = ['real-transaction-contract-gate',
            'host-identity-and-network-enforcement',
            'fresh-account-pins-and-journal-review', 'separate-live-authorization']
PUBLIC = '/etc/velocita-swap/public'


def signer_network_properties(nodes):
    """Approved address-level policy, shared by review units and the host probe."""
    if len(nodes) != 3 or len(set(nodes)) != 3:
        raise ValueError('three distinct voter addresses required')
    return ['PrivateNetwork=no', 'IPAddressDeny=any',
            'RestrictAddressFamilies=AF_UNIX AF_INET'] + [
                'IPAddressAllow=' + ipv4(node) + '/32' for node in nodes]


def ipv4(value):
    try:
        address = ipaddress.IPv4Address(value)
    except (ValueError, TypeError, ipaddress.AddressValueError) as exc:
        raise ValueError('fixed IPv4 address required') from exc
    if address.is_loopback or address.is_unspecified or address.is_multicast or address.is_link_local or address.is_reserved:
        raise ValueError('non-loopback unicast voter address required')
    return str(address)


def validate(m):
    if not isinstance(m, dict) or set(m) != FIELDS or type(m['schema']) is not int or m['schema'] != 1:
        raise ValueError('exact schema 1 public manifest required')
    if type(m['launch_authorized']) is not bool:
        raise ValueError('explicit operator launch authorization required')
    for name, width in [('source_commit', 40), ('incarnation', 32)] + [(k, 64) for k in FIELDS if k.endswith('_sha256')]:
        value = m[name]
        # Installation can precede the fresh account/custody review. Explicit
        # nulls record that missing evidence without inventing executable pins.
        if name in PENDING_ACCOUNT_PINS and value is None and m['launch_authorized'] is False:
            continue
        if not isinstance(value, str) or re.fullmatch('[0-9a-f]{' + str(width) + '}', value) is None or not int(value, 16):
            raise ValueError('invalid ' + name)
    expected = '/opt/velocita/releases/' + m['source_commit'] + '/velocita-v2-gateway'
    if m['binary'] != expected:
        raise ValueError('binary must be pinned under its immutable source release')
    for name in ('gateway_uid', 'gateway_gid', 'signer_uid', 'socket_gid', 'gateway_caller', 'signer_caller', 'gateway_key_id', 'signer_key_id', 'gateway_response_port', 'signer_response_port'):
        upper = 65535 if name.endswith('_port') else (2**31 - 1 if name.endswith('_key_id') else 2**32 - 2)
        if type(m[name]) is not int or not (1024 if name.endswith('_port') else 1) <= m[name] <= upper:
            raise ValueError('invalid ' + name)
    for a, b in [('gateway_uid', 'signer_uid'), ('gateway_gid', 'socket_gid'),
                 ('gateway_caller', 'signer_caller'), ('gateway_key_id', 'signer_key_id'),
                 ('gateway_response_port', 'signer_response_port')]:
        if m[a] == m[b]: raise ValueError('distinct principals/ports required: ' + a)
    host = ipv4(m['host_ip'])
    if not isinstance(m['nodes'], list) or len(m['nodes']) != 3:
        raise ValueError('exactly the existing three voters required')
    nodes = []
    for number, endpoint in enumerate(m['nodes'], 1):
        if not isinstance(endpoint, str) or re.fullmatch(r'[123]=[0-9.]+:[0-9]+', endpoint) is None:
            raise ValueError('invalid voter endpoint')
        identity, address = endpoint.split('='); ip, port = address.split(':')
        if int(identity) != number or not 0 < int(port) <= 65535:
            raise ValueError('ordered voter ids 1,2,3 and bounded ports required')
        nodes.append(ipv4(ip))
    if len(set(nodes)) != 3 or host not in nodes:
        raise ValueError('distinct existing voters including local host required')
    return nodes


def launch_contract(m, role):
    root = '/var/lib/velocita-swap-' + role
    args = [m['binary'], '--venue', 'truemarkets', '--execution-contract', 'exact-input-swap',
            '--swap-process', role, '--swap-runtime-profile', PUBLIC + '/runtime.profile',
            '--swap-account-review', PUBLIC + '/account.review',
            '--swap-account-history', PUBLIC + '/account.history',
            '--swap-recovery-dir', root + '/recovery', '--swap-secret-fd', '3',
            '--arm-live-venue', '--allow-new-risk', '--incarnation', m['incarnation'],
            '--caller-id', str(m[role + '_caller']), '--caller-key-id', str(m[role + '_key_id']),
            '--key-file', '/etc/velocita-swap/' + role + '/caller.key',
            '--session-file', root + '/client.session', '--state-file', root + '/submitter.state',
            '--catalog', PUBLIC + '/catalog.bin',
            '--aeron-dir', '/run/velocita-swap-' + role + '/aeron',
            '--metrics-textfile', root + '/metrics/runtime.prom',
            '--response-endpoint', m['host_ip'] + ':' + str(m[role + '_response_port']),
            '--freshness-ms', '2000']
    for node in m['nodes']: args.extend(['--node', node])
    return dict(deployable=False, blockers=BLOCKERS, argv=args,
                source_commit=m['source_commit'], binary_sha256=m['binary_sha256'],
                public_files_sha256={PUBLIC + '/' + filename: m[key] for filename, key in (
                    ('runtime.profile', 'runtime_profile_sha256'), ('account.review', 'account_review_sha256'),
                    ('account.history', 'account_history_sha256'), ('catalog.bin', 'catalog_sha256'))},
                uid=m[role + '_uid'], peer_uid=m['signer_uid' if role == 'gateway' else 'gateway_uid'],
                socket='/run/velocita-swap-signer/sign.sock', socket_mode='0660',
                socket_directory_uid=m['signer_uid'], socket_directory_gid=m['socket_gid'],
                socket_directory_mode='0750',
                secret_fd=dict(number=3, path='/etc/velocita-swap/' + role + '/' +
                               ('session.bearer' if role == 'gateway' else 'signer.pem'),
                               uid=m[role + '_uid'], mode='0600', hard_links=1,
                               regular=True, access='read-only', inherit_once=True),
                state_directory=dict(path=root, uid=m[role + '_uid'], mode='0700'))


def unit(m, role, nodes):
    signer = role == 'signer'
    group = m['socket_gid'] if signer else m['gateway_gid']
    lines = ['# INSTALLATION REVIEW: activation requires a separately authorized manifest.',
             '# No Install section; no automatic recovery-directory initialization.',
             '[Unit]', 'Description=Velocita swap ' + role,
             'After=network-online.target', '', '[Service]', 'Type=simple',
             'User=' + str(m[role + '_uid']), 'Group=' + str(group)]
    if not signer: lines.append('SupplementaryGroups=' + str(m['socket_gid']))
    if not signer:
        lines[lines.index('After=network-online.target')] = 'After=network-online.target velocita-swap-signer.service'
        lines.insert(lines.index('[Service]')-1, 'BindsTo=velocita-swap-signer.service')
    lines += ['RootDirectory=/opt/velocita/runtimes/' + m['runtime_image_sha256'] + '/rootfs',
              'MountAPIVFS=yes',
              'BindReadOnlyPaths=/opt/velocita/releases/' + m['source_commit'],
              'BindReadOnlyPaths=' + PUBLIC,
              # Preserve existing in-unit paths, sourcing credentials only from
              # the separately provisioned role-owned host tmpfs directory.
              'BindReadOnlyPaths=/run/velocita-swap-credentials/' + role + ':/etc/velocita-swap/' + role]
    if not signer:
        lines += ['BindReadOnlyPaths=/run/velocita-swap-signer',
                  'BindReadOnlyPaths=/etc/resolv.conf']
    # StateDirectory and RuntimeDirectory are automatically mounted by systemd
    # inside RootDirectory. No other role credentials/state are exposed.
    lines += ['UMask=0077', 'NoNewPrivileges=yes', 'ProtectSystem=strict', 'ProtectHome=yes',
              'PrivateTmp=yes', 'PrivateDevices=yes', 'ProtectKernelTunables=yes',
              'ProtectKernelModules=yes', 'ProtectControlGroups=yes', 'RestrictSUIDSGID=yes',
              'LockPersonality=yes', 'MemoryDenyWriteExecute=yes', 'LimitCORE=0',
              'CapabilityBoundingSet=', 'AmbientCapabilities=',
              'RestrictAddressFamilies=AF_UNIX AF_INET',
              'Environment=AERON_TERM_BUFFER_LENGTH=1048576',
              'RuntimeDirectory=velocita-swap-' + role,
              'RuntimeDirectoryMode=' + ('0750' if signer else '0700'),
              'StateDirectory=velocita-swap-' + role, 'StateDirectoryMode=0700',
              'InaccessiblePaths=-/etc/velocita-swap/' + ('gateway' if signer else 'signer'),
              'InaccessiblePaths=-/var/lib/velocita-swap-' + ('gateway' if signer else 'signer')]
    if signer:
        lines.remove('RestrictAddressFamilies=AF_UNIX AF_INET')
        lines += ['# Owner-approved 2026-10-13: voter-address-only IP reachability.',
                  '# Host cgroup BPF enforcement qualification remains mandatory.']
        lines += signer_network_properties(nodes)
    launcher = '/opt/velocita/releases/' + m['source_commit'] + '/service_launcher.py'
    digest = hashlib.sha256(manifest_bytes(m)).hexdigest()
    lines += ['# The default manifest refuses launch before opening any credential.',
              'ExecStart=/usr/bin/python3 -I -B ' + launcher +
              ' --manifest ' + PUBLIC + '/manifest.json --manifest-sha256 ' + digest +
              ' --role ' + role + ' --launch', 'Restart=no', 'TimeoutStopSec=15', '']
    return '\n'.join(lines)


def manifest_bytes(manifest):
    return (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()


def render(manifest):
    nodes = validate(manifest)
    result = {'manifest.json': manifest_bytes(manifest).decode()}
    for role in ('gateway', 'signer'):
        result['velocita-swap-' + role + '.service.review'] = unit(manifest, role, nodes)
        result[role + '-launch-contract.json'] = json.dumps(launch_contract(manifest, role), indent=2, sort_keys=True) + '\n'
    return result


def write_review(manifest, output):
    files = render(manifest)
    output = Path(output)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    for name, contents in files.items():
        with (output / name).open('x') as stream: stream.write(contents)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True, help='public metadata only')
    parser.add_argument('--output', type=Path, required=True, help='fresh review directory')
    args = parser.parse_args()
    if args.manifest.stat().st_size > 16384: parser.error('manifest too large')
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out: raise ValueError('duplicate manifest field')
            out[key] = value
        return out
    write_review(json.loads(args.manifest.read_text(), object_pairs_hook=pairs), args.output)


if __name__ == '__main__': main()
