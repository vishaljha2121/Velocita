"""At most two unsigned adjacent-boundary pairs; never broadcast these bytes."""
import base64
import json
import urllib.request
from pathlib import Path
import read_byreal as R
A=R.A

def output(value,keys):
    rows=[]
    for group in value['innerInstructions']:
        for ix in group['instructions']:
            parsed=ix.get('parsed',{});info=parsed.get('info',{})
            if group['index']==3 and parsed.get('type')=='transferChecked' and info.get('source')==keys[17] and info.get('destination')==keys[4]:
                A.require(ix['programId']==A.TOKEN,'output program');rows.append(int(info['tokenAmount']['amount']))
    A.require(len(rows)==1 and rows[0]>0,'one positive output transfer');return rows[0]

def main():
    raw=(R.CAPTURE/'response.json').read_bytes();A.require(A.sha(raw)=='1601a54d32f2f9825e614f59b776c4bef2fc8e19179a8ff623373bb694f113ee','capture pin')
    tx=bytes.fromhex(json.loads(json.loads(raw)['payloads'][0]['payload'])['parameters']['unsignedTransaction'])
    route=bytes.fromhex(json.loads((R.CAPTURE/'decoded.json').read_bytes())['instructions'][3]['data'])
    A.require(len(route)==35 and route[12:16]==bytes([156,100,0,1]) and route[32:]==bytes([244,1,20]) and tx.count(route)==1,'route shape')
    offset=tx.index(route)+24
    resolved=json.loads((R.OUTPUT/'resolved.json').read_bytes());keys=[A.encode58(bytes.fromhex(k)) for k in resolved['keys_hex']]
    baseline=json.loads((R.OUTPUT/'baseline-response.json').read_bytes())['result']
    A.require(baseline['value']['err'] is None,'baseline must succeed');actual=output(baseline['value'],keys)
    folder=R.OUTPUT/'rounding';folder.mkdir(mode=0o700,exist_ok=False)
    A.write(folder/'attempt.json',A.canonical(dict(max_batches=2,max_simulations=4,no_broadcast=True,no_retry=True)))
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),R.I.NoRedirect());history=[]
    for attempt in range(2):
        low=actual*10000//9500;cases=[]
        for n,q in enumerate((low,low+1)):
            changed=tx[:offset]+q.to_bytes(8,'little')+tx[offset+8:]
            cases.append(dict(jsonrpc='2.0',id=n+1,method='simulateTransaction',params=[base64.b64encode(changed).decode(),dict(encoding='base64',commitment='finalized',sigVerify=False,replaceRecentBlockhash=True,innerInstructions=True,minContextSlot=baseline['context']['slot'])]))
        body=A.canonical(cases);A.write(folder/f'{attempt}-request.json',body)
        req=urllib.request.Request(A.ENDPOINT,data=body,method='POST',headers={'Content-Type':'application/json','User-Agent':'velocita-contract-read/1'})
        with opener.open(req,timeout=25) as response:
            A.require(response.status==200,'HTTP status');raw=response.read(4_000_001)
        A.write(folder/f'{attempt}-response.json',raw);A.require(len(raw)<=4_000_000,'response bound')
        docs=R.I.parse_json(raw);A.require(isinstance(docs,list) and len(docs)==2 and {x['id'] for x in docs}=={1,2} and all('error' not in x for x in docs),'batch envelope')
        rows=[]
        for x in sorted(docs,key=lambda x:x['id']):
            v=x['result']['value'];a=output(v,keys);q=low+x['id']-1
            rows.append(dict(quote=q,actual_output=a,ceiling=(q*9500+9999)//10000,floor=q*9500//10000,error=v['err'],slot=x['result']['context']['slot'],jupiter_guard_failure=any('Program JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4 failed: custom program error: 0x1771' in line for line in v['logs'])))
        passed=(rows[0]['actual_output']==rows[1]['actual_output']==rows[0]['ceiling']==rows[1]['floor'] and rows[0]['error'] is None and rows[1]['error']=={'InstructionError':[3,{'Custom':6001}]} and rows[1]['jupiter_guard_failure'])
        history.append(dict(rows=rows,ceiling_discriminated=passed))
        if passed:break
        actual=rows[-1]['actual_output']
    report=dict(simulation_only=True,broadcast=False,pairs=history,ceiling_discriminated=history[-1]['ceiling_discriminated'],unconditional_future_guarantee=False)
    A.write(folder/'result.json',A.canonical(report));print(json.dumps(report,indent=2))

if __name__=='__main__':main()
