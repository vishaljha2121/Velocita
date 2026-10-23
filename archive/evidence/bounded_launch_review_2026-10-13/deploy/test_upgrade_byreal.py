import importlib.util
import json
import contextlib
import fcntl
import os
import stat
import types
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('upgrade',HERE/'upgrade_byreal.py')
U=importlib.util.module_from_spec(spec);spec.loader.exec_module(U)
REPO=HERE.parents[3]

class UpgradeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base=Path.home()/'.cache/velocita-build/byreal-upgrade-tests'
        cls.base.mkdir(parents=True,exist_ok=True)
        cls.tmp=tempfile.TemporaryDirectory(dir=cls.base)
        cls.bundle=Path(cls.tmp.name)
        for name in U.PINS:
            if name=='velocita-v2-gateway':continue
            raw=subprocess.check_output(['git','-C',str(REPO),'show',U.COMMIT+':deploy/v2/swap-runtime/'+name])
            (cls.bundle/name).write_bytes(raw)
        source=(cls.bundle/'service_profile.py').read_bytes()
        import types
        cls.profile=types.ModuleType('profile');exec(compile(source,'profile','exec'),cls.profile.__dict__)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def old(self):
        p=self.profile
        m={k:'1'*64 for k in p.FIELDS if k.endswith('_sha256')}
        m.update(schema=1,launch_authorized=True,initialize_swap_recovery=False,source_commit='a'*40,
          incarnation='b'*32,binary='/opt/velocita/releases/'+'a'*40+'/velocita-v2-gateway',runtime_image_sha256=U.IMAGE,
          gateway_uid=29001,gateway_gid=29001,signer_uid=29002,socket_gid=29002,
          gateway_caller=22,signer_caller=23,gateway_key_id=13,signer_key_id=14,
          gateway_response_port=44413,signer_response_port=44414,host_ip='100.72.127.102',
          nodes=['1=100.72.127.102:44301','2=100.84.151.116:44302','3=100.85.36.22:44303'])
        return m
    def test_new_release_binary_and_sources_match_qualified_pins(self):
        status=json.loads((HERE.parent/'native/nuc1/status.json').read_text())
        self.assertTrue(status['qualified'])
        self.assertEqual(U.COMMIT,status['commit'])
        self.assertEqual(U.BINARY,status['binary_sha256']['velocita-v2-gateway'])
        for n,p in U.PINS.items():
            if n!='velocita-v2-gateway':self.assertEqual(U.digest((self.bundle/n).read_bytes()),p)
    def test_stopped_accepts_prior_failure_without_reset(self):
        with patch.object(U,'run',return_value='ActiveState=failed\nMainPID=0\nControlPID=0\nUnitFileState=static\n') as run:
            U.stopped()
            self.assertEqual(run.call_count,2)
            self.assertTrue(all(c.args[0][1]=='show' for c in run.call_args_list))
    def test_refuse_active_or_control_process(self):
        for text in ('ActiveState=active\nMainPID=2\nControlPID=0\nUnitFileState=static\n','ActiveState=inactive\nMainPID=0\nControlPID=3\nUnitFileState=static\n'):
            with patch.object(U,'run',return_value=text),self.assertRaises(ValueError):U.stopped()
    def test_artifact_pin_mismatch_refused_before_python_execution(self):
        with patch.object(U,'read',return_value=b'raise RuntimeError("must not execute")'),self.assertRaisesRegex(ValueError,'pin mismatch'):
            U.payload(self.bundle)
    def test_no_symlink_or_replacement_in_artifact_read(self):
        path=self.bundle/'sample';path.write_bytes(b'example')
        link=self.bundle/'alias';link.symlink_to(path)
        self.assertEqual(U.read(path),b'example')
        with self.assertRaises(OSError):U.read(link)
    def test_unloaded_guard_refused_before_publication(self):
        with patch.object(U,'run',return_value='DropInPaths=\nRefuseManualStart=no\nExecCondition=\n'),self.assertRaisesRegex(ValueError,'not loaded'):
            U.verify_guards()
    def test_guards_cover_manual_and_dependency_start(self):
        self.assertIn(b'RefuseManualStart=yes',U.GUARD)
        self.assertIn(b'ExecCondition=/bin/false',U.GUARD)

class FilesystemApplyTests(unittest.TestCase):
    setUpClass=UpgradeTests.__dict__['setUpClass']
    tearDownClass=UpgradeTests.__dict__['tearDownClass']
    old=UpgradeTests.old
    # Real atomic writes/flock/archive/guard operations in a disposable tree.
    # Host UID/service manager and manifest rendering alone are injected.
    def exercise(self,fail_after=None,lock_busy=False,guard_case="exact",profile_override=None):
        with tempfile.TemporaryDirectory(dir=self.base) as folder,contextlib.ExitStack() as stack:
            root=Path(folder);public=root/'public';public.mkdir()
            systemd=root/'systemd';systemd.mkdir()
            releases=root/'releases';releases.mkdir()
            release=releases/U.COMMIT
            runtime=root/'runtimes';marker=runtime/U.IMAGE/'rootfs/etc/velocita-runtime-image'
            marker.parent.mkdir(parents=True);marker.write_text(U.IMAGE+'\n')
            false=marker.parent.parent/'bin/false';false.parent.mkdir();false.write_bytes(b'fixture');false.chmod(0o755)
            states={r:root/r for r in ('gateway','signer')}
            journal_paths=[]
            for key,leaf in U.JOURNALS.items():
                role,name=key.split('/');d=states[role]/'recovery'/name
                d.mkdir(parents=True,mode=0o700);f=d/leaf;f.write_bytes(b'preserve-'+key.encode());journal_paths.append(f)
            old=self.old();old.update(profile_override or {});oldbinary=root/'old-binary';oldbinary.write_bytes(b'old-ELF')
            old['binary']=str(oldbinary);old['binary_sha256']=U.digest(oldbinary.read_bytes())
            tx=b'fixed-old-expired-transaction\n'
            docs={'transaction.review':tx,'runtime.profile':('transaction_review_digest='+U.digest(tx)+'\n').encode(),
                  'account.review':b'original review','account.history':b'original history','catalog.bin':b'original catalog'}
            for name,key in U.DOC_PINS.items():old[key]=U.digest(docs[name])
            profile=types.SimpleNamespace(validate=lambda m:None,manifest_bytes=self.profile.manifest_bytes)
            profile.render=lambda m:{u+'.review':'[Service]\n# '+U.digest(profile.manifest_bytes(m))+'\n' for u in U.UNITS}
            oldraw=profile.manifest_bytes(old);(public/'manifest.json').write_bytes(oldraw);(public/'manifest.json').chmod(0o640)
            for name,raw in docs.items():(public/name).write_bytes(raw)
            (public/'before-existing-archive').mkdir();(public/'before-existing-archive'/'untouched').write_bytes(b'archive')
            for u in U.UNITS:
                (systemd/u).write_text(profile.render(old)[u+'.review']);(systemd/u).chmod(0o444)
                d=systemd/(u+'.d');d.mkdir()
                (d/U.GUARD_NAME).write_bytes(U.GUARD)
                (d/'90-idle-renewal.conf').write_bytes(b'preserve-independent-renewal-guard')
            if guard_case=='missing':
                for u in U.UNITS:(systemd/(u+'.d')/U.GUARD_NAME).unlink()
            if guard_case=='partial':(systemd/(U.UNITS[1]+'.d')/U.GUARD_NAME).unlink()
            if guard_case=='modified':(systemd/(U.UNITS[1]+'.d')/U.GUARD_NAME).write_bytes(b'[Unit]\nRefuseManualStart=no\n')
            guard_before={p:p.read_bytes() for p in systemd.rglob('*.conf')}
            for name,value in [('PUBLIC',public),('RELEASE',release),('SYSTEMD',systemd),('RUNTIME',runtime),('STATE_ROOTS',states),('ROOT_UID',os.getuid()),('ROLE_UIDS',{'gateway':os.getuid(),'signer':os.getuid()})]:stack.enter_context(patch.object(U,name,value))
            stack.enter_context(patch.object(U,'require_host'))
            stack.enter_context(patch.object(U,'stopped'))
            calls=[]
            def command(args):
                calls.append(args)
                if 'DropInPaths' in args:
                    return 'DropInPaths='+str(systemd/(args[2]+'.d')/U.GUARD_NAME)+'\nRefuseManualStart=yes\nExecCondition={ path=/bin/false ; argv[]=/bin/false ; }\n'
                return ''
            stack.enter_context(patch.object(U,'run',side_effect=command))
            stack.enter_context(patch.object(U,'payload',return_value=({n:b'new-ELF' if n=='velocita-v2-gateway' else b'# immutable source' for n in U.PINS},profile)))
            real_write=U.fresh_write;written=[]
            def writing(path,raw,mode=0o444,gid=None):
                real_write(path,raw,mode,gid)
                if path.parent==release:
                    written.append(path)
                    if len(written)==fail_after:raise RuntimeError('injected write boundary')
            stack.enter_context(patch.object(U,'fresh_write',side_effect=writing))
            archive=root/'archive'
            if guard_case!='exact' or profile_override:
                with self.assertRaises((ValueError,OSError)):U.apply(self.bundle,U.digest(oldraw),archive)
                self.assertFalse(archive.exists());self.assertFalse(release.exists())
                self.assertEqual((public/'manifest.json').read_bytes(),oldraw)
                for u in U.UNITS:self.assertEqual((systemd/u).read_text(),profile.render(old)[u+'.review'])
                self.assertEqual({p:p.read_bytes() for p in systemd.rglob('*.conf')},guard_before)
                self.assertFalse(written)
                return
            if lock_busy:
                held=os.open(journal_paths[0].parent,os.O_RDONLY|os.O_DIRECTORY)
                try:
                    fcntl.flock(held,fcntl.LOCK_EX|fcntl.LOCK_NB)
                    with self.assertRaises(BlockingIOError):U.apply(self.bundle,U.digest(oldraw),archive)
                    self.assertFalse(archive.exists());self.assertFalse(release.exists())
                    self.assertEqual((public/'manifest.json').read_bytes(),oldraw)
                    for u in U.UNITS:self.assertEqual((systemd/(u+'.d')/U.GUARD_NAME).read_bytes(),U.GUARD)
                finally:os.close(held)
                return
            if fail_after is None:U.apply(self.bundle,U.digest(oldraw),archive)
            else:
                with self.assertRaisesRegex(RuntimeError,'injected'):U.apply(self.bundle,U.digest(oldraw),archive)
            for u in U.UNITS:self.assertEqual((systemd/(u+'.d')/U.GUARD_NAME).read_bytes(),U.GUARD)
            for key,leaf in U.JOURNALS.items():
                role,name=key.split('/')
                self.assertEqual((states[role]/'recovery'/name/leaf).read_bytes(),b'preserve-'+key.encode())
            self.assertEqual({p:p.read_bytes() for p in systemd.rglob('*.conf')},guard_before)
            for n,b in docs.items():self.assertEqual((public/n).read_bytes(),b)
            self.assertEqual((public/'before-existing-archive'/'untouched').read_bytes(),b'archive')
            self.assertEqual((archive/'manifest.json').read_bytes(),oldraw)
            self.assertEqual(stat.S_IMODE((public/'manifest.json').stat().st_mode),0o640)
            self.assertEqual((public/'manifest.json').stat().st_gid,os.getgid())
            self.assertEqual((public/'manifest.json').read_bytes(),oldraw)
            for u in U.UNITS:self.assertEqual((systemd/u).read_text(),profile.render(old)[u+'.review'])
            self.assertTrue(all(a[:2]==['systemctl','show'] for a in calls))
            if fail_after is None:
                result=json.loads((archive/'result.json').read_bytes())
                self.assertTrue(result['release_staged']);self.assertFalse(result['manifest_adopted'])
                self.assertEqual((release/'velocita-v2-gateway').read_bytes(),b'new-ELF')
                self.assertEqual(stat.S_IMODE(release.stat().st_mode),0o755)
    def test_reject_unsealed_or_same_release_and_wrong_runtime_without_staging(self):
        for key,value in [('initialize_swap_recovery',True),('recovery_only',True),('source_commit',U.COMMIT),('runtime_image_sha256','f'*64)]:
            with self.subTest(key=key):self.exercise(profile_override={key:value})
    def test_missing_modified_or_partial_guards_refuse_without_publication(self):
        for case in ('missing','modified','partial'):
            with self.subTest(case=case):self.exercise(guard_case=case)
    def test_busy_native_directory_lock_refuses_before_release_or_publication(self):self.exercise(lock_busy=True)
    def test_stage_preserves_archive_dirs_private_manifest_units_and_journals(self):self.exercise()
    def test_every_release_write_boundary_keeps_original_identity_guards_and_journals(self):
        for boundary in (1,2,3,4):
            with self.subTest(boundary=boundary):self.exercise(boundary)

if __name__=='__main__':unittest.main()
