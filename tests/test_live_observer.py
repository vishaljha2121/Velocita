"""Independent snapshot/journal examples for read-only live observability."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('live',ROOT/'deploy/v2/observability/live_observer.py')
O=importlib.util.module_from_spec(spec);spec.loader.exec_module(O)

class LiveObserverTests(unittest.TestCase):
    def journal(self,phase=1,effect=0):
        b=bytearray(464);b[:8]=b'VSSJ\1\0\0\0';struct.pack_into('<QQQQ',b,8,1,phase,6,effect);b[-32:]=hashlib.sha256(b[:-32]).digest();return bytes(b)
    def test_idle_is_one_not_zero_and_not_a_fill(self):
        d=O.journal(self.journal(),'submission');self.assertEqual(d,{'ordinal':1,'phase':1,'index':6,'effect':0})
        self.assertNotIn('settled',d)
    def test_attempt_is_not_settlement(self):
        d=O.journal(self.journal(2,1),'submission');self.assertEqual(d['phase'],2);self.assertNotIn('fill',d)
    def test_corrupt_or_unknown_phase_refused(self):
        for b in (self.journal()[:-1]+b'X',self.journal(0),self.journal(4),b''):
            with self.subTest(size=len(b)),self.assertRaises(ValueError):O.journal(b,'submission')
    def test_balances_keep_evidence_time_not_poll_time(self):
        h={'base_atoms':40764166,'quote_atoms':40006989,'capture_finished_ms':1000000,'stable_window':True,'blocking_orders':0,'unsupported_accounts':0,'pagination_exhausted':True}
        r={'reviewed_at_ms':'1001000','expires_at_ms':'1900000'}
        d=O.account(h,r,2000000)
        self.assertEqual(d['reviewed_sol'],.040764166);self.assertEqual(d['reviewed_usdc'],40.006989)
        self.assertEqual(d['account_observed_seconds'],1000);self.assertEqual(d['review_current'],0)
        self.assertNotIn('current_balance',d)
    def test_future_or_unstable_account_refused(self):
        h={'base_atoms':0,'quote_atoms':1000000,'capture_finished_ms':1000000,'stable_window':False,'blocking_orders':0,'unsupported_accounts':0,'pagination_exhausted':True}
        with self.assertRaises(ValueError):O.account(h,{'reviewed_at_ms':'1001000','expires_at_ms':'1900000'},2000000)
        h['stable_window']=True
        with self.assertRaises(ValueError):O.account(h,{'reviewed_at_ms':'1001000','expires_at_ms':'1900000'},999999)
    def test_error_drops_balances_instead_of_republishing_stale_as_fresh(self):
        text=O.render(None,2000)
        self.assertIn('velocita_live_observer_up 0',text);self.assertNotIn('reviewed_usdc',text)
        self.assertNotIn('bearer',text)
    def test_fixed_labels_no_arbitrary_secret_output(self):
        with self.assertRaises(ValueError):O.render({'run':'unsafe"','reviewed_usdc':1},2000)

if __name__=='__main__':unittest.main()
