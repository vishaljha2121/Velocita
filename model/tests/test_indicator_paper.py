from datetime import datetime,timezone
import json
from pathlib import Path
import tempfile
import unittest
import sqlite3

import hashlib
import time
from unittest import mock

from velocita_model.shadow.indicator_paper import decode_bars, frame, Journal, SPECS, snapshot, ingest, Source, Book, NS
from velocita_model.shadow.indicator_observer import metrics
from velocita_model.shadow.run_indicator_paper import closed_bar_present


def klines(n=122):
    base=int(datetime(2026,10,7,tzinfo=timezone.utc).timestamp())*1000
    return [[base+i*300000,str(100+i),str(101+i),str(99+i),str(100+i),"1",
             base+(i+1)*300000-1,"1",1,"1","1","0"] for i in range(n)]


class IndicatorPaperTests(unittest.TestCase):
    def test_sma_delayed_signal_and_integer_price(self):
        rows=klines(121)
        now=(rows[-1][6]+1)*1000000+2000000000
        bars=decode_bars(rows,now)
        got=frame('sma',bars)
        self.assertIsNotNone(got)
        self.assertEqual(got['flags'],(0,1000000))
        self.assertEqual(got['price_ticks'],22000)
        rows[-1][1:5]=['1','1','1','1']
        other=frame('sma',decode_bars(rows,now))
        self.assertEqual(other['flags'],got['flags'])
        self.assertEqual(other['price_ticks'],100)

    def test_sma_only_after_ten_minute_signal_close(self):
        rows=klines(122)
        self.assertIsNone(frame('sma',decode_bars(rows,(rows[-1][6]+1)*1000000)))

    def test_stochastic_flat_range_is_exit(self):
        rows=klines(20)
        for row in rows: row[1:5]=['100']*4
        got=frame('stochastic',decode_bars(rows,(rows[-1][6]+1)*1000000))
        self.assertEqual(got['flags'],(1000000,0))

    def test_incomplete_bars_excluded_and_gaps_refused(self):
        rows=klines(120)
        now=(rows[-1][0])*1000000+1000000000
        self.assertEqual(len(decode_bars(rows,now).dates),119)
        with self.assertRaisesRegex(ValueError,'gap'):
            decode_bars(rows[:30]+rows[31:],now)
        rows[0][2]='1'
        with self.assertRaisesRegex(ValueError,'geometry'):
            decode_bars(rows,now)

    def test_ledgers_isolate_and_bind_on_restart(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            a=Journal(Path(root)/'a.db','sma','11'*16)
            b=Journal(Path(root)/'b.db','stochastic','22'*16)
            self.assertTrue(a.attempt(100,0))
            a.finish(100)
            self.assertFalse(a.attempt(100,0))
            self.assertTrue(b.attempt(100,0))
            a.close();b.close()
            a=Journal(Path(root)/'a.db','sma','11'*16)
            self.assertFalse(a.attempt(100,0))
            a.close()
            with self.assertRaisesRegex(ValueError,'identity'):
                Journal(Path(root)/'a.db','stochastic','22'*16)

    def test_registered_profiles_are_distinct(self):
        self.assertEqual(SPECS['sma'],(2,10,126,136))
        self.assertEqual(SPECS['stochastic'],(7,5,127,137))

    def test_unsigned_ids_and_nonmonotonic_order_preserved_across_restart(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            path=Path(root)/'a.db'
            j=Journal(path,'stochastic','22'*16)
            orders=[dict(id=i,side=side,price=10000,lots=12,notional=12000000,fee=36000)
                    for i,side in ((2**64-1,1),(1,2),(2**63+1,1))]
            for n in (1,2,3):
                try: ingest(j,dict(orders=orders[:n],last_fill_stamp=100+n),100+n)
                except (ValueError,OverflowError) as error: self.fail(str(error))
                self.assertEqual(j.db.execute('select id from fills order by rowid').fetchall(),
                                 [(str(o['id']),) for o in orders[:n]])
            j.close();j=Journal(path,'stochastic','22'*16)
            ingest(j,dict(orders=orders,last_fill_stamp=103),104)
            self.assertEqual(j.db.execute('select id,side from fills order by rowid').fetchall(),
                [('18446744073709551615',1),('1',2),('9223372036854775809',1)])
            j.close()

    def test_signed_schema_migration_uses_confirmed_order_and_retains_original(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            path=Path(root)/'a.db';j=Journal(path,'sma','11'*16);j.close()
            db=sqlite3.connect(path)
            db.execute('drop table fills')
            db.execute('create table fills(id integer primary key,side integer,price integer,lots integer,notional integer,fee integer,stamp real,first_seen real)')
            db.executemany('insert into fills values (?,?,?,?,?,?,?,?)',
                [(9,1,10000,12,12000000,36000,100.,100.),(2,2,10000,12,12000000,36000,101.,101.)])
            db.execute('insert into observations values (1,101,?)',(json.dumps({'orders':[{'id':9},{'id':2}]}),))
            db.commit();db.close()
            j=Journal(path,'sma','11'*16)
            self.assertEqual(j.db.execute('select id,stamp from fills order by rowid').fetchall(),[('9',100.),('2',101.)])
            self.assertEqual(j.db.execute('select count(*) from fills_signed_v1').fetchone()[0],2)
            j.close()

    def test_confirmed_cash_fee_and_volume_oracle(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            j=Journal(Path(root)/'a.db','sma','11'*16)
            p=Path(root)/'venue.json'
            s=dict(version=1,incarnation='11'*16,account=2,venue=20,instrument=2,funding=25000000,
                buy_bps=30,sell_bps=30,failed=False,stamp=100.,capacity=256,pending_reports=0,
                cash=12964000,lots=12,fees=36000,buy_volume=12000000,sell_volume=0,last_fill_stamp=99.,
                orders=[dict(id=1,side=1,price=10000,lots=12,notional=12000000,fee=36000)])
            p.write_text(json.dumps(s))
            confirmed=snapshot(p,'11'*16,102.)
            ingest(j,confirmed,102.);ingest(j,confirmed,103.)
            self.assertEqual(j.db.execute('select count(*),sum(fee) from fills').fetchone(),(1,36000))
            self.assertEqual(j.db.execute('select stamp from fills').fetchone()[0],99.)
            with self.assertRaisesRegex(ValueError,'identity'):
                snapshot(p,'22'*16,102.)
            with self.assertRaisesRegex(ValueError,'stale'):
                snapshot(p,'11'*16,106.)
            s['orders'][0]['fee']=0;p.write_text(json.dumps(s))
            with self.assertRaisesRegex(ValueError,'economics'): snapshot(p,'11'*16,102.)
            s['orders'][0]['fee']=36000;s['buy_volume']=0;p.write_text(json.dumps(s))
            with self.assertRaisesRegex(ValueError,'committed'): snapshot(p,'11'*16,102.)
            j.close()

    def test_source_staleness_resync_bound_and_restart(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            journal=Journal(Path(root)/'a.db','sma','11'*16)
            source=Source('sma',journal)
            raw=json.dumps(klines(121));now=(klines(121)[-1][6]+1)*1000000+2*NS
            tick=source.tick(raw,receive_ns=now,tick_id=1)
            self.assertEqual(tick.features,(0,1000000,22000))
            source.observe_decision(dict(authorization='stale_risk_state',resync=1),tick)
            again=source.tick(raw,receive_ns=now+2*NS,tick_id=2)
            self.assertIsNotNone(again)
            source.observe_decision(dict(authorization='stale_risk_state',resync=1),again)
            self.assertIsNone(source.tick(raw,receive_ns=now+3*NS,tick_id=3))
            self.assertIsNone(Source('sma',journal).tick(raw,receive_ns=now+4*NS,tick_id=4))
            self.assertIsNone(source.tick(raw,receive_ns=now+30*NS,tick_id=5))
            self.assertEqual(source.last_outcome,'stale')
            journal.close()

    def test_observer_net_equity_period_costs_and_markers(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            root=Path(root)
            c=dict(strategy='sma',incarnation='11'*16,ledger=str(root/'a.db'),
                venue_report=str(root/'venue.json'),stop_ns=1000*NS,
                edge_metrics=str(root/'edge.prom'),gateway_metrics=str(root/'gateway.prom'))
            j=Journal(c['ledger'],'sma',c['incarnation']);j.mark(100,10000)
            s=dict(version=1,incarnation='11'*16,account=2,venue=20,instrument=2,funding=25000000,
                buy_bps=30,sell_bps=30,failed=False,stamp=100.,capacity=256,pending_reports=0,
                cash=12964000,lots=12,fees=36000,buy_volume=12000000,sell_volume=0,last_fill_stamp=99.,
                orders=[dict(id=1,side=1,price=10000,lots=12,notional=12000000,fee=36000)])
            Path(c['venue_report']).write_text(json.dumps(s))
            Path(c['gateway_metrics']).write_text('velocita_gateway_new_risk_stopped 0\n')
            self.assertIn('ledger_fills_behind{strategy="sma"} 1',metrics(c,102.))
            ingest(j,snapshot(c['venue_report'],c['incarnation'],102.),102.);j.close()
            before=hashlib.sha256(Path(c['ledger']).read_bytes()).hexdigest()
            text=metrics(c,102.)
            self.assertEqual(hashlib.sha256(Path(c['ledger']).read_bytes()).hexdigest(),before)
            for expected in ('equity_usd{strategy="sma"} 24.928',
                             'net_pnl_usd{strategy="sma"} -0.072',
                             'period_volume_usd{strategy="sma",days="7"} 12.0',
                             'period_fees_usd{strategy="sma",days="14"} 0.036',
                             'fill_marker_usd{strategy="sma",side="buy"} 100.0',
                             'gateway_new_risk_stopped{strategy="sma"} 0',
                             'ledger_fills_behind{strategy="sma"} 0',
                             'measured_cost_fees_usd_total{strategy="sma"} 0.12',
                             'measured_cost_net_pnl_usd{strategy="sma"} -0.2376',
                             'volume_per_usd_lost{strategy="sma",cost="scenario"} 166.66666666666666',
                             'volume_per_usd_lost{strategy="sma",cost="measured"} 50.505050505050505',
                             'bar_close_to_fill_seconds{strategy="sma",stat="last"} 99.0',
                             'bar_close_to_fill_samples{strategy="sma"} 1',
                             'source_file_age_seconds{strategy="sma",source="gateway"}'):
                self.assertIn('velocita_indicator_'+expected,text)
            self.assertNotIn('strategy="stochastic"',text)
            s['stamp']=140.;Path(c['venue_report']).write_text(json.dumps(s))
            self.assertIn('fill_marker_usd{strategy="sma",side="buy"} NaN',metrics(c,140.))
            s['pending_reports']=1;Path(c['venue_report']).write_text(json.dumps(s))
            text=metrics(c,141.)
            self.assertIn('reconciled{strategy="sma"} 0',text)
            self.assertNotIn('equity_usd',text)
            self.assertIn('gateway_new_risk_stopped{strategy="sma"} 0',text)
            self.assertIn('fills_total{strategy="sma",side="buy"} 1',text)

    def test_observer_refuses_a_ledger_it_cannot_open_or_that_belongs_to_another_run(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            root=Path(root)
            c=dict(strategy='sma',incarnation='11'*16,ledger=str(root/'missing.db'),
                venue_report=str(root/'venue.json'),stop_ns=1000*NS,
                edge_metrics=str(root/'edge.prom'),gateway_metrics=str(root/'gateway.prom'))
            text=metrics(c,102.)
            self.assertIn('ledger_readable{strategy="sma"} 0',text)
            self.assertFalse((root/'missing.db').exists())
            Journal(root/'other.db','sma','22'*16).close()
            self.assertIn('ledger_readable{strategy="sma"} 0',metrics(dict(c,ledger=str(root/'other.db')),102.))

    def test_bad_payloads_are_counted_not_raised(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            journal=Journal(Path(root)/'a.db','sma','11'*16)
            source=Source('sma',journal)
            gapped=klines(121);del gapped[60]
            now=(klines(121)[-1][6]+1)*1000000+2*NS
            for raw in ('not json','{"code":-1}',json.dumps(gapped)):
                self.assertIsNone(source.tick(raw,receive_ns=now,tick_id=1))
                self.assertEqual(source.last_outcome,'invalid')
            self.assertEqual(journal.db.execute('select count(*) from attempts').fetchone()[0],0)
            journal.close()

    def test_a_payload_without_the_bar_that_just_closed_is_bar_missing_and_uses_no_attempt(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            journal=Journal(Path(root)/'a.db','sma','11'*16)
            source=Source('sma',journal)
            late=(klines(121)[-1][6]+1)*1000000+300*NS+2*NS
            self.assertIsNone(source.tick(json.dumps(klines(121)),receive_ns=late,tick_id=1))
            self.assertEqual(source.last_outcome,'bar_missing')
            self.assertEqual(journal.db.execute('select count(*) from attempts').fetchone()[0],0)
            source.tick(json.dumps(klines(122)),receive_ns=late,tick_id=2)
            self.assertNotIn(source.last_outcome,('bar_missing','invalid'))
            journal.close()

    def test_closed_bar_present_checks_for_the_bar_ending_at_the_bucket_start(self):
        rows=klines(121);bucket=(rows[-1][6]+1)//300000
        self.assertTrue(closed_bar_present(json.dumps(rows),bucket))
        self.assertFalse(closed_bar_present(json.dumps(rows),bucket+1))
        self.assertFalse(closed_bar_present(json.dumps(rows[:-1]),bucket))
        self.assertFalse(closed_bar_present('not json',bucket))
        self.assertFalse(closed_bar_present('{"code":-1}',bucket))

    def test_the_edge_ingests_fills_between_bars_and_a_ledger_error_is_unreconciled_not_a_crash(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as root:
            root=Path(root)
            journal=Journal(root/'a.db','sma','11'*16)
            report=root/'venue.json'
            now=time.time()
            s=dict(version=1,incarnation='11'*16,account=2,venue=20,instrument=2,funding=25000000,
                buy_bps=30,sell_bps=30,failed=False,stamp=now,capacity=256,pending_reports=0,
                cash=12964000,lots=12,fees=36000,buy_volume=12000000,sell_volume=0,last_fill_stamp=now-1,
                orders=[dict(id=1,side=1,price=10000,lots=12,notional=12000000,fee=36000)])
            report.write_text(json.dumps(s))
            book=Book(journal,report,10**30)
            source=Source('sma',journal,book)
            self.assertIsNone(source.tick('not json',receive_ns=1,tick_id=1))
            self.assertEqual(journal.db.execute('select count(*) from fills').fetchone()[0],1)
            with mock.patch('velocita_model.shadow.indicator_paper.ingest',side_effect=sqlite3.IntegrityError('x')):
                self.assertEqual(book.gate(1000*NS,10000),'unreconciled')
            self.assertIsNone(book.lots)
            self.assertIsNone(book.gate(1000*NS,10000))
            self.assertEqual(book.lots,12)
            journal.close()


if __name__=='__main__': unittest.main()
