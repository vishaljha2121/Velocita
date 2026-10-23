"""Synthetic orchestration checks; no host services or venue connections."""
import importlib.util
from pathlib import Path
import unittest
import tempfile
import os
import subprocess
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('start_first', HERE/'start_first_run.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


class StartTests(unittest.TestCase):
    def metrics(self):
        return '\n'.join('velocita_v2_'+key+'{host="nuc1",node="1"'+extra+'} '+str(value)
            for key, extra, value in [('up','',1),('ready','',1),('failed','',0),
                ('strategy_mode','',5),('term','',1),('leader_id','',3),
                ('log_index',',kind="commit"',7),('log_index',',kind="applied"',7)])

    def test_halted_health_rejects_mode_index_missing_and_duplicate(self):
        good = self.metrics()
        self.assertEqual(M.health_row(good)['applied'], 7)
        for bad in (good.replace('} 5','} 0'), good.replace('} 7','} 8'),
                    good.replace('velocita_v2_ready','missing'),good+'\n'+good.splitlines()[0]):
            with self.subTest(bad=bad), self.assertRaises(ValueError): M.health_row(bad)

    def test_first_observation_refuses_stale_future_or_any_effect(self):
        good = dict(schema=2,incarnation=M.F.INCARNATION,observed_at_ms=1000,
            applied_index=7,base_atoms=0,quote_atoms=25000000,fees_atoms=0,
            realized_pnl_atoms=0,last_effect=0,pending_effect=0,settlements=[])
        M.check_observation(good,1001)
        for change in ({'observed_at_ms':1002},{'last_effect':1},{'pending_effect':1},
                       {'quote_atoms':24999999},{'fees_atoms':1},{'base_atoms':1},
                       {'applied_index':8},{'incarnation':'f'*32}):
            with self.subTest(change=change),self.assertRaises(ValueError):
                M.check_observation(dict(good,**change),1001)
        with self.assertRaises(ValueError):M.check_observation(good,14001)

    def flow(self, fail=None):
        calls=[]
        def step(name,result=None):
            def f(*args):
                calls.append(name)
                if name==fail:raise RuntimeError('injected '+name)
                return result
            return f
        funcs={'preflight':step('preflight',{}),'begin_attempt':step('begin'),
            'release_guards':step('release'),'restore_guards':step('guard'),
            'check_running':step('running'),'verify_initialized':step('journals',{}),
            'seal':step('seal',{}),'publish_result':step('result')}
        def start(role):return step('start-'+role)()
        def stop():return step('stop')()
        funcs.update(start_service=start,stop_services=stop)
        import contextlib
        with contextlib.ExitStack() as stack:
            for name,value in funcs.items():stack.enter_context(patch.object(M,name,value))
            if fail:
                with self.assertRaises(RuntimeError):M.start_and_seal()
            else:M.start_and_seal()
        return calls

    def test_success_starts_in_order_and_guards_before_seal(self):
        self.assertEqual(self.flow(),['preflight','begin','release','start-signer',
            'start-gateway','guard','running','journals','seal','running','result'])

    def test_preflight_failure_never_releases_guards_or_starts(self):
        self.assertEqual(self.flow('preflight'),['preflight'])

    def test_failure_never_retries_or_seals_and_restores_guards(self):
        for where in ('release','start-signer','start-gateway','guard','journals','seal'):
            with self.subTest(where=where):
                calls=self.flow(where)
                self.assertEqual(calls[-1],'stop')
                self.assertIn('guard',calls)
                self.assertLessEqual(calls.count('start-signer'),1)
                self.assertLessEqual(calls.count('start-gateway'),1)
                if where not in ('seal',):self.assertNotIn('seal',calls)

    def test_stop_failure_still_attempts_both_services_once(self):
        with patch.object(M.subprocess,'run',side_effect=[subprocess.TimeoutExpired('stop',30),None]) as run:
            with self.assertRaises(RuntimeError):M.stop_services()
        self.assertEqual([c.args[0][-1] for c in run.call_args_list],
                         ['velocita-swap-gateway.service','velocita-swap-signer.service'])

    def test_guard_moves_preserve_bytes_and_refuse_overwrite_or_tampering(self):
        base=Path.home()/'.cache/velocita-start-tests';base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp,patch.object(M.U,'ROOT_UID',os.getuid()):
            root=Path(tmp);source=root/'active';saved=root/'saved'
            source.write_bytes(M.U.GUARD)
            M.move_guard(source,saved)
            self.assertFalse(source.exists());self.assertEqual(saved.read_bytes(),M.U.GUARD)
            source.write_bytes(b'preserve foreign guard')
            with self.assertRaises(ValueError):M.move_guard(saved,source)
            self.assertEqual(source.read_bytes(),b'preserve foreign guard')
            with self.assertRaises(ValueError):M.move_guard(source,root/'another')

    def test_voter_disagreement_refuses_before_any_start(self):
        from io import BytesIO
        rows=[self.metrics(),self.metrics().replace('leader_id{host="nuc1",node="1"} 3',
              'leader_id{host="nuc1",node="1"} 2'),self.metrics()]
        with patch.object(M.urllib.request,'urlopen',side_effect=[BytesIO(r.encode()) for r in rows]):
            with self.assertRaisesRegex(ValueError,'disagree'):M.check_health()


if __name__=='__main__':unittest.main()
