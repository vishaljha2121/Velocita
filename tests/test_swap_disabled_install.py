"""Public bundle validation and fail-before-mutation installation checks."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'deploy/v2/swap-runtime'))
import install_disabled as INSTALL
from test_swap_service_profile import fixture


class DisabledInstallTests(unittest.TestCase):
    def setUp(self):
        scratch=Path.home()/'.cache/velocita-build/disabled-install-tests'
        scratch.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name)
        m=fixture()
        m.update(gateway_uid=29001,gateway_gid=29001,signer_uid=29002,socket_gid=29002,
                 gateway_response_port=44413,signer_response_port=44414,
                 runtime_profile_sha256=None,account_review_sha256=None,account_history_sha256=None)
        self.m=m
        self.data={n:b'public synthetic source' for n in INSTALL.FILES}
        self.data['velocita-v2-gateway']=b'\x7fELFsynthetic-never-executed'
        self.data['catalog.bin']=b'synthetic catalog'
        m['binary_sha256']=hashlib.sha256(self.data['velocita-v2-gateway']).hexdigest()
        m['catalog_sha256']=hashlib.sha256(self.data['catalog.bin']).hexdigest()
        self.render()

    def render(self):
        files=INSTALL.PROFILE.render(self.m)
        self.data['manifest.json']=files['manifest.json'].encode()
        for role in ('gateway','signer'):
            name='velocita-swap-'+role+'.service'
            self.data[name]=files[name+'.review'].encode()

    def bundle(self):
        pins=dict(schema=1,files={n:hashlib.sha256(b).hexdigest() for n,b in self.data.items()})
        for n,b in self.data.items(): (self.base/n).write_bytes(b)
        raw=json.dumps(pins).encode();(self.base/'install-bundle.json').write_bytes(raw)
        return hashlib.sha256(raw).hexdigest()

    def test_accepts_dormant_bundle_with_explicit_unreviewed_account(self):
        m,data=INSTALL.payload(self.base,self.bundle())
        self.assertFalse(m['launch_authorized']);self.assertIsNone(m['account_review_sha256'])
        self.assertEqual(set(data),INSTALL.FILES)
        self.assertTrue(all(p.startswith(('/opt/velocita/releases/','/etc/systemd/system/',INSTALL.PROFILE.PUBLIC+'/'))
                            for p in INSTALL.destinations(m).values()))

    def test_rejects_activation_even_with_complete_account_pins(self):
        self.m.update(launch_authorized=True,runtime_profile_sha256='a'*64,
                      account_review_sha256='b'*64,account_history_sha256='c'*64)
        self.render()
        with self.assertRaisesRegex(ValueError,'dormant'): INSTALL.payload(self.base,self.bundle())

    def test_rejects_rehashed_unit_injection(self):
        self.data['velocita-swap-gateway.service']+=b'ExecStart=/usr/bin/true\n'
        with self.assertRaisesRegex(ValueError,'unit differs'): INSTALL.payload(self.base,self.bundle())

    def test_rejects_mismatched_binary_and_identity(self):
        self.data['velocita-v2-gateway']+=b'changed'
        with self.assertRaisesRegex(ValueError,'binary or catalog'): INSTALL.payload(self.base,self.bundle())
        self.setUpIdentityTest()

    def setUpIdentityTest(self):
        self.m['gateway_uid']=2001;self.render()
        with self.assertRaisesRegex(ValueError,'identities'): INSTALL.payload(self.base,self.bundle())

    def test_refuses_symlink_and_existing_destination(self):
        digest=self.bundle();target=self.base/'catalog.bin'
        target.rename(self.base/'target');target.symlink_to(self.base/'target')
        with self.assertRaises(OSError): INSTALL.payload(self.base,digest)
        with self.assertRaisesRegex(ValueError,'already exists'): INSTALL.fresh(str(target))

    def test_bad_bundle_stops_before_host_mutation(self):
        self.bundle()
        with patch.object(INSTALL.PROVISION,'require_host_root'), \
             patch.object(INSTALL.PROVISION,'snapshot') as snapshot, \
             patch.object(INSTALL,'mkdir') as mkdir, patch.object(INSTALL,'command') as command:
            with self.assertRaisesRegex(ValueError,'digest mismatch'):
                INSTALL.install(self.base,'0'*64)
            snapshot.assert_not_called();mkdir.assert_not_called();command.assert_not_called()

    def test_existing_raft_caller_is_preserved_but_venue_credentials_refuse(self):
        from types import SimpleNamespace as Meta
        import stat
        uid=29001
        directory=Meta(st_mode=stat.S_IFDIR|0o700,st_uid=uid,st_gid=uid)
        caller=Meta(st_mode=stat.S_IFREG|0o600,st_uid=uid,st_gid=uid,st_nlink=1,st_size=32)
        try:
            INSTALL.replacement_credential_metadata(uid,directory,{'caller.key':caller})
        except ValueError as error:
            self.fail('existing_raft_caller_preserved: '+str(error))
        INSTALL.replacement_credential_metadata(uid,directory,{})
        for name in ('signer.pem','session.bearer','unexpected'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError,'caller only',msg='venue_credential_blocks_replacement'):
                INSTALL.replacement_credential_metadata(uid,directory,{name:caller})
        for field,value in [('st_mode',stat.S_IFLNK|0o600),('st_mode',stat.S_IFREG|0o644),
                            ('st_uid',29002),('st_gid',29002),('st_nlink',2),('st_size',31)]:
            bad=Meta(**vars(caller));setattr(bad,field,value)
            with self.subTest(field=field,value=value), self.assertRaisesRegex(ValueError,'caller metadata',msg='unsafe_caller_metadata_refused'):
                INSTALL.replacement_credential_metadata(uid,directory,{'caller.key':bad})
        directory.st_mode=stat.S_IFDIR|0o755
        with self.assertRaisesRegex(ValueError,'directory',msg='unsafe_credential_directory_refused'):
            INSTALL.replacement_credential_metadata(uid,directory,{})

    def test_replacement_changes_release_only_and_preserves_dormant_policy(self):
        new=dict(self.m,source_commit='9'*40,binary='/opt/velocita/releases/'+'9'*40+'/velocita-v2-gateway', binary_sha256='8'*64)
        try:
            INSTALL.replacement(self.m,new)
        except ValueError as error:
            self.fail('new_release_hash_is_allowed: '+str(error))
        for key,value in [('gateway_caller',24),('incarnation','f'*32),('account_review_sha256','a'*64),
                          ('initialize_swap_recovery',True)]:
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'release location only',msg='replacement_preserves_reviewed_policy'):
                INSTALL.replacement(self.m,dict(new,**{key:value}))
        with self.assertRaises(ValueError): INSTALL.replacement(self.m,self.m)
        for key,value in [('binary_sha256','0'*64),('binary','/usr/bin/false')]:
            with self.subTest(key=key),self.assertRaises(ValueError):
                INSTALL.replacement(self.m,dict(new,**{key:value}))
        complete=dict(launch_authorized=True,runtime_profile_sha256='a'*64,
                      account_review_sha256='b'*64,account_history_sha256='c'*64)
        with self.assertRaisesRegex(ValueError,'release location only',msg='active_replacement_refused'):
            INSTALL.replacement(dict(self.m,**complete),dict(new,**complete))


if __name__=='__main__': unittest.main()
