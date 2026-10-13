"""Offline controller tests: no sudo, host operations, networking or credentials."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parents[1]/'deploy/v2/swap-runtime'
with patch.object(sys,'path',[str(HERE)]+sys.path):
    spec=importlib.util.spec_from_file_location('authenticated_read_run',HERE/'authenticated_read_run.py')
    PROBE=importlib.util.module_from_spec(spec);spec.loader.exec_module(PROBE)
CONFIG=PROBE.pins('a'*64,'b'*64,'c'*32,'d'*64,'e'*64)


def evidence(role):
    return dict(role=role,uid=PROBE.ROLE_UID[role],caller=22 if role=='gateway' else 23,
        key_id=13 if role=='gateway' else 14,session_epoch=2,term=5,applied_index=6,leader=1,state_version=5,
        catalog_root=CONFIG['catalog_root'],authority_domain=CONFIG['authority_domain'],state_digest='f'*64,
        authenticated_voter_read=True,read_only=True,live_admitted=False)


class AuthenticatedReadRunTests(unittest.TestCase):
    def test_exact_roles_ports_private_keys_and_bounded_voter_only_units(self):
        for role in ('gateway','signer'):
            cmd=PROBE.command(role,'ab'*8,CONFIG)
            for prop in ('RuntimeMaxSec=60','Restart=no','CapabilityBoundingSet=',
                         'NoNewPrivileges=yes','RootDirectory='+str(PROBE.ROOTFS),
                         'User='+str(PROBE.ROLE_UID[role]),'RuntimeDirectoryMode=0700'):
                self.assertIn('--property='+prop,cmd)
            for prop in PROBE.signer_network_properties(PROBE.VOTERS):
                self.assertIn('--property='+prop,cmd)
            self.assertEqual(cmd[cmd.index('--response-port')+1],'44413' if role=='gateway' else '44414')
            self.assertIn('--property=BindReadOnlyPaths=/run/velocita-swap-credentials/'+role+'/caller.key:/etc/velocita-swap/'+role+'/caller.key',cmd)
            self.assertEqual(cmd[cmd.index('--session-file')+1],'/var/lib/velocita-swap-'+role+'/client.session')
            self.assertFalse(any('bearer' in arg or 'signer.pem' in arg or 'arm-live' in arg for arg in cmd))

    def test_native_result_requires_exact_pins_role_read_assertions_and_types(self):
        def done(data): return subprocess.CompletedProcess([],0,json.dumps(data),'')
        self.assertEqual(PROBE.evaluate('signer',CONFIG,done(evidence('signer'))),evidence('signer'))
        for key,value in [('uid',0),('caller',22),('key_id',13),('catalog_root','1'*64),
                          ('authority_domain','1'*64),('state_digest','0'*64),('term',True),
                          ('leader',4),('authenticated_voter_read',False),('read_only',False),('live_admitted',True)]:
            data=evidence('signer');data[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): PROBE.evaluate('signer',CONFIG,done(data))
        out=json.dumps(evidence('signer'))[:-1]+',"uid":29002}'
        with self.assertRaises(ValueError): PROBE.evaluate('signer',CONFIG,subprocess.CompletedProcess([],0,out,''))

    def test_collision_guard_checks_paper_and_both_production_units_and_udp_ports(self):
        seen=[]
        def runner(cmd,**kwargs):
            seen.append(cmd)
            return subprocess.CompletedProcess(cmd,0,'' if cmd[0]=='/usr/bin/ss' else 'inactive\n','')
        PROBE.collision_check(runner)
        self.assertEqual(len(seen),4)
        self.assertTrue(any(PROBE.PAPER_UNIT in cmd for cmd in seen))
        for index in range(4):
            calls=[]
            def bad(cmd,**kwargs):
                calls.append(cmd)
                return subprocess.CompletedProcess(cmd,0,'occupied\n' if len(calls)-1==index else
                    ('' if cmd[0]=='/usr/bin/ss' else 'inactive\n'),'')
            with self.subTest(index=index),self.assertRaises(ValueError): PROBE.collision_check(bad)

    def exercise(self,fault=None):
        scratch=Path.home()/'.cache/velocita-build/authenticated-read-run-tests'
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            output=Path(temp)/'evidence';launched=[];stopped=[]
            def runner(cmd,**kwargs):
                if cmd[0]=='/usr/bin/systemd-run':
                    role=cmd[cmd.index('--role')+1];launched.append(role)
                    self.assertEqual(kwargs['timeout'],75)
                    if fault=='timeout': raise subprocess.TimeoutExpired(cmd,75)
                    data=evidence(role)
                    if fault=='bad-result': data['authenticated_voter_read']=False
                    return subprocess.CompletedProcess(cmd,0,json.dumps(data),'')
                if 'stop' in cmd: stopped.append(cmd);return subprocess.CompletedProcess(cmd,0,'','')
                if cmd[0]=='/usr/bin/ss': return subprocess.CompletedProcess(cmd,0,'','')
                if any('velocita-authread-' in arg for arg in cmd):
                    if fault=='cleanup-timeout': raise subprocess.TimeoutExpired(cmd,8)
                    return subprocess.CompletedProcess(cmd,0,'active' if fault=='timeout' else 'inactive','')
                return subprocess.CompletedProcess(cmd,0,'inactive','')
            with patch.object(PROBE.sys,'platform','linux'),patch.object(PROBE.socket,'gethostname',return_value='bulbazar-NUC'),\
                 patch.object(PROBE.os,'geteuid',return_value=0),patch.object(PROBE,'inputs'):
                result=PROBE.run(output,CONFIG,runner)
            saved=json.loads((output/'result.json').read_text())
            self.assertEqual(result,saved)
            return result,launched,stopped

    def test_sequential_both_reads_required_and_no_live_admission(self):
        result,launched,stopped=self.exercise()
        self.assertEqual(launched,['signer','gateway']);self.assertEqual(stopped,[])
        self.assertTrue(result['authenticated_voter_reads']);self.assertFalse(result['live_admitted'])

    def test_first_failure_stops_batch_without_retry(self):
        result,launched,_=self.exercise('bad-result')
        self.assertEqual(launched,['signer']);self.assertFalse(result['authenticated_voter_reads'])

    def test_timeout_stops_only_run_transient_units_and_preserves_evidence(self):
        result,launched,stopped=self.exercise('timeout')
        self.assertEqual(launched,['signer']);self.assertFalse(result['authenticated_voter_reads'])
        self.assertTrue(stopped)
        self.assertTrue(all('velocita-authread-' in cmd[-1] for cmd in stopped))

    def test_cleanup_failure_cannot_report_complete_qualification(self):
        result,_,_=self.exercise('cleanup-timeout')
        self.assertFalse(result['authenticated_voter_reads'])
        self.assertEqual(len(result['cleanup']),2)

    def test_default_mode_validates_pins_without_run_or_secret_access(self):
        args=['probe','--helper-sha256',CONFIG['helper_sha256'],'--catalog-sha256',CONFIG['catalog_sha256'],
              '--incarnation',CONFIG['incarnation'],'--catalog-root',CONFIG['catalog_root'],
              '--authority-domain',CONFIG['authority_domain'],'--output','/not-created']
        with patch.object(PROBE.sys,'argv',args),patch.object(PROBE,'run') as run,patch('builtins.print'):
            self.assertEqual(PROBE.main(),0);run.assert_not_called()
        result=subprocess.run([sys.executable,'-I','-B',str(HERE/'authenticated_read_run.py')]+args[1:],
                              capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(json.loads(result.stdout)['authenticated_voter_reads'])


if __name__=='__main__': unittest.main()
