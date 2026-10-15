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
SETTLEMENT_SOURCE=Path('/var/lib/velocita-swap-gateway/metrics/settlements.json')
SETTLEMENT_CACHE=Path('/var/lib/velocita-live-observer/settlements.json')
MAX_SETTLEMENTS=16

def integer(value,low=0,high=2**64-1):
    if type(value) is not int or not low<=value<=high:raise ValueError('integer range')
    return value

def exact_keys(value,keys):
    if type(value) is not dict or set(value)!=set(keys.split()):raise ValueError('schema keys')

def unique_object(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('duplicate JSON key')
        result[key]=value
    return result

def settlements(value,run,now):
    """Only the authenticated producer's successful, finalized economic records."""
    exact_keys(value,'schema incarnation observed_at_ms applied_index base_atoms quote_atoms fees_atoms realized_pnl_atoms settlements')
    if type(value['schema']) is not int or value['schema']!=1 or value['incarnation']!=run:raise ValueError('settlement identity')
    integer(value['observed_at_ms'],1,now);integer(value['applied_index'],1)
    for key in ('base_atoms','quote_atoms','fees_atoms'):integer(value[key])
    integer(value['realized_pnl_atoms'],-2**63,2**63-1)
    rows=value['settlements']
    if type(rows) is not list or len(rows)>MAX_SETTLEMENTS:raise ValueError('settlement capacity')
    seen=set()
    for row in rows:
        exact_keys(row,'effect side base_atoms quote_atoms fee_atoms outcome')
        effect=integer(row['effect'],1)
        if effect in seen or row['side'] not in ('buy','sell') or row['outcome']!='succeeded':raise ValueError('settlement event')
        seen.add(effect)
        integer(row['base_atoms'],1);integer(row['quote_atoms'],1);integer(row['fee_atoms'])
    return value

def reconcile_settlements(source,previous,now):
    """Persist observation time once. A replay never manufactures a new fill."""
    prior={}
    if previous is not None:
        # Cache has the producer schema, with one additional field per event.
        clean=dict(previous);clean['settlements']=[]
        for row in previous['settlements']:
            exact_keys(row,'effect side base_atoms quote_atoms fee_atoms outcome first_observed_ms')
            integer(row['first_observed_ms'],1,now)
            clean['settlements'].append({k:v for k,v in row.items() if k!='first_observed_ms'})
        settlements(clean,source['incarnation'],now)
        if source['applied_index']<previous['applied_index'] or source['observed_at_ms']<previous['observed_at_ms']:raise ValueError('settlement regression')
        if source['applied_index']==previous['applied_index']:
            if any(source[k]!=clean[k] for k in source if k not in ('observed_at_ms','settlements')) or sorted(source['settlements'],key=lambda r:r['effect'])!=sorted(clean['settlements'],key=lambda r:r['effect']):raise ValueError('changed committed snapshot')
        prior={r['effect']:r for r in previous['settlements']}
        if not set(prior).issubset({r['effect'] for r in source['settlements']}):raise ValueError('settlement disappeared')
    rows=[]
    for row in sorted(source['settlements'],key=lambda r:r['effect']):
        old=prior.get(row['effect'])
        if old is not None:
            if any(row[k]!=old[k] for k in row):raise ValueError('conflicting settlement replay')
            observed=old['first_observed_ms']
        else:
            if prior and row['effect']<=max(prior):raise ValueError('unseen older effect')
            observed=now
        rows.append(dict(row,first_observed_ms=observed))
    # The kernel retains every event in this bounded run and stops at capacity.
    # Rows may never disappear and later acquire a second observation timestamp.
    out=dict(source);out['settlements']=rows
    return out

def settlement_snapshot(run,now,persist):
    """An absent or invalid optional producer cannot mask service-status metrics."""
    try:
        raw=read(SETTLEMENT_SOURCE)
        # Production samples AFTER the read; systemctl and the producer can run
        # concurrently. Tests inject a fixed time through the now argument.
        if now is None:now=time.time_ns()//1000000
        source=settlements(json.loads(raw,object_pairs_hook=unique_object),run,now)
    except FileNotFoundError:return dict(settlement_feed_ready=0,settlement_feed_status=0)
    except Exception:return dict(settlement_feed_ready=0,settlement_feed_status=3)
    try:
        try:previous=json.loads(read(SETTLEMENT_CACHE),object_pairs_hook=unique_object)
        except FileNotFoundError:previous=None
        state=reconcile_settlements(source,previous,now)
        if persist and state!=previous:write_file(SETTLEMENT_CACHE,json.dumps(state,sort_keys=True)+'\n',0o600)
        fresh=now-source['observed_at_ms']<45000
        return dict(settlement_feed_ready=int(fresh),settlement_feed_status=1 if fresh else 2,
                    settlement_observed_seconds=source['observed_at_ms']/1000,
                    settlement_applied_index=source['applied_index'],settled_sol=source['base_atoms']/1e9,
                    settled_usdc=source['quote_atoms']/1e6,settled_fees_usdc=source['fees_atoms']/1e6,
                    settled_realized_pnl_usdc=source['realized_pnl_atoms']/1e6,
                    settlement_events=state['settlements'])
    except Exception:return dict(settlement_feed_ready=0,settlement_feed_status=3)

def parent_fd(path):
    fd=os.open('/',os.O_RDONLY|os.O_DIRECTORY)
    try:
        for part in path.absolute().parent.parts[1:]:
            next_fd=os.open(part,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,dir_fd=fd);os.close(fd);fd=next_fd
        return fd
    except Exception:os.close(fd);raise

def read(path,limit=32768):
    parent=parent_fd(path)
    try:fd=os.open(path.name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=parent)
    finally:os.close(parent)
    try:
        before=os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or not 0<before.st_size<=limit:raise ValueError('file metadata')
        raw=os.read(fd,limit+1);after=os.fstat(fd)
        if len(raw)!=before.st_size or (before.st_ino,before.st_mtime_ns,before.st_ctime_ns,before.st_size)!=(after.st_ino,after.st_mtime_ns,after.st_ctime_ns,after.st_size):raise ValueError('changed file')
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
    if not ordinal or not index or phase not in ((1,2,3) if kind=='submission' else (1,2,3,4)):raise ValueError('journal counters')
    return dict(ordinal=ordinal,phase=phase,index=index,effect=effect)

def account(history,review,now):
    captured=history['capture_finished_ms'];start=int(review['reviewed_at_ms']);end=int(review['expires_at_ms'])
    if not history['stable_window'] or history['blocking_orders'] or history['unsupported_accounts'] or not history['pagination_exhausted']:raise ValueError('unreviewable history')
    if not 0<captured<=start<=now or not start<end<=start+900000:raise ValueError('review timestamps')
    base,quote=history['base_atoms'],history['quote_atoms']
    if any(type(v) is not int or v<0 or v>2**63-1 for v in (base,quote)):raise ValueError('balance atoms')
    return dict(reviewed_sol=base/1e9,reviewed_usdc=quote/1e6,account_observed_seconds=captured/1000,
                review_expires_seconds=end/1000,review_current=int(now<end and now-captured<=900000))

def snapshot(now,persist=False):
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
    out.update(settlement_snapshot(out['run'],None,persist))
    return out

def render(data,now):
    lines=['velocita_live_observer_up '+str(int(data is not None)),f'velocita_live_observer_observed_seconds {now}']
    if data is not None:
        run=data['run']
        if not re.fullmatch('[0-9a-f]{32}',run):raise ValueError('run label')
        for key,value in data.items():
            if key=='run':continue
            if key=='settlement_events':
                for event in value:
                    # Stable low-cardinality labels give an instant table one row
                    # per effect. Grafana uses this persisted time, never scrape time.
                    labels=f'run="{run}",effect="{event["effect"]}",side="{event["side"]}",first_observed_ms="{event["first_observed_ms"]}",quantity_sol="{event["base_atoms"]/1e9}",quote_usdc="{event["quote_atoms"]/1e6}",fee_usdc="{event["fee_atoms"]/1e6}"'
                    price=event['quote_atoms']*1000/event['base_atoms']
                    lines.append(f'velocita_live_fill_effective_price_usdc_per_sol{{{labels}}} {price}')
                continue
            if not re.fullmatch('[a-z_]+',key) or type(value) not in (int,float):raise ValueError('scalar metrics only')
            lines.append(f'velocita_live_{key}{{run="{run}"}} {value}')
    return '\n'.join(lines)+'\n'

def write_file(path,raw,mode=0o644):
    fd=parent_fd(path)
    try:
        leaf=path.name+'.'+os.urandom(8).hex()+'.pending'
        out=os.open(leaf,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,mode,dir_fd=fd)
        with os.fdopen(out,'wb') as f:f.write(raw.encode());f.flush();os.fchmod(f.fileno(),mode);os.fsync(f.fileno())
        os.rename(leaf,path.name,src_dir_fd=fd,dst_dir_fd=fd);os.fsync(fd)
    finally:os.close(fd)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    now=time.time_ns()//1000000
    try:data=snapshot(now,args.write)
    except Exception:data=None
    rendered=render(data,now/1000)
    if args.write:write_file(OUTPUT,rendered)
    else:print(rendered,end='')
    return 0 if data is not None else 1

if __name__=='__main__':raise SystemExit(main())
