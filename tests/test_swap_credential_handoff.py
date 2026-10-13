"""Synthetic credential pinning, private-file and create-only handoff checks."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('handoff',ROOT/'deploy/v2/swap-runtime/credential_handoff.py')
H=importlib.util.module_from_spec(spec);spec.loader.exec_module(H)

class HandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pem=subprocess.run([H.OPENSSL,'ecparam','-name','prime256v1','-genkey','-noout'],capture_output=True,check=True).stdout
        cls.pub=H.public_key(cls.pem)
    def setUp(self):
        self.profile=dict(gateway_uid='29001',signer_uid='29002',bearer_digest=hashlib.sha256(b'synthetic.token').hexdigest(),expires_at_ms='100000',signer_public_key=self.pub)
    def test_correct_credentials(self):
        H.validate_secret('gateway',b'synthetic.token',self.profile,1000)
        H.validate_secret('signer',self.pem,self.profile,1000)
    def test_wrong_bearer_refused(self):
        with self.assertRaisesRegex(ValueError,'pin mismatch',msg='wrong_bearer_refused'):H.validate_secret('gateway',b'other.token',self.profile,1000)
    def test_wrong_signer_refused(self):
        with self.assertRaisesRegex(ValueError,'identity mismatch',msg='wrong_signer_refused'):H.validate_secret('signer',self.pem,dict(self.profile,signer_public_key='02'+'11'*32),1000)
    def test_expired_bearer_refused(self):
        with self.assertRaisesRegex(ValueError,'lifetime',msg='expired_bearer_refused'):H.validate_secret('gateway',b'synthetic.token',self.profile,40000)
    def test_wrong_role_users_refused(self):
        with self.assertRaisesRegex(ValueError,'users',msg='wrong_role_users_refused'):H.validate_secret('gateway',b'synthetic.token',dict(self.profile,gateway_uid='1002'),1000)
    def test_format_and_curve_refused(self):
        for raw in (b'',b'token\n',b'x'*4097):
            with self.subTest(raw_length=len(raw)),self.assertRaises(ValueError):H.validate_secret('gateway',raw,self.profile,1000)
        pem=subprocess.run([H.OPENSSL,'ecparam','-name','secp384r1','-genkey','-noout'],capture_output=True,check=True).stdout
        with self.assertRaisesRegex(ValueError,'P-256'):H.public_key(pem)
        with self.assertRaisesRegex(ValueError,'PEM'):H.public_key(self.pem+b'extra')
    def test_staging_path_exact(self):
        H.source_path(Path('/run/user/1002/velocita-credential-gateway-'+'1'*32),'gateway')
        for path in ('/tmp/secret','/run/user/1002/../1002/velocita-credential-gateway-'+'1'*32,'/run/user/1002/velocita-credential-signer-'+'1'*32):
            with self.subTest(path=path),self.assertRaisesRegex(ValueError,'staging',msg='source_path_confined'):H.source_path(Path(path),'gateway')
    def test_persistent_storage_refused(self):
        with patch.object(H.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'ext4\n','')):
            with self.assertRaisesRegex(ValueError,'tmpfs',msg='persistent_storage_refused'):H.tmpfs('/run')
    def test_private_files_and_create_only(self):
        root=Path.home()/'.cache/velocita-build/credential-tests';root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as d:
            path=Path(d);fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY)
            try:
                H.exclusive(fd,'secret',b'synthetic.token',os.getuid(),os.getgid())
                self.assertEqual(H.private_read(path/'secret',os.getuid()),b'synthetic.token')
                with self.assertRaises(FileExistsError,msg='existing_credential_never_overwritten'):H.exclusive(fd,'secret',b'replacement',os.getuid(),os.getgid())
                self.assertEqual((path/'secret').read_bytes(),b'synthetic.token')
                os.chmod(path/'secret',0o644)
                with self.assertRaisesRegex(ValueError,'metadata',msg='public_secret_refused'):H.private_read(path/'secret',os.getuid())
                os.chmod(path/'secret',0o600);os.link(path/'secret',path/'hardlink')
                with self.assertRaisesRegex(ValueError,'metadata'):H.private_read(path/'secret',os.getuid())
                os.symlink(path/'secret',path/'symlink')
                with self.assertRaises(OSError):H.private_read(path/'symlink',os.getuid())
            finally:os.close(fd)
    def test_rotation_requires_old_pin_and_never_rotates_signer(self):
        root=Path.home()/'.cache/velocita-build/credential-tests';root.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as d:
            fd=os.open(d,os.O_RDONLY|os.O_DIRECTORY);uid=os.getuid()
            try:
                H.exclusive(fd,'session.bearer',b'old.token',uid,os.getgid())
                H.exclusive(fd,'signer.pem',b'old.pem',uid,os.getgid())
                with self.assertRaisesRegex(ValueError,'only gateway',msg='signer_rotation_refused'):
                    H.replace_bearer(fd,'signer','signer.pem',b'new',uid,'a'*64)
                original=os.fstat
                with patch.object(H.os,'fstat',wraps=original) as fstat:
                    def same_gid(n):
                        m=original(n)
                        from types import SimpleNamespace
                        return SimpleNamespace(**{k:getattr(m,k) for k in ('st_mode','st_uid','st_nlink','st_size','st_ino','st_dev','st_mtime_ns','st_ctime_ns')},st_gid=uid)
                    fstat.side_effect=same_gid
                    with patch.object(H.os,'fchown'),self.assertRaisesRegex(ValueError,'pin mismatch',msg='old_bearer_pin_required'):
                        H.replace_bearer(fd,'gateway','session.bearer',b'new.token',uid,'a'*64)
                    self.assertEqual((Path(d)/'session.bearer').read_bytes(),b'old.token')
                    with patch.object(H.os,'fchown'):
                        H.replace_bearer(fd,'gateway','session.bearer',b'new.token',uid,hashlib.sha256(b'old.token').hexdigest())
                    self.assertEqual((Path(d)/'session.bearer').read_bytes(),b'new.token')
            finally:os.close(fd)

    def test_active_services_refuse_before_private_read(self):
        with patch.object(H.PROVISION,'require_host_root'),patch.object(H,'tmpfs'),patch.object(H.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'active\n','')),patch.object(H.LAUNCH,'pinned_bytes',return_value=b'schema=1\n'),patch.object(H,'private_read',side_effect=AssertionError('active_services_refused')) as read:
            with self.assertRaisesRegex(ValueError,'inactive',msg='active_services_refused'):
                H.install('gateway',Path('/run/user/1002/velocita-credential-gateway-'+'1'*32),'1'*64)
            read.assert_not_called()

if __name__=='__main__':unittest.main()
