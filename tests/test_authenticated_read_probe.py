"""Compile and exercise the probe's actual argument parser without Aeron or secrets.

The macro removes only the runtime body; it cannot emit a successful read result.
Native authenticated reads remain a separately bounded deployment qualification.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AuthenticatedReadProbeArguments(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = Path.home()/'.cache/velocita-authenticated-read-parser'
        scratch.mkdir(mode=0o700, parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(dir=scratch)
        cls.binary = str(Path(cls.temp.name)/'probe')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-DVELOCITA_AUTH_READ_ARGUMENTS_ONLY',
                        str(ROOT/'tests/v2_authenticated_read_probe.cpp'), '-o', cls.binary],
                       check=True, capture_output=True, text=True)
        # Include the same C++ ancestry implementation; no replacement security logic.
        harness=Path(cls.temp.name)/'filesystem.cpp'
        harness.write_text('#define VELOCITA_AUTH_READ_ARGUMENTS_ONLY\n#define main unused_probe_main\n'
            '#include "'+str(ROOT/'tests/v2_authenticated_read_probe.cpp')+'"\n#undef main\n'
            'int main(int argc,char**argv) { if(argc!=2) return 2; try {'
            'ParentDirectory p(argv[1],::getuid()); p.private_leaf(::getuid()); p.absent_temporary();'
            'return 0;}catch(const std::exception&){return 1;}}\n')
        cls.fs_binary=str(Path(cls.temp.name)/'filesystem')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',
                        str(harness),'-o',cls.fs_binary],check=True,capture_output=True,text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def args(self, role='gateway'):
        return ['--role', role, '--incarnation', 'ab'*16,
                '--catalog-root', 'cd'*32, '--authority-domain', 'ef'*32,
                '--response-port', '44413' if role=='gateway' else '44414',
                '--key-file', '/not-created/caller.key', '--session-file', '/not-created/epoch',
                '--catalog', '/not-created/catalog', '--aeron-dir', '/not-created/aeron']

    def run_probe(self, args):
        return subprocess.run([self.binary]+args, capture_output=True, text=True)

    def test_both_roles_validate_without_files_credentials_or_network(self):
        for role, uid, caller, key in [('gateway', 29001, 22, 13), ('signer', 29002, 23, 14)]:
            out = self.run_probe(self.args(role)+['--check-config'])
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn(f'uid={uid} caller={caller} key_id={key}', out.stdout)
            self.assertIn('authenticated_read=0 live_admitted=0', out.stdout)

    def test_fixture_binary_cannot_run_transport(self):
        out = self.run_probe(self.args()+['--run'])
        self.assertNotEqual(out.returncode, 0)
        self.assertIn('parser-only build', out.stderr)

    def test_requires_explicit_mode_and_all_pins(self):
        self.assertNotEqual(self.run_probe(self.args()).returncode, 0)
        for key in ('--role', '--incarnation', '--catalog-root', '--authority-domain', '--session-file'):
            args = self.args(); at = args.index(key); del args[at:at+2]
            self.assertNotEqual(self.run_probe(args+['--check-config']).returncode, 0, key)

    def test_rejects_wrong_roles_zero_malformed_pins_and_privileged_ports(self):
        for key, bad in [('--role', 'operator'), ('--incarnation', '00'*16),
                         ('--catalog-root', '00'*32), ('--authority-domain', '00'*32),
                         ('--incarnation', 'x'*32), ('--catalog-root', 'A'*64),
                         ('--response-port', '22'), ('--response-port', '65536'),
                         ('--response-port', '44413junk'), ('--response-port', '44414'),
                         ('--response-port', '42413'), ('--key-file', 'relative'),
                         ('--aeron-dir', '/a/../b')]:
            args=self.args();args[args.index(key)+1]=bad
            self.assertNotEqual(self.run_probe(args+['--check-config']).returncode, 0, (key,bad))

    def test_rejects_duplicate_modes_options_and_unsupported_command_flags(self):
        for extra in [['--check-config', '--run'], ['--role','signer','--check-config'],
                      ['--submit','anything','--check-config'], ['--timeout-ms','60000','--check-config']]:
            self.assertNotEqual(self.run_probe(self.args()+extra).returncode, 0)

    def test_rejects_aliasing_state_secret_and_catalog_paths(self):
        args=self.args();args[args.index('--session-file')+1]='/not-created/caller.key'
        self.assertNotEqual(self.run_probe(args+['--check-config']).returncode, 0)

    def test_each_role_requires_its_registered_response_port(self):
        for role, wrong in [('gateway', '44414'), ('signer', '44413')]:
            args=self.args(role); args[args.index('--response-port')+1]=wrong
            self.assertNotEqual(self.run_probe(args+['--check-config']).returncode,0)

    def test_filesystem_refuses_symlink_ancestry_mutable_parents_hardlinks_and_stale_temp(self):
        base=Path(self.temp.name)/'fs-test'; base.mkdir(mode=0o700)
        parent=base/'private'; parent.mkdir(mode=0o700)
        leaf=parent/'epoch'; leaf.write_bytes(b'synthetic-session'); leaf.chmod(0o600)
        def run(path):
            return subprocess.run([self.fs_binary,str(path)],capture_output=True).returncode
        self.assertEqual(run(leaf),0)
        alias=base/'alias';alias.symlink_to(parent,target_is_directory=True)
        self.assertNotEqual(run(alias/'epoch'),0,'symlink parent admitted')
        linked=parent/'linked';linked.symlink_to(leaf)
        self.assertNotEqual(run(linked),0,'symlink leaf admitted')
        hard=parent/'hard';os.link(leaf,hard)
        self.assertNotEqual(run(leaf),0,'hard-linked epoch admitted');hard.unlink()
        parent.chmod(0o770)
        self.assertNotEqual(run(leaf),0,'group-writable parent admitted');parent.chmod(0o700)
        leaf.chmod(0o640)
        self.assertNotEqual(run(leaf),0,'group-readable epoch admitted');leaf.chmod(0o600)
        (parent/'epoch.tmp').write_bytes(b'existing-recovery-file')
        self.assertNotEqual(run(leaf),0,'existing temporary file admitted')


if __name__ == '__main__':
    unittest.main()
