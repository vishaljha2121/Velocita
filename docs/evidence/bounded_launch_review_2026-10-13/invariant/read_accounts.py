import importlib.util,json,os,signal,urllib.request
from pathlib import Path
root=Path('/Users/vishaljha/aeron-raft-pi');spec=importlib.util.spec_from_file_location('A',root/'docs/evidence/bounded_launch_review_2026-10-13/account_data.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):raise ValueError('redirect refused')
def main():
 os.umask(0o077);private=Path.home()/'.velocita-credentials/truemarkets';capture=private/'bison-launch-quote-1a46fd8-20261014';out=private/'invariant-account-review-1a46fd8-20261014';out.mkdir(mode=0o700,exist_ok=False)
 d=json.loads((capture/'decoded.json').read_bytes());A.require(len(d['static_keys'])==11 and len(d['lookups'])==1,'capture shape');lookup=d['lookups'][0]
 A.write(out/'attempt.json',A.canonical(dict(max_requests=2,retries=0,methods=['getMultipleAccounts'],signed=False,broadcast=False,capture_sha256=A.sha((capture/'response.private.json').read_bytes()))))
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
 def rpc(name,keys,slot=None):
  cfg={'encoding':'base64','commitment':'finalized'}
  if slot:cfg['minContextSlot']=slot
  body=A.canonical(dict(jsonrpc='2.0',id=1,method='getMultipleAccounts',params=[keys,cfg]));A.write(out/(name+'-request.json'),body)
  req=urllib.request.Request(A.ENDPOINT,data=body,method='POST',headers={'Content-Type':'application/json','User-Agent':'velocita-contract-read/1'})
  with opener.open(req,timeout=25) as r:
   A.require(r.status==200,'HTTP');raw=r.read(4000001)
  A.write(out/(name+'-response.json'),raw);A.require(len(raw)<=4000000,'size');v=json.loads(raw);A.require(v.get('id')==1 and 'error' not in v,'RPC');return v['result']
 result=rpc('lookup',[A.encode58(bytes.fromhex(lookup['table']))]);A.require(len(result['value'])==1,'lookup count');slot=result['context']['slot'];b=A.account_bytes(result['value'][0],A.ALT)
 A.require(len(b)>=56 and (len(b)-56)%32==0 and b[:4]==b'\x01\0\0\0' and b[4:12]==b'\xff'*8 and int.from_bytes(b[12:20],'little')<slot,'active warm table')
 addresses=[b[i:i+32] for i in range(56,len(b),32)];indices=lookup['writable']+lookup['readonly'];A.require(all(type(i)is int and 0<=i<len(addresses) for i in indices),'coverage');keys=[bytes.fromhex(k) for k in d['static_keys']]+[addresses[i] for i in indices];A.require(len(keys)==len(set(keys))==19,'unique19')
 result=rpc('accounts',[A.encode58(k) for k in keys],slot);A.require(len(result['value'])==19,'accounts count');A.write(out/'resolved.json',A.canonical(dict(keys_hex=[k.hex() for k in keys],slot=result['context']['slot'],lookup=lookup)))
 summary=dict(read_only=True,requests=2,signed=False,broadcast=False,lookup_addresses=len(addresses),slot=result['context']['slot'],table_data_sha256=A.sha(b),hashes={p.name:A.sha(p.read_bytes()) for p in out.iterdir() if p.is_file()});A.write(out/'summary.json',A.canonical(summary));print(json.dumps(summary,indent=2))
signal.signal(signal.SIGALRM,lambda *_:(_ for _ in ()).throw(TimeoutError('expired')));signal.alarm(60)
try:main()
finally:signal.alarm(0)
