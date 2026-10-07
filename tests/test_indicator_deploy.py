import importlib.util
import json
import re
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class IndicatorDeployment(unittest.TestCase):
    def test_isolated_profiles_ports_and_authorities(self):
        spec=importlib.util.spec_from_file_location('prepare_indicator',ROOT/'deploy/v2/indicator-paper/prepare.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(dir=ROOT/'tests') as folder:
            out=Path(folder)/'profiles'
            manifest=module.generate(out,'test-image','12'*32,1000000000)
            self.assertEqual(manifest['voter_tag'],'m3-cp081')
            self.assertEqual(set(manifest['profiles']),{'stochastic'})
            self.assertFalse((out/'sma').exists())
            for strategy in ('stochastic',):
                conf=(out/strategy/'nuc1/node-1.conf').read_text()
                self.assertNotRegex(conf,r'@[A-Z_]+@')
                self.assertNotIn(':42301',conf)
                edge=(out/strategy/'nuc1'/f'velocita-paper-{strategy}-edge.service').read_text()
                self.assertNotIn('caller-11',edge)
                self.assertIn('--mode paper',edge)
                self.assertIn(f'ExecStopPost=-/usr/bin/podman rm --ignore velocita-paper-{strategy}-edge',edge)
                self.assertNotIn('podman rm --force',edge)
                self.assertNotIn('--mode live',edge)
                self.assertIn(f'{strategy}/gateway:/venue:ro',edge)
                exporter=(out/strategy/'nuc1'/f'velocita-paper-{strategy}-exporter.service').read_text()
                self.assertIn('localhost/velocita-v2:m3-cp081',exporter)
                self.assertIn(f'PartOf=velocita-paper-{strategy}-node.service',exporter)
                node=(out/strategy/'nuc1'/f'velocita-paper-{strategy}-node.service').read_text()
                self.assertIn(f'ExecStartPre=/bin/sh %h/src/aeron-raft-pi/deploy/v2/one-cluster-guard.sh velocita-paper-{strategy}-node\n',node)
                self.assertNotIn('one-cluster-guard',exporter)
                gateway=(out/strategy/'nuc1'/f'velocita-paper-{strategy}-gateway.service').read_text()
                self.assertIn('--venue paper',gateway)
                self.assertNotIn('--mode',gateway)
            watcher=(out/'velocita-paper-indicator-observer.service').read_text()
            self.assertIn('stochastic/edge:/runs/stochastic/edge:ro',watcher)
            self.assertNotRegex(watcher,r'/runs/[a-z]+/(edge|gateway)(?!:ro)[ \n]')
            self.assertTrue((ROOT/'deploy/v2/one-cluster-guard.sh').exists())
            original=(ROOT/'deploy/v2/systemd/velocita-v2-node@.service').read_text()
            self.assertIn('ExecStartPre=/bin/sh %h/src/aeron-raft-pi/deploy/v2/one-cluster-guard.sh velocita-v2-node-%i\n',original)
            observer=json.loads((out/'observer.json').read_text())
            self.assertEqual(len(observer),1)
            self.assertEqual(observer[0]['strategy'],'stochastic')

    def test_the_setup_script_prints_operator_commands_and_sends_none(self):
        import subprocess,sys
        spec=importlib.util.spec_from_file_location('prepare_indicator',ROOT/'deploy/v2/indicator-paper/prepare.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        control=ROOT/'deploy/v2/indicator-paper/control.py'
        self.assertNotIn('caller-9',control.read_text())
        with tempfile.TemporaryDirectory(dir=ROOT/'tests') as folder:
            out=Path(folder)/'profiles'
            module.generate(out,'test-image','12'*32,1000000000)
            result=subprocess.run([sys.executable,str(control),'--manifest',str(out/'manifest.json'),
                '--strategy','stochastic','ceremony'],capture_output=True,text=True)
            self.assertEqual(result.returncode,64)
            for verb in ('kill-epoch 1','executor-generation 1','install-catalog','risk-limits account=2'):
                self.assertIn(verb,result.stdout)
            self.assertIn('from the Mac',result.stdout)

    def test_dashboard_has_rules_and_confirmed_fill_points_for_selected_strategy(self):
        d=json.loads((ROOT/'deploy/v2/observability/grafana/dashboards/velocita-v2-paper.json').read_text())
        texts=[p['options']['content'] for p in d['panels'] if p['type']=='text']
        self.assertFalse(any('SMA(20) > SMA(50)' in t for t in texts))
        self.assertTrue(any('K <= D OR K <= 40' in t for t in texts))
        for strategy in ('stochastic',):
            panels=[p for p in d['panels'] if any('fill_marker_usd' in t.get('expr','') and
                f'strategy="{strategy}"' in t['expr'] for t in p.get('targets',[]))]
            self.assertEqual(len(panels),1)
            self.assertEqual({o['matcher']['options'] for o in panels[0]['fieldConfig']['overrides']},{'Entry','Exit'})

    def test_all_active_cluster_queries_are_scoped_and_latency_matches_exporter(self):
        for path in (ROOT/'deploy/v2/observability/grafana/dashboards').glob('*.json'):
            d=json.loads(path.read_text())
            for p in d['panels']:
                for t in p.get('targets',[]):
                    for m in re.finditer(r'\bvelocita_(?:v2|shadow|gateway)_[a-zA-Z0-9_]+(\{[^}]*\})?',t['expr']):
                        self.assertIn('strategy="stochastic"',m.group(1) or '',t['expr'])
        text=(ROOT/'deploy/v2/observability/grafana/dashboards/velocita-v2-paper.json').read_text()
        self.assertNotIn('decision_to_authorized',text)
        self.assertIn('p99 estimate (100+ samples)',text)
        source=(ROOT/'src/v2_gateway_loop.cpp').read_text()
        for stage in ('authorized_to_proof','proof_to_sent','sent_to_fill','fill_to_committed'):
            self.assertIn('GatewayStage::'+stage,source)


if __name__=='__main__': unittest.main()
