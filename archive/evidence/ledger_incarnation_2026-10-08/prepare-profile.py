import json,time
from pathlib import Path
from velocita_model.shadow.indicator_paper import snapshot
from velocita_model.shadow.stochastic_second import CASH_VARIANT,NO_DEADLINE,pilot_program
incarnation='abd1e149a26d4062ba5bbba2d90b3305'
root='3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe'
now=time.time_ns()
epoch=dict(variant=CASH_VARIANT,start_ns=now,end_ns=NO_DEADLINE,previous_orders=[],previous_cost_bps=20,
 cash=25000000,fees=0,volume=0,cost_bps=20)
s=snapshot('/venue/venue.json',incarnation,now/1e9,epoch)
assert s['cash']==25000000 and s['lots']==0 and not s['orders'] and s['fees']==0
assert s['buy_volume']==s['sell_volume']==0 and s['buy_bps']==s['sell_bps']==20
c=dict(mode='paper',strategy='stochastic',incarnation=incarnation,catalog_root=root,start_ns=now,stop_ns=0,
 program='/edge/indicator-cash.program',ledger='/edge/ledger-cash-v1.db',venue_report='/venue/venue.json',
 edge_metrics='/edge/edge.prom',gateway_metrics='/venue/gateway.prom',epoch=epoch)
with Path('/config/profile-cash.json').open('x') as out: out.write(json.dumps(c,indent=2)+'\n')
with Path('/edge/indicator-cash.program').open('x') as out: out.write(pilot_program(root,NO_DEADLINE,CASH_VARIANT))
print('PROFILE created',incarnation)
