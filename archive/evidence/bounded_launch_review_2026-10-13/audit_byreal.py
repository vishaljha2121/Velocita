"""Offline audit of a fixed unsigned capture; never opens a network connection."""
import base64
import hashlib
import json
from pathlib import Path
import read_byreal as R
A=R.A

def require(ok,name):
    if not ok:raise ValueError(name)

def receipt_floor(quoted,slippage,repayment):
    require(type(quoted) is int and 0<quoted<2**63,'quote bound')
    require(type(slippage) is int and 0<=slippage<=500,'slippage bound')
    require(type(repayment) is int and 0<=repayment<=2039280,'repayment bound')
    floor=(quoted*(10000-slippage)+9999)//10000-repayment
    require(floor>0,'positive net floor');return floor

def deltas(before,after):
    require(len(before)==len(after)==27,'native coverage')
    require(all(type(x) is int and 0<=x<2**64 for x in before+after),'native integer balances')
    return {i:b-a for i,(a,b) in enumerate(zip(before,after)) if a!=b}

def token_deltas(v,keys):
    def balances(rows):
        result={}
        for r in rows:
            i=r['accountIndex'];amount=r['uiTokenAmount']
            require(i in (3,5,17,19) and i not in result,'token coverage')
            require(r['programId']==keys[11] and r['mint']==keys[22 if i==17 else 25],'token mint/program')
            require(amount['decimals']==(9 if i==17 else 6),'token decimals')
            require(r['owner']==keys[1 if i==3 else 18] if i!=5 else r['owner']!=keys[1],'token authority')
            result[i]=int(amount['amount'])
        require(set(result)=={3,5,17,19},'complete token balances');return result
    b,a=map(balances,(v['preTokenBalances'],v['postTokenBalances']))
    return {i:a[i]-b[i] for i in b}

def audit_effects(v,keys):
    require(v['err'] is None,'simulation success')
    groups=v['innerInstructions'];require([g['index'] for g in groups]==[2,3],'inner group coverage')
    create,swap=[g['instructions'] for g in groups]
    require([i.get('parsed',{}).get('type') for i in create]==['getAccountDataSize','createAccount','initializeImmutableOwner','initializeAccount3'],'creation sequence')
    require(all(i['stackHeight']==2 for i in create),'creation depth')
    require([i['programId'] for i in create]==[keys[11],keys[8],keys[11],keys[11]],'creation programs')
    c=create[1]['parsed']['info'];funding=c['lamports']
    require(c==dict(lamports=1488440,newAccount=keys[4],owner=keys[11],source=keys[0],space=165),'sponsor-only funding')
    require(create[3]['parsed']['info']==dict(account=keys[4],mint=keys[22],owner=keys[1]),'destination owner')
    require(len(swap)==6,'swap CPI coverage')
    fee=swap[0];require(fee['programId']==keys[11] and fee['stackHeight']==2 and fee['parsed']==dict(type='transfer',info=dict(amount='2000',authority=keys[1],destination=keys[5],source=keys[3])),'inclusive fee transfer')
    for i in (1,5):require(swap[i]['programId']==keys[10] and swap[i]['stackHeight']==2 and swap[i]['accounts']==[keys[14]],'Jupiter event-only CPI')
    dex=swap[2];require(dex['programId']==keys[21] and dex['stackHeight']==2 and dex['accounts']==[keys[i] for i in (1,24,18,3,4,19,17,16,11,23,20,25,22,26,7,2,6,12,15)],'Byreal CPI exact accounts')
    alphabet='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';n=0
    for c in dex['data']:n=n*58+alphabet.index(c)
    b=n.to_bytes((n.bit_length()+7)//8,'big')
    require(len(b)==41 and b[:8]==bytes([229,46,213,132,105,40,40,228]) and int.from_bytes(b[8:16],'little')==998000 and b[40]==1,'Byreal exact-input instruction')
    amounts=[]
    for pos,source,destination,mint,authority,decimals in ((3,3,19,25,1,6),(4,17,4,22,18,9)):
        ix=swap[pos];info=ix['parsed']['info'];amount=info['tokenAmount']
        require(ix['programId']==keys[11] and ix['stackHeight']==3 and ix['parsed']['type']=='transferChecked','classic token transfer')
        require({k:info[k] for k in ('source','destination','mint','authority')}==dict(source=keys[source],destination=keys[destination],mint=keys[mint],authority=keys[authority]) and amount['decimals']==decimals,'checked transfer roles')
        amounts.append(int(amount['amount']))
    principal,gross=amounts;require(principal==998000 and gross>0,'exact swap principal')
    fee_native=v['fee'];require(type(fee_native) is int and fee_native>=0,'sponsored network fee')
    require(deltas(v['preBalances'],v['postBalances'])=={0:2039280-funding-fee_native,1:gross+funding-2039280,17:-gross},'whole native conservation')
    require(token_deltas(v,keys)=={3:-1000000,5:2000,17:-gross,19:998000},'whole token conservation')
    return dict(total_usdc_debit=1000000,explicit_usdc_fee=2000,swap_principal=principal,gross_lamports=gross,sponsor_funding_lamports=funding,repayment_lamports=2039280,observed_net_lamports=gross+funding-2039280,network_fee_paid_by_sponsor_lamports=fee_native)

def main():
    root=R.OUTPUT;raw=(R.CAPTURE/'response.json').read_bytes()
    require(hashlib.sha256(raw).hexdigest()=='1601a54d32f2f9825e614f59b776c4bef2fc8e19179a8ff623373bb694f113ee','capture pin')
    env=json.loads(json.loads(raw)['payloads'][0]['payload']);tx=bytes.fromhex(env['parameters']['unsignedTransaction'])
    keys_hex=json.loads((root/'resolved.json').read_bytes())['keys_hex'];keys=[A.encode58(bytes.fromhex(h)) for h in keys_hex]
    require(len(keys)==len(set(keys))==27 and tx[1:129]==bytes(128),'unsigned unique keys')
    accounts=json.loads((root/'accounts-response.json').read_bytes())['result']['value'];require(len(accounts)==27,'account coverage')
    for i in (0,1):A.account_bytes(accounts[i],A.SYSTEM,0)
    for i,mint,owner in ((3,25,1),(17,22,18),(19,25,18)):
        t=A.token(accounts[i]);require(t['mint_hex']==keys_hex[mint] and t['authority_hex']==keys_hex[owner] and t['delegate_hex'] is None and t['delegated_atoms']==0 and t['state']==1,'reviewed token account')
    require(accounts[4] is None,'destination absent at snapshot')
    source=A.token(accounts[3]);require(source['native_reserve'] is None and source['close_authority_hex']==keys_hex[0],'source rent authority')
    fee=A.token(accounts[5]);require(fee['mint_hex']==keys_hex[25] and fee['native_reserve'] is None and fee['authority_hex'] not in (keys_hex[0],keys_hex[1]) and fee['state']==1,'fee denomination')
    for i in (22,25):A.account_bytes(accounts[i],A.TOKEN,82)
    for i in (2,6,7,16,18,24,26):A.account_bytes(accounts[i],keys[21])
    require(accounts[21]['executable'] is True,'executable Byreal program')
    v=json.loads((root/'baseline-response.json').read_bytes())['result']['value'];effects=audit_effects(v,keys)
    route=bytes.fromhex(json.loads((R.CAPTURE/'decoded.json').read_bytes())['instructions'][3]['data']);require(len(route)==35 and route[12:16]==bytes([156,100,0,1]) and tx.count(route)==1,'route bytes')
    q=int.from_bytes(route[24:32],'little');s=int.from_bytes(route[32:34],'little');floor=receipt_floor(q,s,2039280)
    pair=json.loads((root/'rounding/1-response.json').read_bytes());requests=json.loads((root/'rounding/1-request.json').read_bytes());offset=tx.index(route)+24
    byid={x['id']:x for x in pair};require(set(byid)=={1,2},'rounding pair coverage');rows=[]
    for request in requests:
        changed=base64.b64decode(request['params'][0],validate=True)
        require(changed[:offset]==tx[:offset] and changed[offset+8:]==tx[offset+8:] and request['params'][1]['sigVerify'] is False,'quote-only unsigned diagnostic change')
        quoted=int.from_bytes(changed[offset:offset+8],'little');r=byid[request['id']]['result']
        # On a failed transaction these CPI transfers are attempted, not settled.
        outs=[i['parsed']['info']['tokenAmount']['amount'] for g in r['value']['innerInstructions'] for i in g['instructions'] if i.get('parsed',{}).get('type')=='transferChecked' and i['parsed']['info']['source']==keys[17]]
        require(len(outs)==1,'one attempted output');rows.append((quoted,int(outs[0]),r['context']['slot'],r['value']['err']))
    require(rows[0][0]+1==rows[1][0] and rows[0][1]==rows[1][1] and rows[0][2]==rows[1][2],'adjacent same-output same-slot pair')
    require(rows[0][3] is None and rows[1][3]=={'InstructionError':[3,{'Custom':6001}]},'boundary guard result')
    actual=rows[0][1];require((rows[0][0]*9500+9999)//10000==actual and rows[1][0]*9500//10000==actual and (rows[1][0]*9500+9999)//10000==actual+1,'ceiling distinguished from floor')
    report=dict(schema=1,snapshot_and_simulation_checks_passed=True,**effects,quoted_gross_lamports=q,slippage_bps=s,conservative_net_floor_lamports=floor,guaranteed_sponsor_credit=0,rounding_pair_ceiling_discriminated=True,simulation_only=True,live_admitted=False,
        hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ('accounts-response.json','lookup-response.json','baseline-response.json','rounding/0-response.json','rounding/1-response.json')})
    (root/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
