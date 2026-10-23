import copy
import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from velocita_model.shadow.indicator_paper import Journal,ingest,NS
from velocita_model.shadow.stochastic_second import register_epoch
from velocita_model.shadow.paper_completion import publish_completion,open_completion,completed_path
from velocita_model.shadow.indicator_observer import metrics


class CompletionTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=Path(__file__).parent)
        self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name)
        self.c=dict(strategy='stochastic',incarnation='a'*32,ledger=str(root/'ledger.db'),
            venue_report=str(root/'venue.json'),edge_metrics=str(root/'edge.prom'),
            gateway_metrics=str(root/'gateway.prom'),stop_ns=90*NS,
            epoch=dict(variant='stochastic_1s_v1',start_ns=NS,end_ns=100*NS,
                       previous_orders=[],cash=25000000,fees=0,volume=0,cost_bps=20))
        self.j=Journal(self.c['ledger'],'stochastic','a'*32)
        self.addCleanup(self.j.close)
        register_epoch(self.j,self.c['epoch'])
        self.s=dict(version=1,incarnation='a'*32,account=2,venue=20,instrument=2,
            funding=25000000,buy_bps=20,sell_bps=20,failed=False,stamp=100.,
            capacity=256,pending_reports=0,cash=25071760,lots=0,fees=48240,
            buy_volume=12000000,sell_volume=12120000,last_fill_stamp=98.,
            orders=[dict(id=1,side=1,price=10000,lots=12,notional=12000000,fee=24000),
                    dict(id=2,side=2,price=10100,lots=12,notional=12120000,fee=24240)])
        ingest(self.j,self.s,100.)
        self.j.mark(98,10100)
        Path(self.c['venue_report']).write_text(json.dumps(self.s))

    def test_standalone_copy_includes_wal_and_preserves_source(self):
        self.assertTrue(Path(self.c['ledger']+'-wal').exists())
        before=self.j.db.total_changes
        path=publish_completion(self.j.db,self.c,self.s,100.)
        self.assertEqual(self.j.db.total_changes,before)
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        db,s,stamp=open_completion(self.c,10000.)
        self.assertEqual(db.execute('pragma journal_mode').fetchone(),('delete',))
        self.assertEqual(db.execute('pragma integrity_check').fetchone(),('ok',))
        self.assertEqual(s['cash'],25071760)
        self.assertEqual(stamp,100.)
        with self.assertRaises(sqlite3.OperationalError): db.execute('delete from fills')
        db.close()
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),digest)
        self.assertFalse(Path(str(path)+'-wal').exists())
        self.assertFalse(Path(str(path)+'-shm').exists())

    def test_completed_metrics_survive_stale_price_and_missing_live_report(self):
        publish_completion(self.j.db,self.c,self.s,100.)
        Path(self.c['venue_report']).unlink()
        text=metrics(self.c,10000.)
        for expected in ('run_state{strategy="stochastic"} 1',
                         'ledger_readable{strategy="stochastic"} 1',
                         'equity_usd{strategy="stochastic"} 25.07176',
                         'variant_net_pnl_usd{strategy="stochastic"} 0.07176',
                         'variant_volume_usd{strategy="stochastic"} 24.12',
                         'variant_fees_usd{strategy="stochastic"} 0.04824',
                         'variant_order_count{strategy="stochastic"} 2',
                         'price_age_seconds{strategy="stochastic"} 9902',
                         'snapshot_age_seconds{strategy="stochastic"} 9900.0'):
            self.assertIn('velocita_indicator_'+expected,text)
        self.assertIn('fill_marker_usd{strategy="stochastic",side="sell"} NaN',text)

    def test_deadline_alone_is_not_completion(self):
        self.assertIn('run_state{strategy="stochastic"} 0',metrics(self.c,99.))
        self.assertIn('run_state{strategy="stochastic"} 2',metrics(self.c,101.))
        with self.assertRaisesRegex(ValueError,'deadline'):
            publish_completion(self.j.db,self.c,self.s,99.)
        self.assertFalse(completed_path(self.c['ledger']).exists())

    def test_unsettled_nonflat_stale_or_mismatched_account_cannot_finalize(self):
        for key,value in [('pending_reports',1),('failed',True),('stamp',90.)]:
            with self.subTest(key=key),self.assertRaises(ValueError):
                publish_completion(self.j.db,self.c,dict(self.s,**{key:value}),100.)
        long=copy.deepcopy(self.s)
        long.update(orders=long['orders'][:1],cash=12976000,lots=12,fees=24000,sell_volume=0)
        with self.assertRaisesRegex(ValueError,'flat inventory'):
            publish_completion(self.j.db,self.c,long,100.)
        with self.j.db: self.j.db.execute('delete from fills where id="2"')
        with self.assertRaisesRegex(ValueError,'fill mismatch'):
            publish_completion(self.j.db,self.c,self.s,100.)
        self.assertFalse(completed_path(self.c['ledger']).exists())

    def test_foreign_epoch_or_tampered_copy_is_rejected(self):
        path=publish_completion(self.j.db,self.c,self.s,100.)
        changed=copy.deepcopy(self.c);changed['epoch']['cost_bps']=30
        with self.assertRaisesRegex(ValueError,'identity or epoch'): open_completion(changed,101.)
        db=sqlite3.connect(path)
        with db: db.execute('update fills set fee=0 where id="1"')
        db.close()
        self.assertIn('ledger_readable{strategy="stochastic"} 0',metrics(self.c,101.))
        self.assertIn('run_state{strategy="stochastic"} 2',metrics(self.c,101.))

    def test_no_overwrite_and_no_publication_on_validation_failure(self):
        path=publish_completion(self.j.db,self.c,self.s,100.)
        before=path.read_bytes()
        with self.assertRaises(FileExistsError): publish_completion(self.j.db,self.c,self.s,100.)
        self.assertEqual(path.read_bytes(),before)

    def test_changed_or_invalid_live_report_cannot_hide_behind_completion(self):
        publish_completion(self.j.db,self.c,self.s,100.)
        for change in ({'pending_reports':1},{'cash':1}):
            Path(self.c['venue_report']).write_text(json.dumps(dict(self.s,**change)))
            text=metrics(self.c,10000.)
            self.assertIn('run_state{strategy="stochastic"} 2',text)
            self.assertNotIn('equity_usd',text)

    def test_continuation_preserves_history_and_actual_twenty_bp_fees(self):
        from velocita_model.shadow.continue_stochastic import prepare
        from velocita_model.shadow.indicator_paper import snapshot
        publish_completion(self.j.db,self.c,self.s,100.)
        c=prepare(dict(self.c,mode='paper'),Path(self.tmp.name)/'continued.db',100*NS)
        self.assertEqual(c['epoch']['cash'],25071760)
        self.assertEqual(c['epoch']['fees'],48240)
        self.assertEqual(c['epoch']['previous_cost_bps'],20)
        self.assertEqual(snapshot(c['venue_report'],c['incarnation'],100.,c['epoch'])['cash'],25071760)
        next_j=Journal(c['ledger'],'stochastic','a'*32)
        try:
            self.assertEqual(next_j.db.execute('select count(*) from fills').fetchone()[0],2)
            self.assertEqual(next_j.db.execute('select count(*) from stops').fetchone()[0],0)
            self.assertEqual(next_j.db.execute('select count(*) from prior_completion').fetchone()[0],1)
        finally: next_j.close()
        original,_,_=open_completion(self.c,100.)
        original.close()

    def test_flat_account_needs_no_mark_but_open_inventory_still_does(self):
        with self.j.db: self.j.db.execute('delete from marks')
        text=metrics(self.c,100.)
        self.assertIn('equity_usd{strategy="stochastic"} 25.07176',text)
        long=copy.deepcopy(self.s)
        long.update(orders=long['orders'][:1],cash=12976000,lots=12,fees=24000,sell_volume=0)
        Path(self.c['venue_report']).write_text(json.dumps(long))
        self.assertNotIn('equity_usd',metrics(self.c,100.))


if __name__=='__main__': unittest.main()
