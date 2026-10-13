import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('reader', ROOT / 'tools/read-jupiter-idl.py')
reader = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reader)


def fixture():
    idl = {'address': reader.PROGRAM, 'instructions': [],
           'types': [{'name': 'Swap', 'type': {'kind': 'enum', 'variants': [{'name': 'Synthetic'}]}}]}
    blob = zlib.compress(json.dumps(idl).encode())
    data = bytes.fromhex('184662bf3a907b9e') + bytes([7]) * 32 + len(blob).to_bytes(4, 'little') + blob + bytes(16)
    return {'jsonrpc': '2.0', 'id': 1, 'result': {'context': {'slot': 99}, 'value': [
        {'owner': 'BPFLoaderUpgradeab1e11111111111111111111111', 'executable': True,
         'data': [base64.b64encode(bytes([2,0,0,0]) + bytes([3])*32).decode(), 'base64']},
        {'owner': reader.PROGRAM, 'executable': False, 'data': [base64.b64encode(data).decode(), 'base64']} ]}}


class ReadIdlTests(unittest.TestCase):
    def test_internal_namespace_discriminator(self):
        self.assertEqual(hashlib.sha256(b"internal:IdlAccount").digest()[:8].hex(), "184662bf3a907b9e")
        doc = fixture()
        try:
            summary, _ = reader.decode_response(json.dumps(doc).encode())
        except ValueError as error:
            self.fail(f"official internal IDL discriminator refused: {error}")
        self.assertEqual(summary["slot"], 99)
        raw = bytearray(base64.b64decode(doc["result"]["value"][1]["data"][0]))
        raw[:8] = hashlib.sha256(b"account:IdlAccount").digest()[:8]
        doc["result"]["value"][1]["data"][0] = base64.b64encode(raw).decode()
        with self.assertRaises(ValueError):
            reader.decode_response(json.dumps(doc).encode())

    def test_decode_owner_checked_idl(self):
        raw = json.dumps(fixture()).encode()
        summary, idl = reader.decode_response(raw)
        self.assertEqual(summary['slot'], 99)
        self.assertEqual(summary['swap_variants'], 1)
        self.assertFalse(summary['covers_variant_161'])
        self.assertEqual(idl['address'], reader.PROGRAM)
        self.assertEqual(summary['response_sha256'], hashlib.sha256(raw).hexdigest())
        self.assertFalse(summary['economic_authorization'])

    def test_refuse_identity_and_layout(self):
        for mutate in [lambda d: d.update(error={'code': 1}),
                       lambda d: d.update(id=2),
                       lambda d: d['result']['context'].update(slot=True),
                       lambda d: d['result']['value'][0].update(executable=False),
                       lambda d: d['result']['value'][0].update(owner='wrong'),
                       lambda d: d['result']['value'][1].update(owner='wrong'),
                       lambda d: d['result']['value'][1].update(executable=True),
                       lambda d: d['result']['value'].__setitem__(1, None),
                       lambda d: d['result']['value'].append(None)]:
            doc = fixture()
            mutate(doc)
            with self.assertRaises(ValueError):
                reader.decode_response(json.dumps(doc).encode())
        doc = fixture()
        data = bytearray(base64.b64decode(doc['result']['value'][1]['data'][0]))
        for offset in [0,40]:
            altered = data.copy()
            altered[offset] ^= 128
            doc['result']['value'][1]['data'][0] = base64.b64encode(altered).decode()
            with self.assertRaises(ValueError):
                reader.decode_response(json.dumps(doc).encode())

    def test_decompression_bound_and_duplicate_keys(self):
        with self.assertRaises(ValueError):
            reader.decode_response(b'{"jsonrpc":"2.0","id":1,"id":1}')
        doc = fixture()
        blob = zlib.compress(b' ' * (reader.MAX_IDL + 1))
        data = bytes.fromhex('184662bf3a907b9e') + bytes(32) + len(blob).to_bytes(4,'little') + blob
        doc['result']['value'][1]['data'][0] = base64.b64encode(data).decode()
        with self.assertRaises(ValueError):
            reader.decode_response(json.dumps(doc).encode())

    def test_one_request_and_consumed_marker(self):
        calls = []
        class Reply:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, count): return json.dumps(fixture()).encode()[:count]
        class Opener:
            def open(self, request, timeout):
                calls.append((request, timeout))
                return Reply()
        scratch = Path.home() / '.cache/velocita-build/abc-contract-20261012/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as d:
            reader.run_once(d, Opener())
            with self.assertRaises(FileExistsError):
                reader.run_once(d, Opener())
        self.assertEqual(len(calls), 1)
        req, timeout = calls[0]
        self.assertEqual(req.full_url, reader.ENDPOINT)
        self.assertEqual(req.get_method(), 'POST')
        self.assertEqual(json.loads(req.data), reader.BODY)
        self.assertEqual(timeout, 20)
        self.assertFalse(req.has_header('Authorization'))

    def test_timeout_has_no_retry(self):
        calls = []
        class Opener:
            def open(self, *args, **kwargs):
                calls.append(1)
                raise TimeoutError('fixture')
        scratch = Path.home() / '.cache/velocita-build/abc-contract-20261012/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as d:
            with self.assertRaises(TimeoutError): reader.run_once(d, Opener())
            with self.assertRaises(FileExistsError): reader.run_once(d, Opener())
        self.assertEqual(len(calls), 1)


if __name__ == '__main__':
    unittest.main()
