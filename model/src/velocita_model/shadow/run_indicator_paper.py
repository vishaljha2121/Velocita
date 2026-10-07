"""Bounded paper-only edge; public OHLC input and the existing C++ submitter."""
import argparse
import json
from pathlib import Path
import time

from ..adapters.base import ReadOnlyHttp
from ..safety import enforce
from .edge import EdgeMetrics,run_once
from .indicator_paper import Journal,Source,Book,program_text,NS


def closed_bar_present(raw,bucket):
    try: rows=json.loads(raw)
    except ValueError: return False
    return isinstance(rows,list) and any(
        isinstance(row,list) and len(row)>6 and type(row[6]) is int and row[6]+1==bucket*300000 for row in rows)


class CandleSocket:
    def __init__(self,stop_ns):
        self.stop_ns=stop_ns
        self.http=ReadOnlyHttp('https://data-api.binance.vision',timeout_s=4)
        self.cached=None
        self.bucket=None

    def open(self): pass
    def close(self): pass
    def subscribe(self,topics): pass

    def recv_text(self):
        time.sleep(1)
        if time.time_ns()>=self.stop_ns: return None
        now=time.time()
        bucket=int(now)//300
        if self.bucket!=bucket:
            payload=self.http.get('/api/v3/klines',{'symbol':'SOLUSDT','interval':'5m','limit':240})
            raw=payload.body.decode()
            self.cached=raw
            if closed_bar_present(raw,bucket) or now-bucket*300>=20:
                self.bucket=bucket
        return self.cached


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--mode',choices=('paper',),required=True)
    parser.add_argument('--program-only',action='store_true')
    parser.add_argument('submitter',nargs=argparse.REMAINDER)
    a=parser.parse_args()
    enforce()
    c=json.loads(a.config.read_text())
    if c['mode']!='paper': raise ValueError('only paper profiles are supported')
    target=Path(c['program'])
    program=program_text(c['strategy'],c['catalog_root'],c['stop_ns']+3600*NS)
    if target.exists() and target.read_text()!=program:
        raise ValueError('program differs from immutable run profile')
    if not target.exists(): target.write_text(program)
    if a.program_only: return 0
    command=a.submitter[1:] if a.submitter[:1]==['--'] else a.submitter
    if not command or '--mode' not in command or command[command.index('--mode')+1]!='paper':
        raise ValueError('paper submitter command required')
    journal=Journal(c['ledger'],c['strategy'],c['incarnation'])
    book=Book(journal,c['venue_report'],c['stop_ns'])
    source=Source(c['strategy'],journal,book)
    metrics=EdgeMetrics(shadow_recorded_code=17)
    try:
        result=run_once(lambda:CandleSocket(c['stop_ns']+3600*NS),command,source,
            textfile=c['edge_metrics'],shadow_recorded_code=17,metrics=metrics,paper_book=book,
            topics=(),reconnect=lambda:time.time_ns()<c['stop_ns']+3600*NS)
        print(f'INDICATOR_PAPER_STOP reason={result.reason} decisions={result.decisions} errors={result.errors}',flush=True)
        return 0 if time.time_ns()>=c['stop_ns']+3600*NS else 2
    finally: journal.close()


if __name__=='__main__': raise SystemExit(main())
