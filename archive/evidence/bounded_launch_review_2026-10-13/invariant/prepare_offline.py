"""Prepare private historical diagnostic documents, never live adoption files."""
import base64
import hashlib
import json
import os
from pathlib import Path
import review_capture as C


def main():
    os.umask(0o077)
    private = Path.home()/'.velocita-credentials/truemarkets'
    capture = private/'bison-launch-quote-1a46fd8-20261014'
    accounts = private/'invariant-account-review-1a46fd8-20261014'
    output = private/'invariant-offline-candidate-20261014'
    d,keys,_ = C.load(); C.review(d,keys,_)
    old = capture/'offline-check'
    fields = lambda b: dict(line.split('=',1) for line in b.decode().splitlines())
    serialize = lambda f: ''.join(k+'='+v+'\n' for k,v in f.items()).encode()
    p = fields((old/'runtime.profile').read_bytes())
    t = fields((old/'transaction.review').read_bytes())
    response = json.loads((capture/'response.private.json').read_bytes())
    envelope = response['payloads'][0]['payload'].encode()
    timestamp = int(json.loads(envelope)['timestampMs'])
    lookup_raw = (accounts/'lookup-response.json').read_bytes()
    lookup = json.loads(lookup_raw)['result']
    table = C.A.account_bytes(lookup['value'][0],C.A.ALT)
    assert len(table)>=56 and (len(table)-56)%32 == 0
    t.update(schema='5',account_keys=''.join(k.hex() for k in keys),
             lookup_table=d['lookups'][0]['table'],lookup_slot=str(lookup['context']['slot']),
             lookup_evidence=hashlib.sha256(lookup_raw).hexdigest(),
             lookup_address_count=str((len(table)-56)//32),lookup_addresses=table[56:].hex(),
             expires_at_ms=str(timestamp+30000))
    review = serialize(t)
    p['transaction_review_digest'] = hashlib.sha256(review).hexdigest()
    assert p['max_buy_output_deduction_lamports']=='2039280'
    assert p['owner_wallet']==keys[1].hex()
    output.mkdir(mode=0o700,exist_ok=False)
    for name,data in [('transaction.review',review),('runtime.profile',serialize(p)),('offline-envelope.json',envelope)]:
        C.A.write(output/name,data)
    C.A.write(output/'diagnostic-scope.json',C.A.canonical(dict(historical_clock=True,
        synthetic_intent=True,account_snapshot_after_transaction=True,live_admitted=False,
        adopted=False,signed=False,broadcast=False)))
    print('Prepared private historical diagnostic candidate; not an adoption bundle.')


if __name__=='__main__':main()
