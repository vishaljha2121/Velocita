"""Fixed private, unsigned diagnostic: two account reads and one simulation."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import urllib.request

REPO=Path(__file__).resolve().parents[3]
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
A=load('account_tools',Path(__file__).with_name('account_data.py'))
I=load('idl_tools',REPO/'tools/read-jupiter-idl.py')
PRIVATE=Path.home()/'.velocita-credentials/truemarkets'
CAPTURE=PRIVATE/'bounded-launch-review-buy-20261013'
OUTPUT=PRIVATE/'byreal-route-review-20261013'

def main():
    os.umask(0o077)
    raw=(CAPTURE/'response.json').read_bytes()
    A.require(A.sha(raw)=='1601a54d32f2f9825e614f59b776c4bef2fc8e19179a8ff623373bb694f113ee','capture pin')
    env=json.loads(json.loads(raw)['payloads'][0]['payload']);tx=bytes.fromhex(env['parameters']['unsignedTransaction'])
    A.require(tx[0]==2 and tx[1:129]==bytes(128),'unsigned transaction')
    d=json.loads((CAPTURE/'decoded.json').read_bytes())
    A.require(len(d['static_keys'])==16 and len(d['lookups'])==1 and len(d['instructions'])==6,'fixed route shape')
    keys=[bytes.fromhex(k) for k in d['static_keys']]
    A.require(A.decode58(env['parameters']['signWith'])==keys[1],'owner binding')
    lookup=d['lookups'][0]
    OUTPUT.mkdir(mode=0o700,exist_ok=False)
    A.write(OUTPUT/'attempt.json',A.canonical(dict(max_requests=3,retries=0,signed=False,broadcast=False)))
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),I.NoRedirect())
    def rpc(name,method,params):
        A.require(method in ('getMultipleAccounts','simulateTransaction'),'read methods only')
        body=A.canonical(dict(jsonrpc='2.0',id=1,method=method,params=params));A.write(OUTPUT/(name+'-request.json'),body)
        req=urllib.request.Request(A.ENDPOINT,data=body,method='POST',headers={'Content-Type':'application/json','User-Agent':'velocita-contract-read/1'})
        with opener.open(req,timeout=25) as response:
            A.require(response.status==200,'HTTP status');raw=response.read(4_000_001)
        A.write(OUTPUT/(name+'-response.json'),raw);A.require(len(raw)<=4_000_000,'response bound')
        value=I.parse_json(raw);A.require(value.get('jsonrpc')=='2.0' and value.get('id')==1 and 'error' not in value,'RPC status')
        return value['result'],raw
    result,raw=rpc('lookup','getMultipleAccounts',[[A.encode58(bytes.fromhex(lookup['table'])),I.PROGRAM,I.IDL],{'encoding':'base64','commitment':'finalized'}])
    A.require(len(result['value'])==3,'lookup coverage');table=result['value'][0];slot=result['context']['slot']
    b=A.account_bytes(table,A.ALT);A.require(len(b)>=56 and (len(b)-56)%32==0 and b[:4]==b'\x01\0\0\0' and b[4:12]==b'\xff'*8 and int.from_bytes(b[12:20],'little')<slot,'active warmed lookup')
    addresses=[b[i:i+32] for i in range(56,len(b),32)]
    selected=lookup['writable']+lookup['readonly'];A.require(all(type(i)is int and 0<=i<len(addresses) for i in selected),'lookup indices')
    keys.extend(addresses[i] for i in selected);A.require(len(keys)==len(set(keys))==27,'unique resolved accounts')
    idl_envelope=A.canonical(dict(jsonrpc='2.0',id=1,result=dict(context=result['context'],value=result['value'][1:])))
    idl_summary,idl=I.decode_response(idl_envelope)
    variants=next(x['type']['variants'] for x in idl['types'] if x['name']=='Swap');A.require(variants[156]=={'name':'ByrealDynamicV3'},'current variant definition')
    A.write(OUTPUT/'idl-summary.json',A.canonical(dict(idl_summary,variant_156=variants[156])))
    result,raw=rpc('accounts','getMultipleAccounts',[[A.encode58(k) for k in keys],{'encoding':'base64','commitment':'finalized','minContextSlot':slot}])
    A.require(len(result['value'])==27,'account coverage')
    A.write(OUTPUT/'resolved.json',A.canonical(dict(keys_hex=[k.hex() for k in keys],slot=result['context']['slot'],lookup=lookup)))
    result,raw=rpc('baseline','simulateTransaction',[base64.b64encode(tx).decode(),{'encoding':'base64','commitment':'finalized','sigVerify':False,'replaceRecentBlockhash':True,'innerInstructions':True,'minContextSlot':result['context']['slot']}])
    report=dict(unsigned=True,broadcast=False,requests=3,variant='ByrealDynamicV3',static_accounts=16,resolved_accounts=27,simulation_slot=result['context']['slot'],simulation_error=result['value']['err'],simulation_success=result['value']['err'] is None,simulation_sha256=A.sha(raw),economic_authorization=False)
    A.write(OUTPUT/'summary.json',A.canonical(report));print(json.dumps(report,indent=2))

if __name__=='__main__':
    def expired(*_):raise TimeoutError('bounded diagnostic expired')
    signal.signal(signal.SIGALRM,expired);signal.alarm(90)
    try:main()
    finally:signal.alarm(0)
