"""Read local reviewed balances and journal progress; no credentials or venue access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import time

PUBLIC=Path('/etc/velocita-swap/public')
OUTPUT=Path('/home/velocitas/velocita-v2/textfile/velocita-live.prom')

def read(path,limit=32768):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        before=os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or not 0<before.st_size<=limit:raise ValueError('file metadata')
        raw=os.read(fd,limit+1);after=os.fstat(fd)
        if len(raw)!=before.st_size or (before.st_ino,before.st_mtime_ns,before.st_size)!=(after.st_ino,after.st_mtime_ns,after.st_size):raise ValueError('changed file')
        return raw
    finally:os.close(fd)

def fields(raw):
    out={}
    for line in raw.decode('ascii').splitlines():
        key,equal,value=line.partition('=')
        if not equal or key in out:raise ValueError('document fields')
        out[key]=value
    return out

def journal(raw,kind):
    size,magic=(464,b'VSSJ\1\0\0\0') if kind=='submission' else (8768,b'VPRP\1\0\0\0')
    if len(raw)!=size or raw[:8]!=magic or hashlib.sha256(raw[:-32]).digest()!=raw[-32:]:raise ValueError('journal checksum or schema')
    ordinal,phase,index,effect=struct.unpack_from('<QQQQ',raw,8)
    if not ordinal or not index or phase not in (1,2,3):raise ValueError('journal counters')
    return dict(ordinal=ordinal,phase=phase,index=index,effect=effect)

def account(history,review,now):
    captured=history['capture_finished_ms'];start=int(review['reviewed_at_ms']);end=int(review['expires_at_ms'])
    if not history['stable_window'] or history['blocking_orders'] or history['unsupported_accounts'] or not history['pagination_exhausted']:raise ValueError('unreviewable history')
    if not 0<captured<=start<=now or not start<end<=start+900000:raise ValueError('review timestamps')
    base,quote=history['base_atoms'],history['quote_atoms']
    if any(type(v) is not int or v<0 or v>2**63-1 for v in (base,quote)):raise ValueError('balance atoms')
    return dict(reviewed_sol=base/1e9,reviewed_usdc=quote/1e6,account_observed_seconds=captured/1000,
                review_expires_seconds=end/1000,review_current=int(now<end and now-captured<=900000))

def snapshot(now):
    manifest_raw=read(PUBLIC/'manifest.json');m=json.loads(manifest_raw)
    data={}
    for leaf,key in [('runtime.profile','runtime_profile_sha256'),('account.review','account_review_sha256'),('account.history','account_history_sha256')]:
        raw=read(PUBLIC/leaf)
        if hashlib.sha256(raw).hexdigest()!=m[key]:raise ValueError('document hash')
        data[leaf]=raw
    p,r=fields(data['runtime.profile']),fields(data['account.review']);h=json.loads(data['account.history'])
    if p['account_review_digest']!=m['account_review_sha256'] or r['history_evidence']!=m['account_history_sha256']:raise ValueError('document binding')
    if h['owner_sha256']!=hashlib.sha256(bytes.fromhex(p['owner_wallet'])).hexdigest() or h['user_id_sha256']!=hashlib.sha256(p['user_id'].encode()).hexdigest():raise ValueError('account identity')
    out=dict(run=m['incarnation'],**account(h,r,now),repayment_cap_sol=int(p['max_buy_output_deduction_lamports'])/1e9)
    for role in ('gateway','signer'):
        raw=subprocess.check_output(['/usr/bin/systemctl','show','--no-pager','velocita-swap-'+role+'.service','-p','ActiveState','-p','SubState','-p','ExecMainStatus','-p','NRestarts'],text=True,timeout=5)
        s=dict(line.split('=',1) for line in raw.splitlines())
        out[role+'_active']=int(s['ActiveState']=='active' and s['SubState']=='running')
        out[role+'_exit_status']=int(s['ExecMainStatus']);out[role+'_restarts']=int(s['NRestarts'])
    entries=[('prepare','gateway','prepared/prepare.bin','prepare'),('submission','gateway','submitted/submission.bin','submission'),('signing_request','gateway','signing-requests/submission.bin','submission'),('protected_signing','signer','protected-signing/submission.bin','submission')]
    for label,role,leaf,kind in entries:
        path=Path('/var/lib/velocita-swap-'+role+'/recovery')/leaf
        d=journal(read(path),kind)
        for key,value in d.items():out[label+'_'+key]=value
        out[label+'_changed_seconds']=path.stat().st_mtime
    if read(PUBLIC/'manifest.json')!=manifest_raw:raise ValueError('manifest changed')
    return out

def render(data,now):
    lines=['velocita_live_observer_up '+str(int(data is not None)),f'velocita_live_observer_observed_seconds {now}']
    if data is not None:
        run=data['run']
        if not re.fullmatch('[0-9a-f]{32}',run):raise ValueError('run label')
        for key,value in data.items():
            if key=='run':continue
            if not re.fullmatch('[a-z_]+',key) or type(value) not in (int,float):raise ValueError('scalar metrics only')
            lines.append(f'velocita_live_{key}{{run="{run}"}} {value}')
    return '\n'.join(lines)+'\n'

def write_output(raw):
    fd=os.open('/',os.O_RDONLY|os.O_DIRECTORY)
    try:
        for part in OUTPUT.parent.parts[1:]:
            next_fd=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd);os.close(fd);fd=next_fd
        leaf='velocita-live.'+os.urandom(8).hex()+'.pending'
        out=os.open(leaf,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o644,dir_fd=fd)
        with os.fdopen(out,'wb') as f:f.write(raw.encode());f.flush();os.fchmod(f.fileno(),0o644);os.fsync(f.fileno())
        os.rename(leaf,OUTPUT.name,src_dir_fd=fd,dst_dir_fd=fd);os.fsync(fd)
    finally:os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    now=time.time_ns()//1000000
    try:data=snapshot(now)
    except Exception:data=None
    rendered=render(data,now/1000)
    if args.write:write_output(rendered)
    else:print(rendered,end='')
    return 0 if data is not None else 1

if __name__=='__main__':raise SystemExit(main())
