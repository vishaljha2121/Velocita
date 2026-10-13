import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('reader', Path(__file__).resolve().parents[1] / 'tools/read-owner-settlement.py')
reader = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reader)


def fixture():
    return {'jsonrpc': '2.0', 'id': 1, 'result': {'slot': 99, 'version': 0,
        'transaction': {'signatures': [reader.SIGNATURE], 'message': {'accountKeys': ['fixture']}},
        'meta': {'fee': 10000, 'err': None, 'loadedAddresses': {'writable': [], 'readonly': []},
                 'preBalances': [20000], 'postBalances': [10000], 'preTokenBalances': [],
                 'postTokenBalances': [], 'innerInstructions': []}}}


class ReaderTests(unittest.TestCase):
    def test_identity_and_capacity(self):
        value = reader.decode(json.dumps(fixture()).encode())
        self.assertEqual(value['network_fee_lamports'], 10000)
        self.assertFalse(value['settlement_authorized'])
        for mutate in [lambda d: d.update(id=2), lambda d: d['result'].update(slot=True), lambda d: d['result'].update(version=False),
                       lambda d: d['result']['transaction'].update(signatures=['wrong']),
                       lambda d: d['result']['meta'].update(preBalances=[]),
                       lambda d: d['result']['meta'].update(fee=-1),
                       lambda d: d['result']['meta'].pop('innerInstructions')]:
            data = fixture(); mutate(data)
            with self.assertRaises(ValueError): reader.decode(json.dumps(data).encode())
        with self.assertRaises(ValueError): reader.decode(b' ' * (reader.MAX_RESPONSE+1))
        with self.assertRaises(ValueError): reader.decode(b'{"id":1,"id":1}')

    def test_absent_and_failed_transaction(self):
        self.assertFalse(reader.decode(b'{"jsonrpc":"2.0","id":1,"result":null}')['found'])
        data = fixture();data['result']['meta']['err'] = {'InstructionError': [1,'fixture']}
        r = reader.decode(json.dumps(data).encode())
        self.assertFalse(r['rpc_reports_success'])
        self.assertEqual(r['network_fee_lamports'], 10000)

    def test_one_exact_request(self):
        calls = []
        class Reply:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, n): return json.dumps(fixture()).encode()[:n]
        class Opener:
            def open(self, request, timeout):
                calls.append((request, timeout));return Reply()
        scratch = Path.home()/'.cache/velocita-build/settlement-20261012/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as d:
            reader.run_once(d, Opener())
            with self.assertRaises(FileExistsError): reader.run_once(d, Opener())
            self.assertEqual((Path(d)/'response.json').stat().st_mode & 0o777, 0o600)
        self.assertEqual(len(calls), 1)
        request, timeout = calls[0]
        self.assertEqual(json.loads(request.data), reader.BODY)
        self.assertEqual(request.full_url, reader.ENDPOINT)
        self.assertEqual(request.method, 'POST')
        self.assertEqual(timeout, 20)
        self.assertFalse(request.has_header('Authorization'))

    def test_timeout_consumes_attempt(self):
        calls = []
        class Opener:
            def open(self, *args, **kwargs): calls.append(1);raise TimeoutError('fixture')
        scratch = Path.home()/'.cache/velocita-build/settlement-20261012/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as d:
            with self.assertRaises(TimeoutError): reader.run_once(d, Opener())
            with self.assertRaises(FileExistsError): reader.run_once(d, Opener())
        self.assertEqual(len(calls), 1)


if __name__ == '__main__': unittest.main()
