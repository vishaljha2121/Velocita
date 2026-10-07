"""Maintenance only: old edge unit MUST be stopped before this is invoked."""
from pathlib import Path
import hashlib,json,sqlite3,time
from velocita_model.shadow.indicator_paper import snapshot
from velocita_model.shadow.paper_completion import publish_completion
c=json.loads(Path('/config/profile-1s.json').read_text())
p=Path(c['ledger'])
assert not Path(str(p)+'-wal').exists() and not Path(str(p)+'-shm').exists(), 'writer must be stopped'
before=hashlib.sha256(p.read_bytes()).hexdigest()
db=sqlite3.connect(p.as_uri()+'?mode=ro&immutable=1',uri=True)
try:
    assert db.execute('pragma integrity_check').fetchall()==[('ok',)]
    s=snapshot(c['venue_report'],c['incarnation'],time.time(),c['epoch'])
    assert s['cash']==24869170 and s['lots']==0 and len(s['orders'])==4
    path=publish_completion(db,c,s,time.time())
finally: db.close()
assert hashlib.sha256(p.read_bytes()).hexdigest()==before
print(json.dumps(dict(final=str(path),original_sha256=before,cash=s['cash'],orders=len(s['orders']))))
