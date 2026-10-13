#!/usr/bin/env python3
"""Adopt an already prepared, normalized rootfs from one pinned qualified OCI image.

Default: verify/review only. Never extract an archive as root, overwrite/resume a
release, install units/credentials, or start services. Source staging is non-root;
regular file modes must be 0644/0755, directories 0755, and symlinks are copied last
without following them. The manifest pins content, not merely the image label.
"""
import argparse
import hashlib
import json
import os
from pathlib import PurePosixPath
import re
import socket
import stat
import sys

IMAGE = '076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f'
HOST = 'bulbazar-NUC'
PARENT = '/opt/velocita/runtimes'
DESTINATION = PARENT + '/' + IMAGE + '/rootfs'
MARKER = 'etc/velocita-runtime-image'
MAX_ENTRIES = 200000
MAX_FILE_BYTES = 2 * 1024**3
MAX_TOTAL_BYTES = 32 * 1024**3
MAX_MANIFEST_BYTES = 64 * 1024**2
CHUNK = 1024**2


class Refused(ValueError):
    pass


def manifest_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + '\n').encode()


def valid_path(path):
    if not isinstance(path, str) or not path or len(path.encode()) > 4096 or any(ord(c) < 32 for c in path):
        raise Refused('invalid relative manifest path')
    if path.startswith('/') or path.count('/') > 128 or any(p in ('', '.', '..') for p in path.split('/')):
        raise Refused('manifest path escape/normalization refused')
    if path == MARKER: raise Refused('preexisting runtime marker refused')
    return path


def validate_manifest(value):
    if not isinstance(value, dict) or set(value) != {'schema', 'image', 'entries'} or type(value['schema']) is not int or value['schema'] != 1 or value['image'] != IMAGE:
        raise Refused('fixed schema/image required')
    entries = value['entries']
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_ENTRIES:
        raise Refused('bounded nonempty entry inventory required')
    seen, total = {}, 0
    for entry in entries:
        if not isinstance(entry, dict): raise Refused('entry object required')
        path = valid_path(entry.get('path'))
        if path in seen: raise Refused('duplicate manifest path')
        kind = entry.get('type')
        fields = {'path', 'type', 'mode'}
        if kind == 'file':
            fields |= {'size', 'sha256'}
            if type(entry.get('size')) is not int or not 0 <= entry['size'] <= MAX_FILE_BYTES or not isinstance(entry.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', entry['sha256']):
                raise Refused('invalid bounded file content pin')
            if type(entry.get('mode')) is not int or entry['mode'] not in (0o644, 0o755):
                raise Refused('clean regular file mode required')
            total += entry['size']
        elif kind == 'directory':
            if type(entry.get('mode')) is not int or entry['mode'] != 0o755:
                raise Refused('clean directory mode required')
        elif kind == 'symlink':
            fields |= {'target'}
            target = entry.get('target')
            if type(entry.get('mode')) is not int or entry['mode'] != 0o777 or not isinstance(target, str) or not target or '\x00' in target or len(target.encode()) > 4096:
                raise Refused('invalid symlink target/mode')
        else: raise Refused('special file refused')
        if set(entry) != fields: raise Refused('unexpected entry fields')
        seen[path] = kind
    if total > MAX_TOTAL_BYTES: raise Refused('runtime byte bound exceeded')
    if [e['path'] for e in entries] != sorted(seen): raise Refused('sorted canonical entries required')
    for path in seen:
        parts = path.split('/')
        for end in range(1, len(parts)):
            if seen.get('/'.join(parts[:end])) != 'directory':
                raise Refused('missing or symlink manifest ancestor')
    if seen.get('etc') != 'directory': raise Refused('real etc directory required')
    return value


def open_source(path):
    value = os.fspath(path)
    if not value.startswith('/') or '..' in PurePosixPath(value).parts:
        raise Refused('absolute source without parent traversal required')
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for part in PurePosixPath(value).parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd); fd = child
        if stat.S_IMODE(os.fstat(fd).st_mode) != 0o755:
            raise Refused('source root must be normalized 0755')
        return fd
    except BaseException:
        os.close(fd); raise


def relative_directory(rootfd, path):
    fd = os.dup(rootfd)
    try:
        for part in path.split('/') if path else ():
            if part in ('', '.', '..'): raise Refused('relative directory path invalid')
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd); fd = child
        return fd
    except BaseException:
        os.close(fd); raise


def file_digest(fd):
    before = os.fstat(fd)
    if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) not in (0o644, 0o755) or before.st_size > MAX_FILE_BYTES:
        raise Refused('special/unclean/oversized regular file refused')
    digest, size = hashlib.sha256(), 0
    while True:
        raw = os.read(fd, CHUNK)
        if not raw: break
        size += len(raw)
        if size > MAX_FILE_BYTES: raise Refused('file grew beyond bound')
        digest.update(raw)
    after = os.fstat(fd)
    if (before.st_dev, before.st_ino, before.st_size, before.st_mode, before.st_mtime_ns, before.st_ctime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mode, after.st_mtime_ns, after.st_ctime_ns) or size != before.st_size:
        raise Refused('source file changed during read')
    return size, digest.hexdigest()


def scan_tree(rootfd):
    result, total = [], 0
    def walk(fd, prefix):
        nonlocal total
        for name in sorted(os.listdir(fd)):
            path = valid_path(prefix + name)
            meta = os.stat(name, dir_fd=fd, follow_symlinks=False)
            mode = stat.S_IMODE(meta.st_mode)
            if stat.S_ISDIR(meta.st_mode):
                if mode != 0o755: raise Refused('unclean directory mode')
                child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                try:
                    actual = os.fstat(child)
                    if (actual.st_dev, actual.st_ino, actual.st_mode) != (meta.st_dev, meta.st_ino, meta.st_mode):
                        raise Refused('source directory changed')
                    result.append(dict(path=path, type='directory', mode=mode))
                    walk(child, path + '/')
                finally: os.close(child)
            elif stat.S_ISREG(meta.st_mode):
                child = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
                try:
                    actual = os.fstat(child)
                    if (actual.st_dev, actual.st_ino, actual.st_mode) != (meta.st_dev, meta.st_ino, meta.st_mode):
                        raise Refused('source file replaced')
                    size, digest = file_digest(child)
                finally: os.close(child)
                total += size
                result.append(dict(path=path, type='file', mode=mode, size=size, sha256=digest))
            elif stat.S_ISLNK(meta.st_mode):
                result.append(dict(path=path, type='symlink', mode=mode, target=os.readlink(name, dir_fd=fd)))
            else: raise Refused('special file refused: ' + path)
            if len(result) > MAX_ENTRIES or total > MAX_TOTAL_BYTES:
                raise Refused('runtime inventory bound exceeded')
    walk(rootfd, '')
    return sorted(result, key=lambda e: e['path'])


def build_manifest(source):
    fd = open_source(source)
    try: return validate_manifest(dict(schema=1, image=IMAGE, entries=scan_tree(fd)))
    finally: os.close(fd)


def verify_pin(manifest, expected_sha256):
    validate_manifest(manifest)
    if not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256) or hashlib.sha256(manifest_bytes(manifest)).hexdigest() != expected_sha256:
        raise Refused('manifest SHA256 hash mismatch')


def review(source, manifest, expected_sha256):
    verify_pin(manifest, expected_sha256)
    if build_manifest(source) != manifest: raise Refused('source tree differs from approved manifest')
    return dict(schema=1, source_verified=True, image=IMAGE, manifest_sha256=expected_sha256,
                entries=len(manifest['entries']), bytes=sum(e.get('size', 0) for e in manifest['entries']),
                destination=DESTINATION, installed=False, units_installed=False, live_admitted=False)


def require_root_host():
    if sys.platform != 'linux' or socket.gethostname() != HOST or os.getuid() != 0 or os.geteuid() != 0:
        raise Refused('apply requires root on fixed Linux NUC1 host')


def set_owner_mode(fd, mode):
    os.fchown(fd, 0, 0)
    os.fchmod(fd, mode)


def open_destination():
    """Only new release; root-owned no-follow ancestors, never repair existing paths."""
    parent = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for name in PurePosixPath(PARENT).parts[1:]:
            created = False
            try:
                os.mkdir(name, 0o755, dir_fd=parent)
                created = True
                os.fsync(parent)
            except FileExistsError: pass
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            os.close(parent); parent = child
            meta = os.fstat(parent)
            if created:
                # mkdir observes the invoking root process's umask. Normalize
                # only this new inode through its no-follow descriptor; existing
                # directory modes/owners are never silently repaired.
                if meta.st_uid != 0:
                    raise Refused('new destination ancestor replaced before ownership assignment')
                set_owner_mode(parent, 0o755)
                os.fsync(parent)
                meta = os.fstat(parent)
            if meta.st_uid != 0 or meta.st_gid != 0 or stat.S_IMODE(meta.st_mode) != 0o755:
                raise Refused('destination ancestor must be root/root 0755')
        os.mkdir(IMAGE, 0o700, dir_fd=parent)  # no overwrite or partial resume
        os.fsync(parent)
        release = os.open(IMAGE, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
        try:
            os.mkdir('rootfs', 0o755, dir_fd=release)
            rootfs = os.open('rootfs', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=release)
            set_owner_mode(rootfs, 0o755)
            os.fsync(release)
            return release, rootfs
        except BaseException:
            os.close(release); raise
    finally: os.close(parent)


def write_all(fd, raw):
    view = memoryview(raw)
    while view:
        written = os.write(fd, view)
        if written <= 0: raise Refused('short write')
        view = view[written:]


def copy_tree(sourcefd, destinationfd, manifest):
    validate_manifest(manifest)
    entries = manifest['entries']
    for entry in entries:
        if entry['type'] != 'directory': continue
        parent, _, name = entry['path'].rpartition('/')
        fd = relative_directory(destinationfd, parent)
        try:
            os.mkdir(name, 0o755, dir_fd=fd)
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            try: set_owner_mode(child, 0o755)
            finally: os.close(child)
        finally: os.close(fd)
    for entry in entries:
        if entry['type'] != 'file': continue
        parent, _, name = entry['path'].rpartition('/')
        srcparent = relative_directory(sourcefd, parent)
        dstparent = relative_directory(destinationfd, parent)
        source = destination = None
        try:
            source = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=srcparent)
            meta = os.fstat(source)
            if not stat.S_ISREG(meta.st_mode) or stat.S_IMODE(meta.st_mode) != entry['mode'] or meta.st_size != entry['size']:
                raise Refused('source file shape changed before copy')
            destination = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=dstparent)
            digest, size = hashlib.sha256(), 0
            while True:
                raw = os.read(source, CHUNK)
                if not raw: break
                size += len(raw)
                if size > entry['size']: raise Refused('source file grew during copy')
                digest.update(raw); write_all(destination, raw)
            if size != entry['size'] or digest.hexdigest() != entry['sha256']:
                raise Refused('source content changed during copy')
            set_owner_mode(destination, entry['mode']); os.fsync(destination)
        finally:
            if source is not None: os.close(source)
            if destination is not None: os.close(destination)
            os.close(srcparent); os.close(dstparent)
    # Symlinks last. Absolute symlinks are meaningful only inside the final
    # RootDirectory; neither scan nor copying traverses any symlink.
    for entry in entries:
        if entry['type'] != 'symlink': continue
        parent, _, name = entry['path'].rpartition('/')
        srcparent = relative_directory(sourcefd, parent)
        dstparent = relative_directory(destinationfd, parent)
        try:
            meta = os.stat(name, dir_fd=srcparent, follow_symlinks=False)
            if not stat.S_ISLNK(meta.st_mode) or os.readlink(name, dir_fd=srcparent) != entry['target']:
                raise Refused('source symlink changed')
            os.symlink(entry['target'], name, dir_fd=dstparent)
        finally: os.close(srcparent); os.close(dstparent)
    if scan_tree(sourcefd) != entries: raise Refused('source inventory changed during copy')
    if scan_tree(destinationfd) != entries: raise Refused('copied runtime differs from manifest')
    for entry in reversed(entries):
        if entry['type'] == 'directory':
            fd = relative_directory(destinationfd, entry['path'])
            try: os.fsync(fd)
            finally: os.close(fd)
    os.fsync(destinationfd)


def new_file(parent, name, raw, mode=0o444):
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=parent)
    try:
        write_all(fd, raw); set_owner_mode(fd, mode); os.fsync(fd)
    finally: os.close(fd)


def complete_install(release, rootfs, manifest, digest):
    # Caller has verified the whole destination. No marker on a failed copy.
    new_file(release, 'rootfs-manifest.json', manifest_bytes(manifest))
    etc = relative_directory(rootfs, 'etc')
    try:
        new_file(etc, 'velocita-runtime-image', (IMAGE + '\n').encode())
        os.fsync(etc)
    finally: os.close(etc)
    record = dict(schema=1, image=IMAGE, manifest_sha256=digest, entries=len(manifest['entries']), complete=True)
    new_file(release, 'installation.json', manifest_bytes(record))
    os.fsync(rootfs); os.fsync(release)
    # Pending/failed installs stay 0700 and cannot be reused automatically.
    set_owner_mode(release, 0o755); os.fsync(release)


def install(source, manifest, expected_sha256):
    require_root_host()
    result = review(source, manifest, expected_sha256)
    sourcefd = open_source(source)
    release = rootfs = None
    try:
        release, rootfs = open_destination()
        copy_tree(sourcefd, rootfs, manifest)
        complete_install(release, rootfs, manifest, expected_sha256)
    finally:
        os.close(sourcefd)
        if rootfs is not None: os.close(rootfs)
        if release is not None: os.close(release)
    result.update(installed=True)
    return result


def load_manifest(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or not 0 < meta.st_size <= MAX_MANIFEST_BYTES:
            raise Refused('bounded regular manifest file required')
        parts, size = [], 0
        while True:
            raw = os.read(fd, CHUNK)
            if not raw: break
            parts.append(raw); size += len(raw)
            if size > MAX_MANIFEST_BYTES: raise Refused('manifest grew beyond bound')
    finally: os.close(fd)
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result: raise Refused('duplicate JSON field')
            result[key] = value
        return result
    raw = b''.join(parts)
    value = json.loads(raw, object_pairs_hook=pairs)
    if raw != manifest_bytes(value): raise Refused('canonical manifest bytes required')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--write-manifest', help='create fresh public review manifest; never modifies source')
    parser.add_argument('--manifest')
    parser.add_argument('--manifest-sha256')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if args.write_manifest:
        if args.manifest or args.manifest_sha256 or args.apply: parser.error('manifest generation is separate from review/apply')
        if os.getuid() == 0 or os.geteuid() == 0: raise Refused('prepare manifest without root')
        value = build_manifest(args.source)
        raw = manifest_bytes(value)
        fd = os.open(args.write_manifest, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
        try: write_all(fd, raw); os.fsync(fd)
        finally: os.close(fd)
        print(json.dumps(dict(image=IMAGE, manifest_sha256=hashlib.sha256(raw).hexdigest(), entries=len(value['entries']), destination=DESTINATION)))
    else:
        if not args.manifest or not args.manifest_sha256: parser.error('review/apply requires manifest and SHA256 pin')
        value = load_manifest(args.manifest)
        print(json.dumps((install if args.apply else review)(args.source, value, args.manifest_sha256), sort_keys=True))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, TypeError, KeyError) as exc:
        print('REFUSED: ' + str(exc) + '; preserve partial output; no retry or overwrite', file=sys.stderr)
        raise SystemExit(64)
