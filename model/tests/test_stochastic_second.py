import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
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
    def test_runner_calls_real_runner_signature(self):
        from velocita_model.shadow import run_stochastic_second as runner
        with tempfile.TemporaryDirectory() as d,patch('time.time_ns',return_value=100*NS):
            j,source,report=self.make_source(d)
            epoch=source.epoch
            j.close()
            program=Path(d)/'program'
            program.write_text(pilot_program('a'*64,epoch['end_ns']))
            config=Path(d)/'config.json'
            config.write_text(json.dumps(dict(mode='paper',strategy='stochastic',
                incarnation='a'*32,catalog_root='a'*64,epoch=epoch,
                program=str(program),ledger=str(Path(d)/'ledger.db'),
                venue_report=str(report),stop_ns=1801*NS,edge_metrics=str(Path(d)/'edge.prom'))))
            with patch('sys.argv',['runner','--config',str(config),'--mode','paper',
                    '--','submitter','--mode','paper']),patch.object(runner,'run_once',autospec=True) as run:
                run.return_value=SimpleNamespace(reason='closed',decisions=0)
                self.assertEqual(runner.main(),2)
                self.assertEqual(run.call_args.kwargs['shadow_recorded_code'],17)

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
            self.assertEqual((s.used(),s.last_outcome),(0,'invalid'))
            self.assertEqual((s.indicators.last,s.indicators.count,s.needs_bootstrap),(99,1201,False))
            s.previous_flags=(0,1000000)
            self.assertIsNone(s.tick(self.raw(100),receive_ns=103*NS,tick_id=2))
            self.assertEqual((s.used(),s.last_outcome),(0,'stale'))
            tick=s.tick(self.raw(101),receive_ns=101*NS,tick_id=3)
            self.assertEqual(tick.features[2],10000)
            j.close()

    def test_late_retry_is_abandoned_and_the_socket_moves_on(self):
        from velocita_model.shadow.run_stochastic_second import PublicSecondSocket
        from velocita_model.adapters.websocket import ReadOnlyMarketSocket
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s);s.previous_flags=(0,1000000)
            socket=PublicSecondSocket(s,2101*NS);socket.previous=98
            feed=[self.raw(99),self.raw(100)]
            with patch.object(ReadOnlyMarketSocket,'recv_text',side_effect=lambda: feed.pop(0)),\
                    patch('time.time_ns',return_value=100*NS):
                first=socket.recv_text()
                tick=s.tick(first,receive_ns=99*NS,tick_id=1)
                s.observe_decision(dict(authorization='stale_risk_state',resync=1),tick)
                self.assertEqual(s.retry_bar,99*NS)
                again=socket.recv_text()
                self.assertEqual(again,first)
                self.assertIsNone(s.tick(again,receive_ns=102*NS,tick_id=2))
                self.assertEqual((s.last_outcome,s.retry_bar),('stale',None))
                self.assertEqual(j.db.execute('select done from attempts where bar=?',(99*NS,)).fetchone()[0],1)
                self.assertEqual(socket.recv_text(),self.raw(100))
            j.close()

    def test_retry_is_presented_once_even_if_the_source_keeps_asking(self):
        from velocita_model.shadow.run_stochastic_second import PublicSecondSocket
        from velocita_model.adapters.websocket import ReadOnlyMarketSocket
        with tempfile.TemporaryDirectory() as d:
            j,s,p=self.make_source(d)
            socket=PublicSecondSocket(s,2101*NS);socket.previous=98
            feed=[self.raw(99),self.raw(100)]
            with patch.object(ReadOnlyMarketSocket,'recv_text',side_effect=lambda: feed.pop(0)),\
                    patch('time.time_ns',return_value=100*NS):
                first=socket.recv_text();s.retry_bar=99*NS
                self.assertEqual(socket.recv_text(),first)
                self.assertEqual(socket.recv_text(),self.raw(100))
            j.close()

    def test_timely_retry_is_forwarded_with_the_original_flags(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s);s.previous_flags=(0,1000000)
            tick=s.tick(self.raw(99),receive_ns=99*NS,tick_id=1)
            s.observe_decision(dict(authorization='stale_risk_state',resync=1),tick)
            again=s.tick(self.raw(99),receive_ns=100*NS,tick_id=2)
            self.assertEqual(again.features,(0,1000000,10000))
            self.assertIsNone(s.retry_bar)
            j.close()

    def test_bad_frame_asks_for_a_fresh_bootstrap_instead_of_warming_up_blind(self):
        from velocita_model.shadow.run_stochastic_second import PublicSecondSocket
        from velocita_model.adapters.websocket import ProtocolError
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d);self.warm(s)
            bad=json.loads(self.raw(99));bad['k']['h']='1'
            self.assertIsNone(s.tick(json.dumps(bad),receive_ns=99*NS,tick_id=1))
            self.assertEqual((s.last_outcome,s.needs_bootstrap),('invalid',True))
            socket=PublicSecondSocket(s,2101*NS)
            with patch('time.time_ns',return_value=100*NS):
                with self.assertRaisesRegex(ProtocolError,'bootstrap again'): socket.recv_text()
            rows=[[t*1000,'100','100','100','100','0',t*1000+999] for t in range(-1200,99)]
            self.assertIsNone(s.tick(json.dumps(dict(bootstrap=rows)),receive_ns=99*NS,tick_id=2))
            self.assertEqual((s.needs_bootstrap,s.indicators.count),(False,1299))
            j.close()

    def test_drawdown_exit_is_sent_while_indicators_warm_up(self):
        with tempfile.TemporaryDirectory() as d,patch('time.time',return_value=100):
            j,s,p=self.make_source(d)
            v=venue();v.update(buy_bps=20,sell_bps=20,cash=952000,lots=12,fees=48000,buy_volume=24000000,
                orders=[dict(id=2**63+1,side=1,price=20000,lots=12,notional=24000000,fee=48000)])
            p.write_text(json.dumps(v))
            self.assertIsNone(s.tick(self.raw(98,'200'),receive_ns=98*NS,tick_id=1))
            self.assertEqual(s.last_outcome,'warming_up')
            tick=s.tick(self.raw(99,'100'),receive_ns=99*NS,tick_id=2)
            self.assertIsNotNone(tick)
            self.assertEqual(tick.features,(1000000,0,10000))
            j.close()

    def test_public_socket_goes_through_the_endpoint_allowlist(self):
        from velocita_model.shadow.run_stochastic_second import PublicSecondSocket
        from velocita_model.adapters.websocket import ALLOWED_ENDPOINTS,ReadOnlyMarketSocket,WebSocketRefused
        socket=PublicSecondSocket(None,0)
        self.assertIn((socket._host,socket._path),ALLOWED_ENDPOINTS)
        with patch.object(ReadOnlyMarketSocket,'__init__',side_effect=WebSocketRefused('refused')) as checked:
            with self.assertRaises(WebSocketRefused): PublicSecondSocket(None,0)
        self.assertEqual(checked.call_args.args,('data-stream.binance.vision','/ws/solusdt@kline_1s'))
        with self.assertRaises(WebSocketRefused):
            ReadOnlyMarketSocket('data-stream.binance.vision','/ws/btcusdt@kline_1s')


if __name__=='__main__': unittest.main()
