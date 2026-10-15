"""Driver flow with a fake committed observation and a recording submitter; no network, keys or cluster."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_live_swap import bars
from velocita_model.adapters.websocket import ProtocolError
from velocita_model.shadow.live_swap_driver import Driver, State, observation

RUN = 'b' * 32


def seen(now_ms, base=0, quote=10**9, pnl=0, last=0, pending=0, **extra):
    value = dict(schema=2, incarnation=RUN, observed_at_ms=now_ms, applied_index=7, base_atoms=base, quote_atoms=quote, fees_atoms=0,
                 realized_pnl_atoms=pnl, last_effect=last, pending_effect=pending, settlements=[])
    value.update(extra)
    return json.dumps(value).encode()


class Observation(unittest.TestCase):
    def test_fresh_committed_observation_is_read(self):
        self.assertEqual(observation(seen(1_000, last=3), RUN, 6_000)['last_effect'], 3)

    def test_stale_or_future_observation_is_not_acted_on(self):
        self.assertIsNone(observation(seen(1_000), RUN, 6_001))
        self.assertIsNone(observation(seen(1_001), RUN, 1_000))

    def test_foreign_or_malformed_observation_refused(self):
        for raw in (seen(1_000, incarnation='c' * 32), seen(1_000, schema=1), seen(1_000, last=1, pending=2),
                    seen(1_000, base_atoms=-1), seen(1_000, extra_key=1)):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                observation(raw, RUN, 1_000)


class Flow(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.path = self.root / 'settlements.json'
        self.sent = []
        self.accept = True
        self.driver = Driver(RUN, self.path, State(self.root / 'state.json'), self.record)
        rows = bars(1_300)
        for row in rows:
            self.driver.warm(row)
        self.row = rows[-1]

    def record(self, order, effect, sequence):
        self.sent.append((order.side, effect, sequence))
        return self.accept

    def bar(self, shift=1):
        start = self.row[0] + 1000 * shift
        return (start, start + 999, *self.row[2:])

    def step(self, shift, flags, now_ms=50_000, **account):
        self.path.write_bytes(seen(now_ms, **account))
        self.driver.rule.previous = flags
        return self.driver.on_bar(self.bar(shift), now_ms)

    def test_entry_sends_one_buy_with_the_next_effect_and_sequence(self):
        self.assertEqual(self.step(1, (0, 1_000_000), last=4), 'buy')
        self.assertEqual(self.sent, [('buy', 5, 1)])
        self.assertEqual(json.loads((self.root / 'state.json').read_text())['inflight_effect'], 5, 'inflight recorded durably')

    def test_inflight_or_pending_effect_blocks_another_order(self):
        self.step(1, (0, 1_000_000), last=4)
        self.assertEqual(self.step(2, (0, 1_000_000), last=4), 'held', 'submitted effect not yet visible')
        self.assertEqual(self.step(3, (1_000_000, 0), last=5, pending=5, base=1), 'held', 'committed effect still pending')
        self.assertEqual(self.step(4, (1_000_000, 0), last=5, base=1), 'sell', 'settled effect releases the next order')
        self.assertEqual([s[1] for s in self.sent], [5, 6])

    def test_unseen_submission_times_out_and_refusal_clears_at_once(self):
        self.step(1, (0, 1_000_000), now_ms=50_000, last=4)
        self.assertEqual(self.step(2, (0, 1_000_000), now_ms=70_001, last=4), 'buy', 'unseen effect expires after twenty seconds')
        self.accept = False
        self.assertEqual(self.step(3, (0, 1_000_000), now_ms=90_002, last=4), 'refused')
        self.assertEqual(self.driver.state.inflight_effect, 0)

    def test_sell_records_the_exit_for_reentry(self):
        self.step(1, (1_000_000, 0), last=2, base=120_000_000)
        self.assertEqual(self.driver.state.last_exit_s, (self.bar(1)[1] + 1) // 1000)

    def test_stale_observation_advances_indicators_without_orders(self):
        count = self.driver.rule.indicators.count
        self.path.write_bytes(seen(1_000))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(self.bar(1), 50_000), 'stale')
        self.assertEqual((self.sent, self.driver.rule.indicators.count), ([], count + 1))

    def test_stop_is_terminal_and_durable(self):
        self.assertEqual(self.step(1, (0, 0), pnl=-10_000_000), 'drawdown')
        self.assertEqual(self.step(2, (0, 1_000_000)), 'drawdown')
        self.assertEqual(State(self.root / 'state.json').stopped, 'drawdown')
        self.assertEqual(self.sent, [])

    def test_restart_continues_the_sequence(self):
        self.step(1, (0, 1_000_000), last=4)
        again = Driver(RUN, self.path, State(self.root / 'state.json'), self.record)
        again.rule = self.driver.rule
        self.path.write_bytes(seen(50_000, last=5, base=1))
        again.rule.previous = (1_000_000, 0)
        again.on_bar(self.bar(2), 50_000)
        self.assertEqual(self.sent[-1], ('sell', 6, 2))

    def test_duplicate_bar_skipped_and_gap_restarts_the_feed(self):
        self.path.write_bytes(seen(50_000))
        self.assertEqual(self.driver.on_bar(self.row, 50_000), 'duplicate')
        with self.assertRaises(ProtocolError):
            self.driver.on_bar(self.bar(3), 50_000)


if __name__ == '__main__':
    unittest.main()
