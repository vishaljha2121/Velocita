import sys,subprocess,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import authenticated_read_run as R
p=R.pins('e8b8fa9888d393cfdcab5a8beda3b2891c8ec8d9aa74e7025a5edee5857e0a58','8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f','b79e65be1ea14e219016ec48ea02efef','3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe','29b772a949627cd25c657a527ef2ea79a7b470454de9be9aa842462982b9d95e')
cmd=R.command('signer','1111222233338888',p)
cmd=cmd[:cmd.index(R.HELPER)]+['/usr/bin/python3','-I','-B','-c',"import os,stat; paths=['/','/etc','/etc/velocita-swap','/etc/velocita-swap/public','/etc/velocita-swap/public/catalog.bin','/etc/velocita-swap/signer','/etc/velocita-swap/signer/caller.key','/var','/var/lib','/var/lib/velocita-swap-signer','/var/lib/velocita-swap-signer/client.session','/run','/run/velocita-authread-1111222233338888-signer']; [(print(p,os.lstat(p).st_uid,os.lstat(p).st_gid,oct(stat.S_IMODE(os.lstat(p).st_mode)))) for p in paths]; print('MOUNTS'); print('\\n'.join(x for x in open('/proc/self/mountinfo') if ' /run ' in x or '/run/velocita-authread' in x))"]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=75)
print(json.dumps(dict(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr)))
