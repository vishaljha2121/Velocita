import contextlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('first_upgrade',HERE/'upgrade_first_start.py')
M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)

class UpgradeTests(unittest.TestCase):
    def setUp(self):
        base=Path.home()/'.cache/velocita-build/focused-mvp-20261015/deploy-tests'
        base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=base);self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
    def old(self):
        m=json.loads((HERE.parent/'dormant-manifest.json').read_bytes())
        m.update(launch_authorized=True,initialize_swap_recovery=True)
        for k in ('runtime_profile_sha256','account_review_sha256','account_history_sha256'):m[k]='a'*64
        return m
    def test_manifest_only_three_executable_pins_change(self):
        old=self.old();new=M.next_manifest(old,M.H.LAUNCH.PROFILE)
        self.assertEqual({k for k in old if old[k]!=new[k]},{'source_commit','binary','binary_sha256'})
        self.assertTrue(new['launch_authorized']);self.assertTrue(new['initialize_swap_recovery'])
    def test_wrong_phase_incarnation_runtime_and_same_release_refused(self):
        for k,v in [('launch_authorized',False),('initialize_swap_recovery',False),('recovery_only',True),('incarnation','f'*32),('runtime_image_sha256','f'*64),('source_commit',M.COMMIT)]:
            with self.subTest(k=k),self.assertRaises(ValueError):M.next_manifest(dict(self.old(),**{k:v}),M.H.LAUNCH.PROFILE)
    def test_payload_rejects_pin_mismatch_before_exec(self):
        with patch.object(M.U,'read',return_value=b'raise RuntimeError()'),self.assertRaisesRegex(ValueError,'pin mismatch'):M.U.payload(self.root)
    def test_stopped_rejects_active_pid_and_control_pid(self):
        for state,pid,control in [('active','1','0'),('inactive','1','0'),('failed','0','1')]:
            with patch.object(M.U,'run',return_value=f'ActiveState={state}\nMainPID={pid}\nControlPID={control}\nUnitFileState=static\n'),self.assertRaises(ValueError):M.U.stopped()
    def fixture(self,stack):
        U=M.U;public=self.root/'public';public.mkdir();systemd=self.root/'systemd';systemd.mkdir()
        releases=self.root/'releases';releases.mkdir();runtime=self.root/'runtime'
        marker=runtime/U.IMAGE/'rootfs/etc/velocita-runtime-image';marker.parent.mkdir(parents=True);marker.write_text(U.IMAGE+'\n')
        false=marker.parent.parent/'bin/false';false.parent.mkdir();false.write_bytes(b'false');false.chmod(0o555)
        states={r:self.root/r for r in ('gateway','signer')}
        creds=self.root/'credentials';creds.mkdir()
        for role in states:
            (states[role]/'recovery').mkdir(parents=True,mode=0o700)
            (creds/role).mkdir(mode=0o700);(creds/role/M.H.ROLES[role][1]).write_bytes(b'unchanged synthetic credential');(creds/role/M.H.ROLES[role][1]).chmod(0o600)
        old=self.old();oldbinary=self.root/'oldbinary';oldbinary.write_bytes(b'oldELF');old['binary']=str(oldbinary);old['binary_sha256']=U.digest(b'oldELF')
        docs={'runtime.profile':b'transaction_review_digest='+U.digest(b'tx').encode()+b'\n','account.review':b'review','account.history':b'history','catalog.bin':b'catalog','transaction.review':b'tx'}
        for name,key in U.DOC_PINS.items():old[key]=U.digest(docs[name])
        profile=types.SimpleNamespace(validate=lambda m:None,manifest_bytes=M.H.LAUNCH.PROFILE.manifest_bytes)
        profile.render=lambda m:{u+'.review':U.digest(profile.manifest_bytes(m))+'\n' for u in U.UNITS}
        raw=profile.manifest_bytes(old);(public/'manifest.json').write_bytes(raw)
        for n,b in docs.items():(public/n).write_bytes(b)
        for u in U.UNITS:(systemd/u).write_text(profile.render(old)[u+'.review'])
        data={n:b'newELF' if n=='velocita-v2-gateway' else b'qualified source' for n in U.PINS}
        for n,v in [('PUBLIC',public),('SYSTEMD',systemd),('RELEASE',releases/M.COMMIT),('RUNTIME',runtime),('STATE_ROOTS',states),('ROOT_UID',os.getuid()),('ROLE_UIDS',{r:os.getuid() for r in states})]:stack.enter_context(patch.object(U,n,v))
        stack.enter_context(patch.object(M,'ROLE_GIDS',{r:os.getgid() for r in states}));stack.enter_context(patch.object(M,'CREDENTIALS',creds));stack.enter_context(patch.object(U,'require_host'))
        stack.enter_context(patch.object(M.H,'harden'));stack.enter_context(patch.object(M.H,'tmpfs'))
        stack.enter_context(patch.object(U,'payload',return_value=(data,profile)))
        stack.enter_context(patch.object(M,'validate_review'))
        self.calls=[]
        def run(args):
            self.calls.append(args)
            if 'DropInPaths' in args:return 'DropInPaths='+str(systemd/(args[2]+'.d')/U.GUARD_NAME)+'\nRefuseManualStart=yes\nExecCondition={ path=/bin/false ; }\n'
            if 'ActiveState' in args:return 'ActiveState=failed\nMainPID=0\nControlPID=0\nUnitFileState=static\n'
            return ''
        stack.enter_context(patch.object(U,'run',side_effect=run))
        return old,raw,docs,states,creds,public
    def test_apply_preserves_docs_credentials_empty_roots_and_keeps_guards(self):
        with contextlib.ExitStack() as stack:
            old,raw,docs,states,creds,public=self.fixture(stack)
            result=M.apply(self.root,M.U.digest(raw),self.root/'archive')
            new=json.loads((public/'manifest.json').read_bytes())
            self.assertEqual({k for k in old if old[k]!=new[k]},{'source_commit','binary','binary_sha256'})
            for n,b in docs.items():self.assertEqual((public/n).read_bytes(),b)
            for r in states:
                self.assertEqual(list((states[r]/'recovery').iterdir()),[])
                self.assertEqual((creds/r/M.H.ROLES[r][1]).read_bytes(),b'unchanged synthetic credential')
            self.assertFalse(result['services_started']);self.assertTrue(result['start_guards_retained'])
            self.assertFalse(any(x in ('start','enable','restart','reset-failed') for a in self.calls for x in a))
            self.assertTrue((self.root/'archive/seal/transition.json').is_file())
            for u in M.U.UNITS:self.assertEqual((M.U.SYSTEMD/(u+'.d')/M.U.GUARD_NAME).read_bytes(),M.U.GUARD)
    def test_active_service_refuses_apply_before_release(self):
        with contextlib.ExitStack() as stack:
            _,raw,_,_,_,public=self.fixture(stack)
            stack.enter_context(patch.object(M.U,'run',return_value='ActiveState=active\nMainPID=9\nControlPID=0\nUnitFileState=static\n'))
            with self.assertRaisesRegex(ValueError,'stopped'):M.apply(self.root,M.U.digest(raw),self.root/'archive')
            self.assertFalse(M.U.RELEASE.exists());self.assertEqual((public/'manifest.json').read_bytes(),raw)
    def test_credentials_changing_during_publish_stops_with_guards(self):
        with contextlib.ExitStack() as stack:
            _,raw,_,_,creds,_=self.fixture(stack)
            original=M.U.replace
            def changed(*args):
                original(*args);(creds/'gateway'/M.H.ROLES['gateway'][1]).write_bytes(b'changed synthetic bytes')
            stack.enter_context(patch.object(M.U,'replace',side_effect=changed))
            with self.assertRaisesRegex(ValueError,'credential changed'):M.apply(self.root,M.U.digest(raw),self.root/'archive')
            for u in M.U.UNITS:self.assertEqual((M.U.SYSTEMD/(u+'.d')/M.U.GUARD_NAME).read_bytes(),M.U.GUARD)
    def test_loaded_guard_required_before_release(self):
        with contextlib.ExitStack() as stack:
            _,raw,_,_,_,public=self.fixture(stack)
            stack.enter_context(patch.object(M.U,'verify_guards',side_effect=ValueError('not loaded')))
            with self.assertRaisesRegex(ValueError,'not loaded'):M.apply(self.root,M.U.digest(raw),self.root/'archive')
            self.assertFalse(M.U.RELEASE.exists());self.assertEqual((public/'manifest.json').read_bytes(),raw)
    def test_expired_review_refused_by_actual_launcher_checks(self):
        F=M.load('first_start_account_fixture',M.ROOT/'tests/test_swap_account_adoption.py')
        _,old,docs=F.fixture();old.update(launch_authorized=True,initialize_swap_recovery=True)
        with self.assertRaises(ValueError):M.validate_review(old,docs)
    def test_wrong_old_manifest_pin_is_no_mutation(self):
        with contextlib.ExitStack() as stack:
            _,raw,_,_,_,public=self.fixture(stack)
            with self.assertRaisesRegex(ValueError,'manifest'):M.apply(self.root,'0'*64,self.root/'archive')
            self.assertEqual((public/'manifest.json').read_bytes(),raw);self.assertFalse(M.U.RELEASE.exists())
    def test_nonempty_and_missing_recovery_refused(self):
        for condition in ('nonempty','missing'):
            with self.subTest(condition=condition),contextlib.ExitStack() as stack:
                _,raw,_,states,_,public=self.fixture(stack)
                root=states['signer']/'recovery'
                if condition=='nonempty':(root/'unexpected').write_bytes(b'preserve')
                else:root.rmdir()
                with self.assertRaises((ValueError,OSError)):M.apply(self.root,M.U.digest(raw),self.root/'archive')
                self.assertFalse(M.U.RELEASE.exists());self.assertEqual((public/'manifest.json').read_bytes(),raw)
                for u in M.U.UNITS:self.assertFalse((M.U.SYSTEMD/(u+'.d')/M.U.GUARD_NAME).exists())
            self.tmp.cleanup();self.setUp()
    def test_installer_lock_refuses_parallel_apply(self):
        with contextlib.ExitStack() as stack:
            _,raw,_,_,_,public=self.fixture(stack)
            fd=os.open(public,os.O_RDONLY|os.O_DIRECTORY)
            try:
                fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
                with self.assertRaises(BlockingIOError):M.apply(self.root,M.U.digest(raw),self.root/'archive')
                self.assertFalse(M.U.RELEASE.exists())
            finally:os.close(fd)
    def test_partial_replacement_preserves_failure_and_loaded_guards(self):
        with contextlib.ExitStack() as stack:
            _,raw,docs,states,_,public=self.fixture(stack)
            real=M.U.replace
            def failure(*args):real(*args);raise RuntimeError('injected publication failure')
            stack.enter_context(patch.object(M.U,'replace',side_effect=failure))
            with self.assertRaisesRegex(RuntimeError,'injected'):M.apply(self.root,M.U.digest(raw),self.root/'archive')
            for n,b in docs.items():self.assertEqual((public/n).read_bytes(),b)
            for u in M.U.UNITS:self.assertEqual((M.U.SYSTEMD/(u+'.d')/M.U.GUARD_NAME).read_bytes(),M.U.GUARD)
            self.assertTrue((self.root/'archive/manifest.json').is_file())

if __name__=='__main__':unittest.main()
