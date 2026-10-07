import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from velocita_model.shadow.indicator_paper import Journal,Book,snapshot,ingest,NS
from velocita_model.shadow.stochastic_second import EMA,Indicators,Signals,FastSource,candle,register_epoch,pilot_program
from velocita_model.shadow.indicator_observer import metrics
def venue():
    return dict(version=1,incarnation='a'*32,account=2,instrument=2,venue=20,
        funding=25000000,buy_bps=30,sell_bps=30,stamp=100,failed=False,
        cash=25000000,lots=0,fees=0,buy_volume=0,sell_volume=0,
        committed_through=0,last_fill_stamp=0,pending_reports=0,capacity=256,orders=[])


class SecondTests(unittest.TestCase):
    def test_ema_hand_oracle(self):
        e=EMA(3)
        self.assertIsNone(e.push(1));self.assertIsNone(e.push(2))
        self.assertEqual(e.push(3),2)
        self.assertEqual(e.push(6),4)
        self.assertEqual(e.push(2),3)

    def test_stochastic_hand_oracle(self):
        e=Indicators(window=3,k_smooth=2,d_smooth=2,fast=2,slow=3)
        values=[(1,1,2,0.5,1),(2,1,3,1,2),(3,2,4,2,3),
                (4,3,5,3,4),(5,4,6,4,5)]
        result=[e.push(x) for x in values]
        self.assertEqual(result[:4],[None]*4)
        self.assertAlmostEqual(result[4][0],75)
        self.assertAlmostEqual(result[4][1],2075/28)
        flat=Indicators(2,1,1,1,2)
        flat.push((1,5,5,5,5))
        self.assertEqual(flat.push((2,5,5,5,5))[:2],(50,50))

    def test_cross_confirmation_and_reset(self):
        s=Signals();s.push((59,60,101,100))
        self.assertEqual(s.push((61,60,101,100)),(0,0))
        self.assertEqual(s.push((62,60,101,100)),(0,0))
        self.assertEqual(s.push((63,60,101,100)),(0,1000000))
        s.push((70,60,99,100))
        self.assertEqual(s.push((71,60,101,100)),(0,0))
        self.assertEqual(s.push((39,40,101,100)),(0,0))
        s.push((38,40,101,100))
        self.assertEqual(s.push((37,40,101,100)),(1000000,0))

    def test_boundaries_and_gap(self):
        self.assertEqual(candle(0,999,'1','2','.5','1')[0],1)
        with self.assertRaisesRegex(ValueError,'boundary'): candle(0,1000,1,2,.5,1)
        with self.assertRaisesRegex(ValueError,'geometry'): candle(0,999,1,2,3,1)
        e=Indicators();e.push((1,1,2,.5,1))
        with self.assertRaisesRegex(ValueError,'gap'): e.push((3,1,2,.5,1))

    def test_cost_epoch_preserves_previous_fees(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'venue.json';s=venue();s['buy_bps']=s['sell_bps']=20
            s['orders']=[dict(id=1,side=1,price=10000,lots=12,notional=12000000,fee=36000),
                dict(id=2,side=2,price=10000,lots=12,notional=12000000,fee=36000)]
            epoch={'previous_orders':json.loads(json.dumps(s['orders']))}
            s.update(cash=24928000,lots=0,fees=72000,buy_volume=12000000,sell_volume=12000000)
            p.write_text(json.dumps(s))
            try: restored=snapshot(p,'a'*32,100,epoch)
            except ValueError as error: self.fail(f'valid historical 30 bp prefix refused: {error}')
            self.assertEqual(restored['cash'],24928000)
            s['orders'].append(dict(id=2**63+5,side=1,price=10000,lots=12,notional=12000000,fee=24000))
            s.update(cash=12904000,lots=12,fees=96000,buy_volume=24000000)
            p.write_text(json.dumps(s))
            self.assertEqual(snapshot(p,'a'*32,100,epoch)['fees'],96000)
            s['orders'][-1]['fee']=36000;p.write_text(json.dumps(s))
            with self.assertRaisesRegex(ValueError,'economics'): snapshot(p,'a'*32,100,epoch)
            s['orders'][0]['price']=9000;p.write_text(json.dumps(s))
            with self.assertRaisesRegex(ValueError,'prefix'): snapshot(p,'a'*32,100,epoch)

    def test_program_new_identity(self):
        text=pilot_program('a'*64,999)
        self.assertIn('policy.id = 128\n',text)
        self.assertIn('policy.feature_schema_id = 138\n',text)
        self.assertIn('order.max_market_data_age_ns = 2000000000\n',text)

    def make_source(self,d):
        j=Journal(Path(d)/'ledger.db','stochastic','a'*32)
        epoch=dict(variant='stochastic_1s_v1',start_ns=NS,end_ns=2101*NS,
            previous_orders=[],cash=25000000,fees=0,volume=0,cost_bps=20)
        p=Path(d)/'venue.json';s=venue();s.update(buy_bps=20,sell_bps=20)
        p.write_text(json.dumps(s))
        b=Book(j,p,1801*NS,epoch)
        return j,FastSource(j,b,epoch),p

    def raw(self,stamp,price='100'):
        return json.dumps(dict(s='SOLUSDT',k=dict(s='SOLUSDT',i='1s',x=True,
            t=(stamp-1)*1000,T=stamp*1000-1,o=price,h=price,l=price,c=price)))

    def warm(self,source):
        for t in range(-1101,99): source.indicators.push((t,100,100,100,100))

    def test_one_second_lag_duplicate_and_restart(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s)
            s.previous_flags=(0,0)
            with patch.object(s.signals,'push',return_value=(0,1000000)):
                tick=s.tick(self.raw(99),receive_ns=99*NS,tick_id=1)
                self.assertEqual(tick.features,(0,0,10000))
                self.assertIsNone(s.tick(self.raw(99),receive_ns=99*NS,tick_id=2))
                tick=s.tick(self.raw(100),receive_ns=100*NS,tick_id=3)
                self.assertEqual(tick.features,(0,1000000,10000))
                self.assertEqual(s.used(),2)
            s2=FastSource(j,s.book,s.epoch);self.warm(s2);s2.previous_flags=(0,1000000)
            self.assertIsNone(s2.tick(self.raw(99),receive_ns=100*NS,tick_id=4))
            self.assertEqual(s2.used(),2)
            j.close()

    def test_capacity_cooldown_and_immutable_epoch(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s);s.previous_flags=(0,1000000)
            with j.db:
                j.db.execute('insert into fills values (?,?,?,?,?,?,?,?)',('12',2,10000,12,12000000,24000,90.,90.))
            with patch.object(s.book,'refresh',return_value={}),patch.object(s.book,'gate',return_value=None):
                s.book.lots=0
                tick=s.tick(self.raw(99),receive_ns=99*NS,tick_id=1)
                self.assertEqual(tick.features[1],0)
            with j.db: j.db.execute('update attempts set count=2400')
            self.assertIsNone(s.tick(self.raw(100),receive_ns=100*NS,tick_id=2))
            self.assertEqual(s.last_outcome,'capacity')
            with self.assertRaisesRegex(ValueError,'immutable'):
                register_epoch(j,dict(s.epoch,cost_bps=30))
            j.close()

    def test_future_and_stale_never_forward(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s);s.previous_flags=(0,1000000)
            self.assertIsNone(s.tick(self.raw(99),receive_ns=98*NS,tick_id=1))
            self.assertEqual(s.used(),0)
            self.warm(s);s.previous_flags=(0,1000000)
            self.assertIsNone(s.tick(self.raw(99),receive_ns=102*NS,tick_id=2))
            self.assertEqual(s.used(),0)
            j.close()

    def test_observer_is_read_only_and_variant_baseline(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);j.mark(100,10000)
            c=dict(strategy='stochastic',incarnation='a'*32,ledger=str(Path(d)/'ledger.db'),
                venue_report=str(p),stop_ns=1801*NS,epoch=s.epoch,
                edge_metrics=str(Path(d)/'edge.prom'),gateway_metrics=str(Path(d)/'gateway.prom'))
            before=j.db.total_changes
            text=metrics(c,100)
            self.assertEqual(j.db.total_changes,before)
            self.assertIn('velocita_indicator_cost_bps{strategy="stochastic"} 20',text)
            self.assertIn('velocita_indicator_variant_net_pnl_usd{strategy="stochastic"} 0.0',text)
            self.assertNotIn('bar_close_to_fill_seconds',text)
            j.close()


if __name__=='__main__': unittest.main()
