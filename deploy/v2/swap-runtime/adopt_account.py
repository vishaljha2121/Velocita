"""Create the initial reviewed account documents in a dormant release; never activate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parent))
import install_disabled as INSTALL
import service_launcher as LAUNCH
import service_profile as PROFILE

DOCUMENTS={'runtime.profile':'runtime_profile_sha256','account.review':'account_review_sha256','account.history':'account_history_sha256','transaction.review':None}
UNITS=tuple('velocita-swap-'+role+'.service' for role in ('gateway','signer'))

def sha(raw):return hashlib.sha256(raw).hexdigest()

def validate(old,new,files,now):
    if (old.get('launch_authorized') is not False or new.get('launch_authorized') is not False or
        any(old.get(k)!=new.get(k) for k in (set(old)|set(new))-PROFILE.PENDING_ACCOUNT_PINS)):
        raise ValueError('only dormant account pins may change')
    PROFILE.validate(old);PROFILE.validate(new)
    if any(old[k] is not None or new[k] is None for k in PROFILE.PENDING_ACCOUNT_PINS):
        raise ValueError('initial account adoption requires absent prior pins')
    if set(files)!=set(DOCUMENTS):raise ValueError('exact account documents required')
    for name,key in DOCUMENTS.items():
        if key and sha(files[name])!=new[key]:raise ValueError('account document digest mismatch')
    profile=LAUNCH.fields(files['runtime.profile'])
    if sha(files['transaction.review'])!=profile.get('transaction_review_digest'):raise ValueError('account document digest mismatch')
    for role in ('gateway','signer'):
        LAUNCH.check_bindings(new,files['runtime.profile'],files['account.review'],files['account.history'],now,role)
    transaction={}
    for line in files['transaction.review'].decode('ascii').splitlines():
        key,sep,value=line.partition('=')
        if not sep or not key or not value or key in transaction or '\x00' in line:raise ValueError('transaction review fields')
        transaction[key]=value
    if LAUNCH.unsigned(transaction['expires_at_ms'])<=now:raise ValueError('transaction review expired')
    if (new['gateway_uid'],new['gateway_gid'],new['signer_uid'],new['socket_gid'],new['gateway_caller'],new['signer_caller'],new['gateway_key_id'],new['signer_key_id'])!=(29001,29001,29002,29002,22,23,13,14):
        raise ValueError('fixed production principals required')

def payload(base,pin):
    raw=INSTALL.read(base/'adoption.json')
    if sha(raw)!=pin:raise ValueError('adoption bundle digest mismatch')
    spec=LAUNCH.strict_json(raw)
    expected=set(DOCUMENTS)|{'previous-manifest.json','manifest.json'}
    if set(spec)!={'schema','files'} or type(spec['schema']) is not int or spec['schema']!=1 or set(spec['files'])!=expected:
        raise ValueError('exact adoption bundle required')
    data={name:INSTALL.read(base/name) for name in expected}
    if any(sha(body)!=spec['files'][name] for name,body in data.items()):raise ValueError('bundle file digest mismatch')
    old=LAUNCH.strict_json(data.pop('previous-manifest.json'));new=LAUNCH.strict_json(data.pop('manifest.json'))
    if any(len(data[n])>limit for n,limit in (('runtime.profile',8192),('account.review',8192),('account.history',4096),('transaction.review',32768))):raise ValueError('document size bound')
    validate(old,new,data,time.time_ns()//1000000)
    return old,new,data

def stopped():
    for unit in UNITS:
        for key,value in (('LoadState','loaded'),('ActiveState','inactive'),('SubState','dead'),('UnitFileState','static')):
            if INSTALL.command(['/usr/bin/systemctl','show',unit,'--property='+key,'--value'])!=value:
                raise ValueError('adoption requires inactive static services')

def install(base,pin):
    INSTALL.PROVISION.require_host_root()
    old,new,files=payload(base,pin);stopped()
    public=Path(PROFILE.PUBLIC)
    INSTALL.installed_bytes(str(public/'manifest.json'),PROFILE.manifest_bytes(old))
    for path,digest in ((new['binary'],new['binary_sha256']),(str(public/'catalog.bin'),new['catalog_sha256'])):
        LAUNCH.pinned_bytes(path,digest,64*1024**2)
    old_units=PROFILE.render(old);new_units=PROFILE.render(new)
    for unit in UNITS:INSTALL.installed_bytes('/etc/systemd/system/'+unit,old_units[unit+'.review'].encode())
    for name in files:INSTALL.fresh(str(public/name))
    archive=public/('before-account-'+pin)
    INSTALL.fresh(str(archive))
    for path in (str(public/'manifest.json'),*('/etc/systemd/system/'+n for n in UNITS)):
        INSTALL.fresh(path+'.'+pin+'.pending')
    INSTALL.mkdir(str(archive),mode=0o700)
    def write(parent,name,raw):
        fd=INSTALL.PROVISION.open_directory(str(parent))
        try:INSTALL.RUNTIME.new_file(fd,name,raw,0o444);os.fsync(fd)
        finally:os.close(fd)
    write(archive,'manifest.json',PROFILE.manifest_bytes(old))
    for unit in UNITS:write(archive,unit,old_units[unit+'.review'].encode())
    for name,body in files.items():write(public,name,body)
    replacements={str(public/'manifest.json'):(PROFILE.manifest_bytes(old),PROFILE.manifest_bytes(new))}
    replacements.update({'/etc/systemd/system/'+unit:(old_units[unit+'.review'].encode(),new_units[unit+'.review'].encode()) for unit in UNITS})
    for path,(_,body) in replacements.items():write(Path(path).parent,Path(path).name+'.'+pin+'.pending',body)
    validate(old,new,files,time.time_ns()//1000000);stopped()
    for path,(previous,_) in replacements.items():INSTALL.installed_bytes(path,previous)
    for path in replacements:
        fd=INSTALL.PROVISION.open_directory(str(Path(path).parent))
        try:
            leaf=Path(path).name;os.rename(leaf+'.'+pin+'.pending',leaf,src_dir_fd=fd,dst_dir_fd=fd);os.fsync(fd)
        finally:os.close(fd)
    INSTALL.command(['/usr/bin/systemctl','daemon-reload']);stopped()
    for name,body in files.items():INSTALL.installed_bytes(str(public/name),body)
    return dict(account_documents_installed=True,manifest_sha256=sha(PROFILE.manifest_bytes(new)),services_started=False,credentials_installed=False,live_admitted=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--sha256',required=True);p.add_argument('--apply',action='store_true');a=p.parse_args()
    if a.apply:result=install(a.bundle,a.sha256)
    else:
        payload(a.bundle,a.sha256)
        result=dict(validated=True,installed=False,services_started=False,live_admitted=False)
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':
    try:main()
    except Exception:
        print('REFUSED: dormant adoption failed; preserve partial state; no automatic retry',file=sys.stderr);raise SystemExit(64)
