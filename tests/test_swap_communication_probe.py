"""No sudo, services, networking or credentials: controller and evidence oracles."""
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import stat
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / 'deploy/v2/swap-runtime'
spec = importlib.util.spec_from_file_location('service_profile', HERE/'service_profile.py')
PROFILE = importlib.util.module_from_spec(spec); spec.loader.exec_module(PROFILE)
with patch.dict(sys.modules, service_profile=PROFILE):
    spec = importlib.util.spec_from_file_location('communication_probe', HERE/'communication_probe.py')
    PROBE = importlib.util.module_from_spec(spec); spec.loader.exec_module(PROBE)
RUN = 'ab'*8
NONCE = 'cd'*16
ROOTFS = str(PROBE.ROOTFS)
HASH = '2'*64


def evidence(role):
    return dict(role=role, run=RUN, nonce=NONCE, uid=PROBE.ROLE_UID[role], gid=PROBE.ROLE_UID[role],
                aeron_local_transport_passed=True, native_signer_ipc_passed=True,
                authenticated_voter_reads=False, live_admitted=False)


class CommunicationProbeTests(unittest.TestCase):
    def test_exact_roles_bounded_hardening_and_isolated_userspace(self):
        for role in ('gateway', 'signer'):
            cmd = PROBE.command(role, RUN, NONCE, ROOTFS, HASH)
            self.assertEqual(cmd[-4:], [PROBE.HELPER, role, RUN, NONCE])
            self.assertIn('--property=User='+str(PROBE.ROLE_UID[role]), cmd)
            for item in PROFILE.signer_network_properties(PROBE.VOTERS):
                self.assertIn('--property='+item, cmd)
            for item in ('NoNewPrivileges=yes', 'CapabilityBoundingSet=', 'AmbientCapabilities=',
                         'RuntimeMaxSec=45', 'TimeoutStopSec=2', 'Restart=no', 'MountAPIVFS=yes',
                         'CPUAffinity=3', 'CPUQuota=100%',
                         'RootDirectory='+ROOTFS, 'MemoryDenyWriteExecute=yes'):
                self.assertIn('--property='+item, cmd)
            self.assertEqual('--property=SupplementaryGroups=29002' in cmd, role=='gateway')
            self.assertIn('--property=BindReadOnlyPaths=/opt/velocita/qualification/'+HASH+'/communication-probe:'+PROBE.HELPER,cmd)
            if role=='gateway':
                self.assertIn('--property=BindReadOnlyPaths=/run/velocita-comm-'+RUN+'-signer:/run/velocita-comm-'+RUN+'-signer',cmd)
            self.assertFalse(any('--arm-live' in a or 'session.bearer' in a or 'caller.key' in a for a in cmd))
        for role, run, nonce, rootfs in [('voter',RUN,NONCE,ROOTFS), ('signer','../x',NONCE,ROOTFS),
                                       ('signer',RUN,'nothex',ROOTFS), ('signer',RUN,NONCE,'/tmp/rootfs')]:
            with self.assertRaises(ValueError): PROBE.command(role,run,nonce,rootfs,HASH)

    def test_result_requires_both_native_assertions_exact_uid_and_nonlive_scope(self):
        self.assertEqual(PROBE.evaluate('signer',RUN,NONCE,0,json.dumps(evidence('signer'))), evidence('signer'))
        for key, bad in [('uid',29001), ('gid',0), ('nonce','ef'*16), ('run','00'*8),
                         ('aeron_local_transport_passed',False), ('native_signer_ipc_passed',False),
                         ('authenticated_voter_reads',True), ('live_admitted',True), ('uid',True)]:
            data=evidence('signer'); data[key]=bad
            with self.subTest(key=key,bad=bad),self.assertRaises(ValueError):
                PROBE.evaluate('signer',RUN,NONCE,0,json.dumps(data))
        with self.assertRaises(ValueError): PROBE.evaluate('signer',RUN,NONCE,1,json.dumps(evidence('signer')))
        for text in [json.dumps(evidence('signer'))[:-1]+',"uid":29002}', '{}', '[]', 'not json']:
            with self.assertRaises(ValueError): PROBE.evaluate('signer',RUN,NONCE,0,text)

    def exercise(self, fault=None):
        children=[];stops=[]
        class Child:
            def __init__(self,role,data):
                self.role=role;self.data=data;self.returncode=None;self.timed_out=False
            def poll(self): return self.returncode
            def kill(self): self.returncode=-9
            def communicate(self,timeout):
                if fault=='timeout' and self.role=='signer' and not self.timed_out:
                    self.timed_out=True;raise subprocess.TimeoutExpired('synthetic-service',timeout)
                if self.returncode is None:self.returncode=0
                return json.dumps(self.data),''
        def popen(cmd, **kw):
            role=cmd[-3];data=evidence(role)
            if fault==role: data['native_signer_ipc_passed']=False
            child=Child(role,data)
            children.append(child);return child
        def stop(cmd, **kw):
            stops.append(cmd);return subprocess.CompletedProcess(cmd,0,'','')
        with tempfile.TemporaryDirectory(dir=Path.home()/'.cache/velocita-build') as scratch:
            with patch.object(PROBE.sys,'platform','linux'), patch.object(PROBE.socket,'gethostname',return_value='bulbazar-NUC'), \
                 patch.object(PROBE.os,'geteuid',return_value=1002), patch.object(PROBE,'identities'), \
                 patch.object(PROBE,'immutable_file'), patch.object(PROBE.os,'urandom',side_effect=[bytes.fromhex(RUN),bytes.fromhex(NONCE)]), \
                 patch.object(PROBE.Path,'is_dir',return_value=True), \
                 patch.object(PROBE,'image_userspace'):
                report=PROBE.run(Path(scratch)/'result',ROOTFS,'2'*64,popen=popen,runner=stop)
            saved=json.loads((Path(scratch)/'result/result.json').read_text())
            self.assertEqual(saved,report)
            self.assertTrue((Path(scratch)/'result/signer.json').exists())
            self.assertTrue((Path(scratch)/'result/gateway.json').exists())
        self.assertEqual(len(children),2)
        pending=['signer','gateway'] if fault=='timeout' else (['gateway'] if fault=='signer' else [])
        self.assertEqual(stops,([['/usr/bin/sudo','-n','/usr/bin/systemctl','stop']+[
                            'velocita-comm-'+RUN+'-'+role+'.service' for role in pending]] if pending else []))
        self.assertFalse(report['authenticated_voter_reads']);self.assertFalse(report['live_admitted'])
        return report

    def test_both_successful_processes_required(self):
        self.assertTrue(self.exercise()['production_uid_communication_qualified'])
        for fault in ('gateway','signer','timeout'):
            with self.subTest(fault=fault):
                report=self.exercise(fault)
                self.assertFalse(report['production_uid_communication_qualified'])
                self.assertFalse(report['aeron_local_transport_qualified'])

    def test_wrong_host_root_and_identity_refuse_before_execution(self):
        for host,uid in [('other',1002),('bulbazar-NUC',0)]:
            with patch.object(PROBE.sys,'platform','linux'),patch.object(PROBE.socket,'gethostname',return_value=host),patch.object(PROBE.os,'geteuid',return_value=uid),self.assertRaises(ValueError):
                PROBE.run(Path('/unused'),ROOTFS,'2'*64)
        account=Mock(pw_uid=29001,pw_gid=29001,pw_shell='/usr/sbin/nologin',pw_name='velocita-swap-gateway')
        with patch.object(PROBE.pwd,'getpwnam',return_value=account),patch.object(PROBE.os,'getgrouplist',return_value=[29001,29002,27]),self.assertRaises(ValueError):
            PROBE.identities()

    def test_permanent_groups_are_minimal_socket_group_only_in_service(self):
        accounts={role:Mock(pw_uid=uid,pw_gid=uid,pw_shell='/usr/sbin/nologin',pw_name='velocita-swap-'+role)
                  for role,uid in PROBE.ROLE_UID.items()}
        def account(name):return accounts[name.removeprefix('velocita-swap-')]
        with patch.object(PROBE.pwd,'getpwnam',side_effect=account), \
             patch.object(PROBE.os,'getgrouplist',side_effect=lambda name,gid:[gid]), \
             patch.object(PROBE.grp,'getgrnam',return_value=Mock(gr_gid=29002)):
            PROBE.identities()
        with patch.object(PROBE.pwd,'getpwnam',side_effect=account), \
             patch.object(PROBE.os,'getgrouplist',side_effect=lambda name,gid:([29001,29002] if gid==29001 else [29002])), \
             patch.object(PROBE.grp,'getgrnam',return_value=Mock(gr_gid=29002)),self.assertRaises(ValueError):
            PROBE.identities()

    def test_executable_pin_rejects_writable_setid_and_nonexecutable_files(self):
        file=Path('/opt/velocita/qualification')/HASH/'communication-probe'
        good=Mock(st_uid=0,st_mode=stat.S_IFREG|0o555,st_nlink=1,st_size=4)
        directory=Mock(st_uid=0,st_mode=stat.S_IFDIR|0o755)
        for mode in [stat.S_IFREG|0o644, stat.S_IFREG|0o4555, stat.S_IFREG|0o555|0o022, stat.S_IFLNK|0o777]:
            bad=Mock(st_uid=0,st_mode=mode,st_nlink=1,st_size=4)
            with self.subTest(mode=mode),patch.object(PROBE.Path,'lstat',side_effect=[directory]*len(file.parents)+[bad]):
                with self.assertRaises(ValueError): PROBE.immutable_file(file,HASH)
        with patch.object(PROBE.Path,'lstat',side_effect=[directory]*len(file.parents)+[good]), \
             patch.object(PROBE.Path,'read_bytes',return_value=b'test'):
            with self.assertRaises(ValueError): PROBE.immutable_file(file,HASH)
        with patch.object(PROBE.Path,'lstat',side_effect=[directory]*len(file.parents)+[good]), \
             patch.object(PROBE.Path,'read_bytes',return_value=b'test'):
            PROBE.immutable_file(file,hashlib.sha256(b'test').hexdigest())

    def test_rootfs_fixed_image_marker_and_ancestry(self):
        directory=Mock(st_uid=0,st_mode=stat.S_IFDIR|0o755)
        marker=Mock(st_uid=0,st_mode=stat.S_IFREG|0o644,st_nlink=1,st_size=65)
        for contents in [(PROBE.IMAGE+'\n').encode(),b'0'*64+b'\n']:
            with patch.object(PROBE.Path,'lstat',side_effect=[directory]*(len(PROBE.ROOTFS.parents)+2)+[marker]), \
                 patch.object(PROBE.Path,'read_bytes',return_value=contents):
                if contents.startswith(b'0'*64):
                    with self.assertRaises(ValueError):PROBE.image_userspace(ROOTFS)
                else: PROBE.image_userspace(ROOTFS)
        with self.assertRaises(ValueError):PROBE.image_userspace('/opt/velocita/runtimes/'+'0'*64+'/rootfs')


if __name__ == '__main__': unittest.main()
