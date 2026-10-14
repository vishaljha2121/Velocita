"""Offline expiry renewal or explicitly reviewed route-transition proposal; never applies files or authorizes launch.

No command-line apply path exists. Caller JSON is not authenticated Raft proof.
Every returned proposal is explicitly non-applyable until a native migration
helper validates an authenticated committed state and controls crash recovery.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
import review_transition as R


def runtime_identity(profile):
    """Byte serialization from swap_runtime_identity_digest, not policy admission."""
    def h(name):
        value=profile[name]
        width=33 if name=='signer_public_key' else 32
        if len(value)!=2*width or any(c not in '0123456789abcdef' for c in value):raise ValueError('identity hex')
        return bytes.fromhex(value)
    def n(name):return struct.pack('<Q',R.L.unsigned(profile[name]))
    raw=b'velocita-swap-runtime-v1'
    for key in ('incarnation','catalog','custody'):raw+=h(key)
    for key in ('account','instrument','policy','policy_version','risk_policy_version'):raw+=n(key)
    for key in ('authority_domain','account_assets','session_evidence','owner_wallet','signer_public_key'):raw+=h(key)
    for key in ('gateway_uid','signer_uid','gateway_caller','signer_caller','ipc_timeout_ms'):raw+=n(key)
    socket=profile['socket'].encode('ascii');raw+=struct.pack('<Q',len(socket))+socket
    if R.L.unsigned(profile.get('max_buy_output_deduction_lamports','0')):
        raw+=b'accepted-buy-output-deduction-v1'+n('max_buy_output_deduction_lamports')
    if 'transaction_review_digest' in profile:raw+=b'transaction-review-v1'+h('transaction_review_digest')
    return hashlib.sha256(raw).hexdigest()


def route_transition(before,after,witness):
    """Validate owner-pinned review scope, not a self-asserted transaction proof.

    The installed native verifier must separately qualify the new route. This
    witness cannot authorize launch or alter financial/caller/opening limits.
    """
    expected={'schema','kind','before_review_sha256','after_review_sha256','contract_evidence_sha256'}
    if type(witness) is not dict or set(witness)!=expected or type(witness['schema']) is not int or witness['schema']!=1 or witness['kind']!='kipseli-v1-to-byreal-v1':raise ValueError('exact route review witness required')
    for name in ('before_review_sha256','after_review_sha256','contract_evidence_sha256'):
        value=witness[name]
        if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value) or not int(value,16):raise ValueError('route review digest shape')
    if witness['before_review_sha256']!=R.digest(before) or witness['after_review_sha256']!=R.digest(after):raise ValueError('route review document pin mismatch')
    prior,current=map(R.transaction_fields,(before,after))
    if set(prior)!=set(current) or prior['schema']!='1' or current['schema']!='2':raise ValueError('only reviewed Kipseli to Byreal transition')
    mutable={'schema','account_keys','lookup_table','lookup_slot','lookup_evidence','lookup_address_count','lookup_addresses','expires_at_ms'}
    if any(prior[k]!=current[k] for k in prior if k not in mutable):raise ValueError('route transition cannot change execution limits')
    a,b=prior['account_keys'],current['account_keys']
    if len(a)!=20*64 or len(b)!=27*64 or any(c not in '0123456789abcdef' for c in a+b):raise ValueError('route account key shapes')
    # Every owner-facing identity survives the topology index remapping.
    for old,new in ((0,0),(1,1),(2,3),(3,4),(4,5),(5,8),(6,9),(7,10),(8,11),(10,13),(11,14),(15,22),(18,25)):
        if a[old*64:(old+1)*64]!=b[new*64:(new+1)*64]:raise ValueError('route changed owner-facing account or program')


def propose(old,new,before,after,journals,now,session_refresh=None,route_review=None):
    """Return review artifacts only. Never accepts a purported authenticated proof."""
    if type(now) is not int or not 0<now<=2**64-1:raise ValueError('bounded evaluation time')
    R.P.validate(old);R.P.validate(new)
    if set(before)!=set(R.A.DOCUMENTS) or set(after)!=set(R.A.DOCUMENTS):raise ValueError('exact documents required')
    for manifest,files in ((old,before),(new,after)):
        for name,key in R.A.DOCUMENTS.items():
            if key and R.digest(files[name])!=manifest[key]:raise ValueError('document pin mismatch')
        if R.digest(files['transaction.review'])!=R.L.fields(files['runtime.profile'])['transaction_review_digest']:raise ValueError('transaction pin mismatch')
    if any(old.get(k)!=new.get(k) for k in (set(old)|set(new))-R.P.PENDING_ACCOUNT_PINS):raise ValueError('immutable manifest authority')
    if not old['launch_authorized'] or old.get('initialize_swap_recovery') is not False or old.get('recovery_only',False):raise ValueError('sealed first-use profile required')
    prior_tx,next_tx=map(R.transaction_fields,(before['transaction.review'],after['transaction.review']))
    if route_review is not None:route_transition(before['transaction.review'],after['transaction.review'],route_review)
    elif set(prior_tx)!=set(next_tx) or any(prior_tx[k]!=next_tx[k] for k in prior_tx if k!='expires_at_ms'):raise ValueError('expiry-only transaction review')
    expiry=R.L.unsigned(next_tx['expires_at_ms'])
    if not max(now,R.L.unsigned(prior_tx['expires_at_ms']))<expiry<=now+900000:raise ValueError('fresh bounded transaction expiry required')
    prior_p,next_p=map(R.L.fields,(before['runtime.profile'],after['runtime.profile']))
    mutable={'transaction_review_digest','account_review_digest'}
    if session_refresh is not None:
        mutable|={'bearer_digest','expires_at_ms','session_evidence'}
    if any(prior_p.get(k)!=next_p.get(k) for k in (set(prior_p)|set(next_p))-mutable):raise ValueError('runtime authority or credentials changed')
    prior_r,next_r=map(R.L.fields,(before['account.review'],after['account.review']))
    mutable={'runtime_identity','history_evidence','reviewed_at_ms','expires_at_ms'}
    if any(prior_r.get(k)!=next_r.get(k) for k in (set(prior_r)|set(next_r))-mutable):raise ValueError('opening or account partition changed')
    if prior_r.get('runtime_identity')!=runtime_identity(prior_p) or next_r.get('runtime_identity')!=runtime_identity(next_p):raise ValueError('runtime identity digest mismatch')
    if not all(k in prior_r for k in ('opening_digest','outside_base','outside_quote','activity_count')):raise ValueError('complete prior opening required')
    original_time=R.L.unsigned(prior_r['reviewed_at_ms'])
    for role in ('gateway','signer'):R.L.check_bindings(old,before['runtime.profile'],before['account.review'],before['account.history'],original_time,role)
    histories=list(map(R.L.strict_json,(before['account.history'],after['account.history'])))
    for h in histories:
        if h.get('stable_window') is not True or h.get('pagination_exhausted') is not True or any(type(h.get(k)) is not int or h[k]!=0 for k in ('blocking_orders','unsupported_accounts')):raise ValueError('unreviewable history')
        if any(type(h.get(k)) is not int or not 0<=h[k]<=2**63-1 for k in ('base_atoms','quote_atoms')):raise ValueError('balance atoms')
    if any(histories[0][k]!=histories[1][k] for k in ('base_atoms','quote_atoms','owner_sha256','user_id_sha256')):raise ValueError('balance or account changed; reconciliation required')
    if histories[1]['capture_finished_ms']<=histories[0]['capture_finished_ms']:raise ValueError('fresh account capture required')
    if session_refresh is not None:
        expected={'schema','setup_sha256','history_sha256','bearer_sha256','token_expires_at_ms'}
        if type(session_refresh) is not dict or set(session_refresh)!=expected or type(session_refresh['schema']) is not int or session_refresh['schema']!=1:raise ValueError('exact session refresh witness required')
        for name in ('setup_sha256','history_sha256','bearer_sha256'):
            value=session_refresh[name]
            if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value) or not int(value,16):raise ValueError('session witness digest shape')
        token_expiry=session_refresh['token_expires_at_ms']
        if type(token_expiry) is not int or not 0<token_expiry<=2**64-1:raise ValueError('session token expiry shape')
        if session_refresh['setup_sha256']!=histories[1].get('setup_sha256') or session_refresh['history_sha256']!=R.digest(after['account.history']):raise ValueError('session witness account evidence mismatch')
        if session_refresh['bearer_sha256']!=next_p.get('bearer_digest'):raise ValueError('session witness bearer mismatch')
        if not now+60000<R.L.unsigned(next_p['expires_at_ms'])<=token_expiry:raise ValueError('session lifetime must be fresh and bounded by token')
        session={k:v for k,v in session_refresh.items() if k!='token_expires_at_ms'}
        canonical=(json.dumps(session,sort_keys=True,separators=(',',':'))+'\n').encode()
        if next_p.get('session_evidence')!=R.digest(canonical):raise ValueError('session evidence does not bind fresh bearer and account capture')
    for role in ('gateway','signer'):R.L.check_bindings(new,after['runtime.profile'],after['account.review'],after['account.history'],now,role)
    old_record=journals['protected-signing']
    ordinal=int.from_bytes(old_record[8:16],'little')
    if not 1<=ordinal<2**64-1:raise ValueError('protected journal ordinal exhausted')
    # Repeated expiry renewal is allowed only while every effect remains pristine.
    # Normalize a COPY for the existing first-use validator, never installed bytes.
    prior_check=bytearray(old_record);struct.pack_into('<Q',prior_check,8,1)
    prior_check[-32:]=hashlib.sha256(prior_check[:-32]).digest()
    if len(old_record)!=464 or hashlib.sha256(old_record[:-32]).digest()!=old_record[-32:]:raise ValueError('original protected journal checksum')
    R.validate_idle_journals(old,before,dict(journals,**{'protected-signing':bytes(prior_check)}))
    record=bytearray(old_record)
    struct.pack_into('<Q',record,8,ordinal+1)
    record[72:104]=R.journal_bindings(after)[4]
    if record[72:104]==old_record[72:104]:raise ValueError('renewal must change protected policy domain')
    record[-32:]=hashlib.sha256(record[:-32]).digest()
    proposed=dict(journals);proposed['protected-signing']=bytes(record)
    # Check every new domain against its corresponding document set. Only the
    # protected ordinal is advanced; normalize a COPY for the pristine validator.
    check=bytearray(record);struct.pack_into('<Q',check,8,1);check[-32:]=hashlib.sha256(check[:-32]).digest()
    R.validate_idle_journals(new,after,dict(proposed,**{'protected-signing':bytes(check)}))
    artifacts={}
    for prefix,manifest,files,records in (('before',old,before,journals),('after',new,after,proposed)):
        artifacts[prefix+'/manifest.json']=R.P.manifest_bytes(manifest)
        artifacts.update({prefix+'/'+name:raw for name,raw in files.items()})
        artifacts.update({prefix+'/journals/'+name:raw for name,raw in records.items()})
    if session_refresh is not None:
        artifacts['session-refresh.json']=(json.dumps(session_refresh,sort_keys=True,separators=(',',':'))+'\n').encode()
    if route_review is not None:
        artifacts['route-review.json']=(json.dumps(route_review,sort_keys=True,separators=(',',':'))+'\n').encode()
    kind='expiry-only-pristine-protected-journal-renewal-proposal' if route_review is None else 'reviewed-route-pristine-protected-journal-transition-proposal'
    report=dict(schema=1,kind=kind,applyable=False,
        authenticated_raft_proof=False,services_started=False,orders_submitted=False,
        original_journal_index=int.from_bytes(old_record[24:32],'little'),original_state_digest=old_record[400:432].hex(),
        blocking_requirement='native authenticated Raft proof, native digest parity and crash-safe migration helper required',
        required_proof=['fresh request-correlated caller-authenticated committed read after intent producer fenced',
                        'verified term and commit/read barrier; current index >= original journal index',
                        'incarnation/catalog/authority/opening and funding match reviewed originals',
                        'halted, initialized, no conflict, no pending intent, last_effect=0, purchases=0, settlement_count=0',
                        'native MVP one-$1 buy scope and existing fee/loss caps unchanged'],
        required_apply_protocol=['stop and inhibit gateway/signer starts; lock all five journal directories exclusively',
                                 'validate original file pins and authenticated state; preserve old bytes in immutable fsynced archive',
                                 'publish ordinal+1 protected journal atomically with fsync; never initialize or reset',
                                 'publish exact reviewed document set with partial-state refusal and explicit recovery manifest',
                                 'leave services stopped; launch and money movement require separate review'],
        artifact_sha256={name:R.digest(raw) for name,raw in artifacts.items()})
    return dict(report=report,artifacts=artifacts)
