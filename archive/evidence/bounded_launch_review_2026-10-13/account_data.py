"""Public account-data helpers retained with the bounded evidence; no network calls."""

import base64
import hashlib
import json
import os

ENDPOINT='https://api.mainnet-beta.solana.com'

ALPHABET='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'

TOKEN='TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA'

SYSTEM='11111111111111111111111111111111'

ALT='AddressLookupTab1e1111111111111111111111111'

USDC='EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v'

SOL='So11111111111111111111111111111111111111112'

def require(ok,name):
    if not ok:raise ValueError(name)

def canonical(d):return json.dumps(d,separators=(',',':'),sort_keys=True).encode()

def sha(b):return hashlib.sha256(b).hexdigest()

def decode58(s):
    n=0
    for c in s:n=n*58+ALPHABET.index(c)
    b=b'\0'*(len(s)-len(s.lstrip('1')))+n.to_bytes((n.bit_length()+7)//8,'big')
    require(len(b)==32,'public key length');return b

def encode58(b):
    n=int.from_bytes(b,'big');s=''
    while n:n,r=divmod(n,58);s=ALPHABET[r]+s
    return '1'*(len(b)-len(b.lstrip(b'\0')))+s

def write(path,b):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'wb') as f:f.write(b);f.flush();os.fsync(f.fileno())

def account_bytes(a,owner,length=None):
    require(isinstance(a,dict) and a.get('owner')==owner and a.get('executable') is False,'account program')
    require(type(a.get('lamports')) is int and 0<=a['lamports']<2**64,'account lamports')
    require(isinstance(a.get('data'),list) and len(a['data'])==2 and a['data'][1]=='base64','encoding')
    b=base64.b64decode(a['data'][0],validate=True)
    require(length is None or len(b)==length,'account size');return b

def token(a):
    b=account_bytes(a,TOKEN,165);opts=[int.from_bytes(b[i:i+4],'little') for i in (72,109,129)]
    require(all(v in (0,1) for v in opts) and b[108] in (0,1,2),'token option/state')
    return {'mint_hex':b[:32].hex(),'authority_hex':b[32:64].hex(),'atoms':int.from_bytes(b[64:72],'little'),
        'delegate_hex':b[76:108].hex() if opts[0] else None,'state':b[108],
        'native_reserve':int.from_bytes(b[113:121],'little') if opts[1] else None,
        'delegated_atoms':int.from_bytes(b[121:129],'little'),
        'close_authority_hex':b[133:165].hex() if opts[2] else None}
