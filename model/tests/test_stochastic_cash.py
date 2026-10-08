import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from velocita_model.shadow.indicator_paper import Journal,Book,NS
from velocita_model.shadow.stochastic_second import CASH_VARIANT,NO_DEADLINE,FastSource,pilot_program
from velocita_model.shadow.paper_completion import publish_completion,open_completion
from test_stochastic_second import venue


class CashTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=Path(__file__).parent);self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name)
        self.epoch=dict(variant=CASH_VARIANT,start_ns=NS,end_ns=NO_DEADLINE,
            previous_orders=[],previous_cost_bps=20,cash=25000000,fees=0,volume=0,cost_bps=20)
        self.c=dict(strategy='stochastic',incarnation='a'*32,epoch=self.epoch,
            ledger=str(root/'ledger.db'),venue_report=str(root/'venue.json'),stop_ns=0)
        self.j=Journal(self.c['ledger'],'stochastic','a'*32);self.addCleanup(self.j.close)
        self.s=venue();self.s.update(buy_bps=20,sell_bps=20,stamp=100.)
        Path(self.c['venue_report']).write_text(json.dumps(self.s))
        self.book=Book(self.j,self.c['venue_report'],0,self.epoch)
        self.source=FastSource(self.j,self.book,self.epoch)
        for t in range(-1100,100): self.source.indicators.push((t,100,100,100,100))
        self.source.previous_flags=(0,1000000)

    def raw(self,price):
        return json.dumps(dict(s='SOLUSDT',k=dict(s='SOLUSDT',i='1s',x=True,
            t=99000,T=99999,o=price,h=price,l=price,c=price)))

    def test_no_time_or_2400_frame_stop(self):
        with self.j.db: self.j.db.execute('insert into attempts values (?,2400,1)',(2*NS,))
        with patch('time.time',return_value=100.):
            tick=self.source.tick(self.raw('100'),receive_ns=100*NS,tick_id=1)
        self.assertIsNotNone(tick)
        self.assertEqual(tick.features[1],1000000)
        self.assertIsNone(self.book.stop)
        self.assertIsNone(self.source.terminal_reason)
        text=pilot_program('a'*64,NO_DEADLINE,CASH_VARIANT)
        self.assertIn('policy.id = 129\n',text)
        self.assertIn('policy.feature_schema_id = 139\n',text)

    def test_cash_exhaustion_is_flat_cannot_afford_clip_and_fee(self):
        # $25 cannot buy 0.12 SOL at $300 (gross $36, fee $0.072).
        with patch('time.time',return_value=100.):
            self.assertIsNone(self.source.tick(self.raw('300'),receive_ns=100*NS,tick_id=1))
        self.assertEqual(self.source.terminal_reason,'insufficient_balance')
        publish_completion(self.j.db,self.c,self.s,100.,'insufficient_balance')
        db,s,_=open_completion(self.c,101.);db.close()
        self.assertEqual(s['cash'],25000000)

    def test_cannot_claim_exhaustion_with_enough_cash(self):
        self.j.mark(100,10000)
        with self.assertRaisesRegex(ValueError,'still sufficient'):
            publish_completion(self.j.db,self.c,self.s,100.,'insufficient_balance')

    def test_holding_sol_does_not_stop_on_small_cash_and_can_exit(self):
        # Buy $24 + $0.048, leaving $0.952 cash and 0.12 SOL.
        self.s.update(cash=952000,lots=12,fees=48000,buy_volume=24000000,
            orders=[dict(id=1,side=1,price=20000,lots=12,notional=24000000,fee=48000)])
        Path(self.c['venue_report']).write_text(json.dumps(self.s))
        self.source.previous_flags=(1000000,0)
        with patch('time.time',return_value=100.):
            tick=self.source.tick(self.raw('200'),receive_ns=100*NS,tick_id=1)
        self.assertEqual(tick.features[0],1000000)
        self.assertIsNone(self.source.terminal_reason)

    def test_reconciliation_fault_never_claims_cash_exhaustion(self):
        self.s['pending_reports']=1
        Path(self.c['venue_report']).write_text(json.dumps(self.s))
        with patch('time.time',return_value=100.):
            tick=self.source.tick(self.raw('300'),receive_ns=100*NS,tick_id=1)
        self.assertEqual(tick.features[:2],(0,0))
        self.assertIsNone(self.source.terminal_reason)

    def bar(self,stamp,price='100'):
        return json.dumps(dict(s='SOLUSDT',k=dict(s='SOLUSDT',i='1s',x=True,
            t=(stamp-1)*1000,T=stamp*1000-1,o=price,h=price,l=price,c=price)))

    def send(self,stamp,price='100'):
        self.s['stamp']=float(stamp);Path(self.c['venue_report']).write_text(json.dumps(self.s))
        with patch('time.time',return_value=float(stamp)):
            return self.source.tick(self.bar(stamp,price),receive_ns=stamp*NS,tick_id=stamp)

    def rows(self,table):
        return self.j.db.execute(f'select count(*) from {table}').fetchone()[0]

    def test_idle_bars_stay_local_until_the_heartbeat(self):
        self.source.previous_flags=(0,0)
        with patch.object(self.source.signals,'push',return_value=(0,0)):
            first=self.send(100)
            self.assertEqual(first.features,(0,0,10000))
            for stamp in range(101,130):
                self.assertIsNone(self.send(stamp))
                self.assertEqual(self.source.last_outcome,'quiet')
            self.assertEqual((self.rows('attempts'),self.rows('evidence')),(1,1))
            self.assertEqual((self.rows('marks'),self.rows('equities')),(30,30))
            beat=self.send(130)
            self.assertIsNotNone(beat)
            self.assertEqual(beat.features,(0,0,10000))
            self.assertIsNone(self.send(131))
            self.assertEqual(self.rows('attempts'),2)

    def test_a_signal_is_sent_at_once_inside_the_idle_window(self):
        self.source.previous_flags=(0,0)
        with patch.object(self.source.signals,'push',side_effect=[(0,0),(0,1000000),(0,0)]):
            self.assertIsNotNone(self.send(100))
            self.assertIsNone(self.send(101))
            entry=self.send(102)
        self.assertIsNotNone(entry)
        self.assertEqual(entry.features,(0,1000000,10000))

    def test_a_latched_stop_is_sent_at_once_inside_the_idle_window(self):
        self.s.update(cash=952000,lots=12,fees=48000,buy_volume=24000000,
            orders=[dict(id=2**63+1,side=1,price=20000,lots=12,notional=24000000,fee=48000)])
        self.source.previous_flags=(0,0)
        with patch.object(self.source.signals,'push',return_value=(0,0)):
            self.assertIsNotNone(self.send(100,'200'))
            self.assertIsNone(self.send(101,'200'))
            exit_now=self.send(102,'100')
        self.assertIsNotNone(exit_now)
        self.assertEqual(exit_now.features,(1000000,0,10000))

    def test_a_retry_is_never_treated_as_idle(self):
        self.source.previous_flags=(0,0)
        with patch.object(self.source.signals,'push',side_effect=[(0,0),(0,1000000),(0,0)]):
            self.assertIsNotNone(self.send(100))
            self.assertIsNone(self.send(101))
            tick=self.send(102)
            self.assertIsNotNone(tick)
            self.source.observe_decision(dict(authorization='stale_risk_state',resync=1),tick)
            again=self.send(102)
        self.assertIsNotNone(again)
        self.assertEqual(again.features,(0,1000000,10000))
