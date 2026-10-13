"""Offline MVP quote boundaries; synthetic sessions/responses, no socket access."""
import datetime
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('mvp_quote',ROOT/'tools/tm-mvp-quote-check.py')
PROBE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class MvpQuoteTests(unittest.TestCase):
    def setUp(self):
        scratch=Path.home()/'.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True,exist_ok=True)
        directory=tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(directory.cleanup)
        self.root=Path(directory.name)
        self.now=int(time.time()*1000)
        self.session=self.root/'session.json'
        self.session.write_text(json.dumps(self.tokens(600)))
        self.session.chmod(0o600)
        self.output=self.root/'quote'
        self.history=self.root/'account.history'
        self.identity=dict(id='11111111-1111-1111-1111-111111111111',trading={'defi':{'enabled':True}},
            wallets=[dict(name='main',provider='turnkey',addresses=[dict(chain='solana',address='1'*31+'3')])])
        self.cut=dict(schema=1,scope='sol-usdc-current-accounts',owner_sha256=hashlib.sha256(bytes(31)+b'\x02').hexdigest(),
            user_id_sha256=hashlib.sha256(self.identity['id'].encode()).hexdigest(),setup_sha256='a'*64,
            balances_sha256='b'*64,evidence_manifest_sha256='c'*64,capture_started_ms=self.now-2000,
            capture_finished_ms=self.now-1000,start_slot=100,end_slot=101,stable_window=True,
            pagination_exhausted=True,unsupported_accounts=0,blocking_orders=0,base_atoms=40764166,quote_atoms=40006989)
        self.pin=self.save_cut(self.cut)
        payload='synthetic unsigned payload'
        self.response=dict(status='initialized',order_id='private-order-identifier',
            payloads=[dict(payload=payload,digest=hashlib.sha256(payload.encode()).hexdigest())],
            quote=dict(qty='0.040764166',qty_out='4.0',fee='0.008'))
        self.calls=[]

    def tokens(self, seconds):
        expiry=datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(seconds=seconds)
        return dict(access_token='fixture.access.token',refresh_token='fixture.refresh.token',
                    expires_in=expiry.isoformat(),token_type='Bearer')

    def save_cut(self, cut):
        raw=json.dumps(cut).encode()
        self.history.write_bytes(raw)
        self.history.chmod(0o600)
        return hashlib.sha256(raw).hexdigest()

    def send(self, method, path, document=None, token=None):
        self.calls.append((method,path,document,token))
        self.assertTrue((self.output/'attempt.json').is_file(),'attempt_durable_before_network')
        if path=='/v1/auth/token/refresh': return 200,json.dumps(self.tokens(600)).encode()
        if path=='/v1/account/me/trading': return 200,json.dumps(self.identity).encode()
        return 201,json.dumps(self.response).encode()

    def run_probe(self, side='sell', send=None, now_ms=None):
        with patch.object(PROBE.CONTRACT,'verify_pinned_catalog'):
            return PROBE.run(self.session,self.output,self.history,self.pin,b'synthetic catalog',side,
                             send or self.send,now_ms or (lambda:self.now))

    def test_exact_history_sell_and_fixed_buy(self):
        cut=dict(base_atoms=40764166,quote_atoms=40006989)
        expected=dict(base_asset='So11111111111111111111111111111111111111112',
                      chain='solana',side='sell',type='market',qty='0.040764166',qty_unit='base')
        self.assertEqual(PROBE.quote_body('sell',cut),expected,'entire_observed_base_exact_decimal')
        self.assertEqual(PROBE.quote_body('buy',cut),dict(expected,side='buy',qty='1.00',qty_unit='quote'),
                         'buy_exactly_one_usdc')

    def test_single_sell_preserves_private_evidence_and_no_guarantee(self):
        result=self.run_probe()
        self.assertEqual([c[:2] for c in self.calls],[('GET','/v1/account/me/trading'),('POST','/v1/gateway/orders')])
        self.assertEqual(self.calls[1][2]['qty'],'0.040764166','sell_input_bound_to_history')
        self.assertEqual(result['quoted_input_atoms'],40764166)
        self.assertEqual(result['quoted_output_atoms'],4000000)
        self.assertFalse(result['guaranteed_net_receipt_verified'],'quote_never_claims_guaranteed_net')
        self.assertFalse(result['opening_valuation_adopted'])
        self.assertFalse(result['signed']);self.assertFalse(result['execute_request_sent'])
        for path in self.output.iterdir(): self.assertEqual(path.stat().st_mode&0o777,0o600)
        self.assertNotIn('private-order-identifier',json.dumps(result))
        self.assertNotIn('fixture.access.token',json.dumps(result))
        with self.assertRaises(FileExistsError): self.run_probe()
        self.assertEqual(len(self.calls),2,'capture_cannot_be_replayed')

    def test_single_fixed_buy(self):
        self.response['quote']=dict(qty='1.00',qty_out='0.009',fee='0.002')
        result=self.run_probe('buy')
        self.assertEqual(self.calls[1][2]['qty'],'1.00','fixed_one_dollar_buy')
        self.assertEqual(result['quoted_input_atoms'],1000000)
        self.assertEqual(result['quoted_explicit_fee_usdc_atoms'],2000)

    def test_refresh_is_once_and_attempt_precedes_it(self):
        self.session.write_text(json.dumps(self.tokens(-600)))
        self.run_probe()
        self.assertEqual([c[1] for c in self.calls],['/v1/auth/token/refresh','/v1/account/me/trading','/v1/gateway/orders'])
        self.assertEqual(self.calls[0][2],{'refresh_token':'fixture.refresh.token'})
        self.assertIsNone(self.calls[0][3])

    def test_refresh_failure_stops_without_identity_or_quote(self):
        self.session.write_text(json.dumps(self.tokens(-600)))
        def refuse(*args,**kwargs): self.calls.append(args);return 401,b'{}'
        with self.assertRaisesRegex(ValueError,'refresh refused'): self.run_probe(send=refuse)
        with self.assertRaises(FileExistsError): self.run_probe(send=refuse)
        self.assertEqual(len(self.calls),1,'refresh_error_has_no_followup')

    def test_identity_mismatch_stops_before_quote(self):
        self.identity['id']='22222222-2222-2222-2222-222222222222'
        with self.assertRaisesRegex(ValueError,'identity differs',msg='wrong_account_never_quotes'): self.run_probe()
        self.assertEqual(len(self.calls),1)

    def test_identity_http_failure_has_no_followup(self):
        def refuse(*args,**kwargs): self.calls.append(args);return 500,b'private error'
        with self.assertRaisesRegex(ValueError,'identity read refused'): self.run_probe(send=refuse)
        self.assertEqual(len(self.calls),1)

    def test_timeout_retains_attempt_and_never_retries(self):
        def timeout(method,path,document=None,token=None):
            if path=='/v1/gateway/orders':
                self.calls.append((method,path,document,token))
                raise TimeoutError('synthetic quote timeout')
            return self.send(method,path,document,token)
        with self.assertRaises(TimeoutError): self.run_probe(send=timeout)
        with self.assertRaises(FileExistsError): self.run_probe(send=timeout)
        self.assertEqual(len(self.calls),2,'ambiguous_quote_never_resent')

    def test_rejected_quote_is_preserved_without_resize(self):
        def reject(method,path,document=None,token=None):
            if path=='/v1/gateway/orders':
                self.calls.append((method,path,document,token));return 400,b'{"private":"minimum"}'
            return self.send(method,path,document,token)
        result=self.run_probe(send=reject)
        self.assertEqual(result['http_status'],400)
        self.assertEqual(len(self.calls),2)
        self.assertEqual((self.output/'response.private.json').read_bytes(),b'{"private":"minimum"}')
        self.assertNotIn('minimum',json.dumps(result))

    def test_pin_and_freshness_are_required(self):
        raw=self.history.read_bytes()
        with self.assertRaisesRegex(ValueError,'digest mismatch',msg='history_hash_required'):
            PROBE.history_cut(raw,'f'*64,self.now)
        for now in (self.cut['capture_finished_ms']-1,self.cut['capture_finished_ms']+900001):
            with self.subTest(now=now),self.assertRaisesRegex(ValueError,'timing',msg='history_freshness_required'):
                PROBE.history_cut(raw,self.pin,now)
        for changed in ({'stable_window':False},{'blocking_orders':1},{'base_atoms':True},
                        {'base_atoms':-1},{'quote_atoms':2**63},{'pagination_exhausted':False},
                        {'unsupported_accounts':1},{'end_slot':99}):
            altered=dict(self.cut,**changed);raw=json.dumps(altered).encode()
            with self.subTest(changed=changed),self.assertRaises(ValueError):
                PROBE.history_cut(raw,hashlib.sha256(raw).hexdigest(),self.now)

    def test_staleness_after_identity_prevents_create(self):
        counter=[self.now]
        def age(method,path,document=None,token=None):
            result=self.send(method,path,document,token)
            counter[0]+=900001
            return result
        with self.assertRaisesRegex(ValueError,'timing',msg='freshness_rechecked_before_create'):
            self.run_probe(send=age,now_ms=lambda:counter[0])
        self.assertEqual(len(self.calls),1)

    def test_insufficient_balance_never_accesses_session_or_network(self):
        self.pin=self.save_cut(dict(self.cut,quote_atoms=999999))
        with self.assertRaisesRegex(ValueError,'insufficient',msg='buy_cannot_exceed_observed_usdc'):
            self.run_probe('buy')
        self.assertEqual(self.calls,[])
        self.assertFalse(self.output.exists())
        with self.assertRaises(ValueError): PROBE.quote_body('sell',dict(self.cut,base_atoms=0))

    def test_quote_input_mismatch_refuses(self):
        self.response['quote']['qty']='0.05'
        with self.assertRaisesRegex(ValueError,'quantity differs',msg='response_input_checked'):
            self.run_probe()

    def test_lock_contention_refuses_before_request(self):
        lock=self.session.with_name('session.json.lock')
        fd=os.open(lock,os.O_RDWR|os.O_CREAT,0o600)
        try:
            PROBE.fcntl.flock(fd,PROBE.fcntl.LOCK_EX|PROBE.fcntl.LOCK_NB)
            with self.assertRaises(BlockingIOError): self.run_probe()
        finally: os.close(fd)
        self.assertEqual(self.calls,[])

    def test_exchange_refuses_execution_resize_and_other_methods(self):
        body=PROBE.quote_body('sell',self.cut)
        exchange=PROBE.Exchange(body)
        cases=[('POST','/v1/gateway/orders/x/execute',body),
               ('POST','/v1/gateway/orders',dict(body,qty='0.040764167')),
               ('POST','/v1/auth/api-key/token',{}),('DELETE','/v1/gateway/orders',body),
               ('GET','/v1/gateway/orders',None)]
        with patch.object(PROBE.urllib.request,'build_opener',side_effect=AssertionError('forbidden_network_reached')):
            for method,path,data in cases:
                with self.subTest(path=path),self.assertRaisesRegex(ValueError,'bounded unsigned',msg='execution_endpoint_refused'):
                    exchange(method,path,data,'fixture.access.token')

    def test_wire_origin_and_no_redirect_or_proxy(self):
        body=PROBE.quote_body('buy',self.cut)
        response=io.BytesIO(b'{}');response.code=201
        opener=Mock();opener.open.return_value=response
        with patch.object(PROBE.urllib.request,'build_opener',return_value=opener) as build:
            self.assertEqual(PROBE.Exchange(body)('POST','/v1/gateway/orders',body,'fixture.access.token'),(201,b'{}'))
        request=opener.open.call_args.args[0]
        self.assertEqual(request.full_url,'https://api.truemarkets.co/v1/gateway/orders')
        self.assertEqual(request.method,'POST')
        self.assertEqual(json.loads(request.data),body)
        self.assertEqual(build.call_args.args[0].proxies,{})
        self.assertIsInstance(build.call_args.args[1],PROBE.ACCOUNT.NoRedirect)

    def test_private_input_rejects_symlink_and_bad_modes(self):
        self.history.chmod(0o644)
        with self.assertRaisesRegex(ValueError,'private input'): self.run_probe()
        self.history.chmod(0o600)
        alias=self.root/'alias';alias.symlink_to(self.history)
        with self.assertRaisesRegex(RuntimeError,'not a regular file'): PROBE.read_private(alias,4096)
        self.assertEqual(self.calls,[])


if __name__=='__main__': unittest.main()
