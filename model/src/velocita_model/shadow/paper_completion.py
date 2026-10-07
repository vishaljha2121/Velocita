"""Publish a checked, standalone completed ledger; the observer never writes it."""
import json
import os
from pathlib import Path
import sqlite3
import tempfile

from .indicator_paper import identity_text, validate_snapshot


def completed_path(ledger):
    return Path(str(ledger)+'.completed')


def validate_completion(db, c, record, now):
    epoch=c['epoch']
    cash_run=epoch['variant']=='stochastic_1s_cash_v1'
    earliest=epoch['start_ns']/1e9 if cash_run else epoch['end_ns']/1e9
    if record['version']!=1 or not earliest<=record['finalized_at']<=now:
        raise ValueError('completion deadline')
    identity=db.execute('select value from identity where id=1').fetchone()
    stored=db.execute('select payload from variant_epoch where id=1').fetchone()
    if (identity is None or identity[0]!=identity_text(c['strategy'],c['incarnation']) or
            stored is None or json.loads(stored[0])!=epoch):
        raise ValueError('completion identity or epoch')
    s=validate_snapshot(record['venue'],c['incarnation'],record['finalized_at'],epoch)
    if s['lots']!=0: raise ValueError('completion requires flat inventory')
    actual=[(int(r[0]),*r[1:]) for r in db.execute(
        'select id,side,price,lots,notional,fee from fills order by rowid')]
    expected=[tuple(o[k] for k in ('id','side','price','lots','notional','fee')) for o in s['orders']]
    if actual!=expected: raise ValueError('completion fill mismatch')
    if cash_run:
        reason=record['reason']
        mark=db.execute('select stamp,price from marks order by stamp desc limit 1').fetchone()
        if reason=='insufficient_balance':
            if not mark or not 0<=record['finalized_at']-mark[0]<=5:
                raise ValueError('balance stop requires fresh price')
            notional=mark[1]*12*100
            if s['cash']>=notional-(-notional*20//10000):
                raise ValueError('balance still sufficient')
        elif reason=='capacity':
            if len(s['orders'])<240: raise ValueError('order capacity not exhausted')
        elif reason=='drawdown':
            peak=db.execute('select max(equity) from equities').fetchone()[0] or 25000000
            if max(25000000,peak)-s['cash']<10000000:
                raise ValueError('drawdown not reached')
        else: raise ValueError('invalid cash run stop reason')
    return s


def publish_completion(db,c,venue,now,reason="duration"):
    """Called by the finished writer, or a stopped-writer maintenance operation.

    SQLite backup includes committed WAL content. Only the copy is modified;
    DELETE journal mode lets an ordinary mode=ro reader open it on a read-only
    mount. Publish via no-clobber hard link after fsync, then fsync the directory.
    """
    record=dict(version=1,finalized_at=now,venue=venue,reason=reason)
    validate_completion(db,c,record,now)
    dest=completed_path(c['ledger'])
    if dest.exists(): raise FileExistsError(dest)
    fd,name=tempfile.mkstemp(prefix=dest.name+'.',dir=dest.parent)
    os.close(fd)
    try:
        copy=sqlite3.connect(name)
        try:
            db.backup(copy)
            mode=copy.execute('pragma journal_mode=DELETE').fetchone()[0]
            if mode!='delete': raise ValueError('completion journal mode')
            with copy:
                copy.execute('create table completion (id integer primary key check(id=1), payload text not null)')
                copy.execute('insert into completion values (1,?)',(json.dumps(record,sort_keys=True),))
            validate_completion(copy,c,record,now)
            if copy.execute('pragma integrity_check').fetchall()!=[('ok',)]:
                raise ValueError('completion integrity')
        finally: copy.close()
        with open(name,'rb') as f: os.fsync(f.fileno())
        os.link(name,dest)
        directory=os.open(dest.parent,os.O_RDONLY)
        try: os.fsync(directory)
        finally: os.close(directory)
    finally: Path(name).unlink(missing_ok=True)
    return dest


def open_completion(c,now):
    db=sqlite3.connect(completed_path(c['ledger']).resolve().as_uri()+'?mode=ro',uri=True)
    try:
        if db.execute('pragma journal_mode').fetchone()[0]!='delete':
            raise ValueError('completion must be standalone')
        row=db.execute('select payload from completion where id=1').fetchone()
        if row is None: raise ValueError('completion record missing')
        record=json.loads(row[0])
        return db,validate_completion(db,c,record,now),record['finalized_at']
    except BaseException:
        db.close();raise
