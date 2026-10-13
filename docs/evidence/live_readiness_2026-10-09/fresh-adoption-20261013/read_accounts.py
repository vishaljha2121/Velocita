"""One fresh six-account snapshot for the pinned unsigned buy; never sign or submit."""
import hashlib
import importlib.util
import json
import signal
import urllib.request
from pathlib import Path

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('account_reader',HERE.parent/'conservative-mvp-account-check-20261013/read.py')
A=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(A)
ROOT=Path.home()/'.velocita-credentials/truemarkets'
CAPTURE=ROOT/'fresh-adoption-buy-20261013'
OUTPUT=ROOT/'fresh-adoption-route-20261013'
QUOTE='2507afcb5931ac5181dc64a8fae81c716641aeacf6de554e6783b22f78887344'
TX='3c03b007a7415f666fef53f6ccdac03d0fb40f03dc38a3d682ceadf08b6309d2'

def prepare():
    raw=(CAPTURE/'response.private.json').read_bytes();A.require(A.sha(raw)==QUOTE,'quote hash')
    env=json.loads(json.loads(raw)['payloads'][0]['payload'])
    tx=bytes.fromhex(env['parameters']['unsignedTransaction']);A.require(A.sha(tx)==TX,'transaction hash')
    old=json.loads((A.CAPTURE/'decoded.json').read_bytes());d=json.loads((CAPTURE/'decoded.json').read_bytes())
    A.require(len(tx)==712 and tx[:1]==b'\x02' and tx[1:129]==bytes(128) and tx[129:134]==bytes([128,2,0,7,12]),'unsigned shape')
    keys=[tx[134+32*i:166+32*i] for i in range(12)]
    A.require(d['static_keys']==old['static_keys']==[k.hex() for k in keys],'reviewed static accounts')
    A.require(d['lookups']==old['lookups'] and len(d['lookups'])==1,'reviewed lookup selection')
    lookup=d['lookups'][0]
    suffix=b'\x01'+bytes.fromhex(lookup['table'])+bytes([len(lookup['writable'])])+bytes(lookup['writable'])+bytes([len(lookup['readonly'])])+bytes(lookup['readonly'])
    A.require(tx.endswith(suffix) and A.decode58(env['parameters']['signWith'])==keys[1],'lookup and signer bytes')
    A.require(len(d['instructions'])==6,'instruction count')
    for n in (0,1,2,4,5):A.require(d['instructions'][n]==old['instructions'][n],'reviewed non-route instruction')
    new_ix=d['instructions'][3];old_ix=old['instructions'][3]
    a=bytes.fromhex(new_ix['data']);b=bytes.fromhex(old_ix['data'])
    A.require(new_ix['program']==old_ix['program'] and new_ix['accounts']==old_ix['accounts'] and len(a)==len(b)==36 and a[:25]==b[:25] and a[33:]==b[33:],'reviewed route except quoted output')
    selected=[keys[1],keys[0],keys[2],keys[4],keys[3],bytes.fromhex(lookup['table'])]
    cut=json.loads((ROOT/'fresh-adoption-history-20261013/baseline-cut.json').read_bytes())
    request={'jsonrpc':'2.0','id':1,'method':'getMultipleAccounts','params':[[A.encode58(k) for k in selected],{'encoding':'base64','commitment':'finalized','minContextSlot':cut['end_slot']}]}
    return A.canonical(request),keys,lookup,cut

def main():
    body,keys,lookup,cut=prepare()
    OUTPUT.mkdir(mode=0o700,exist_ok=False)
    A.write(OUTPUT/'request.json',body)
    A.write(OUTPUT/'attempt.json',A.canonical({'max_requests':1,'request_sha256':A.sha(body),'no_retry':True}))
    signal.signal(signal.SIGALRM,A.expired);signal.alarm(30)
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),A.NoRedirect())
    req=urllib.request.Request(A.ENDPOINT,data=body,method='POST',headers={'Content-Type':'application/json','User-Agent':'velocita-contract-read/1'})
    try:
        with opener.open(req,timeout=25) as response:status=response.code;raw=response.read(A.MAX+1)
        A.write(OUTPUT/'response.json',raw)
        A.require(status==200 and len(raw)<=A.MAX,'bounded HTTP response')
        facts,result=A.analyze(raw,keys,lookup)
        A.require(facts['slot']>=cut['end_slot'],'current snapshot')
        A.require(facts['source']['atoms']==cut['quote_atoms'] and facts['owner_lamports']==cut['base_atoms'],'history balances unchanged')
        result.update(quote_sha256=QUOTE,transaction_sha256=TX,request_sha256=A.sha(body),history_balances_matched=True)
        A.write(OUTPUT/'facts.json',A.canonical(facts));A.write(OUTPUT/'summary.json',A.canonical(result))
        print(json.dumps(result,indent=2))
    finally:signal.alarm(0)

if __name__=='__main__':
    try:main()
    except Exception as e:
        print(json.dumps({'stopped':True,'error_type':type(e).__name__,'no_retry':True}));raise SystemExit(1)
