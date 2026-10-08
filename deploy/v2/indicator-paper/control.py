"""Explicit one-shot setup steps for already prepared isolated paper instances."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def expand(command):
    return [x.replace('%h',str(Path.home())).replace('%t',f'/run/user/{os.getuid()}') for x in command]


def run(command,expected=None):
    result=subprocess.run(expand(command),text=True,capture_output=True)
    print(result.stdout,end='',flush=True)
    print(result.stderr,end='',flush=True)
    if result.returncode or (expected is not None and expected not in result.stdout):
        raise RuntimeError(f'paper setup step refused, exit={result.returncode}, expected={expected}')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--strategy',choices=('sma','stochastic'),required=True)
    p.add_argument('action',choices=('program','ceremony','setup','initialize'))
    a=p.parse_args()
    m=json.loads(a.manifest.read_text());c=m['profiles'][a.strategy]
    base=f'%h/velocita-indicator-paper/{a.strategy}'
    tag=m['tag'];offset=c['offset']
    common=['/usr/bin/podman','run','--rm','--network','host','--userns=keep-id','--read-only',
        '--security-opt','no-new-privileges','--cap-drop','all','--memory','512m',
        '--shm-size','256m','-e','AERON_TERM_BUFFER_LENGTH=1048576']
    if a.action=='program':
        command=c['edge_command'][:c['edge_command'].index('--')]+['--program-only']
        command[command.index('--name')+1]+='-program'
        command[command.index('--network')+1]='none'
        run(command)
    elif a.action=='ceremony':
        actions=[['kill-epoch','1'],['executor-generation','1'],['install-catalog',m['catalog_root'],'1','1'],
            ['assign-role','21','strategy'],['assign-role','22','gateway'],
            ['risk-limits','account=2','instrument=2','risk_policy_version=1','expected_risk_version=0',
             'kill_epoch=1','executor_generation=1','max_order_lots=12','max_reserved_lots=12',
             'max_order_notional_micros=25000000','max_reserved_exposure_micros=25000000',
             'initial_margin_ppm=1000000','maximum_fee_ppm=3000','max_position_lots=12',
             'allow_short=0','reason_code=20261007','max_window_loss_micros=10000000','reset_loss_window=0']]
        print('Operator commands are sent from the Mac, not from a voter host (ADR 0016, 2026-10-07).')
        print('Run each with velocita-v2-client --key-stdin and the operator key from the Keychain:')
        for sequence,action in enumerate(actions,1):
            print('  --sequence',sequence,' '.join(action),*c['transport'])
        raise SystemExit(64)
    elif a.action=='setup':
        command=common+['-v',f'{base}/config:/config:ro','-v',f'{base}/edge:/edge:ro',
            '-v',f'{base}/setup:/state',
            '-v','%t/velocita-secrets/caller-11.key:/run/velocita-secrets/caller-11.key:ro',
            '--entrypoint','/usr/local/bin/velocita-v2-shadow-submitter',f'localhost/velocita-v2-shadow:{tag}',
            '--setup-only','--program','/edge/indicator.program','--catalog','/config/catalog.bin',
            '--caller-key-id','11','--caller-id','9','--key-file','/run/velocita-secrets/caller-11.key',
            '--session-file','/state/setup.session','--state-file','/state/setup.state',
            '--response-endpoint',f'100.72.127.102:{42411+offset}',*c['transport'],
            '--aeron-dir','/dev/shm/velocita-setup-aeron']
        run(command,expected='SETUP ')
    else:
        command=c['gateway_command']+['--paper-state-init','--paper-quote-usd','25']
        command[command.index('--name')+1]+='-initialize'
        run(command,expected='PAPER_STATE created')


if __name__=='__main__': main()
