#!/usr/bin/env python3
"""Protected FD handoff mechanics. Production entry remains unconditionally closed.

Helpers are exercised only with synthetic files and a harmless child by the tests.
No CLI flag enables launch; service_launcher.py calls the protected helpers.
"""
import hashlib
import os
import stat
import sys


class Refused(ValueError):
    pass


def _open_components(path, uid):
    """Open absolute path without symlinks; inspect every ancestor via its FD."""
    if not isinstance(path, str) or not path.startswith('/') or any(x in ('', '.', '..') for x in path.split('/')[1:]):
        raise Refused('canonical absolute path required')
    directory = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for component in path.split('/')[1:-1]:
            child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=directory)
            os.close(directory); directory = child
            meta = os.fstat(directory)
            if meta.st_uid not in (0, uid) or meta.st_mode & 0o022:
                raise Refused('mutable or foreign ancestor')
        return os.open(path.split('/')[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=directory)
    finally:
        os.close(directory)


def open_secret(path, uid):
    if type(uid) is not int or uid <= 0 or os.getuid() != uid or os.geteuid() != uid:
        raise Refused('secret requires its non-root role UID')
    fd = _open_components(path, uid)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or meta.st_uid != uid or stat.S_IMODE(meta.st_mode) != 0o600 or meta.st_nlink != 1:
            raise Refused('secret must be owned, singly linked, regular and exactly0600')
        return fd
    except BaseException:
        os.close(fd)
        raise


def open_pinned_public(path, digest, executable=False, limit=268435456):
    """Root-owned immutable-path input; no role-writable ancestor or final file."""
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise Refused('SHA256 pin required')
    fd = _open_components(path, 0)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_uid != 0 or before.st_nlink != 1 or before.st_mode & 0o022 or not 0 < before.st_size <= limit:
            raise Refused('public artifact must be root-owned and non-writable by roles')
        if executable and (not before.st_mode & stat.S_IXOTH or before.st_mode & 0o6000):
            raise Refused('binary must be executable without set-id privileges')
        hasher = hashlib.sha256()
        while data := os.read(fd, 65536): hasher.update(data)
        after = os.fstat(fd)
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) or hasher.hexdigest() != digest:
            raise Refused('public artifact changed or digest mismatched')
        os.lseek(fd, 0, os.SEEK_SET)
        return fd
    except BaseException:
        os.close(fd)
        raise


def exec_handoff(binary, argv, secret_fd, environment):
    """Low-level tested primitive; caller has pinned immutable binary/public paths.

    Transfers only FD3 plus standard streams. Exec failure closes FD3. This is not
    the production authorization boundary; main() cannot reach it.
    """
    if not argv or argv[0] != binary or secret_fd < 3:
        raise Refused('invalid exec mapping')
    if secret_fd == 3:
        # dup2(fd, fd) is a no-op and can preserve FD_CLOEXEC despite its
        # inheritable argument. Our protected opens deliberately set CLOEXEC.
        os.set_inheritable(3, True)
    else:
        os.dup2(secret_fd, 3, inheritable=True)
        os.close(secret_fd)
    # CLOEXEC closes descriptors from our open helpers. Refuse accidental unrelated
    # inherited descriptors too; do not rely on the supervisor's ambient state.
    limit = os.sysconf('SC_OPEN_MAX')
    os.closerange(4, limit)
    try:
        os.execve(binary, argv, environment)
    except BaseException:
        os.close(3)
        raise


def validate_public_inputs(contract):
    """Offline/public-only filesystem qualification, does not open the secret."""
    descriptors = []
    try:
        descriptors.append(open_pinned_public(contract['argv'][0], contract['binary_sha256'], True))
        for path, digest in contract['public_files_sha256'].items():
            descriptors.append(open_pinned_public(path, digest))
    finally:
        for fd in descriptors: os.close(fd)


def main():
    # Deliberately refuse before even opening a supplied configuration/credential.
    # Removing this requires a reviewed enabled contract and separate live approval.
    print('REFUSED: service review only; production descriptor launch is not admitted', file=sys.stderr)
    return 64


if __name__ == '__main__': raise SystemExit(main())
