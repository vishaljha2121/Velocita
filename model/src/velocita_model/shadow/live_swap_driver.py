"""Run the live stochastic swap rule unattended: Binance one-second bars in, at most one strategy intent at a time out."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

from ..adapters.base import ReadOnlyHttp
from ..adapters.websocket import ProtocolError, ReadOnlyMarketSocket
from .live_swap import MAX_ORDERS, Account, LiveRule, Order, Stop

OBSERVATION_MAX_AGE_MS = 12_000
INFLIGHT_TIMEOUT_MS = 20_000
CANDLE_MAX_AGE_MS = 2_000
ENTRY_DRAIN_MS = 420_000
EXIT_DRAIN_MS = 210_000
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
    def __init__(self, run, observation_path, state, submit, max_orders=MAX_ORDERS, *, review_expires_ms):
        self.run, self.observation_path, self.state, self.submit = run, Path(observation_path), state, submit
        if type(review_expires_ms) is not int or review_expires_ms <= 0:
            raise ValueError('positive reviewed expiry required')
        self.review_expires_ms = review_expires_ms
        self.rule = LiveRule(max_orders)

    def warm(self, kline):
        self.rule.bar(*kline, Account(0, 0, 0, 0, True), 0, None)

    def stop(self, reason):
        self.state.stopped = reason
        self.state.save()
        return reason

    def observe(self, now_ms):
        try:
            return observation(self.observation_path.read_bytes(), self.run, now_ms)
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def tick(self, now_ms):
        return self.check(now_ms, self.observe(now_ms))

    def check(self, now_ms, seen):
        if self.state.stopped:
            return self.state.stopped
        inflight = self.state.inflight_effect
        if inflight and seen is not None and inflight <= seen['last_effect']:
            self.state.inflight_effect = 0
            self.state.save()
        pending = bool(self.state.inflight_effect or (seen and seen['pending_effect']))
        if now_ms >= self.review_expires_ms:
            reason = ('pending' if pending else 'unknown' if seen is None else
                      'exposure' if seen['base_atoms'] else 'flat')
            return self.stop('review_expired_' + reason)
        if self.state.inflight_effect and now_ms - self.state.inflight_ms > INFLIGHT_TIMEOUT_MS:
            return self.stop('submission_unconfirmed')
        if now_ms >= self.review_expires_ms - ENTRY_DRAIN_MS and not pending:
            if seen is not None and not seen['base_atoms']:
                return self.stop('review_drained')
            if now_ms >= self.review_expires_ms - EXIT_DRAIN_MS:
                return self.stop('review_exit_deadline_' + ('unknown' if seen is None else 'exposure'))
        return None

    def on_bar(self, kline, now_ms):
        seen = self.observe(now_ms)
        stopped = self.check(now_ms, seen)
        if stopped:
            return stopped
        last, stamp = self.rule.indicators.last, (kline[1] + 1) // 1000
        if last is not None and stamp <= last:
            return 'duplicate'
        age = now_ms - (kline[1] + 1)
        if age < 0:
            raise ProtocolError('future candle age; warm up again')
        if last is not None and stamp > last + 1:
            raise ProtocolError('live bar gap; warm up again')
        if age > CANDLE_MAX_AGE_MS:
            self.warm(kline)
            return 'catchup'
        if seen is None:
            self.warm(kline)
            return 'stale'
        pending = bool(seen['pending_effect'] or self.state.inflight_effect)
        account = Account(seen['base_atoms'], seen['quote_atoms'], seen['realized_pnl_atoms'], seen['last_effect'], pending)
        decision = self.rule.bar(*kline, account, now_ms, self.state.last_exit_s,
                                 draining=now_ms >= self.review_expires_ms - ENTRY_DRAIN_MS)
        if isinstance(decision, Stop):
            return self.stop(decision.reason)
        if not isinstance(decision, Order):
            return 'held' if pending else 'quiet'
        effect = seen['last_effect'] + 1
        self.state.sequence += 1
        self.state.inflight_effect, self.state.inflight_ms = effect, now_ms
        if decision.side == 'sell':
            self.state.last_exit_s = (kline[1] + 1) // 1000
        self.state.save()
        try:
            accepted = self.submit(decision, effect, self.state.sequence)
        except (OSError, subprocess.SubprocessError):
            accepted = None
        if accepted is None:
            return self.stop('submission_unconfirmed')
        if accepted is not True:
            self.state.inflight_effect = 0
            self.state.save()
            return 'refused'
        return decision.side


class Metrics:
    MARKER_MS = 30_000

    def __init__(self, path, run):
        self.path, self.run, self.seen, self.markers = Path(path), run, None, []

    def lines(self, driver, kline, now_ms):
        seen = driver.observe(now_ms)
        if seen is not None:
            effects = {row['effect'] for row in seen['settlements']}
            if self.seen is None:
                self.seen = effects
            for row in seen['settlements']:
                if row['effect'] not in self.seen and row['base_atoms']:
                    self.markers.append((row['side'], row['quote_atoms'] * 1000 / row['base_atoms'], now_ms + self.MARKER_MS))
            self.seen |= effects
        self.markers = [m for m in self.markers if m[2] > now_ms]
        label = f'run="{self.run}"'
        out = [f'velocita_strategy_price_usd{{{label}}} {float(kline[5])}', f'velocita_strategy_bar_seconds{{{label}}} {(kline[1] + 1) // 1000}',
               f'velocita_strategy_sequence{{{label}}} {driver.state.sequence}', f'velocita_strategy_stopped{{{label}}} {int(bool(driver.state.stopped))}']
        values = driver.rule.signals.previous
        if values is not None:
            k, d, fast, slow = values
            out += [f'velocita_strategy_ema_usd{{{label},span="300"}} {fast}', f'velocita_strategy_ema_usd{{{label},span="1200"}} {slow}',
                    f'velocita_strategy_stochastic{{{label},line="k"}} {k}', f'velocita_strategy_stochastic{{{label},line="d"}} {d}']
        for side in ('buy', 'sell'):
            prices = [m[1] for m in self.markers if m[0] == side]
            if prices:
                out.append(f'velocita_strategy_fill_marker_usd{{{label},side="{side}"}} {prices[-1]}')
        return out

    def write(self, driver, kline, now_ms):
        try:
            pending = self.path.with_name(self.path.name + '.pending')
            pending.write_text('\n'.join(self.lines(driver, kline, now_ms)) + '\n')
            os.replace(pending, self.path)
        except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError):
            pass


def client_submitter(client, opening, kill_epoch, executor_generation, timeout_s=30):
    def submit(order, effect, sequence):
        now_ms = time.time_ns() // 1_000_000
        if not 0 <= now_ms - order.source_ms <= CANDLE_MAX_AGE_MS or now_ms >= order.sign_by_ms:
            return False
        argv = [*client, '--sequence', str(sequence), 'spot-swap-intent', str(opening), order.side, str(effect), str(order.amount),
                str(order.min_receive), str(order.fee_cap), str(order.source_ms), str(order.sign_by_ms),
                str(kill_epoch), str(executor_generation)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout_s, stdin=subprocess.DEVNULL)
        lines = [line for line in result.stdout.splitlines() if line.startswith('COMMITTED ')]
        if len(lines) == 1 and lines[0].startswith(f'COMMITTED sequence={sequence} '):
            fields = dict(field.split('=', 1) for field in lines[0].split()[1:] if '=' in field)
            if fields.get('code_name') == 'applied' and result.returncode == 0:
                return True
            if fields.get('code_name') and fields['code_name'] != 'applied' and result.returncode == 1:
                return False
        return None
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


def extend(config_path, max_orders, now_ms=None):
    config = json.loads(Path(config_path).read_text())
    now_ms = time.time_ns() // 1_000_000 if now_ms is None else now_ms
    expiry = config.get('review_expires_ms')
    if type(expiry) is not int or expiry <= now_ms:
        raise ValueError('unexpired review required')
    current = config.get('max_orders', MAX_ORDERS)
    if type(max_orders) is not int or not current < max_orders <= MAX_ORDERS:
        raise ValueError('a higher order limit up to 240 is required')
    state = State(config['state'])
    if state.stopped not in (None, 'capacity'):
        raise ValueError('only a capacity stop can be extended')
    config['max_orders'] = max_orders
    pending = Path(config_path).with_suffix('.pending')
    pending.write_text(json.dumps(config, indent=2) + '\n')
    os.replace(pending, config_path)
    state.stopped = None
    state.save()
    return dict(max_orders=max_orders, sequence=state.sequence)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--extend', type=int)
    args = parser.parse_args()
    if args.extend is not None:
        print(json.dumps(extend(args.config, args.extend)))
        return 0
    config = json.loads(args.config.read_text())
    submit = client_submitter(config['client'], config['opening_record'], config['kill_epoch'], config['executor_generation'])
    driver = Driver(config['incarnation'], config['observation'], State(config['state']), submit, config.get('max_orders', MAX_ORDERS),
                    review_expires_ms=config['review_expires_ms'])
    metrics = Metrics(Path(config['observation']).with_name('velocita-strategy.prom'), config['incarnation'])
    while not driver.state.stopped and not driver.tick(time.time_ns() // 1_000_000):
        socket = ReadOnlyMarketSocket('data-stream.binance.vision', '/ws/solusdt@kline_1s', timeout_s=4, max_message_bytes=16384)
        try:
            socket.open()
            bootstrap(driver)
            while not driver.state.stopped and not driver.tick(time.time_ns() // 1_000_000):
                raw = socket.recv_text()
                if raw is None:
                    break
                kline = klines(raw)
                if kline is not None:
                    print(driver.on_bar(kline, time.time_ns() // 1_000_000), flush=True)
                    metrics.write(driver, kline, time.time_ns() // 1_000_000)
        except (OSError, ProtocolError, ValueError) as error:
            print('feed restart: ' + type(error).__name__, flush=True)
            if not driver.tick(time.time_ns() // 1_000_000):
                time.sleep(2)
        finally:
            socket.close()
    print('stopped: ' + driver.state.stopped, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
