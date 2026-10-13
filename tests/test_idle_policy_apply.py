"""Expiry renewal failure boundaries: real flock and durable filesystem test doubles."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
N=load('renewal_vectors',ROOT/'tests/test_idle_policy_renewal.py')
A=load('idle_policy_apply',ROOT/'deploy/v2/swap-runtime/idle_policy_apply.py')

class ApplyTests(unittest.TestCase):
    def setUp(self):
        scratch=Path.home()/'.cache/velocita-build/idle-policy-apply-tests';scratch.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=scratch);self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.args=N.RenewalTests().fixture();self.proposal=A.N.propose(*self.args)

    def write_bundle(self,proposal,now):
        base=self.root/'bundle';base.mkdir()
        for name,raw in proposal['artifacts'].items():
            target=base/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        spec=dict(schema=1,evaluated_at_ms=now,helper_sha256='ab'*32,
                  files={n:A.R.digest(raw) for n,raw in proposal['artifacts'].items()})
        raw=json.dumps(spec,sort_keys=True).encode();(base/'renewal.json').write_bytes(raw)
        return base,A.R.digest(raw)

    def test_original_bundle_retains_exact_artifact_contract(self):
        base,pin=self.write_bundle(self.proposal,self.args[-1])
        parsed=A.bundle(base,pin)
        self.assertEqual(parsed[-1]['artifacts'],self.proposal['artifacts'])

    def test_session_refresh_witness_is_pinned_and_revalidated(self):
        args,witness=N.RenewalTests().session_fixture()
        proposal=A.N.propose(*args,session_refresh=witness)
        base,pin=self.write_bundle(proposal,args[-1])
        self.assertEqual(A.bundle(base,pin)[-1]['artifacts'],proposal['artifacts'])
        proof=base/'session-refresh.json';original=proof.read_bytes()
        proof.write_bytes(original+b' ')
        with self.assertRaisesRegex(ValueError,'artifact pin',msg='session_witness_byte_pin'):
            A.bundle(base,pin)
        altered=json.loads(original);altered['token_expires_at_ms']=args[-1]-1
        proof.write_bytes(json.dumps(altered).encode())
        spec=json.loads((base/'renewal.json').read_bytes());spec['files']['session-refresh.json']=A.R.digest(proof.read_bytes())
        raw=json.dumps(spec).encode();(base/'renewal.json').write_bytes(raw)
        with self.assertRaises(ValueError,msg='repinned_expired_session_revalidated'):
            A.bundle(base,A.R.digest(raw))

    def test_exact_state_proof_requires_both_live_role_reads(self):
        proof=self.proposal['report'];rows=[dict(role=r,applied_index=6,state_digest=proof['original_state_digest']) for r in ('signer','gateway')]
        A.exact_state(rows,proof)
        for changed in ([rows[0]], [rows[0],dict(rows[1],state_digest='ff'*32)], [rows[0],dict(rows[1],applied_index=5)]):
            with self.assertRaises(ValueError,msg='changed_or_incomplete_state_refused'):A.exact_state(changed,proof)

    def test_flock_excludes_native_compatible_second_open(self):
        import fcntl
        folder=self.root/'journal';folder.mkdir()
        with A.directory_locks([folder],os.getuid()):
            fd=os.open(folder,os.O_RDONLY)
            try:
                with self.assertRaises(BlockingIOError,msg='native_directory_lock_exclusion'):
                    fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            finally:os.close(fd)
        with A.directory_locks([folder],os.getuid()):pass

    def test_symlink_directory_is_refused(self):
        folder=self.root/'real';folder.mkdir();link=self.root/'link';link.symlink_to(folder)
        with self.assertRaises((OSError,ValueError)): 
            with A.directory_locks([link],os.getuid()):pass

    def test_preflight_unknown_bytes_refuses_before_any_publish(self):
        paths={self.root/'a':(b'old-a',b'new-a'),self.root/'b':(b'old-b',b'new-b')}
        for path,(old,_) in paths.items():path.write_bytes(old)
        (self.root/'b').write_bytes(b'unknown')
        with self.assertRaises(ValueError,msg='unknown_state_prevents_all_writes'):A.publish(paths,self.root/'archive',False)
        self.assertEqual((self.root/'a').read_bytes(),b'old-a')

    def test_partial_apply_recovery_finishes_without_rollback(self):
        paths={self.root/'a':(b'old-a',b'new-a'),self.root/'b':(b'old-b',b'new-b')}
        for path,(old,_) in paths.items():path.write_bytes(old)
        original=A.replace;count=0
        def crash(path,raw):
            nonlocal count
            original(path,raw);count+=1
            if count==1:raise RuntimeError('power loss after first rename')
        with mock.patch.object(A,'replace',crash),self.assertRaises(RuntimeError):A.publish(paths,self.root/'archive',False)
        self.assertEqual((self.root/'a').read_bytes(),b'new-a')
        A.publish(paths,self.root/'archive',True)
        for path,(old,new) in paths.items():
            self.assertEqual(path.read_bytes(),new)
        # Completed replay is idempotent, preserving archive and new bytes.
        A.publish(paths,self.root/'archive',True)

    def test_initial_apply_refuses_mixed_state_and_recovery_requires_archive(self):
        path=self.root/'a';path.write_bytes(b'new')
        with self.assertRaises(ValueError,msg='implicit_resume_refused'):A.publish({path:(b'old',b'new')},self.root/'archive',False)
        with self.assertRaises((ValueError,OSError),msg='recovery_without_archive_refused'):A.publish({path:(b'old',b'new')},self.root/'absent',True)

    def test_mutated_archive_refuses_recovery(self):
        path=self.root/'a';path.write_bytes(b'old');archive=self.root/'archive'
        A.publish({path:(b'old',b'new')},archive,False)
        (archive/'before-0').chmod(0o600);(archive/'before-0').write_bytes(b'tampered')
        with self.assertRaises(ValueError,msg='archive_corruption_refused'):A.publish({path:(b'old',b'new')},archive,True)

    def test_crash_while_archiving_is_explicitly_recoverable(self):
        path=self.root/'a';path.write_bytes(b'old');archive=self.root/'archive'
        original=A.private_new;count=0
        def crash(*args,**kwargs):
            nonlocal count
            original(*args,**kwargs);count+=1
            if count==1:raise RuntimeError('power loss in archive preparation')
        with mock.patch.object(A,'private_new',crash),self.assertRaises(RuntimeError):
            A.publish({path:(b'old',b'new')},archive,False)
        self.assertEqual(path.read_bytes(),b'old','archive_precedes_mutation')
        A.publish({path:(b'old',b'new')},archive,True)
        self.assertEqual(path.read_bytes(),b'new')

    def test_pending_atomic_replacement_is_resumed_without_orphan(self):
        path=self.root/'a';path.write_bytes(b'old')
        original=A.os.replace
        with mock.patch.object(A.os,'replace',side_effect=RuntimeError('power loss before rename')),self.assertRaises(RuntimeError):A.replace(path,b'new')
        self.assertTrue((self.root/'a.idle-renewal.pending').exists())
        A.replace(path,b'new')
        self.assertEqual(path.read_bytes(),b'new')
        self.assertFalse((self.root/'a.idle-renewal.pending').exists())

    def test_every_publication_crash_boundary_recovers_forward(self):
        for crash_at in range(3):
            for after in (False,True):
                with self.subTest(crash_at=crash_at,after=after):
                    folder=self.root/(str(crash_at)+str(after));folder.mkdir()
                    paths={folder/str(i):(b'old'+bytes([i]),b'new'+bytes([i])) for i in range(3)}
                    for path,(old,_) in paths.items():path.write_bytes(old)
                    original=A.replace;count=0
                    def crash(path,raw):
                        nonlocal count
                        current=count;count+=1
                        if current==crash_at and not after:raise RuntimeError('crash before rename')
                        original(path,raw)
                        if current==crash_at and after:raise RuntimeError('crash after rename')
                    with mock.patch.object(A,'replace',crash),self.assertRaises(RuntimeError):A.publish(paths,folder/'archive',False)
                    A.publish(paths,folder/'archive',True)
                    self.assertEqual([p.read_bytes() for p in paths],[pair[1] for pair in paths.values()],'every_crash_recovers_forward')

    def test_effective_systemd_condition_requires_negative_and_semantics(self):
        import json
        row=['ConditionPathExists',False,True,str(A.SENTINEL),0]
        A.condition_verified(json.dumps(dict(type='a(sbbsi)',data=[row])))
        for changed in ([],[row[:1]+[True]+row[2:]],[row[:2]+[False]+row[3:]]):
            with self.assertRaises(ValueError,msg='ineffective_inhibitor_refused'):A.condition_verified(json.dumps(dict(type='a(sbbsi)',data=changed)))

    def test_inhibitor_location_can_be_stated_by_user_manager(self):
        self.assertEqual(A.SENTINEL.parent,Path('/etc'),'publicly_traversable_inhibitor_parent')
        # A private ancestor must be rejected before any file or systemctl change.
        with mock.patch.object(A,'SENTINEL',self.root/'guard'),mock.patch.object(A,'command') as command,self.assertRaises(ValueError,msg='private_inhibitor_refused'):
            A.inhibit('ab'*32)
        command.assert_not_called()

    def test_failed_or_changed_live_read_prevents_publication(self):
        old,_,before,*_=self.args
        for reply in (RuntimeError('native timeout'),[dict(role=r,applied_index=6,state_digest='ff'*32) for r in ('signer','gateway')]):
            call=mock.patch.object(A,'read_live',side_effect=reply) if isinstance(reply,Exception) else mock.patch.object(A,'read_live',return_value=reply)
            with call,mock.patch.object(A,'publish') as publish,self.assertRaises((ValueError,RuntimeError),msg='live_proof_precedes_any_mutation'):
                A.checked_publish(old,before,'ab'*32,self.root/'proof',self.proposal,{},self.root/'archive',False)
            publish.assert_not_called()

    def test_recovery_also_requires_fresh_native_read(self):
        old,_,before,*_=self.args;proof=self.proposal['report']
        rows=[dict(role=r,applied_index=7,state_digest=proof['original_state_digest']) for r in ('signer','gateway')]
        with mock.patch.object(A,'read_live',return_value=rows) as probe,mock.patch.object(A,'publish') as publish:
            A.checked_publish(old,before,'ab'*32,self.root/'proof',self.proposal,{},self.root/'archive',True)
        probe.assert_called_once();self.assertTrue(publish.call_args.args[-1])

    def test_authenticated_proof_cannot_be_supplied_as_saved_json(self):
        # Only read_live executes the fixed helper. No API parameter accepts report bytes.
        import inspect
        self.assertEqual(list(inspect.signature(A.read_live).parameters),['manifest','files','helper_sha256','output'])
        self.assertNotIn('proof',inspect.signature(A.apply).parameters)

if __name__=='__main__':unittest.main()
