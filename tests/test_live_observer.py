"""Independent snapshot/journal examples for read-only live observability."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import unittest
import tempfile
import types
from unittest.mock import patch

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
    def prepare(self,phase):
        b=bytearray(8768);b[:8]=b'VPRP\1\0\0\0';struct.pack_into('<QQQQ',b,8,3,phase,15,1);b[-32:]=hashlib.sha256(b[:-32]).digest();return bytes(b)
    def test_released_prepare_journal_is_observed(self):
        self.assertEqual(O.journal(self.prepare(4),'prepare'),{'ordinal':3,'phase':4,'index':15,'effect':1})
        with self.assertRaises(ValueError):O.journal(self.prepare(5),'prepare')
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

class ReviewWindowTests(unittest.TestCase):
    def history(self, captured=1000000):
        return dict(base_atoms=40764166,quote_atoms=40006989,capture_finished_ms=captured,
                    stable_window=True,blocking_orders=0,unsupported_accounts=0,pagination_exhausted=True)
    def review(self, start=1001000, end=87400000):
        return dict(reviewed_at_ms=str(start),expires_at_ms=str(end))
    def test_day_review_remains_current_after_fifteen_minutes(self):
        try:
            data=O.account(self.history(),self.review(),10000000)
        except ValueError as error:
            self.fail('approved day-long review must remain observable: '+str(error))
        self.assertEqual(data['review_current'],1,'ten million ms is inside both approved review and capture lifetimes')
        self.assertEqual(data['account_observed_seconds'],1000,'old capture is never retimestamped as a fresh wallet read')
    def test_exact_day_review_and_capture_expiry_boundaries(self):
        try:
            before=O.account(self.history(),self.review(1000000,87400000),87399999)
            expired=O.account(self.history(),self.review(1000000,87400000),87400000)
            at_cut=O.account(self.history(),self.review(1001000,87401000),87400000)
            old_cut=O.account(self.history(),self.review(1001000,87401000),87400001)
        except ValueError as error:
            self.fail('valid day-long boundary snapshots must remain observable: '+str(error))
        self.assertEqual(before['review_current'],1)
        self.assertEqual(expired['review_current'],0,'review expiry is exclusive')
        self.assertEqual(at_cut['review_current'],1,'capture age equal to24h matches launcher contract')
        self.assertEqual(old_cut['review_current'],0,'a newer review cannot revive an old capture')
    def test_old_fifteen_minute_review_retains_its_own_cutoff(self):
        review=self.review(1001000,1900000)
        self.assertEqual(O.account(self.history(),review,1899999)['review_current'],1)
        self.assertEqual(O.account(self.history(),review,1900000)['review_current'],0)
    def test_day_review_snapshot_publishes_original_producer_timestamp(self):
        now=10000000;run='a'*32;owner='11'*32;user='synthetic-user'
        history=dict(self.history(),owner_sha256=hashlib.sha256(bytes.fromhex(owner)).hexdigest(),
                     user_id_sha256=hashlib.sha256(user.encode()).hexdigest())
        history_raw=json.dumps(history).encode()
        review_raw=('reviewed_at_ms=1001000\nexpires_at_ms=87400000\nhistory_evidence='+hashlib.sha256(history_raw).hexdigest()+'\n').encode()
        profile_raw=('owner_wallet='+owner+'\nuser_id='+user+'\naccount_review_digest='+hashlib.sha256(review_raw).hexdigest()+
                     '\nmax_buy_output_deduction_lamports=2039280\n').encode()
        docs={'runtime.profile':profile_raw,'account.review':review_raw,'account.history':history_raw}
        manifest=dict(incarnation=run,**{key:hashlib.sha256(docs[leaf]).hexdigest() for leaf,key in
                      [('runtime.profile','runtime_profile_sha256'),('account.review','account_review_sha256'),('account.history','account_history_sha256')]})
        source=dict(schema=2,incarnation=run,observed_at_ms=now-50,applied_index=8,base_atoms=0,quote_atoms=25000000,
                    fees_atoms=0,realized_pnl_atoms=0,last_effect=0,pending_effect=0,settlements=[])
        fixtures=LiveObserverTests()
        def read(path):
            if path==O.PUBLIC/'manifest.json':return json.dumps(manifest).encode()
            if path.parent==O.PUBLIC:return docs[path.name]
            if path==O.SETTLEMENT_SOURCE:return json.dumps(source).encode()
            if path==O.SETTLEMENT_CACHE:raise FileNotFoundError
            return fixtures.prepare(1) if path.name=='prepare.bin' else fixtures.journal()
        with patch.object(O,'read',side_effect=read),patch.object(O,'write_file') as write, \
                patch.object(O.time,'time_ns',return_value=now*1000000), \
                patch.object(O.subprocess,'check_output',return_value='ActiveState=active\nSubState=running\nExecMainStatus=0\nNRestarts=0\n'), \
                patch.object(Path,'stat',return_value=types.SimpleNamespace(st_mtime=10)):
            try:data=O.snapshot(now,True)
            except ValueError as error:self.fail('approved day-long review must reach snapshot publication: '+str(error))
        published=[call for call in write.call_args_list if call.args[0]==O.SETTLEMENT_PUBLISH]
        self.assertEqual(len(published),1,'validated current producer snapshot reaches strategy')
        self.assertEqual(json.loads(published[0].args[1]),source,'observer does not freshen producer timestamp')
        self.assertEqual(data['review_current'],1)
        self.assertEqual(data['account_observed_seconds'],1000)
        self.assertEqual(data['settlement_observed_seconds'],9999.95)

    def test_overlong_or_future_and_malformed_review_times_refused(self):
        cases=[(self.history(),self.review(1000000,87400001),1001000),
               (self.history(),self.review(87400001,87400002),87400001),
               (self.history(1001001),self.review(1001000,1900000),1002000),
               (self.history(),self.review(1001000,1900000),1000999),
               (self.history(True),self.review(),1001000),
               (self.history(),dict(reviewed_at_ms='+1001000',expires_at_ms='1900000'),1002000),
               (self.history(),dict(reviewed_at_ms=True,expires_at_ms='1900000'),1002000)]
        for history,review,now in cases:
            with self.subTest(history=history,review=review),self.assertRaises(ValueError):
                O.account(history,review,now)


class ConfirmedSettlementsTests(unittest.TestCase):
    RUN='a'*32
    def reference(self,**changes):
        return dict(dict(run=self.RUN,effect=1,side='buy',sequence=3,source_ms=999000,decision_ms=999100,price=100),**changes)
    def test_decision_difference_buy_and_sell_oracles_and_missing(self):
        for side,quote,expected in [('buy',1010000,100),('sell',990000,100),('sell',1010000,-100)]:
            event=dict(effect=1,side=side,base_atoms=10000000,quote_atoms=quote,fee_atoms=2000,first_observed_ms=1000100,decision_price=100)
            text=O.render(dict(run=self.RUN,settlement_events=[event]),1001)
            line=next(r for r in text.splitlines() if r.startswith('velocita_live_fill_decision_difference_bps'))
            self.assertAlmostEqual(float(line.rsplit(' ',1)[1]),expected)
            self.assertIn(f'decision_difference_bp="{expected:.2f}"',text)
            del event['decision_price']
            text=O.render(dict(run=self.RUN,settlement_events=[event]),1001)
            self.assertNotIn('velocita_live_fill_decision_difference_bps',text)
            self.assertIn('decision_difference_bp="N/A"',text)
    def test_reference_identity_conflicts_invalid_and_missing(self):
        cases=[([self.reference()],1),([self.reference(run='b'*32)],0),
               ([self.reference(),self.reference(price=101)],0),([self.reference(price=0)],0),
               ([self.reference(price=float('nan'))],0),([self.reference(decision_ms=1000101)],0)]
        for rows,count in cases:
            with self.subTest(rows=rows),patch.object(O,'read',return_value='\n'.join(json.dumps(r) for r in rows).encode()):
                self.assertEqual(len(O.decision_references(self.RUN,1000100)),count)
        with patch.object(O,'read',side_effect=FileNotFoundError):
            self.assertEqual(O.decision_references(self.RUN,1000100),{})
    def test_reference_joins_only_same_effect_side_and_preconfirmation_time(self):
        for changes,matched in [({},True),({'effect':2},False),({'side':'sell'},False),({'decision_ms':1000101},False)]:
            def reader(path,*args):
                if path==O.SETTLEMENT_SOURCE:return json.dumps(self.source()).encode()
                if path==O.SETTLEMENT_CACHE:raise FileNotFoundError
                return json.dumps(self.reference(**changes)).encode()
            with self.subTest(changes=changes),patch.object(O,'read',side_effect=reader):
                data=O.settlement_snapshot(self.RUN,1000100,False)
            self.assertEqual('decision_price' in data['settlement_events'][0],matched)
    def source(self):
        return {'schema':2,'incarnation':self.RUN,'observed_at_ms':1000000,'applied_index':8,
                'base_atoms':9000000,'quote_atoms':24000000,'fees_atoms':2000,'realized_pnl_atoms':0,'last_effect':1,'pending_effect':0,
                'settlements':[{'effect':1,'side':'buy','base_atoms':9000000,'quote_atoms':1000000,
                                'fee_atoms':2000,'outcome':'succeeded'}]}
    def test_hand_calculated_net_buy_and_sell_prices(self):
        s=self.source();s['settlements'].append({'effect':2,'side':'sell','base_atoms':5000000,
                                              'quote_atoms':600000,'fee_atoms':1200,'outcome':'succeeded'})
        parsed=O.settlements(s,self.RUN,1000100)
        state=O.reconcile_settlements(parsed,None,1000100)
        text=O.render({'run':self.RUN,'settlement_events':state['settlements']},1000.1)
        self.assertIn('111.11111111111111',text);self.assertIn(' 120.0\n',text)
        self.assertIn('first_observed_ms="1000100"',text)
        self.assertIn('fee_usdc="0.002"',text)
    def test_marker_timestamp_is_persisted_not_scrape_time(self):
        state=O.reconcile_settlements(self.source(),None,1000100)
        for now in (1000.1,2000.0):
            text=O.render({'run':self.RUN,'settlement_events':state['settlements']},now)
            rows=[r for r in text.splitlines() if r.startswith('velocita_live_fill_first_observed_seconds{')]
            self.assertEqual(len(rows),1)
            self.assertEqual(rows[0].rsplit(' ',1)[1],'1000.1')
    def test_volume_counts_gross_buy_and_sell_once(self):
        source=self.source()
        source['settlements'].append(dict(effect=3,side='sell',base_atoms=5000000,quote_atoms=600000,fee_atoms=1200,outcome='succeeded'))
        with patch.object(O,'read',side_effect=lambda p: json.dumps(source).encode() if p==O.SETTLEMENT_SOURCE else (_ for _ in ()).throw(FileNotFoundError())):
            result=O.settlement_snapshot(self.RUN,1000100,False)
            again=O.settlement_snapshot(self.RUN,1000100,False)
        self.assertEqual(result['executed_volume_usdc'],1.5992)
        self.assertEqual(again['executed_volume_usdc'],1.5992)
    def test_quote_pending_failed_duplicates_and_unknown_fields_rejected(self):
        for field,value in [('outcome','initialized'),('outcome','failed'),('base_atoms',0),
                            ('quote_atoms',-1),('effect',True),('side','deposit')]:
            s=self.source();s['settlements'][0][field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):O.settlements(s,self.RUN,1000100)
        s=self.source();s['settlements']*=2
        with self.assertRaises(ValueError):O.settlements(s,self.RUN,1000100)
        s=self.source();s['settlements'][0]['signature']='private'
        with self.assertRaises(ValueError):O.settlements(s,self.RUN,1000100)
    def test_incarnation_future_and_schema_are_strict(self):
        for field,value in [('incarnation','b'*32),('observed_at_ms',1000200),('schema',True),
                            ('fees_atoms',-1),('realized_pnl_atoms',2**63),('base_atoms',2**64)]:
            s=self.source();s[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):O.settlements(s,self.RUN,1000100)
    def test_restart_keeps_first_observation_and_conflict_fails_closed(self):
        s=self.source();first=O.reconcile_settlements(s,None,1000100)
        restored=json.loads(json.dumps(first))
        second=O.reconcile_settlements(s,restored,1050000)
        self.assertEqual(second['settlements'][0]['first_observed_ms'],1000100)
        s['applied_index']=9;s['settlements'][0]['quote_atoms']=1000001
        with self.assertRaises(ValueError):O.reconcile_settlements(s,restored,1050000)
        s=self.source();s['applied_index']=7
        with self.assertRaises(ValueError):O.reconcile_settlements(s,restored,1050000)
    def test_same_index_cannot_change_balances_or_add_trade(self):
        s=self.source();first=O.reconcile_settlements(s,None,1000100)
        s['quote_atoms']+=1
        with self.assertRaises(ValueError):O.reconcile_settlements(s,first,1000200)
    def test_prior_effect_cannot_disappear_even_at_a_new_index(self):
        s=self.source();first=O.reconcile_settlements(s,None,1000100)
        s['applied_index']=9;s['observed_at_ms']=1000200;s['settlements']=[]
        with self.assertRaisesRegex(ValueError,'settlement disappeared'):
            O.reconcile_settlements(s,first,1000200)
    def window(self,first,last,index,observed):
        s=self.source();s['applied_index']=index;s['observed_at_ms']=observed
        s['settlements']=[{'effect':e,'side':'buy','base_atoms':9000000,'quote_atoms':1000000,'fee_atoms':2000,'outcome':'succeeded'} for e in range(first,last+1)]
        return s
    def test_full_window_may_drop_older_fills_which_the_cache_keeps(self):
        cache=O.reconcile_settlements(self.window(1,16,20,1000000),None,1000000)
        moved=O.reconcile_settlements(self.window(3,18,24,1000100),cache,1000100)
        effects=[r['effect'] for r in moved['settlements']]
        self.assertEqual(effects,list(range(1,19)),'evicted fills retained in effect order')
        self.assertEqual(moved['settlements'][0]['first_observed_ms'],1000000,'evicted fill keeps first observation')
        self.assertEqual(moved['settlements'][-1]['first_observed_ms'],1000100,'new fill observed now')
        again=O.reconcile_settlements(self.window(3,18,24,1000200),json.loads(json.dumps(moved)),1000200)
        self.assertEqual(again['settlements'],moved['settlements'],'retained cache survives restart')
    def test_partial_window_or_newer_gap_still_refused(self):
        cache=O.reconcile_settlements(self.window(1,16,20,1000000),None,1000000)
        with self.assertRaisesRegex(ValueError,'settlement disappeared'):
            O.reconcile_settlements(self.window(3,17,24,1000100),cache,1000100)
        gap=self.window(1,17,24,1000100);del gap['settlements'][5]
        with self.assertRaisesRegex(ValueError,'settlement disappeared'):
            O.reconcile_settlements(gap,cache,1000100)
    def test_optional_missing_invalid_stale_and_fresh_source(self):
        scratch=Path.home()/'.cache/velocita-build/live-observer-tests';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as root:
            source=Path(root)/'source.json';cache=Path(root)/'cache.json';published=Path(root)/'published.json'
            with patch.object(O,'SETTLEMENT_SOURCE',source),patch.object(O,'SETTLEMENT_CACHE',cache),patch.object(O,'SETTLEMENT_PUBLISH',published):
                absent=O.settlement_snapshot(self.RUN,1000100,False)
                self.assertEqual(absent,{'settlement_feed_ready':0,'settlement_feed_status':0})
                source.write_text(json.dumps(self.source()))
                O.settlement_snapshot(self.RUN,1000100,False)
                self.assertFalse(published.exists(),'a read-only pass publishes nothing')
                fresh=O.settlement_snapshot(self.RUN,1000100,True)
                self.assertEqual(fresh['settlement_feed_status'],1)
                self.assertEqual(json.loads(published.read_text()),self.source(),'the validated producer snapshot is published unchanged')
                self.assertEqual(published.stat().st_mode&0o777,0o644,'the strategy driver can read the published snapshot')
                stale=O.settlement_snapshot(self.RUN,1050000,True)
                self.assertEqual(stale['settlement_feed_status'],2)
                self.assertEqual(stale['settlement_observed_seconds'],1000)
                self.assertEqual(stale['settlement_events'][0]['first_observed_ms'],1000100)
                kept=published.read_text()
                source.write_text('{broken')
                bad=O.settlement_snapshot(self.RUN,1050000,True)
                self.assertEqual(published.read_text(),kept,'an invalid producer snapshot is never published')
                self.assertEqual(bad,{'settlement_feed_ready':0,'settlement_feed_status':3})
                self.assertNotIn('settled_sol',bad)
    def test_symlink_source_refused(self):
        scratch=Path.home()/'.cache/velocita-build/live-observer-tests';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as root:
            actual=Path(root)/'actual';actual.write_text(json.dumps(self.source()))
            link=Path(root)/'link';link.symlink_to(actual)
            with patch.object(O,'SETTLEMENT_SOURCE',link):
                self.assertEqual(O.settlement_snapshot(self.RUN,1000100,False)['settlement_feed_status'],3)
    def test_production_validation_clock_is_sampled_after_source_read(self):
        steps=[]
        def read(path):
            steps.append('read')
            if path==O.SETTLEMENT_SOURCE:return json.dumps(self.source()).encode()
            raise FileNotFoundError
        def clock():steps.append('clock');return 1000100*1000000
        with patch.object(O,'read',read),patch.object(O.time,'time_ns',clock):
            d=O.settlement_snapshot(self.RUN,None,False)
        self.assertEqual(steps[:2],['read','clock'])
        self.assertEqual(d['settlement_feed_status'],1)
        self.assertEqual(d['settlement_events'][0]['first_observed_ms'],1000100)

if __name__=='__main__':unittest.main()
