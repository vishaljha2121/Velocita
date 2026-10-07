"""Generate fresh, isolated paper profiles without contacting or changing hosts."""
import argparse
import json
from pathlib import Path
import shlex
import time
import uuid

ROOT=Path(__file__).resolve().parents[3]
HOSTS={1:('nuc1','100.72.127.102'),2:('pi','100.84.151.116'),3:('nuc2','100.85.36.22')}


GUARD='/bin/sh %h/src/aeron-raft-pi/deploy/v2/one-cluster-guard.sh'


def unit(name,command,*,after='network-online.target',extra='',memory='512m',guard=False):
    return '\n'.join(['[Unit]',f'Description={name}',f'After={after}',extra,'',
        '[Service]',*([f'ExecStartPre={GUARD} {name}'] if guard else []),'ExecStart='+shlex.join(command),
        f'ExecStop=/usr/bin/podman stop -t 15 {name}',
        f'ExecStopPost=-/usr/bin/podman rm --ignore {name}',
        'Restart=on-failure','RestartPreventExitStatus=64','RestartSec=10','TimeoutStopSec=30',
        '', '[Install]','WantedBy=default.target',''])


def container(name,mounts,memory='512m'):
    command=['/usr/bin/podman','run','--rm','--name',name,'--network','host','--userns=keep-id',
        '--read-only','--security-opt','no-new-privileges','--cap-drop','all','--pids-limit','128',
        '--memory',memory,'--shm-size','256m','-e','AERON_TERM_BUFFER_LENGTH=1048576']
    for mount in mounts: command+=['-v',mount]
    return command


def generate(out,tag,catalog_root,start_ns,voter_tag='m3-cp081',strategy='stochastic'):
    if strategy not in ('sma','stochastic'):
        raise ValueError('exactly one strategy must be selected')
    out.mkdir(parents=True,exist_ok=False)
    manifest=dict(tag=tag,voter_tag=voter_tag,catalog_root=catalog_root,start_ns=start_ns,
        stop_ns=start_ns+14*86400*1000000000,profiles={})
    observer=[]
    for strategy,offset in ((strategy,1000 if strategy=='sma' else 2000),):
        incarnation=uuid.uuid4().hex
        base=f'%h/velocita-indicator-paper/{strategy}'
        local=out/strategy;local.mkdir()
        c=dict(mode='paper',strategy=strategy,incarnation=incarnation,catalog_root=catalog_root,
            start_ns=start_ns,stop_ns=manifest['stop_ns'],program='/edge/indicator.program',
            ledger='/edge/ledger.db',venue_report='/venue/venue.json',edge_metrics='/edge/edge.prom',
            gateway_metrics='/venue/gateway.prom')
        (local/'profile.json').write_text(json.dumps(c,indent=2)+'\n')
        watched=dict(c)
        for key in ('program','ledger','edge_metrics'):
            watched[key]=c[key].replace('/edge/',f'/runs/{strategy}/edge/')
        for key in ('venue_report','gateway_metrics'):
            watched[key]=c[key].replace('/venue/',f'/runs/{strategy}/gateway/')
        observer.append(watched)
        nodes=[]
        for node,(_,ip) in HOSTS.items(): nodes+=['--node',f'{node}={ip}:{42300+offset+node}']
        transport=['--incarnation',incarnation,*nodes]
        for node,(host,ip) in HOSTS.items():
            folder=local/host;folder.mkdir()
            text=(ROOT/f'deploy/v2/three-host/node-{node}.conf.template').read_text()
            text=text.replace('@INCARNATION@',incarnation).replace('@CATALOG_ROOT@',catalog_root)
            for port in (42201,42202,42203,42301,42302,42303,42400,42410,42411,42412,42413):
                text=text.replace(':'+str(port),':'+str(port+offset))
            text=text.replace('100.91.80.64:','100.72.127.102:')
            (folder/f'node-{node}.conf').write_text(text)
            voter=f'velocita-paper-{strategy}-node'
            exporter=f'velocita-paper-{strategy}-exporter'
            cmd=container(voter,[f'{base}/config:/etc/velocita:ro',f'{base}/data:/var/lib/velocita-v2',
                '%t/velocita-secrets:/run/velocita-secrets:ro'],memory='1g')
            cmd += [f'localhost/velocita-v2:{voter_tag}','--config',f'/etc/velocita/node-{node}.conf',
                    '--aeron-dir','/dev/shm/velocita-aeron']
            (folder/f'{voter}.service').write_text(unit(voter,cmd,extra=f'Wants={exporter}.service',guard=True))
            cmd=container(exporter,[f'{base}/data:/var/lib/velocita-v2:ro'],memory='64m')
            cmd+=['--entrypoint','/usr/local/bin/velocita-v2-exporter',f'localhost/velocita-v2:{voter_tag}',
                '--health',f'/var/lib/velocita-v2/node-{node}/health.page','--host',host,
                '--listen',ip,'--port',str(9410+offset)]
            (folder/f'{exporter}.service').write_text(unit(exporter,cmd,after=f'{voter}.service',
                extra=f'BindsTo={voter}.service\nPartOf={voter}.service'))
        gateway=f'velocita-paper-{strategy}-gateway'
        gargs=['--venue','paper','--catalog','/etc/velocita/catalog.bin','--catalog-row','2,20,2,1,1',
            '--caller-key-id','13','--caller-id','22','--key-file','/run/velocita-secrets/caller-13.key',
            *transport,'--session-file','/state/gateway.session','--state-file','/state/gateway.state',
            '--paper-state-file','/state/paper.state','--paper-report-file','/state/venue.json',
            '--response-endpoint',f'100.72.127.102:{42413+offset}',
            '--aeron-dir','/dev/shm/velocita-gateway-aeron','--metrics-textfile','/state/gateway.prom',
            '--buy-fee-bps','30','--sell-fee-bps','30','--freshness-ms','2000','--allow-new-risk']
        gm=[f'{base}/config/catalog.bin:/etc/velocita/catalog.bin:ro',f'{base}/gateway:/state',
            '%t/velocita-secrets/caller-13.key:/run/velocita-secrets/caller-13.key:ro']
        gcmd=container(gateway,gm)+['--entrypoint','/usr/local/bin/velocita-v2-gateway',f'localhost/velocita-v2:{tag}',*gargs]
        (local/'nuc1'/f'{gateway}.service').write_text(unit(gateway,gcmd,after=f'velocita-paper-{strategy}-node.service'))
        submitter=['velocita-v2-shadow-submitter','--program','/edge/indicator.program',
            '--catalog','/config/catalog.bin','--caller-key-id','12','--caller-id','21',
            '--key-file','/run/velocita-secrets/caller-12.key',*transport,
            '--session-file','/edge/strategy.session','--state-file','/edge/strategy.state',
            '--response-endpoint',f'100.72.127.102:{42412+offset}','--mode','paper',
            '--aeron-dir','/dev/shm/velocita-shadow-aeron']
        edge=f'velocita-paper-{strategy}-edge'
        em=[f'{base}/config:/config:ro',f'{base}/edge:/edge',f'{base}/gateway:/venue:ro',
            '%t/velocita-secrets/caller-12.key:/run/velocita-secrets/caller-12.key:ro']
        ecmd=container(edge,em)+['--entrypoint','python3',f'localhost/velocita-v2-shadow:{tag}',
            '-m','velocita_model.shadow.run_indicator_paper','--mode','paper','--config','/config/profile.json','--',*submitter]
        (local/'nuc1'/f'{edge}.service').write_text(unit(edge,ecmd,after=gateway+'.service'))
        manifest['profiles'][strategy]=dict(incarnation=incarnation,offset=offset,
            gateway_command=gcmd,edge_command=ecmd,transport=transport)
    (out/'observer.json').write_text(json.dumps(observer,indent=2)+'\n')
    name='velocita-paper-indicator-observer'
    mounts=['%h/velocita-indicator-paper/observer.json:/config/observer.json:ro']
    for strategy in manifest['profiles']:
        base=f'%h/velocita-indicator-paper/{strategy}'
        mounts += [f'{base}/edge:/runs/{strategy}/edge:ro',f'{base}/gateway:/runs/{strategy}/gateway:ro']
    cmd=container(name,mounts,memory='128m')+['--entrypoint','python3',f'localhost/velocita-v2-shadow:{tag}',
        '-m','velocita_model.shadow.indicator_observer','--config','/config/observer.json']
    (out/f'{name}.service').write_text(unit(name,cmd))
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--tag',required=True)
    parser.add_argument('--voter-tag',default='m3-cp081')
    parser.add_argument('--strategy',choices=('sma','stochastic'),default='stochastic')
    parser.add_argument('--catalog-root',required=True)
    args=parser.parse_args()
    generate(args.out,args.tag,args.catalog_root,time.time_ns(),args.voter_tag,args.strategy)
