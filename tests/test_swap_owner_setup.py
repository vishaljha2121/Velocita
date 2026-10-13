import importlib.util
import hashlib
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('owner_setup',ROOT/'deploy/v2/swap-runtime/owner_setup.py')
M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
class SetupTests(unittest.TestCase):
    def test_helper_rejects_wrong_content_symlinks_permissions(self):
        base=Path.home()/'.cache/velocita-build';base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            p=Path(tmp)/'helper';data=b'\x7fELFsynthetic';p.write_bytes(data);p.chmod(0o755);digest=hashlib.sha256(data).hexdigest()
            self.assertEqual(M.helper_bytes(p,digest),data)
            with self.assertRaises(ValueError):M.helper_bytes(p,'0'*64)
            for mode in [0o777]:
                p.chmod(mode)
                with self.assertRaises(ValueError):M.helper_bytes(p,digest)
            p.chmod(0o755)
            meta=p.stat()
            with patch.object(M.os,'fstat',return_value=SimpleNamespace(st_mode=meta.st_mode|0o4000,st_nlink=1,st_size=len(data))):
                with self.assertRaises(ValueError):M.helper_bytes(p,digest)
            link=Path(tmp)/'link';link.symlink_to(p)
            with self.assertRaises(OSError):M.helper_bytes(link,digest)
    def test_root_guard_precedes_install(self):
        with patch.object(M.PROVISION,'require_host_root',side_effect=ValueError('no root')),patch.object(M,'helper_bytes') as read:
            with self.assertRaises(ValueError):M.install_helper('/unused','a'*64)
            read.assert_not_called()
    def test_setup_stops_first_failure_without_communication(self):
        base=Path.home()/'.cache/velocita-build'
        with tempfile.TemporaryDirectory(dir=base) as tmp:
            with patch.object(M.sys,'platform','linux'),patch.object(M.COMM.socket,'gethostname',return_value='bulbazar-NUC'),patch.object(M.os,'getuid',return_value=1002),patch.object(M.os,'geteuid',return_value=1002),patch.object(M,'bundle',return_value={}),patch.object(M,'commands',return_value=[['first'],['second']]),patch.object(M.COMM,'run') as communication:
                calls=[]
                def runner(args,**kw):
                    calls.append(args)
                    return type('Result',(),{'returncode':64})()
                report=M.run(Path(tmp),Path(tmp)/'out',runner)
                self.assertEqual(calls,[['first']]);self.assertFalse(report['complete']);communication.assert_not_called()
                self.assertFalse(report['credentials_installed']);self.assertFalse(report['live_admitted'])
    def test_no_service_activation_in_setup_commands(self):
        pins=dict(runtime_source='/fixed',runtime_manifest_sha256='a'*64,helper_sha256='b'*64)
        commands=M.commands(Path('/review'),pins)
        self.assertEqual(len(commands),3)
        self.assertTrue(all(c[:5]==['/usr/bin/sudo','-n','/usr/bin/python3','-I','-B'] for c in commands))
        self.assertFalse(any(word in {'start','enable','--launch','--arm-live-venue'} for c in commands for word in c))
if __name__=='__main__':unittest.main()
