"""Apply a hash-pinned review refresh or startup flag transition; never start services."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parent))
import adopt_account as A
import credential_handoff as H

P=A.PROFILE
I=A.INSTALL
L=A.LAUNCH

def digest(raw):return hashlib.sha256(raw).hexdigest()

def validate(mode,old,new,before,after,now):
    P.validate(old);P.validate(new)
    if set(before)!=set(A.DOCUMENTS) or set(after)!=set(A.DOCUMENTS):raise ValueError('exact documents required')
    for manifest,files in ((old,before),(new,after)):
        for name,key in A.DOCUMENTS.items():
            if key and digest(files[name])!=manifest[key]:raise ValueError('document pin mismatch')
        if digest(files['transaction.review'])!=L.fields(files['runtime.profile'])['transaction_review_digest']:raise ValueError('transaction pin mismatch')
    allowed=P.PENDING_ACCOUNT_PINS if mode=='refresh' else {'launch_authorized','initialize_swap_recovery'}
    if any(old.get(k)!=new.get(k) for k in (set(old)|set(new))-allowed):raise ValueError('unrelated manifest change')
    if old.get('recovery_only',False) or new.get('recovery_only',False):raise ValueError('recovery transition out of scope')
    if mode=='refresh':
        if old['launch_authorized'] or new['launch_authorized'] or old.get('initialize_swap_recovery',False) or new.get('initialize_swap_recovery',False):raise ValueError('refresh requires dormant unused profile')
        previous,current=map(L.fields,(before['runtime.profile'],after['runtime.profile']))
        mutable={'account_review_digest','transaction_review_digest','session_evidence','expires_at_ms'}
        if any(previous.get(k)!=current.get(k) for k in (set(previous)|set(current))-mutable):raise ValueError('refresh changed identity or credential')
        old_history,new_history=map(L.strict_json,(before['account.history'],after['account.history']))
        if any(old_history[k]!=new_history[k] for k in ('base_atoms','quote_atoms','owner_sha256','user_id_sha256')):raise ValueError('account balance or identity changed')
    elif mode=='arm':
        if old['launch_authorized'] or old.get('initialize_swap_recovery',False) or not new['launch_authorized'] or new.get('initialize_swap_recovery') is not True:raise ValueError('invalid first-start transition')
        if before!=after:raise ValueError('startup cannot change reviewed documents')
    elif mode=='seal':
        if not old['launch_authorized'] or old.get('initialize_swap_recovery') is not True or not new['launch_authorized'] or new.get('initialize_swap_recovery') is not False:raise ValueError('invalid recovery transition')
        if before!=after:raise ValueError('startup cannot change reviewed documents')
    else:raise ValueError('unknown transition')
    for role in ('gateway','signer'):L.check_bindings(new,after['runtime.profile'],after['account.review'],after['account.history'],now,role)
    tx=dict(line.split('=',1) for line in after['transaction.review'].decode('ascii').splitlines())
    if L.unsigned(tx['expires_at_ms'])<=now:raise ValueError('transaction review expired')

def bundle(base,pin):
    raw=I.read(base/'transition.json')
    if digest(raw)!=pin:raise ValueError('bundle pin mismatch')
    spec=L.strict_json(raw)
    expected={'old-manifest.json','new-manifest.json'}|{prefix+name for prefix in ('old-','new-') for name in A.DOCUMENTS}
    if set(spec)!={'schema','mode','files'} or spec['schema']!=1 or set(spec['files'])!=expected:raise ValueError('exact transition bundle required')
    files={name:I.read(base/name) for name in expected}
    if any(digest(body)!=spec['files'][name] for name,body in files.items()):raise ValueError('bundle content changed')
    old,new=(L.strict_json(files[k+'-manifest.json']) for k in ('old','new'))
    before,after=({name:files[k+'-'+name] for name in A.DOCUMENTS} for k in ('old','new'))
    validate(spec['mode'],old,new,before,after,time.time_ns()//1000000)
    return spec['mode'],old,new,before,after

def state(mode):
    if mode!='seal':A.stopped()
    for role in ('gateway','signer'):
        root=Path('/var/lib/velocita-swap-'+role+'/recovery')
        L.directory(str(root),29001 if role=='gateway' else 29002,29001 if role=='gateway' else 29002,0o700)
        entries=set(p.name for p in root.iterdir())
        expected={'prepared','submitted','signing-requests','results'} if role=='gateway' else {'protected-signing'}
        if mode=='seal':
            if entries!=expected:raise ValueError('expected initialized journals missing')
            if I.command(['/usr/bin/systemctl','show','velocita-swap-'+role+'.service','--property=ActiveState','--value'])!='active':raise ValueError('startup not active')
        elif entries:raise ValueError('refuse replacing an initialized run')

def apply(base,pin):
    I.PROVISION.require_host_root();H.harden()
    mode,old,new,before,after=bundle(base,pin);state(mode)
    old_units,new_units=P.render(old),P.render(new)
    replacements={str(Path(P.PUBLIC)/'manifest.json'):(P.manifest_bytes(old),P.manifest_bytes(new))}
    for name in A.DOCUMENTS:replacements[str(Path(P.PUBLIC)/name)]=(before[name],after[name])
    for role in ('gateway','signer'):
        name='velocita-swap-'+role+'.service'
        replacements['/etc/systemd/system/'+name]=(old_units[name+'.review'].encode(),new_units[name+'.review'].encode())
    for path,(previous,_) in replacements.items():I.installed_bytes(path,previous)
    for path,pin_value in ((new['binary'],new['binary_sha256']),(P.PUBLIC+'/catalog.bin',new['catalog_sha256'])):L.pinned_bytes(path,pin_value,64*1024**2)
    profile=L.fields(after['runtime.profile'])
    for role,(uid,leaf) in H.ROLES.items():
        parent=Path('/run/velocita-swap-credentials')/role
        H.tmpfs(parent);secret=H.private_read(parent/leaf,uid)
        H.validate_secret(role,secret,profile,time.time_ns()//1000000)
    changed={path:pair for path,pair in replacements.items() if pair[0]!=pair[1]}
    archive=Path(P.PUBLIC)/('before-'+mode+'-'+pin);I.fresh(str(archive))
    for path in changed:I.fresh(path+'.'+pin+'.pending')
    I.mkdir(str(archive),mode=0o700)
    def write(path,raw):
        parent=I.PROVISION.open_directory(str(path.parent))
        try:I.RUNTIME.new_file(parent,path.name,raw,0o444);os.fsync(parent)
        finally:os.close(parent)
    for path,(previous,next_bytes) in changed.items():
        write(archive/Path(path).name,previous)
        write(Path(path+'.'+pin+'.pending'),next_bytes)
    validate(mode,old,new,before,after,time.time_ns()//1000000);state(mode)
    for path,(previous,_) in replacements.items():I.installed_bytes(path,previous)
    for path in changed:
        parent=I.PROVISION.open_directory(str(Path(path).parent))
        try:os.rename(Path(path).name+'.'+pin+'.pending',Path(path).name,src_dir_fd=parent,dst_dir_fd=parent);os.fsync(parent)
        finally:os.close(parent)
    I.command(['/usr/bin/systemctl','daemon-reload'])
    for path,(_,body) in replacements.items():I.installed_bytes(path,body)
    return dict(transition=mode,manifest_sha256=digest(P.manifest_bytes(new)),services_started=False,orders_submitted=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle',type=Path,required=True);parser.add_argument('--sha256',required=True);parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    if args.apply:result=apply(args.bundle,args.sha256)
    else:result=dict(validated=bundle(args.bundle,args.sha256)[0],applied=False)
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':
    try:main()
    except Exception as error:
        print('REFUSED '+type(error).__name__+': '+str(error)+'; preserve partial state, no automatic retry',file=sys.stderr);raise SystemExit(64)
