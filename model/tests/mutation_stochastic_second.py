"""Named mutation checks on isolated temporary source copies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
MUTANTS=[
 ('ema_alpha','stochastic_second.py','2/(self.n+1)','1/(self.n+1)','test_ema_hand_oracle'),
 ('confirmation','stochastic_second.py','self.entry>=3','self.entry>=2','test_cross_confirmation_and_reset'),
 ('delay','stochastic_second.py','flags=self.previous_flags','flags=(0,1000000)','test_one_second_lag_duplicate_and_restart'),
 ('future','stochastic_second.py','if age<0:', 'if False:', 'test_future_and_stale_never_forward'),
 ('stale','stochastic_second.py','if age>2*NS:', 'if False:', 'test_future_and_stale_never_forward'),
 ('retry_cleared','stochastic_second.py','        self.retry_bar=None\n        def dropped','        def dropped','test_late_retry_is_abandoned_and_the_socket_moves_on'),
 ('retry_finished','stochastic_second.py','if retrying: self.journal.finish(event_ns)','pass','test_late_retry_is_abandoned_and_the_socket_moves_on'),
 ('present_once','run_stochastic_second.py','raw=self.last_raw;self.last_raw=None;return raw','return self.last_raw','test_retry_is_presented_once_even_if_the_source_keeps_asking'),
 ('bootstrap_request','stochastic_second.py','self.needs_bootstrap=True','self.needs_bootstrap=False','test_bad_frame_asks_for_a_fresh_bootstrap_instead_of_warming_up_blind'),
 ('bootstrap_raise','run_stochastic_second.py','if self.source.needs_bootstrap:','if False:','test_bad_frame_asks_for_a_fresh_bootstrap_instead_of_warming_up_blind'),
 ('warm_stop','stochastic_second.py','if not (protective and self.book.lots):','if True:','test_drawdown_exit_is_sent_while_indicators_warm_up'),
 ('allowlist','run_stochastic_second.py',"super().__init__('data-stream.binance.vision','/ws/solusdt@kline_1s',timeout_s=4,max_message_bytes=16384)","self._host='data-stream.binance.vision';self._path='/ws/solusdt@kline_1s'",'test_public_socket_goes_through_the_endpoint_allowlist'),
 ('quiet_window','stochastic_second.py','<HEARTBEAT_NS','<0','test_stochastic_cash.CashTests.test_idle_bars_stay_local_until_the_heartbeat'),
 ('quiet_never_beats','stochastic_second.py','HEARTBEAT_NS=30*10**9','HEARTBEAT_NS=31*10**9','test_stochastic_cash.CashTests.test_idle_bars_stay_local_until_the_heartbeat'),
 ('quiet_swallows_signal','stochastic_second.py','if idle and not (exit_flag or entry_flag):','if idle:','test_stochastic_cash.CashTests.test_a_signal_is_sent_at_once_inside_the_idle_window'),
 ('quiet_swallows_stop','stochastic_second.py','if idle and not (exit_flag or entry_flag):','if idle and not entry_flag:','test_stochastic_cash.CashTests.test_a_latched_stop_is_sent_at_once_inside_the_idle_window'),
 ('quiet_swallows_retry','stochastic_second.py','        if not retrying:\n            idle=','        if True:\n            idle=','test_stochastic_cash.CashTests.test_a_retry_is_never_treated_as_idle'),
 ('quiet_unpublished','edge.py','if source.last_outcome == "quiet":','if False:','test_shadow_edge.EndToEndWithAFakeSubmitter.test_a_quiet_message_still_refreshes_the_metrics_file'),
 ('cooldown','stochastic_second.py','event_ns/NS-last_exit<60','event_ns/NS-last_exit<0','test_capacity_cooldown_and_immutable_epoch'),
 ('fee_epoch','indicator_paper.py',"order_bps=epoch.get('previous_cost_bps',30) if order_index<len(prior) else cost_bps",'order_bps=cost_bps','test_cost_epoch_preserves_previous_fees'),
]
results=[]
for name,file,before,after,test in MUTANTS:
    with tempfile.TemporaryDirectory(dir=ROOT/'model/tests') as folder:
        folder=Path(folder)
        shutil.copytree(ROOT/'model/src',folder/'src')
        p=folder/'src/velocita_model/shadow'/file
        s=p.read_text()
        if before not in s: raise RuntimeError(name)
        p.write_text(s.replace(before,after,1))
        result=subprocess.run([sys.executable,'-m','unittest',test if '.' in test else f'test_stochastic_second.SecondTests.{test}'],
            cwd=ROOT/'model/tests',env=dict(os.environ,PYTHONPATH=str(folder/'src'),NO_COLOR='1',PYTHONDONTWRITEBYTECODE='1'),
            capture_output=True,text=True)
        killed=result.returncode!=0 and f"FAIL: {test.rsplit('.',1)[-1]}" in result.stderr
        results.append(dict(mutant=name,test=test,killed=killed,output=result.stderr))
print(json.dumps(results,indent=2))
raise SystemExit(0 if all(r['killed'] for r in results) else 1)
