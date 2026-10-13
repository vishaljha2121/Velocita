"""One fixed historical transaction read; no signing or submission methods."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import urllib.request

SIGNATURE = '4QmcU9go1f5eLoQmw9qhg4BjCeHb9igjGgbKBgjzpEAiJFnqgmnpLELaRP4khxf2P7s4cAK5UFeSQ55uiEPpDXea'
ENDPOINT = 'https://api.mainnet-beta.solana.com'
BODY = {'jsonrpc': '2.0', 'id': 1, 'method': 'getTransaction', 'params': [SIGNATURE,
        {'encoding': 'json', 'commitment': 'finalized', 'maxSupportedTransactionVersion': 0}]}
MAX_RESPONSE = 524288


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def integer(value):
    return type(value) is int and 0 <= value <= 18446744073709551615


def decode(raw):
    if len(raw) > MAX_RESPONSE:
        raise ValueError('response capacity')
    doc = json.loads(raw, object_pairs_hook=unique,
                     parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')))
    if not isinstance(doc, dict) or doc.get('jsonrpc') != '2.0' or type(doc.get('id')) is not int or doc['id'] != 1 or 'error' in doc:
        raise ValueError('RPC identity/error')
    result = doc.get('result')
    if result is None:
        return {'found': False, 'response_sha256': hashlib.sha256(raw).hexdigest(), 'settlement_authorized': False}
    try:
        tx, meta = result['transaction'], result['meta']
        if not integer(result['slot']) or result['slot'] == 0 or not (type(result.get('version')) is int and result['version'] == 0 or result.get('version') == 'legacy'):
            raise ValueError('unsupported slot/version')
        if not isinstance(tx['signatures'], list) or not tx['signatures'] or tx['signatures'][0] != SIGNATURE:
            raise ValueError('transaction identity')
        keys = tx['message']['accountKeys']
        loaded = meta.get('loadedAddresses', {'writable': [], 'readonly': []})
        if not isinstance(keys, list) or not all(isinstance(k, str) for k in keys):
            raise ValueError('account key shape')
        for name in ('writable', 'readonly'):
            if not isinstance(loaded[name], list) or not all(isinstance(k, str) for k in loaded[name]):
                raise ValueError('loaded key shape')
        count = len(keys) + len(loaded['writable']) + len(loaded['readonly'])
        if not 1 <= count <= 256 or not integer(meta['fee']) or 'err' not in meta:
            raise ValueError('metadata shape')
        for name in ('preBalances', 'postBalances'):
            if not isinstance(meta[name], list) or len(meta[name]) != count or not all(integer(x) for x in meta[name]):
                raise ValueError('balance shape')
        for name in ('preTokenBalances', 'postTokenBalances', 'innerInstructions'):
            if not isinstance(meta.get(name), list):
                raise ValueError('missing token/inner history')
        return {'found': True, 'slot': result['slot'], 'version': result['version'],
                'rpc_reports_success': meta['err'] is None, 'network_fee_lamports': meta['fee'],
                'account_count': count, 'pre_token_rows': len(meta['preTokenBalances']),
                'post_token_rows': len(meta['postTokenBalances']), 'inner_instruction_groups': len(meta['innerInstructions']),
                'response_sha256': hashlib.sha256(raw).hexdigest(), 'settlement_authorized': False}
    except (KeyError, TypeError, AttributeError) as error:
        raise ValueError('unusable transaction metadata') from error


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('redirect refused')


def write_private(path, raw):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def run_once(output, opener=None):
    output = Path(output)
    output.mkdir(mode=0o700, parents=True, exist_ok=True)
    body = json.dumps(BODY, separators=(',', ':')).encode()
    write_private(output / 'attempt.json', json.dumps({'endpoint': ENDPOINT, 'request': BODY}).encode())
    request = urllib.request.Request(ENDPOINT, data=body, method='POST',
        headers={'Content-Type': 'application/json', 'User-Agent': 'velocita-contract-read/1'})
    if opener is None:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    with opener.open(request, timeout=20) as reply:
        if reply.status != 200:
            raise ValueError('HTTP status')
        raw = reply.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError('response capacity')
    write_private(output / 'response.json', raw)
    summary = decode(raw)
    write_private(output / 'summary.json', (json.dumps(summary, indent=2) + '\n').encode())
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run_once(args.output) if args.run else {'sent': False, 'endpoint': ENDPOINT, 'request': BODY}, indent=2))


if __name__ == '__main__':
    main()
