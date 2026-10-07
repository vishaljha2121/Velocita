"""One-second public-data paper runner with bounded bootstrap and reconnects."""
import argparse
import json
from pathlib import Path
import time

from ..adapters.base import ReadOnlyHttp
from ..adapters.websocket import ReadOnlyMarketSocket,FrameDecoder,ProtocolError
from ..safety import enforce
from .edge import EdgeMetrics,run_once
from .indicator_paper import Journal,Book,NS
from .stochastic_second import FastSource,pilot_program,LIMIT


class PublicSecondSocket(ReadOnlyMarketSocket):
    def __init__(self,source,deadline_ns):
        self._host='data-stream.binance.vision'
        self._path='/ws/solusdt@kline_1s'
        self._timeout_s=4;self._max=16384;self._connect=self._tls_connect
        self._sock=None;self._decoder=FrameDecoder(max_message_bytes=self._max)
        self._pending=[];self._closed=False
        self.source=source;self.deadline_ns=deadline_ns;self.bootstrap=None
        self.previous=None;self.last_raw=None

    def open(self):
        super().open()
        http=ReadOnlyHttp('https://data-api.binance.vision',timeout_s=4)
        end=int(time.time())*1000-1
        rows=[]
        for offset in (2,1,0):
            stop=end-offset*1000000
            response=http.get('/api/v3/klines',{'symbol':'SOLUSDT','interval':'1s',
                'startTime':stop-999999,'endTime':stop,'limit':1000})
            page=json.loads(response.body)
            if not isinstance(page,list) or len(page)!=1000: raise ProtocolError('bootstrap incomplete')
            rows.extend(page)
        self.previous=(rows[-1][6]+1)//1000
        self.bootstrap=json.dumps({'bootstrap':rows})

    def subscribe(self,topics):
        if tuple(topics): raise ValueError('fixed public stream only')

    def recv_text(self):
        if time.time_ns()>=self.deadline_ns or self.source.used()>=LIMIT: return None
        if self.bootstrap is not None:
            raw=self.bootstrap;self.bootstrap=None;return raw
        if self.source.retry_bar is not None and self.last_raw is not None: return self.last_raw
        raw=super().recv_text()
        if raw is None: return None
        try:
            data=json.loads(raw);k=data['k']
            if k['x'] is True:
                stamp=(k['T']+1)//1000
                if stamp>self.previous+1: raise ProtocolError('live candle gap; bootstrap again')
                self.previous=max(self.previous,stamp)
        except (ValueError,KeyError,TypeError): pass
        self.last_raw=raw
        return raw


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--mode',choices=('paper',),required=True)
    parser.add_argument('submitter',nargs=argparse.REMAINDER)
    a=parser.parse_args();enforce();c=json.loads(a.config.read_text());epoch=c['epoch']
    if c['mode']!='paper' or c['strategy']!='stochastic' or epoch['variant']!='stochastic_1s_v1':
        raise ValueError('one-second paper profile required')
    if Path(c['program']).read_text()!=pilot_program(c['catalog_root'],epoch['end_ns']):
        raise ValueError('immutable one-second program mismatch')
    if time.time_ns()>=epoch['end_ns']: return 0
    command=a.submitter[1:] if a.submitter[:1]==['--'] else a.submitter
    if not command or '--mode' not in command or command[command.index('--mode')+1]!='paper':
        raise ValueError('paper submitter required')
    journal=Journal(c['ledger'],'stochastic',c['incarnation'])
    book=Book(journal,c['venue_report'],c['stop_ns'],epoch)
    source=FastSource(journal,book,epoch)
    connections=0
    def connected():
        nonlocal connections
        connections+=1
        if connections>12: raise ProtocolError('pilot reconnect limit')
        return PublicSecondSocket(source,epoch['end_ns'])
    def again():
        return connections<12 and time.time_ns()<epoch['end_ns'] and source.used()<LIMIT
    try:
        result=run_once(connected,command,source,textfile=c['edge_metrics'],
            metrics=EdgeMetrics(17),paper_book=book,topics=(),reconnect=again)
        print(f'ONE_SECOND_STOP reason={result.reason} decisions={result.decisions}',flush=True)
        return 0
    finally: journal.close()


if __name__=='__main__': raise SystemExit(main())
