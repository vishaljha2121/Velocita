import json,time
from pathlib import Path
from velocita_model.shadow.indicator_paper import snapshot
from velocita_model.shadow.stochastic_second import VARIANT,pilot_program
c=json.loads(Path('/config/profile.json').read_text());now=time.time_ns()
s=snapshot('/venue/venue.json',c['incarnation'],now/1e9,{'previous_orders':[]})
assert s['cash']==25000000 and s['lots']==0 and not s['orders'] and s['fees']==0
assert s['buy_volume']==s['sell_volume']==0
assert s['buy_bps']==s['sell_bps']==20
c['epoch']=dict(variant=VARIANT,start_ns=now,end_ns=now+2100*10**9,
 previous_orders=[],cash=25000000,fees=0,volume=0,cost_bps=20)
c['start_ns']=now;c['stop_ns']=now+1800*10**9;c['program']='/edge/indicator-1s.program'
with Path('/config/profile-1s.json').open('x') as out: out.write(json.dumps(c,indent=2)+'\n')
with Path('/edge/indicator-1s.program').open('x') as out: out.write(pilot_program(c['catalog_root'],c['epoch']['end_ns']))
