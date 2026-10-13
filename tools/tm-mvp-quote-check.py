"""One explicit unsigned quote from a pinned fresh history cut; never sign or execute."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import stat
import time
import urllib.error
import urllib.request

SPEC=importlib.util.spec_from_file_location('mvp_route',Path(__file__).with_name('tm-route-a-probe.py'))
ROUTE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ROUTE)
CONTRACT=ROUTE.CONTRACT
ACCOUNT=ROUTE.ACCOUNT
SPEC=importlib.util.spec_from_file_location('mvp_history',Path(__file__).with_name('tm-account-history-check.py'))
HISTORY=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HISTORY)
CUT_FIELDS=set('schema scope owner_sha256 user_id_sha256 setup_sha256 balances_sha256 evidence_manifest_sha256 capture_started_ms capture_finished_ms start_slot end_slot stable_window pagination_exhausted unsupported_accounts base_atoms quote_atoms blocking_orders'.split())
LIMIT=4_000_000


def read_private(path, limit):
    parent, normalized=ACCOUNT.MINT._open_private_parent(path)
    try:
        fd=os.open(normalized.name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd,'rb') as stream:
        meta=os.fstat(stream.fileno())
        if not stat.S_ISREG(meta.st_mode) or meta.st_uid!=os.getuid() or meta.st_mode&0o077 or meta.st_nlink!=1:
            raise ValueError('private input ownership/type refused')
        raw=stream.read(limit+1)
        if len(raw)>limit: raise ValueError('private input bound exceeded')
        return raw


def history_cut(raw, pin, now_ms):
    if not isinstance(pin,str) or re.fullmatch('[0-9a-f]{64}',pin) is None or hashlib.sha256(raw).hexdigest()!=pin:
        raise ValueError('history digest mismatch')
    cut=HISTORY.strict(raw)
    if not isinstance(cut,dict) or set(cut)!=CUT_FIELDS or type(cut['schema']) is not int or cut['schema']!=1 or cut['scope']!='sol-usdc-current-accounts':
        raise ValueError('history schema refused')
    if cut['stable_window'] is not True or cut['pagination_exhausted'] is not True:
        raise ValueError('history is not a stable exhausted cut')
    for key in ('unsupported_accounts','blocking_orders','capture_started_ms','capture_finished_ms','start_slot','end_slot','base_atoms','quote_atoms'):
        if type(cut[key]) is not int or not 0<=cut[key]<=2**63-1: raise ValueError('history integer refused')
    if cut['unsupported_accounts'] or cut['blocking_orders']: raise ValueError('history contains unsupported accounts or blocking orders')
    for key in (k for k in CUT_FIELDS if k.endswith('_sha256')):
        if not isinstance(cut[key],str) or re.fullmatch('[0-9a-f]{64}',cut[key]) is None or cut[key]=='0'*64:
            raise ValueError('history evidence digest refused')
    start,finish=cut['capture_started_ms'],cut['capture_finished_ms']
    if (type(now_ms) is not int or not 0<start<=finish<=now_ms or finish-start>300000 or
        now_ms-finish>900000 or not 0<cut['start_slot']<=cut['end_slot']):
        raise ValueError('history cut stale or timing invalid')
    return cut


def quote_body(side, cut):
    if side not in ('buy','sell'): raise ValueError('explicit buy or sell side required')
    amount=cut['base_atoms'] if side=='sell' else cut['quote_atoms']
    if type(amount) is not int or not 0<amount<=2**63-1 or (side=='buy' and amount<1000000):
        raise ValueError('observed balance insufficient or invalid')
    qty=f'{amount//1000000000}.{amount%1000000000:09d}' if side=='sell' else '1.00'
    return dict(base_asset='So11111111111111111111111111111111111111112',chain='solana',
                side=side,type='market',qty=qty,qty_unit='base' if side=='sell' else 'quote')


class Exchange:
    def __init__(self, request_body):
        self.expected=dict(request_body)

    def __call__(self, method, path, document=None, token=None):
        if (method,path)==('POST','/v1/gateway/orders') and document==self.expected:
            ACCOUNT.MINT._validate_token(token)
        elif (method,path)==('GET','/v1/account/me/trading') and document is None:
            ACCOUNT.MINT._validate_token(token)
        elif ((method,path)==('POST','/v1/auth/token/refresh') and token is None and
              isinstance(document,dict) and set(document)=={'refresh_token'}):
            ACCOUNT.MINT._validate_token(document['refresh_token'])
        else:
            raise ValueError('outside bounded unsigned protocol')
        headers={'Accept':'application/json','Content-Type':'application/json','User-Agent':'velocita-mvp-quote/1'}
        if token is not None: headers['Authorization']='Bearer '+token
        request=urllib.request.Request(ACCOUNT.ORIGIN+path,method=method,headers=headers,
                    data=None if document is None else json.dumps(document,separators=(',',':')).encode())
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),ACCOUNT.NoRedirect())
        try:
            response=opener.open(request,timeout=20)
        except urllib.error.HTTPError as error:
            response=error
        with response:
            raw=response.read(LIMIT+1)
            if len(raw)>LIMIT: raise ValueError('response bound exceeded')
            return response.code,raw


def run(session, output, history, history_sha256, catalog_raw, side, send=None, now_ms=None):
    clock=now_ms or (lambda: time.time_ns()//1000000)
    history_raw=read_private(history,4096)
    cut=history_cut(history_raw,history_sha256,clock())
    request_body=quote_body(side,cut)
    CONTRACT.verify_pinned_catalog(catalog_raw)
    send=send or Exchange(request_body)
    if os.path.lexists(output): raise FileExistsError('capture destination already exists; do not retry')
    parent, output=ACCOUNT.MINT._open_private_parent(output)
    try:
        os.mkdir(output.name,0o700,dir_fd=parent)
        os.fsync(parent)
    finally:
        os.close(parent)
    parent, normalized=ACCOUNT.MINT._open_private_parent(session)
    try:
        fd=os.open(normalized.name+'.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW|os.O_NONBLOCK,0o600,dir_fd=parent)
    finally:
        os.close(parent)
    with os.fdopen(fd,'w') as lock:
        meta=os.fstat(lock.fileno())
        if not stat.S_ISREG(meta.st_mode) or meta.st_uid!=os.getuid() or meta.st_mode&0o077 or meta.st_nlink!=1:
            raise ValueError('unsafe session lock')
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        write=CONTRACT.PROBE.private_new
        attempt=dict(request=request_body,history_sha256=history_sha256,started_ms=clock(),
                     max_refresh=1,max_identity_get=1,max_create=1,retries=0,signed=False,execute=False)
        write(output/'attempt.json',json.dumps(attempt,sort_keys=True).encode())
        write(output/'account.history',history_raw)
        write(output/'catalog.json',catalog_raw)
        pair,expiry=ACCOUNT.token_pair(ACCOUNT.read_private(normalized),require_future=False)
        if expiry*1000<=clock()+120000:
            status,raw=send('POST','/v1/auth/token/refresh',{'refresh_token':pair['refresh_token']})
            write(output/'refresh-http-status.json',json.dumps(dict(http_status=status)).encode())
            write(output/'refresh-response.private.json',raw)
            if status!=200: raise ValueError('session refresh refused')
            pair,expiry=ACCOUNT.token_pair(HISTORY.strict(raw),require_future=False)
            if expiry*1000<=clock()+120000: raise ValueError('refreshed session too short')
            ACCOUNT.MINT._atomic_write_token(normalized,json.dumps(pair))
        history_cut(history_raw,history_sha256,clock())
        status,raw=send('GET','/v1/account/me/trading',token=pair['access_token'])
        write(output/'identity-http-status.json',json.dumps(dict(http_status=status)).encode())
        write(output/'identity.private.json',raw)
        if status!=200: raise ValueError('account identity read refused')
        user,owner=HISTORY.identity(HISTORY.strict(raw))
        if (hashlib.sha256(user.encode()).hexdigest()!=cut['user_id_sha256'] or
            hashlib.sha256(HISTORY.key_bytes(owner)).hexdigest()!=cut['owner_sha256']):
            raise ValueError('account identity differs from history')
        history_cut(history_raw,history_sha256,clock())
        if expiry*1000<=clock()+60000: raise ValueError('session expired before quote')
        quote_started_ms=clock()
        status,raw=send('POST','/v1/gateway/orders',request_body,pair['access_token'])
        quote_received_ms=clock()
        write(output/'quote-http-status.json',json.dumps(dict(http_status=status)).encode())
        write(output/'response.private.json',raw)
        result=CONTRACT.summarize(status,raw)
        if status==201:
            HISTORY.strict(raw)
            if result['verdict']!='initialized_unsigned_requires_semantic_review' or result.get('payload_count')!=1:
                raise ValueError('unsigned quote status or payload refused')
            quote=result['quote']
            input_atoms=HISTORY.atoms(quote.get('qty'),9 if side=='sell' else 6)
            output_atoms=HISTORY.atoms(quote.get('qty_out'),6 if side=='sell' else 9)
            fee_atoms=HISTORY.atoms(quote.get('fee'),6)
            if input_atoms!=(cut['base_atoms'] if side=='sell' else 1000000) or not output_atoms:
                raise ValueError('quote quantity differs from request')
            result.update(quoted_input_atoms=input_atoms,quoted_output_atoms=output_atoms,
                          quoted_explicit_fee_usdc_atoms=fee_atoms)
        result.update(side=side,history_sha256=history_sha256,catalog_sha256=hashlib.sha256(catalog_raw).hexdigest(),
                      request_sha256=hashlib.sha256(json.dumps(request_body,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
                      quote_started_ms=quote_started_ms,quote_received_ms=quote_received_ms,
                      observed_available_sol_atoms=cut['base_atoms'],observed_available_usdc_atoms=cut['quote_atoms'],
                      account_identity_matched=True,guaranteed_net_receipt_verified=False,opening_valuation_adopted=False,
                      quote_only=True,live_admitted=False)
        write(output/'summary.json',(json.dumps(result,indent=2)+'\n').encode())
        return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side',choices=('buy','sell'),required=True)
    parser.add_argument('--session',type=Path,required=True)
    parser.add_argument('--history',type=Path,required=True)
    parser.add_argument('--history-sha256',required=True)
    parser.add_argument('--catalog',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--run',action='store_true')
    args=parser.parse_args()
    if not args.run:
        cut=history_cut(read_private(args.history,4096),args.history_sha256,time.time_ns()//1000000)
        print(json.dumps(dict(proposed_only=True,request=quote_body(args.side,cut),max_refresh=1,max_identity_get=1,
                             max_unsigned_create=1,signed=False,execute=False,retries=0),indent=2))
        return 0
    signal.signal(signal.SIGALRM,CONTRACT.expired)
    signal.alarm(70)
    try:
        catalog=CONTRACT.load_pinned_catalog(args.catalog)
        result=run(args.session,args.output,args.history,args.history_sha256,catalog,args.side)
        print(json.dumps(result,indent=2))
        return 0 if result['http_status']==201 else 1
    except Exception as error:
        print(json.dumps(dict(stopped=True,error_type=type(error).__name__,do_not_retry=True,
                              signed=False,execute_request_sent=False)))
        return 1
    finally:
        signal.alarm(0)


if __name__=='__main__': raise SystemExit(main())
