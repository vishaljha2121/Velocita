"""Run the live stochastic swap rule unattended: Binance one-second bars in, at most one strategy intent at a time out."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

from ..adapters.base import ReadOnlyHttp
from ..adapters.websocket import ProtocolError, ReadOnlyMarketSocket
from .live_swap import Account, LiveRule, Order, Stop

OBSERVATION_MAX_AGE_MS = 5_000
INFLIGHT_TIMEOUT_MS = 20_000
OBSERVATION_KEYS = {'schema', 'incarnation', 'observed_at_ms', 'applied_index', 'base_atoms', 'quote_atoms', 'fees_atoms',
                    'realized_pnl_atoms', 'last_effect', 'pending_effect', 'settlements'}


def observation(raw, run, now_ms):
    value = json.loads(raw)
    if type(value) is not dict or set(value) != OBSERVATION_KEYS or value['schema'] != 2 or value['incarnation'] != run:
        raise ValueError('observation identity')
    for key in ('observed_at_ms', 'applied_index', 'base_atoms', 'quote_atoms', 'last_effect', 'pending_effect'):
        if type(value[key]) is not int or value[key] < 0:
            raise ValueError('observation field ' + key)
    if type(value['realized_pnl_atoms']) is not int or value['pending_effect'] > value['last_effect']:
        raise ValueError('observation accounting')
    if value['observed_at_ms'] > now_ms or now_ms - value['observed_at_ms'] > OBSERVATION_MAX_AGE_MS:
        return None
    return value


class State:
    def __init__(self, path):
        self.path = Path(path)
        try:
            data = json.loads(self.path.read_text())
        except FileNotFoundError:
            data = dict(sequence=0, inflight_effect=0, inflight_ms=0, last_exit_s=None, stopped=None)
        self.__dict__.update(data)

    def save(self):
        data = {k: getattr(self, k) for k in ('sequence', 'inflight_effect', 'inflight_ms', 'last_exit_s', 'stopped')}
        pending = self.path.with_suffix('.pending')
        with open(pending, 'w') as stream:
            stream.write(json.dumps(data, sort_keys=True))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(pending, self.path)


class Driver:
    def __init__(self, run, observation_path, state, submit):
        self.run, self.observation_path, self.state, self.submit = run, Path(observation_path), state, submit
        self.rule = LiveRule()

    def warm(self, kline):
        self.rule.bar(*kline, Account(0, 0, 0, 0, True), 0, None)

    def on_bar(self, kline, now_ms):
        if self.state.stopped:
            return self.state.stopped
        last, stamp = self.rule.indicators.last, (kline[1] + 1) // 1000
        if last is not None and stamp <= last:
            return 'duplicate'
        if last is not None and stamp > last + 1:
            raise ProtocolError('live bar gap; warm up again')
        try:
            seen = observation(self.observation_path.read_bytes(), self.run, now_ms)
        except (OSError, ValueError, KeyError, TypeError):
            seen = None
        if seen is None:
            self.warm(kline)
            return 'stale'
        inflight = self.state.inflight_effect
        if inflight and (inflight <= seen['last_effect'] or now_ms - self.state.inflight_ms > INFLIGHT_TIMEOUT_MS):
            self.state.inflight_effect = 0
            self.state.save()
        pending = bool(seen['pending_effect'] or self.state.inflight_effect)
        account = Account(seen['base_atoms'], seen['quote_atoms'], seen['realized_pnl_atoms'], seen['last_effect'], pending)
        decision = self.rule.bar(*kline, account, now_ms, self.state.last_exit_s)
        if isinstance(decision, Stop):
            self.state.stopped = decision.reason
            self.state.save()
            return decision.reason
        if not isinstance(decision, Order):
            return 'held' if pending else 'quiet'
        effect = seen['last_effect'] + 1
        self.state.sequence += 1
        self.state.inflight_effect, self.state.inflight_ms = effect, now_ms
        if decision.side == 'sell':
            self.state.last_exit_s = (kline[1] + 1) // 1000
        self.state.save()
        if not self.submit(decision, effect, self.state.sequence):
            self.state.inflight_effect = 0
            self.state.save()
            return 'refused'
        return decision.side


def client_submitter(client, opening, kill_epoch, executor_generation, timeout_s=30):
    def submit(order, effect, sequence):
        argv = [*client, '--sequence', str(sequence), 'spot-swap-intent', str(opening), order.side, str(effect), str(order.amount),
                str(order.min_receive), str(order.fee_cap), str(order.source_ms), str(order.sign_by_ms),
                str(kill_epoch), str(executor_generation)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout_s, stdin=subprocess.DEVNULL)
        return result.returncode == 0 and 'code_name=applied' in result.stdout
    return submit


def klines(raw):
    data = json.loads(raw)
    k = data['k']
    if data.get('s') != 'SOLUSDT' or k['s'] != 'SOLUSDT' or k['i'] != '1s':
        raise ProtocolError('stream identity')
    if k['x'] is not True:
        return None
    return (k['t'], k['T'], k['o'], k['h'], k['l'], k['c'])


def bootstrap(driver):
    http = ReadOnlyHttp('https://data-api.binance.vision', timeout_s=4)
    end = int(time.time()) * 1000 - 1
    rows = []
    for offset in (2, 1, 0):
        stop = end - offset * 1_000_000
        page = json.loads(http.get('/api/v3/klines', {'symbol': 'SOLUSDT', 'interval': '1s', 'startTime': stop - 999_999,
                                                      'endTime': stop, 'limit': 1000}).body)
        if not isinstance(page, list) or len(page) != 1000:
            raise ProtocolError('bootstrap incomplete')
        rows.extend(page)
    driver.rule.reset()
    for row in rows:
        driver.warm((row[0], row[6], row[1], row[2], row[3], row[4]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    config = json.loads(parser.parse_args().config.read_text())
    submit = client_submitter(config['client'], config['opening_record'], config['kill_epoch'], config['executor_generation'])
    driver = Driver(config['incarnation'], config['observation'], State(config['state']), submit)
    while not driver.state.stopped:
        socket = ReadOnlyMarketSocket('data-stream.binance.vision', '/ws/solusdt@kline_1s', timeout_s=4, max_message_bytes=16384)
        try:
            socket.open()
            bootstrap(driver)
            while not driver.state.stopped:
                raw = socket.recv_text()
                if raw is None:
                    break
                kline = klines(raw)
                if kline is not None:
                    print(driver.on_bar(kline, time.time_ns() // 1_000_000), flush=True)
        except (OSError, ProtocolError, ValueError) as error:
            print('feed restart: ' + type(error).__name__, flush=True)
            time.sleep(2)
        finally:
            socket.close()
    print('stopped: ' + driver.state.stopped, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
