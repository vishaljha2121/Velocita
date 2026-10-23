"""Voter cutover retires into an archive, rewrites only the incarnation, startup and image, and refuses a running voter."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('voters', Path(__file__).with_name('voters.py'))
V = importlib.util.module_from_spec(spec)
spec.loader.exec_module(V)
NEW = 'c' * 32
IMAGE = 'localhost/velocita-v2:qualified-54a5fcb'
CONF = f'# placeholder {V.OLD}\nnode_id = 2\nstartup = recover\ncluster.incarnation = {V.OLD}\ncatalog.root = abc\n'
UNIT = f'ExecStart=/usr/bin/podman run --rm {V.OLD_IMAGE} --config /etc/velocita/node-2.conf\n'
METRICS = 'velocita_v2_up{host="pi"} 1\nvelocita_v2_ready{host="pi"} 1\nvelocita_v2_failed{host="pi"} 0\n'


class Voters(unittest.TestCase):
    def setUp(self):
        scratch = Path.home()/'.cache/velocita-build/voters-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.home = Path(self.tmp.name)
        live = self.home/'velocita-indicator-paper/stochastic'
        (live/'config').mkdir(parents=True)
        (live/'data/node-2').mkdir(parents=True)
        (live/'data/node-2/log').write_text('old wal')
        (live/'config/node-2.conf').write_text(CONF)
        (live/'config/catalog.bin').write_bytes(b'catalog')
        units = self.home/'.config/systemd/user'
        units.mkdir(parents=True)
        for u in V.UNITS:
            (units/(u + '.service')).write_text(UNIT)
        self.state, self.image, self.calls = 'inactive', 0, []

    def tearDown(self):
        self.tmp.cleanup()

    def run_(self, argv, **kwargs):
        self.calls.append(argv)
        if argv[:2] == ['podman', 'image']:
            return subprocess.CompletedProcess(argv, self.image, '', '')
        return subprocess.CompletedProcess(argv, 0, self.state + '\n', '')

    def stage(self, **change):
        args = dict(new=NEW, image=IMAGE, date='20261015')
        args.update(change)
        return V.stage(self.home, 2, args['new'], args['image'], args['date'], run=self.run_)

    def test_stage_archives_everything_and_bootstraps_the_new_incarnation(self):
        result = self.stage()
        retired = self.home/'velocita-indicator-paper/retired/stochastic-b79e65be-20261015'
        self.assertEqual((retired/'data/node-2/log').read_text(), 'old wal', 'old log kept')
        self.assertEqual((retired/'config/node-2.conf').read_text(), CONF)
        self.assertEqual((retired/'units-before/velocita-paper-stochastic-node.service').read_text(), UNIT)
        live = self.home/'velocita-indicator-paper/stochastic'
        self.assertEqual((live/'config/node-2.conf').read_text(), CONF.replace(V.OLD, NEW).replace('startup = recover', 'startup = bootstrap'))
        self.assertEqual(list((live/'data').iterdir()), [], 'fresh empty log')
        self.assertEqual((live/'config/catalog.bin').read_bytes(), b'catalog')
        for u in V.UNITS:
            self.assertEqual((self.home/'.config/systemd/user'/(u + '.service')).read_text(), UNIT.replace(V.OLD_IMAGE, IMAGE))
        self.assertEqual((result['started'], result['startup']), (False, 'bootstrap'))
        self.assertEqual(self.calls[-1], ['systemctl', '--user', 'daemon-reload'])
        self.assertFalse(any('start' in c or 'stop' in c for c in self.calls))

    def test_stage_refusals_change_nothing(self):
        cases = [(dict(new=V.OLD), 'incarnation'), (dict(new='C' * 32), 'incarnation'), (dict(image=V.OLD_IMAGE), 'image'),
                 (dict(image='docker.io/x:latest'), 'image'), (dict(date='2026-10-15'), 'date')]
        for change, reason in cases:
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, reason):
                self.stage(**change)
        self.state = 'active'
        with self.assertRaisesRegex(ValueError, 'stopped'):
            self.stage()
        self.state, self.image = 'inactive', 1
        with self.assertRaisesRegex(ValueError, 'image missing'):
            self.stage()
        self.image = 0
        conf = self.home/'velocita-indicator-paper/stochastic/config/node-2.conf'
        conf.write_text(CONF.replace('startup = recover', 'startup = bootstrap'))
        with self.assertRaisesRegex(ValueError, 'recover mode'):
            self.stage()
        conf.write_text(CONF.replace(f'cluster.incarnation = {V.OLD}', 'cluster.incarnation = ' + 'e' * 32))
        with self.assertRaisesRegex(ValueError, 'recover mode'):
            self.stage()
        conf.write_text(CONF)
        (self.home/'.config/systemd/user/velocita-paper-stochastic-exporter.service').write_text('ExecStart=other\n')
        with self.assertRaisesRegex(ValueError, 'old qualified image'):
            self.stage()
        self.assertFalse((self.home/'velocita-indicator-paper/retired').exists())

    def test_stage_never_overwrites_an_archive(self):
        (self.home/'velocita-indicator-paper/retired/stochastic-b79e65be-20261015').mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'preserve'):
            self.stage()

    def test_recover_only_after_the_new_voter_is_ready(self):
        self.stage()
        with self.assertRaisesRegex(ValueError, 'not ready'):
            V.recover(self.home, 2, '1.2.3.4', NEW, fetch=lambda url: METRICS.replace('ready{host="pi"} 1', 'ready{host="pi"} 0'))
        with self.assertRaisesRegex(ValueError, 'bootstrapped'):
            V.recover(self.home, 2, '1.2.3.4', 'd' * 32, fetch=lambda url: METRICS)
        urls = []
        result = V.recover(self.home, 2, '1.2.3.4', NEW, fetch=lambda url: urls.append(url) or METRICS)
        self.assertEqual((urls, result['startup']), (['http://1.2.3.4:11410/metrics'], 'recover'))
        conf = (self.home/'velocita-indicator-paper/stochastic/config/node-2.conf').read_text()
        self.assertEqual(conf, CONF.replace(V.OLD, NEW))
        with self.assertRaisesRegex(ValueError, 'bootstrapped'):
            V.recover(self.home, 2, '1.2.3.4', NEW, fetch=lambda url: METRICS)


if __name__ == '__main__':
    unittest.main()
