from pathlib import Path
import shutil,subprocess
home=Path.home();units=home/'.config/systemd/user'
backup=home/'velocita-indicator-paper/retired/stochastic-19137567-20261007/units-before-reset'
for name,old,new in (
 ('velocita-v2-prometheus.service','%h/src/aeron-raft-pi/deploy/v2/observability/prometheus.yml','%h/stochastic-fresh-c8a6a0a7/observability/prometheus.yml'),
 ('velocita-v2-grafana.service','%h/src/aeron-raft-pi/deploy/v2/observability/grafana/dashboards','%h/stochastic-fresh-c8a6a0a7/observability')):
 p=units/name;s=p.read_text();assert old in s
 assert not (backup/name).exists()
 shutil.copy2(p,backup/name);p.write_text(s.replace(old,new))
subprocess.run(['systemctl','--user','daemon-reload'],check=True)
subprocess.run(['systemctl','--user','restart','velocita-v2-prometheus.service','velocita-v2-grafana.service'],check=True)
