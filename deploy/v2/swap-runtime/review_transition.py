"""Apply a hash-pinned review refresh or startup flag transition; never start services.

refresh-idle is proposal validation only: account-only, stopped, sealed, first-use recovery. The account
capture/review may advance; funding/opening, transaction review, credential and
launch authority cannot. All five journals must be pristine and bind the existing
profile. Apply is refused until service-start inhibition and exclusive native journal locks
span the transition; double-read snapshots alone cannot prevent a concurrent start.
Nothing writes journals. A transaction-review expiry cannot be extended
here: its expiry participates in the protected signer journal domain. A separate
reviewed migration must preserve the old generation and establish authenticated
Raft no-intent/no-effect proof before introducing any new policy/journal generation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import stat
import struct
import time

sys.path.insert(0,str(Path(__file__).resolve().parent))
import adopt_account as A
import credential_handoff as H

P=A.PROFILE
I=A.INSTALL
L=A.LAUNCH

def digest(raw):return hashlib.sha256(raw).hexdigest()

def transaction_fields(raw):
    """Transaction reviews include up to 256 lookup addresses; cap at native 32KiB.

    Runtime/account profiles still use service_launcher's independent 8KiB cap.
    Field grammar is identical: ASCII, nonempty unique names/values, no NUL.
    """
    if not raw or len(raw)>32768:raise ValueError('bounded transaction review required')
    result={}
    for line in raw.decode('ascii').splitlines():
        name,equal,value=line.partition('=')
        if not equal or not name or not value or name in result or '\x00' in line:raise ValueError('transaction review field shape')
        result[name]=value
    return result


def validate(mode,old,new,before,after,now):
    P.validate(old);P.validate(new)
    if set(before)!=set(A.DOCUMENTS) or set(after)!=set(A.DOCUMENTS):raise ValueError('exact documents required')
    for manifest,files in ((old,before),(new,after)):
        for name,key in A.DOCUMENTS.items():
            if key and digest(files[name])!=manifest[key]:raise ValueError('document pin mismatch')
        if digest(files['transaction.review'])!=L.fields(files['runtime.profile'])['transaction_review_digest']:raise ValueError('transaction pin mismatch')
    allowed=P.PENDING_ACCOUNT_PINS if mode in ('refresh','refresh-idle') else {'launch_authorized','initialize_swap_recovery'}
    if any(old.get(k)!=new.get(k) for k in (set(old)|set(new))-allowed):raise ValueError('unrelated manifest change')
    if old.get('recovery_only',False) or new.get('recovery_only',False):raise ValueError('recovery transition out of scope')
    if mode in ('refresh','refresh-idle'):
        if mode=='refresh' and (old['launch_authorized'] or new['launch_authorized'] or old.get('initialize_swap_recovery',False) or new.get('initialize_swap_recovery',False)):raise ValueError('refresh requires dormant unused profile')
        if mode=='refresh-idle' and (not old['launch_authorized'] or old.get('initialize_swap_recovery') is not False):raise ValueError('idle refresh requires sealed initialized profile')
        previous,current=map(L.fields,(before['runtime.profile'],after['runtime.profile']))
        mutable=({'account_review_digest'} if mode=='refresh-idle' else
                 {'account_review_digest','transaction_review_digest','session_evidence','expires_at_ms'})
        if any(previous.get(k)!=current.get(k) for k in (set(previous)|set(current))-mutable):raise ValueError('refresh changed identity or credential')
        old_history,new_history=map(L.strict_json,(before['account.history'],after['account.history']))
        if any(old_history[k]!=new_history[k] for k in ('base_atoms','quote_atoms','owner_sha256','user_id_sha256')):raise ValueError('account balance or identity changed')
        if mode=='refresh-idle':
            if before['transaction.review']!=after['transaction.review']:raise ValueError('transaction policy migration required; preserve journals')
            previous_review,current_review=map(L.fields,(before['account.review'],after['account.review']))
            mutable_review={'history_evidence','reviewed_at_ms','expires_at_ms'}
            if any(previous_review.get(k)!=current_review.get(k) for k in (set(previous_review)|set(current_review))-mutable_review):raise ValueError('opening or account authority changed')
            for key in ('runtime_identity','opening_digest','outside_base','outside_quote','activity_count'):
                if key not in previous_review:raise ValueError('complete previous account authority required')
            for h in (old_history,new_history):
                if h.get('stable_window') is not True or h.get('pagination_exhausted') is not True or any(type(h.get(k)) is not int or h[k]!=0 for k in ('blocking_orders','unsupported_accounts')):raise ValueError('unreviewable account history')
                if any(type(h[k]) is not int or not 0<=h[k]<=2**63-1 for k in ('base_atoms','quote_atoms')):raise ValueError('account balance atoms')
            if transaction_expired(transaction_fields(after['transaction.review']),now):raise ValueError('transaction review expired; separate policy/journal migration required for initialized run')
            if new_history['capture_finished_ms']<=old_history['capture_finished_ms']:raise ValueError('fresh account capture required')
    elif mode=='arm':
        if old['launch_authorized'] or old.get('initialize_swap_recovery',False) or not new['launch_authorized'] or new.get('initialize_swap_recovery') is not True:raise ValueError('invalid first-start transition')
        if before!=after:raise ValueError('startup cannot change reviewed documents')
    elif mode=='seal':
        if not old['launch_authorized'] or old.get('initialize_swap_recovery') is not True or not new['launch_authorized'] or new.get('initialize_swap_recovery') is not False:raise ValueError('invalid recovery transition')
        if before!=after:raise ValueError('startup cannot change reviewed documents')
    else:raise ValueError('unknown transition')
    tx=transaction_fields(after['transaction.review'])
    if transaction_expired(tx,now):raise ValueError('transaction review expired; separate policy/journal migration required for initialized run')
    for role in ('gateway','signer'):L.check_bindings(new,after['runtime.profile'],after['account.review'],after['account.history'],now,role)

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

# First-use journal layouts mirror the versioned C++ stores. This mode deliberately
# refuses even completed prior trades: it is not a reconciliation/re-arming tool.
JOURNALS={'prepared':('gateway','prepare.bin',8768,b'VPRP\1\0\0\0'),
          'submitted':('gateway','submission.bin',464,b'VSSJ\1\0\0\0'),
          'signing-requests':('gateway','submission.bin',464,b'VSSJ\1\0\0\0'),
          'protected-signing':('signer','submission.bin',464,b'VSSJ\1\0\0\0'),
          'results':('gateway','result.bin',152,b'VSRW\1\0\0\0')}

STABLE_FIELDS={'schema','sponsor','owner_usdc','owner_wsol','platform_fee',
               'max_envelope_age_ms','max_compute_price','max_compute_units','max_slippage_bps'}
STABLE_CONSTANTS=('0479d55bf231c06eee74c56ece681507fdb1b2dea3f48e5102b1cda256bc138f',
                  'b43ffa27f5d7f64a74c09b1f295879de4b09ab36dfc9dd514b321aa7b38ce5e8',
                  '069b8857feab8184fb687f634618c035dac439dc1aeb3b5598a0f00000000001',
                  '7d0ea3424053053bbbd860f74c32f04bc3dc872adf9a4f4ed170c24ee0f43ed3')
STABLE_CONTRACTS=((18,'fc39066f804ec84d465acc0cafceb1debdfb9e4751e3c99ecb5463ac63cd89c9'),
                  (109,'f06b836245fa463674a002608d8edcd4e84affb0a4bc9271d2e4991bc0108aab'),
                  (156,'0634d20f1bf56dd164e3c13c7836a4229ec5ab21ff54528390a51789f3cdbbdb'),
                  (177,'9f32da9c1f6c1250c09b733923ecc3670d2863e7d46a9c1cf47dfdbbd0d5fe57'),
                  (181,'2474fcd2b335779c5b0a8f0ac74dbd8b92ba3467ac33ed63a150b935828bd589'))

def _key(fields,name,size=32):
    value=fields[name]
    if len(value)!=2*size or any(c not in '0123456789abcdef' for c in value):raise ValueError('journal binding hex field')
    return bytes.fromhex(value)

def verification_digest(p,tx):
    """Mirror of swap_signer_verification_digest in v2_swap_transaction_review.cpp."""
    def h(raw):return hashlib.sha256(raw).digest()
    def number(value):return struct.pack('<Q',L.unsigned(value))
    inc,cat,cust=(_key(p,k) for k in ('incarnation','catalog','custody'))
    allocation=b''.join(number(p[k]) for k in ('account','instrument','policy','policy_version','risk_policy_version'))
    identity=b''
    for name in ('organization','signer_id'):
        value=p[name].encode('ascii');identity+=struct.pack('<Q',len(value))+value
    if tx.get('schema')=='6':
        if set(tx)!=STABLE_FIELDS:raise ValueError('exact stable execution review fields')
        keys=[_key(tx,'sponsor'),_key(p,'owner_wallet'),_key(tx,'owner_usdc'),_key(tx,'owner_wsol'),_key(tx,'platform_fee')]
        if any(not any(k) for k in keys) or len(set(keys))!=len(keys):raise ValueError('stable execution keys must be distinct and nonzero')
        age,price,units,slippage,deduction=(L.unsigned(v) for v in (tx['max_envelope_age_ms'],tx['max_compute_price'],tx['max_compute_units'],
                                                                     tx['max_slippage_bps'],p.get('max_buy_output_deduction_lamports','0')))
        if not 0<age<=30000 or not 0<units<=1400000 or slippage>10000 or price>(2**64-1)//units or deduction>2**63-1:raise ValueError('stable execution limits')
        policy=h(b'velocita-swap-execution-policy-v1'+inc+cat+cust+allocation+b''.join(keys)+
                 b''.join(struct.pack('<Q',v) for v in (age,price,units,slippage,deduction))+
                 b''.join(bytes.fromhex(k) for k in STABLE_CONSTANTS)+
                 b''.join(struct.pack('<Q',kind)+bytes.fromhex(program) for kind,program in STABLE_CONTRACTS))
        return h(b'velocita-signer-verification-v2'+policy+identity)
    count=L.unsigned(tx['lookup_address_count'])
    if not 1<=count<=256:raise ValueError('lookup count')
    lookup=h(b'velocita-direct-buy-lookup-v1'+_key(tx,'lookup_table')+number(tx['lookup_slot'])+
             _key(tx,'lookup_evidence')+number(tx['lookup_address_count'])+_key(tx,'lookup_addresses',32*count))
    schemas={'1':(b'velocita-direct-kipseli-buy-v1',640),
             '2':(b'velocita-direct-byreal-v3-buy-v1',864),
             '3':(b'velocita-direct-obsidian-buy-v1',544),
             '4':(b'velocita-direct-bison-market-backed-buy-v1',640),
             '5':(b'velocita-direct-invariant-buy-v1',608)}
    if tx.get('schema') not in schemas:raise ValueError('unsupported transaction review schema')
    policy_label,key_bytes=schemas[tx['schema']]
    policy=h(policy_label+inc+cat+cust+allocation+_key(tx,'account_keys',key_bytes)+
             b''.join(number(tx[k]) for k in ('max_envelope_age_ms','max_compute_price','max_compute_units','max_slippage_bps'))+
             number(p.get('max_buy_output_deduction_lamports','0'))+lookup+number(tx['expires_at_ms']))
    return h(b'velocita-signer-verification-v1'+policy+lookup+identity)

def transaction_expired(tx,now):
    """Schemas 1-5 carry their own expiry; the stable schema 6 relies on the account review expiry."""
    return tx.get('schema')!='6' and L.unsigned(tx['expires_at_ms'])<=now

def journal_bindings(files):
    """Compute every journal domain from unchanged reviewed public identities.

    These hashes bind bytes; native policy validation still owns route admission.
    See v2_swap_{transaction_verifier,signer_session,signing_connection}.cpp.
    """
    p=L.fields(files['runtime.profile']);tx=transaction_fields(files['transaction.review'])
    def h(raw):return hashlib.sha256(raw).digest()
    def number(value):return struct.pack('<Q',L.unsigned(value))
    def key(fields,name,size=32):
        value=fields[name]
        if len(value)!=2*size or any(c not in '0123456789abcdef' for c in value):raise ValueError('journal binding hex field')
        return bytes.fromhex(value)
    inc,cat,cust,domain=(key(p,k) for k in ('incarnation','catalog','custody','authority_domain'))
    if not all(any(k) for k in (inc,cat,cust,domain)):raise ValueError('zero journal identity')
    allocation=b''.join(number(p[k]) for k in ('account','instrument','policy','policy_version','risk_policy_version'))
    mapping=h(b'velocita.tm.fee-inclusive-principal-plus-cap.v1')
    public=key(p,'signer_public_key',33)
    signing=h(h(b'velocita.gateway.signer-request.v1')+inc+cat+cust+domain+mapping+public+allocation)
    protected=h(b'swap-signer-v2\0\0'+number(p['signer_caller'])+number(p['key_version'])+domain+
                key(p,'signer_instance')+public+cat+cust+allocation+verification_digest(p,tx))
    return inc,cat+cust+allocation+domain+mapping,domain,signing,protected

def validate_idle_journals(manifest,files,journals):
    if set(journals)!=set(JOURNALS):raise ValueError('all five initialized journals required')
    inc,prepare,domain,signing,protected=journal_bindings(files)
    if inc!=hashlib.sha256(bytes.fromhex(manifest['incarnation'])).digest():raise ValueError('journal incarnation mismatch')
    indices=[];evidence=[]
    for name,(_,_,size,magic) in JOURNALS.items():
        raw=journals[name]
        if len(raw)!=size or raw[:8]!=magic or hashlib.sha256(raw[:-32]).digest()!=raw[-32:]:raise ValueError('journal checksum or schema')
        u64=lambda at:struct.unpack_from('<Q',raw,at)[0]
        if u64(8)!=1:raise ValueError('only pristine initialized journals may refresh')
        if name=='results':
            index=u64(16);proof=raw[88:120]
            if raw[24:88]!=inc+domain:raise ValueError('result journal binding')
        else:
            index=u64(24)
            if u64(16)!=1 or u64(32)!=0:raise ValueError('journal must be idle with no prior effect')
            if raw[40:72]!=inc:raise ValueError('journal incarnation')
            if name=='prepared':
                proof=raw[304:336]
                if raw[72:240]!=prepare or any(raw[240:304]) or any(raw[336:-32]):raise ValueError('prepare binding or idle payload')
            else:
                proof=raw[400:432]
                expected={'submitted':domain,'signing-requests':signing,'protected-signing':protected}[name]
                if raw[72:104]!=expected or any(raw[104:400]):raise ValueError('submission binding or idle payload')
        if not 0<index<=2**63-1 or not any(proof):raise ValueError('journal index or state evidence')
        indices.append(index);evidence.append(proof)
    if len(set(indices))!=1 or len(set(evidence))!=1:raise ValueError('inconsistent first-use journal state')

def idle_state(manifest,files):
    A.stopped()
    journals={}
    for role,uid in (('gateway',29001),('signer',29002)):
        root=Path('/var/lib/velocita-swap-'+role+'/recovery')
        L.directory(str(root),uid,uid,0o700)
        expected={k for k,v in JOURNALS.items() if v[0]==role}
        if {p.name for p in root.iterdir()}!=expected:raise ValueError('exact initialized journal directories required')
        for name in expected:
            _,leaf,size,_=JOURNALS[name];folder=root/name
            L.directory(str(folder),uid,uid,0o700)
            if {p.name for p in folder.iterdir()}!={leaf}:raise ValueError('partial or unexpected journal files')
            parent=L.FD._open_components(str(folder),uid)
            try:fd=os.open(leaf,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=parent)
            finally:os.close(parent)
            try:
                before=os.fstat(fd)
                if not stat.S_ISREG(before.st_mode) or before.st_uid!=uid or stat.S_IMODE(before.st_mode)!=0o600 or before.st_nlink!=1 or before.st_size!=size:raise ValueError('private journal metadata')
                raw=os.read(fd,size+1);after=os.fstat(fd)
                if (before.st_ino,before.st_mtime_ns,before.st_ctime_ns,before.st_size)!=(after.st_ino,after.st_mtime_ns,after.st_ctime_ns,after.st_size):raise ValueError('journal changed during read')
                journals[name]=raw
            finally:os.close(fd)
    validate_idle_journals(manifest,files,journals)
    return {name:digest(raw) for name,raw in journals.items()}

def state(mode,manifest=None,files=None):
    if mode=='refresh-idle':return idle_state(manifest,files)
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
    mode,old,new,before,after=bundle(base,pin)
    if mode=='refresh-idle':raise ValueError('initialized refresh is proposal-only until exclusive migration guard is qualified')
    journal_snapshot=state(mode,old,before)
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
    validate(mode,old,new,before,after,time.time_ns()//1000000)
    if state(mode,old,before)!=journal_snapshot:raise ValueError('journals changed before transition')
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
