"""One fixed public program/legacy-IDL read; no wallet, signing or transaction methods."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import urllib.request
import zlib

PROGRAM = 'JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4'
IDL = 'C88XWfp26heEmDkmfSzeXP7Fd7GQJ2j9dDTUsyiZbUTa'
ENDPOINT = 'https://api.mainnet-beta.solana.com'
MAX_RESPONSE = 524288
MAX_IDL = 524288
BODY = {'jsonrpc': '2.0', 'id': 1, 'method': 'getMultipleAccounts',
        'params': [[PROGRAM, IDL], {'encoding': 'base64', 'commitment': 'finalized'}]}


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError('duplicate JSON key')
        out[key] = value
    return out


def parse_json(raw):
    return json.loads(raw, object_pairs_hook=unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('nonfinite JSON')))


def decode_response(raw):
    if len(raw) > MAX_RESPONSE:
        raise ValueError('response capacity')
    try:
        doc = parse_json(raw)
        if doc.get('jsonrpc') != '2.0' or type(doc.get('id')) is not int or doc['id'] != 1 or 'error' in doc:
            raise ValueError('RPC identity or error')
        result = doc['result']
        slot = result['context']['slot']
        rows = result['value']
        if type(slot) is not int or slot <= 0 or not isinstance(rows, list) or len(rows) != 2:
            raise ValueError('RPC result shape')
        program, account = rows
        if program['executable'] is not True or program['owner'] != 'BPFLoaderUpgradeab1e11111111111111111111111':
            raise ValueError('program identity')
        if account['owner'] != PROGRAM or account['executable'] is not False:
            raise ValueError('IDL owner or executable')
        for row in rows:
            if not isinstance(row['data'], list) or len(row['data']) != 2 or row['data'][1] != 'base64':
                raise ValueError('account encoding')
        program_bytes = base64.b64decode(program['data'][0], validate=True)
        if len(program_bytes) != 36 or program_bytes[:4] != bytes([2, 0, 0, 0]):
            raise ValueError('program loader layout')
        data = base64.b64decode(account['data'][0], validate=True)
        if len(data) < 44 or data[:8] != bytes.fromhex('184662bf3a907b9e'):
            raise ValueError('IDL account layout')
        size = int.from_bytes(data[40:44], 'little')
        if size == 0 or size > len(data) - 44:
            raise ValueError('IDL data length')
        decoder = zlib.decompressobj()
        payload = decoder.decompress(data[44:44 + size], MAX_IDL + 1)
        if len(payload) > MAX_IDL or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
            raise ValueError('IDL compression or capacity')
        idl = parse_json(payload)
        if not isinstance(idl, dict) or idl.get('address', PROGRAM) != PROGRAM:
            raise ValueError('IDL declared address')
        if not isinstance(idl.get('instructions'), list) or not isinstance(idl.get('types'), list):
            raise ValueError('IDL definitions missing')
        swaps = [t for t in idl['types'] if t.get('name') == 'Swap']
        if len(swaps) != 1 or swaps[0]['type']['kind'] != 'enum':
            raise ValueError('Swap enum missing or duplicate')
        variants = swaps[0]['type']['variants']
        if not isinstance(variants, list) or not 0 < len(variants) <= 256 or any(
                not isinstance(v, dict) or not isinstance(v.get('name'), str) for v in variants):
            raise ValueError('Swap variants shape')
        return {'slot': slot, 'program': PROGRAM, 'idl_account': IDL,
                'response_sha256': hashlib.sha256(raw).hexdigest(),
                'idl_data_sha256': hashlib.sha256(payload).hexdigest(),
                'swap_variants': len(variants), 'covers_variant_161': len(variants) > 161,
                'variant_161': variants[161] if len(variants) > 161 else None,
                'economic_authorization': False,
                'scope': 'One RPC observation of program-owned metadata; no deployed binary equivalence or route semantics proven.'}, idl
    except (KeyError, TypeError, AttributeError, zlib.error) as error:
        raise ValueError('unusable metadata response') from error


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('redirect refused')


def run_once(output, opener=None):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    body = json.dumps(BODY, separators=(',', ':')).encode()
    with (output / 'attempt.json').open('x') as stream:
        json.dump({'endpoint': ENDPOINT, 'request': BODY,
                   'request_sha256': hashlib.sha256(body).hexdigest()}, stream, indent=2)
        stream.write('\n')
    request = urllib.request.Request(ENDPOINT, data=body, method='POST',
        headers={'Content-Type': 'application/json', 'User-Agent': 'velocita-contract-read/1'})
    if opener is None:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    with opener.open(request, timeout=20) as response:
        if response.status != 200:
            raise ValueError('non-success HTTP status')
        raw = response.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError('response capacity')
    with (output / 'response.json').open('xb') as stream:
        stream.write(raw)
    summary, idl = decode_response(raw)
    with (output / 'idl.json').open('x') as stream:
        json.dump(idl, stream, indent=2)
        stream.write('\n')
    with (output / 'summary.json').open('x') as stream:
        json.dump(summary, stream, indent=2)
        stream.write('\n')
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.run:
        print(json.dumps({'endpoint': ENDPOINT, 'request': BODY, 'sent': False}, indent=2))
        return
    print(json.dumps(run_once(args.output), indent=2))


if __name__ == '__main__':
    main()
