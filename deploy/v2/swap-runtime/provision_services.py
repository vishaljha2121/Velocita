#!/usr/bin/env python3
"""Review fixed NUC1 service principals/directories; --apply creates only missing ones.

No credentials, units, release files, journal initialization, runtime directories,
service activation or account funding. Existing identities/paths must match exactly;
never repair, reset, delete, recursively chown, or retry a failed operation.
"""
import argparse
import grp
import json
import os
from pathlib import PurePosixPath
import pwd
import socket
import stat
import subprocess
import sys

HOST = 'bulbazar-NUC'
GROUPS = (
    dict(name='velocita-swap-gateway', gid=29001, members=[]),
    dict(name='velocita-swap-socket', gid=29002, members=[]),
)
USERS = (
    dict(name='velocita-swap-gateway', uid=29001, gid=29001, home='/nonexistent', shell='/usr/sbin/nologin'),
    dict(name='velocita-swap-signer', uid=29002, gid=29002, home='/nonexistent', shell='/usr/sbin/nologin'),
)


def directory_spec(uid, gid, mode):
    return dict(type='directory', uid=uid, gid=gid, mode=mode)


DIRECTORIES = {
    '/etc/velocita-swap': directory_spec(0, 0, 0o755),
    '/etc/velocita-swap/public': directory_spec(0, 29002, 0o750),
    '/etc/velocita-swap/gateway': directory_spec(29001, 29001, 0o700),
    '/etc/velocita-swap/signer': directory_spec(29002, 29002, 0o700),
}
for _user, _role in zip(USERS, ('gateway', 'signer')):
    for _suffix in ('', '/recovery', '/metrics'):
        DIRECTORIES['/var/lib/velocita-swap-' + _role + _suffix] = directory_spec(_user['uid'], _user['gid'], 0o700)


class Refused(ValueError):
    pass


def plan(users, groups, directories):
    """Pure full preflight. All collisions fail before any mutation is returned."""
    actions = []
    for expected in GROUPS:
        matches = [g for g in groups if g['name'] == expected['name'] or g['gid'] == expected['gid']]
        if matches and matches != [expected]:
            raise Refused('group name/id/members collision: ' + expected['name'])
        if not matches: actions.append(dict(kind='group', **expected))
    for expected in USERS:
        matches = [u for u in users if u['name'] == expected['name'] or u['uid'] == expected['uid']]
        if matches and matches != [expected]:
            raise Refused('identity name/uid/gid/home/shell collision: ' + expected['name'])
        if not matches: actions.append(dict(kind='user', **expected))
        if any(expected['name'] in g['members'] for g in groups):
            raise Refused('unexpected supplementary membership: ' + expected['name'])
    for group in GROUPS:
        expected_members = {u['name'] for u in USERS if u['gid'] == group['gid']}
        if any(u['gid'] == group['gid'] and u['name'] not in expected_members for u in users):
            raise Refused('unexpected primary group membership: ' + group['name'])
    if set(directories) != set(DIRECTORIES): raise Refused('incomplete directory inventory')
    for path, expected in DIRECTORIES.items():
        actual = directories[path]
        if actual is not None and actual != expected:
            raise Refused('directory owner/group/mode/type mismatch: ' + path)
        if actual is None: actions.append(dict(kind='directory', path=path, **expected))
    return dict(schema=1, host=HOST, actions=actions, users=USERS, groups=GROUPS,
                directories=DIRECTORIES, runtime_directories='systemd creates at service start',
                credentials_installed=False, units_installed=False, live_admitted=False,
                gateway_socket_membership='systemd SupplementaryGroups=29002 only',
                existing_state='preserved; no journal initialization or file inspection',
                failure_behavior='stop on first error; partial creations preserved; no rollback or retry')


def metadata(meta):
    return directory_spec(meta.st_uid, meta.st_gid, stat.S_IMODE(meta.st_mode))


def open_directory(path):
    """Anchor every component by descriptor; reject symlinks and unsafe ancestry."""
    if not path.startswith('/') or '..' in PurePosixPath(path).parts:
        raise Refused('fixed absolute directory required')
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    prefix = ''
    try:
        for part in PurePosixPath(path).parts[1:]:
            prefix += '/' + part
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd); fd = next_fd
            meta = os.fstat(fd)
            if prefix in DIRECTORIES:
                if metadata(meta) != DIRECTORIES[prefix]:
                    raise Refused('directory owner/group/mode/type mismatch: ' + prefix)
            elif meta.st_uid != 0 or stat.S_IMODE(meta.st_mode) & 0o022:
                raise Refused('unsafe root ancestor: ' + prefix)
        return fd
    except BaseException:
        os.close(fd)
        raise


def inspect_directory(path):
    try: fd = open_directory(path)
    except FileNotFoundError: return None
    try: return metadata(os.fstat(fd))
    finally: os.close(fd)


def snapshot():
    # NSS enumeration plus keyed lookups cover IDs/names whose provider does not
    # enumerate all entries. No password/shadow contents are accessed.
    users = {u.pw_name: u for u in pwd.getpwall()}
    groups = {g.gr_name: g for g in grp.getgrall()}
    for expected in USERS:
        for lookup, key in ((pwd.getpwnam, expected['name']), (pwd.getpwuid, expected['uid'])):
            try:
                item = lookup(key); users[item.pw_name] = item
            except KeyError: pass
    for expected in GROUPS:
        for lookup, key in ((grp.getgrnam, expected['name']), (grp.getgrgid, expected['gid'])):
            try:
                item = lookup(key); groups[item.gr_name] = item
            except KeyError: pass
    user_rows = [dict(name=u.pw_name, uid=u.pw_uid, gid=u.pw_gid, home=u.pw_dir, shell=u.pw_shell) for u in users.values()]
    group_rows = [dict(name=g.gr_name, gid=g.gr_gid, members=list(g.gr_mem)) for g in groups.values()]
    # initgroups may include NSS memberships omitted by group enumeration.
    for expected in USERS:
        if expected['name'] in users and set(os.getgrouplist(expected['name'], expected['gid'])) != {expected['gid']}:
            raise Refused('unexpected NSS supplementary membership: ' + expected['name'])
    return user_rows, group_rows, {path: inspect_directory(path) for path in DIRECTORIES}


def require_host_root():
    if sys.platform != 'linux' or socket.gethostname() != HOST or os.getuid() != 0 or os.geteuid() != 0:
        raise Refused('apply requires root on fixed NUC1 host')


def command(action):
    if action['kind'] == 'group':
        return ['/usr/sbin/groupadd', '--gid', str(action['gid']), action['name']]
    if action['kind'] == 'user':
        return ['/usr/sbin/useradd', '--system', '--key', 'SYS_UID_MAX=29002',
                '--uid', str(action['uid']), '--gid', str(action['gid']),
                '--no-create-home', '--no-user-group', '--no-log-init', '--home-dir', action['home'],
                '--shell', action['shell'], '--password', '!', action['name']]
    raise Refused('fixed principal command required')


def create_directory(action):
    path = PurePosixPath(action['path'])
    parent = open_directory(str(path.parent))
    try:
        # Never exist_ok/chown an existing object. Creation races stop the run.
        os.mkdir(path.name, mode=0o700, dir_fd=parent)
        fd = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
        try:
            meta = os.fstat(fd)
            if meta.st_uid != 0 or stat.S_IMODE(meta.st_mode) != 0o700:
                raise Refused('new directory changed before ownership assignment')
            os.fchown(fd, action['uid'], action['gid'])
            os.fchmod(fd, action['mode'])
        finally: os.close(fd)
    finally: os.close(parent)
    if inspect_directory(str(path)) != DIRECTORIES[str(path)]:
        raise Refused('created directory postcondition failed')


def perform(action):
    if action['kind'] == 'directory': create_directory(action)
    else:
        subprocess.run(command(action), check=True, timeout=15,
                       env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LANG': 'C'}, stdin=subprocess.DEVNULL)


def apply():
    require_host_root()
    initial = plan(*snapshot())
    for action in initial['actions']:
        # Fresh all-or-refuse preflight before each operation. A failure can leave
        # earlier new identities/directories, but never edits/deletes old state.
        current = plan(*snapshot())
        if action not in current['actions']: raise Refused('host changed after plan; review again')
        perform(action)
    final = plan(*snapshot())
    if final['actions']: raise Refused('provisioning postconditions incomplete')
    return dict(initial, actions_applied=len(initial['actions']), verified=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='root-only create missing fixed principals/directories')
    args = parser.parse_args()
    if sys.platform != 'linux' or socket.gethostname() != HOST:
        raise Refused('review requires fixed Linux NUC1 host')
    print(json.dumps(apply() if args.apply else plan(*snapshot()), indent=2, sort_keys=True))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print('REFUSED: ' + str(exc) + '; preserve partial state; no automatic retry', file=sys.stderr)
        raise SystemExit(64)
