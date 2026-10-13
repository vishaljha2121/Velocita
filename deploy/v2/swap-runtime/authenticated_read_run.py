#!/usr/bin/env python3
"""One bounded authenticated state read per installed service user; never trading.

Credential/session provisioning and caller handoff are separate owner steps.
Default invocation prints commands only. --run requires root on NUC1, stopped
gateway/signer services, existing private sessions and caller keys. No retries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from communication_probe import ROOTFS, ROLE_UID, VOTERS, identities, image_userspace, immutable_file, token
from fd_handoff import open_pinned_public
from provision_services import open_directory
from service_profile import signer_network_properties

HELPER = '/opt/velocita-authenticated-read-probe'
CATALOG = '/etc/velocita-swap/public/catalog.bin'
PAPER_UNIT = 'velocita-paper-stochastic-gateway.service'
FIELDS = set('role uid caller key_id session_epoch term applied_index leader state_version catalog_root authority_domain state_digest authenticated_voter_read read_only live_admitted'.split())


def pins(helper_sha, catalog_sha, incarnation, catalog_root, domain):
    for value, length in ((helper_sha,64),(catalog_sha,64),(incarnation,32),(catalog_root,64),(domain,64)):
        token(value,length)
        if set(value)=={'0'}: raise ValueError('nonzero independent pins required')
    return dict(helper_sha256=helper_sha,catalog_sha256=catalog_sha,incarnation=incarnation,
                catalog_root=catalog_root,authority_domain=domain)


def command(role, run_id, config):
    if role not in ROLE_UID: raise ValueError('fixed role required')
    token(run_id,16)
    pins(config['helper_sha256'],config['catalog_sha256'],config['incarnation'],config['catalog_root'],config['authority_domain'])
    root='/run/velocita-authread-'+run_id+'-'+role
    state='/var/lib/velocita-swap-'+role
    key='/etc/velocita-swap/'+role
    source='/run/velocita-swap-credentials/'+role
    props=['User='+str(ROLE_UID[role]),'Group='+str(ROLE_UID[role]),'UMask=0077',
        'NoNewPrivileges=yes','ProtectSystem=strict','ProtectHome=yes','PrivateTmp=yes','PrivateDevices=yes',
        'ProtectKernelTunables=yes','ProtectKernelModules=yes','ProtectControlGroups=yes','RestrictSUIDSGID=yes',
        'LockPersonality=yes','MemoryDenyWriteExecute=yes','LimitCORE=0','CapabilityBoundingSet=',
        'AmbientCapabilities=','Restart=no','RuntimeMaxSec=60','TimeoutStopSec=2','MemoryMax=512M',
        'TasksMax=32','CPUAffinity=3','CPUQuota=100%','RootDirectory='+str(ROOTFS),'MountAPIVFS=yes',
        # An explicit bind outranks systemd249's API-VFS /run fallback. The
        # immutable empty parent is on disk; the role runtime child remains tmpfs.
        'BindReadOnlyPaths=/etc/velocita-swap/runtime-roots/'+role+':/run',
        'BindReadOnlyPaths=/opt/velocita/qualification/'+config['helper_sha256']+'/authenticated-read-probe:'+HELPER,
        'BindReadOnlyPaths='+CATALOG+':'+CATALOG,
        'BindReadOnlyPaths='+source+'/caller.key:'+key+'/caller.key',
        'BindPaths='+state+':'+state,'ReadWritePaths='+state,
        'Environment=AERON_TERM_BUFFER_LENGTH=1048576',
        'RuntimeDirectory='+Path(root).name,'RuntimeDirectoryMode=0700','BindPaths='+root+':'+root,
        'InaccessiblePaths=-/etc/velocita-swap/'+('signer' if role=='gateway' else 'gateway'),
        'InaccessiblePaths=-/var/lib/velocita-swap-'+('signer' if role=='gateway' else 'gateway')]
    if role=='gateway': props.append('SupplementaryGroups=29002')
    props += signer_network_properties(VOTERS)
    args=[HELPER,'--role',role,'--incarnation',config['incarnation'],'--catalog-root',config['catalog_root'],
        '--authority-domain',config['authority_domain'],'--response-port','44413' if role=='gateway' else '44414',
        '--key-file',key+'/caller.key','--session-file',state+'/client.session','--catalog',CATALOG,
        '--aeron-dir',root+'/aeron','--run']
    return ['/usr/bin/systemd-run','--quiet','--wait','--pipe','--collect',
            '--unit=velocita-authread-'+run_id+'-'+role]+['--property='+p for p in props]+args


def no_duplicate(pairs):
    out={}
    for key,value in pairs:
        if key in out: raise ValueError('duplicate result field')
        out[key]=value
    return out


def evaluate(role, config, completed):
    if completed.returncode != 0: raise ValueError('native authenticated read failed')
    data=json.loads(completed.stdout,object_pairs_hook=no_duplicate)
    if not isinstance(data,dict) or set(data)!=FIELDS: raise ValueError('native result schema differs')
    expected=dict(role=role,uid=ROLE_UID[role],caller=22 if role=='gateway' else 23,
                  key_id=13 if role=='gateway' else 14,catalog_root=config['catalog_root'],
                  authority_domain=config['authority_domain'],authenticated_voter_read=True,
                  read_only=True,live_admitted=False)
    for key,value in expected.items():
        if type(data[key]) is not type(value) or data[key]!=value: raise ValueError('native identity or assertion differs')
    for key in ('session_epoch','term','applied_index','state_version'):
        if type(data[key]) is not int or data[key]<=0: raise ValueError('positive read metadata required')
    if type(data['leader']) is not int or data['leader'] not in (1,2,3): raise ValueError('leader differs')
    token(data['state_digest'],64)
    if set(data['state_digest'])=={'0'}: raise ValueError('nonzero verified state digest required')
    return data


def collision_check(runner):
    units=[(['/usr/bin/systemctl'], 'velocita-swap-'+role+'.service') for role in ('gateway','signer')]
    units.append((['/usr/sbin/runuser','-u','velocitas','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1003',
                   'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1003/bus',
                   '/usr/bin/systemctl','--user'],PAPER_UNIT))
    for prefix,unit in units:
        result=runner(prefix+['show',unit,'--property=ActiveState','--value'],capture_output=True,text=True,timeout=10,check=False)
        if result.returncode or result.stdout.strip()!='inactive': raise ValueError('production or paper caller is not inactive')
    # Both response endpoints must be free. A shell is deliberately not involved.
    result=runner(['/usr/bin/ss','-H','-u','-a','-n','sport = :44413 or sport = :44414'],
                  capture_output=True,text=True,timeout=10,check=False)
    if result.returncode or result.stdout.strip(): raise ValueError('caller response endpoint already in use')


def inputs(config):
    identities(); image_userspace(ROOTFS)
    immutable_file(Path('/opt/velocita/qualification')/config['helper_sha256']/'authenticated-read-probe',config['helper_sha256'])
    fd=open_pinned_public(CATALOG,config['catalog_sha256']);os.close(fd)
    for role,uid in ROLE_UID.items():
        directory=open_directory('/etc/velocita-swap/runtime-roots/'+role)
        try:
            info=os.fstat(directory)
            if (info.st_uid,info.st_gid,stat.S_IMODE(info.st_mode))!=(0,0,0o755):
                raise ValueError('root-owned0755 runtime namespace parent required')
        finally: os.close(directory)
        # Metadata only. Credentials and session bytes are opened only by their role helper.
        for name in ('/run/velocita-swap-credentials/'+role+'/caller.key',
                     '/var/lib/velocita-swap-'+role+'/client.session'):
            info=Path(name).lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid!=uid or stat.S_IMODE(info.st_mode)!=0o600 or info.st_nlink!=1:
                raise ValueError('existing private caller credential/session required')


def run(output,config,runner=subprocess.run):
    if sys.platform!='linux' or socket.gethostname()!='bulbazar-NUC' or os.geteuid()!=0:
        raise ValueError('root owner invocation on NUC1 required')
    inputs(config); collision_check(runner)
    output=Path(output);output.mkdir(mode=0o700,parents=False,exist_ok=False)
    run_id=os.urandom(8).hex()
    commands={role:command(role,run_id,config) for role in ('signer','gateway')}
    report=dict(run=run_id,pins=config,commands=commands,results=[],authenticated_voter_reads=False,live_admitted=False)
    def save(name,value):
        with (output/name).open('x') as stream:
            os.chmod(output/name,0o600);json.dump(value,stream,indent=2);stream.write('\n')
    try:
        for role in ('signer','gateway'):
            collision_check(runner)
            try:
                result=runner(commands[role],capture_output=True,text=True,timeout=75,check=False)
            except subprocess.TimeoutExpired as exc:
                save(role+'.json',dict(error_type='TimeoutExpired',stdout=str(exc.stdout or ''),stderr=str(exc.stderr or '')))
                raise
            save(role+'.json',dict(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
            report['results'].append(evaluate(role,config,result))
        report['authenticated_voter_reads']=True
    except (OSError,ValueError,subprocess.SubprocessError) as exc:
        report['error_type']=type(exc).__name__;report['error']=str(exc)
    finally:
        # Stop only this run's two transient units; never any cluster/production unit.
        report['cleanup']=[]
        for role in ('signer','gateway'):
            unit='velocita-authread-'+run_id+'-'+role+'.service'
            try:
                result=runner(['/usr/bin/systemctl','show',unit,'--property=ActiveState','--value'],
                              capture_output=True,text=True,timeout=8,check=False)
                if result.stdout.strip() not in ('inactive','') or result.returncode not in (0,1):
                    stopped=runner(['/usr/bin/systemctl','stop',unit],capture_output=True,text=True,timeout=8,check=False)
                    report['cleanup'].append(dict(unit=unit,returncode=stopped.returncode))
                    if stopped.returncode: report['authenticated_voter_reads']=False
            except (OSError,subprocess.SubprocessError) as exc:
                report['cleanup'].append(dict(unit=unit,error_type=type(exc).__name__))
                report['authenticated_voter_reads']=False
        report['source_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        save('result.json',report)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for option in ('helper-sha256','catalog-sha256','incarnation','catalog-root','authority-domain'):
        p.add_argument('--'+option,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--run',action='store_true')
    a=p.parse_args();config=pins(a.helper_sha256,a.catalog_sha256,a.incarnation,a.catalog_root,a.authority_domain)
    if not a.run:
        print(json.dumps(dict(commands={r:command(r,'ab'*8,config) for r in ('signer','gateway')},
                              authenticated_voter_reads=False,live_admitted=False),indent=2));return 0
    report=run(a.output,config)
    print(json.dumps({k:report[k] for k in ('authenticated_voter_reads','live_admitted')}))
    return 0 if report['authenticated_voter_reads'] else 1


if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError,ValueError,subprocess.SubprocessError):
        print('Authenticated read refused; preserve existing evidence and do not retry.',file=sys.stderr)
        raise SystemExit(1)
