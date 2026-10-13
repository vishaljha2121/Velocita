#!/usr/bin/env python3
"""One owner-run preparation and synthetic drill; no production activation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import provision_services as PROVISION
import install_runtime as RUNTIME
import communication_probe as COMM


def helper_bytes(path, digest):
    if not isinstance(digest, str) or re.fullmatch('[0-9a-f]{64}', digest) is None:
        raise ValueError('helper hash required')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or meta.st_nlink != 1 or meta.st_mode & 0o6022 or not 0 < meta.st_size <= 16*1024**2:
            raise ValueError('bounded non-writable, non-setid helper required')
        data = bytearray()
        while part := os.read(fd, 65536):
            data.extend(part)
            if len(data) > 16*1024**2: raise ValueError('helper grew')
        if not data.startswith(b'\x7fELF') or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('native helper content differs')
        return bytes(data)
    finally: os.close(fd)


def install_helper(source, digest):
    PROVISION.require_host_root()
    raw = helper_bytes(source, digest)
    parent = PROVISION.open_directory('/opt/velocita')
    try:
        created = False
        try:
            os.mkdir('qualification', 0o700, dir_fd=parent); created = True
        except FileExistsError: pass
        if created:
            child = os.open('qualification', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            try:
                if os.fstat(child).st_uid != 0: raise ValueError('new helper directory owner changed')
                os.fchmod(child, 0o755); os.fsync(child)
            finally: os.close(child)
    finally: os.close(parent)
    parent = PROVISION.open_directory('/opt/velocita/qualification')
    try:
        if stat.S_IMODE(os.fstat(parent).st_mode) != 0o755:
            raise ValueError('helper parent must be exactly0755')
        os.mkdir(digest, 0o700, dir_fd=parent)  # Never overwrite/resume.
        directory = os.open(digest, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
        try:
            RUNTIME.new_file(directory, 'communication-probe', raw, 0o555)
            os.fsync(directory); os.fchmod(directory, 0o755); os.fsync(directory)
        finally: os.close(directory)
        os.fsync(parent)
    finally: os.close(parent)
    return {'helper_installed': True, 'binary_sha256': digest, 'live_admitted': False}


def bundle(base):
    value = json.loads((base/'bundle.json').read_text())
    if set(value) != {'schema','files','helper_sha256','runtime_manifest_sha256','runtime_source'} or value['schema'] != 1:
        raise ValueError('exact setup bundle required')
    if value['runtime_source'] != '/home/velocitas/.cache/velocita-build/swap-services-20261013-r4/rootfs-stage':
        raise ValueError('fixed prepared runtime source required')
    required = {'owner_setup.py','provision_services.py','install_runtime.py','communication_probe.py',
                'service_profile.py','communication-probe','rootfs-manifest.json'}
    if set(value['files']) != required: raise ValueError('exact setup artifact set required')
    for name, digest in value['files'].items():
        path = base/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('setup artifact hash mismatch: '+name)
    if value['files']['communication-probe'] != value['helper_sha256'] or value['files']['rootfs-manifest.json'] != value['runtime_manifest_sha256']:
        raise ValueError('setup content pin disagreement')
    return value


def commands(base, pins):
    python = ['/usr/bin/sudo','-n','/usr/bin/python3','-I','-B']
    return [python+[str(base/'provision_services.py'),'--apply'],
            python+[str(base/'install_runtime.py'),'--source',pins['runtime_source'],
                    '--manifest',str(base/'rootfs-manifest.json'),'--manifest-sha256',pins['runtime_manifest_sha256'],'--apply'],
            python+[str(base/'owner_setup.py'),'--install-helper','--binary-sha256',pins['helper_sha256']]]


def run(base, output, runner=subprocess.run):
    if sys.platform != 'linux' or COMM.socket.gethostname() != 'bulbazar-NUC' or os.getuid() != 1002 or os.geteuid() != 1002:
        raise ValueError('run as verified owner vishal on NUC1 after sudo -v')
    pins = bundle(base)
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    result = {'production_services_installed':False, 'credentials_installed':False,
              'live_admitted':False, 'steps':[], 'complete':False}
    try:
        for number, cmd in enumerate(commands(base,pins),1):
            print('Setup step '+str(number)+'/3; evidence: '+str(output/f'step-{number}.log'), flush=True)
            bundle(base)  # Recheck supplied artifacts before each privileged call.
            with (output/f'step-{number}.log').open('x') as log:
                child = runner(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=240, check=False)
            result['steps'].append({'number':number,'command':cmd,'returncode':child.returncode})
            if child.returncode: raise ValueError('setup step failed; inspect preserved log; do not rerun')
        result['communication'] = COMM.run(output/'communication', COMM.ROOTFS, pins['helper_sha256'])
        result['complete'] = result['communication']['production_uid_communication_qualified']
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        result['error'] = str(exc)
    finally:
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',action='store_true')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--install-helper',action='store_true',help=argparse.SUPPRESS)
    parser.add_argument('--binary-sha256',help=argparse.SUPPRESS)
    args=parser.parse_args();base=Path(__file__).resolve().parent
    if args.install_helper:
        if args.run or args.output: raise ValueError('separate privileged helper install')
        print(json.dumps(install_helper(base/'communication-probe',args.binary_sha256)));return 0
    if not args.run:
        print('Prepared: provision two fixed users/private directories, adopt pinned runtime, install synthetic helper, run two bounded transient services. No production activation or real credentials.');return 0
    if args.output is None: parser.error('--run requires a fresh --output directory')
    result=run(base,args.output)
    print(json.dumps({key:result[key] for key in ('complete','production_services_installed','credentials_installed','live_admitted')}))
    return 0 if result['complete'] else 1


if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,OSError,KeyError) as exc:
        print('REFUSED: '+str(exc)+'; preserve output; no retry',file=sys.stderr);raise SystemExit(64)
