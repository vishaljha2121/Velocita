"""Driver flow with a fake committed observation and a recording submitter; no network, keys or cluster."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_live_swap import bars
from velocita_model.adapters.websocket import ProtocolError
from velocita_model.shadow.live_swap_driver import Driver, State, extend, observation, client_submitter, main

RUN = 'b' * 32
EXPIRY = 2_000_000_000_000


def seen(now_ms, base=0, quote=10**9, pnl=0, last=0, pending=0, **extra):
    value = dict(schema=2, incarnation=RUN, observed_at_ms=now_ms, applied_index=7, base_atoms=base, quote_atoms=quote, fees_atoms=0,
                 realized_pnl_atoms=pnl, last_effect=last, pending_effect=pending, settlements=[])
    value.update(extra)
    return json.dumps(value).encode()


class Observation(unittest.TestCase):
    def test_fresh_committed_observation_is_read(self):
        self.assertEqual(observation(seen(1_000, last=3), RUN, 13_000)['last_effect'], 3)

    def test_stale_or_future_observation_is_not_acted_on(self):
        self.assertIsNone(observation(seen(1_000), RUN, 13_001))
        self.assertIsNone(observation(seen(1_001), RUN, 1_000))

    def test_foreign_or_malformed_observation_refused(self):
        for raw in (seen(1_000, incarnation='c' * 32), seen(1_000, schema=1), seen(1_000, last=1, pending=2),
                    seen(1_000, base_atoms=-1), seen(1_000, extra_key=1)):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                observation(raw, RUN, 1_000)


class Flow(unittest.TestCase):
    def setUp(self):
        scratch = Path.home() / '.cache/velocita-build/focused-mvp-20261015/driver/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / 'settlements.json'
        self.sent = []
        self.accept = True
        self.driver = Driver(RUN, self.path, State(self.root / 'state.json'), self.record, review_expires_ms=EXPIRY)
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

    def step(self, shift, flags, now_ms=None, **account):
        now_ms = self.bar(shift)[1] + 1 if now_ms is None else now_ms
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

    def test_unseen_submission_stops_without_retrying_after_deadline(self):
        self.step(1, (0, 1_000_000), last=4)
        now = self.bar(1)[1] + 1 + 20_001
        self.path.write_bytes(seen(now, last=4))
        self.assertEqual(self.driver.tick(now), 'submission_unconfirmed')
        self.assertEqual(self.sent, [('buy', 5, 1)], 'unknown commit must not be retried')
        state = State(self.root / 'state.json')
        self.assertEqual((state.stopped, state.inflight_effect, state.sequence), ('submission_unconfirmed', 5, 1))

    def test_definite_committed_refusal_clears_inflight(self):
        self.accept = False
        self.assertEqual(self.step(1, (0, 1_000_000), last=4), 'refused')
        self.assertEqual(self.driver.state.inflight_effect, 0)

    def test_sell_records_the_exit_for_reentry(self):
        self.step(1, (1_000_000, 0), last=2, base=120_000_000)
        self.assertEqual(self.driver.state.last_exit_s, (self.bar(1)[1] + 1) // 1000)

    def test_stale_observation_advances_indicators_without_orders(self):
        count = self.driver.rule.indicators.count
        self.path.write_bytes(seen(1_000))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(self.bar(1), self.bar(1)[1] + 1), 'stale')
        self.assertEqual((self.sent, self.driver.rule.indicators.count), ([], count + 1))

    def test_stop_is_terminal_and_durable(self):
        self.assertEqual(self.step(1, (0, 0), pnl=-10_000_000), 'drawdown')
        self.assertEqual(self.step(2, (0, 1_000_000)), 'drawdown')
        self.assertEqual(State(self.root / 'state.json').stopped, 'drawdown')
        self.assertEqual(self.sent, [])

    def test_restart_continues_the_sequence(self):
        self.step(1, (0, 1_000_000), last=4)
        again = Driver(RUN, self.path, State(self.root / 'state.json'), self.record, review_expires_ms=EXPIRY)
        again.rule = self.driver.rule
        self.path.write_bytes(seen(self.bar(2)[1] + 1, last=5, base=1))
        again.rule.previous = (1_000_000, 0)
        again.on_bar(self.bar(2), self.bar(2)[1] + 1)
        self.assertEqual(self.sent[-1], ('sell', 6, 2))

    def test_drill_limit_reaches_the_rule(self):
        self.assertEqual(Driver(RUN, self.path, State(self.root / 'other.json'), self.record, 6, review_expires_ms=EXPIRY).rule.max_orders, 6)
        self.assertEqual(self.driver.rule.max_orders, 240)

    def test_extend_keeps_the_sequence_and_clears_only_a_capacity_stop(self):
        config = self.root / 'driver.json'
        config.write_text(json.dumps(dict(state=str(self.root / 'state.json'), max_orders=6, client=['x'], review_expires_ms=EXPIRY)))
        self.driver.state.sequence, self.driver.state.stopped = 6, 'capacity'
        self.driver.state.save()
        self.assertEqual(extend(config, 240), dict(max_orders=240, sequence=6))
        self.assertEqual(json.loads(config.read_text()), dict(state=str(self.root / 'state.json'), max_orders=240, client=['x'], review_expires_ms=EXPIRY))
        self.assertEqual((State(self.root / 'state.json').stopped, State(self.root / 'state.json').sequence), (None, 6))
        for limit in (240, 241, 6, '240'):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                extend(config, limit)
        config.write_text(json.dumps(dict(state=str(self.root / 'state.json'), max_orders=6, review_expires_ms=EXPIRY)))
        for reason in ('drawdown', 'insufficient_balance'):
            state = State(self.root / 'state.json')
            state.stopped = reason
            state.save()
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, 'capacity'):
                extend(config, 240)
        self.assertEqual(json.loads(config.read_text())['max_orders'], 6, 'a refused extension changes nothing')

    def test_duplicate_bar_skipped_and_gap_restarts_the_feed(self):
        self.path.write_bytes(seen(50_000))
        self.assertEqual(self.driver.on_bar(self.row, 50_000), 'duplicate')
        with self.assertRaises(ProtocolError):
            self.driver.on_bar(self.bar(3), self.bar(3)[1] + 1)


class GuardRegressions(Flow):
    def test_bootstrap_backlog_warms_without_requests_then_fresh_bar_can_trade(self):
        now = self.bar(1)[1] + 1 + 3615
        self.path.write_bytes(seen(now))
        count = self.driver.rule.indicators.count
        for shift in (1, 2):
            self.driver.rule.previous = (0, 1_000_000)
            self.assertEqual(self.driver.on_bar(self.bar(shift), now), 'catchup')
        self.assertEqual(self.driver.rule.indicators.count, count + 2)
        self.assertEqual((self.sent, self.driver.state.sequence, self.driver.state.inflight_effect), ([], 0, 0))
        self.assertFalse((self.root / 'state.json').exists(), 'catch-up must not reserve a request')
        captured = []
        self.driver.submit = lambda order, effect, sequence: captured.append(order) or True
        self.driver.rule.previous = (0, 1_000_000)
        fresh = self.bar(3)[1] + 1 + 500
        self.path.write_bytes(seen(fresh))
        self.assertEqual(self.driver.on_bar(self.bar(3), fresh), 'buy')
        self.assertEqual(captured[0].source_ms, self.bar(3)[1] + 1)
        self.assertEqual(captured[0].sign_by_ms, self.bar(3)[1] + 1 + 20_000)

    def test_catchup_never_accepts_future_candle_or_missing_second(self):
        row = self.bar(1)
        self.path.write_bytes(seen(row[1]))
        with self.assertRaisesRegex(ProtocolError, 'candle age'):
            self.driver.on_bar(row, row[1])
        now = self.bar(3)[1] + 1 + 3615
        self.path.write_bytes(seen(now))
        with self.assertRaisesRegex(ProtocolError, 'gap'):
            self.driver.on_bar(self.bar(3), now)
        self.assertEqual(self.sent, [])

    def test_contiguous_stale_candle_never_submits(self):
        row = self.bar(1)
        now = row[1] + 1 + 300_000
        self.path.write_bytes(seen(now))
        self.driver.rule.previous = (0, 1_000_000)
        count = self.driver.rule.indicators.count
        self.assertEqual(self.driver.on_bar(row, now), 'catchup')
        self.assertEqual((self.sent, self.driver.rule.indicators.count), ([], count + 1), 'old data warms the rule but never trades')

    def test_future_candle_never_submits(self):
        row = self.bar(1)
        now = row[1]
        self.path.write_bytes(seen(now))
        self.driver.rule.previous = (0, 1_000_000)
        with self.assertRaisesRegex(ProtocolError, 'candle age'):
            self.driver.on_bar(row, now)
        self.assertEqual(self.sent, [], 'an unclosed future candle cannot trade')

    def test_candle_exactly_at_the_freshness_limit_still_trades(self):
        row = self.bar(1)
        now = row[1] + 1 + 2_000
        self.path.write_bytes(seen(now))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(row, now), 'buy')
        self.driver.rule.previous = (0, 1_000_000)
        later = self.bar(2)[1] + 1 + 2_001
        self.path.write_bytes(seen(later, last=5, pending=5))
        self.assertEqual(self.driver.on_bar(self.bar(2), later), 'catchup')

    def test_source_time_is_not_replaced_by_decision_time(self):
        row = self.bar(1)
        now = row[1] + 1 + 700
        self.path.write_bytes(seen(now))
        captured = []
        self.driver.submit = lambda order, effect, sequence: captured.append(order) or True
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(row, now), 'buy')
        self.assertEqual(captured[0].source_ms, row[1] + 1, 'intent preserves candle close time')
        self.assertEqual(captured[0].sign_by_ms, row[1] + 1 + 20_000,
                         'CLI record time is source time; the 20s lifetime must not be extended by feed delay')

    def test_final_effect_slot_is_reserved_for_exit(self):
        row = self.bar(1)
        self.path.write_bytes(seen(row[1] + 1, last=239))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(row, row[1] + 1), 'capacity')
        self.assertEqual(self.sent, [], 'buy240 must not require an unauthorized sell241')

    def test_no_sell_after_effect_ceiling(self):
        row = self.bar(1)
        self.path.write_bytes(seen(row[1] + 1, last=240, base=120_000_000))
        self.driver.rule.previous = (1_000_000, 0)
        self.assertEqual(self.driver.on_bar(row, row[1] + 1), 'capacity_exposure')
        self.assertEqual(self.sent, [], 'failed exit240 leaves exposure for review, not sell241')

    def test_review_drain_exits_without_an_indicator_signal(self):
        row = self.bar(1)
        now = row[1] + 1
        self.driver.review_expires_ms = now + 420_000
        self.path.write_bytes(seen(now, base=120_000_000))
        self.driver.rule.previous = (0, 0)
        self.assertEqual(self.driver.on_bar(row, now), 'sell')
        self.assertEqual(self.sent, [('sell', 1, 1)])

    def test_review_drain_blocks_new_entries(self):
        row = self.bar(1)
        now = row[1] + 1
        self.driver.review_expires_ms = now + 420_000
        self.path.write_bytes(seen(now))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(row, now), 'review_drained')
        self.assertEqual(self.sent, [])

    def test_review_exit_deadline_does_not_start_late_sell(self):
        row = self.bar(1)
        now = row[1] + 1
        self.driver.review_expires_ms = now + 210_000
        self.path.write_bytes(seen(now, base=120_000_000))
        self.driver.rule.previous = (1_000_000, 0)
        self.assertEqual(self.driver.on_bar(row, now), 'review_exit_deadline_exposure')
        self.assertEqual(State(self.root / 'state.json').stopped, 'review_exit_deadline_exposure')
        self.assertEqual(self.sent, [])


class BoundaryRecovery(Flow):
    def test_candle_age_boundary_is_inclusive(self):
        row = self.bar(1)
        now = row[1] + 1 + 2_000
        self.path.write_bytes(seen(now))
        self.driver.rule.previous = (0, 1_000_000)
        self.assertEqual(self.driver.on_bar(row, now), 'buy')

    def test_six_effect_limit_counts_released_attempts_and_reserves_exit(self):
        self.driver.rule.max_orders = 6
        self.assertEqual(self.step(1, (0, 1_000_000), last=5), 'capacity')
        self.assertEqual(self.sent, [], 'five effects can include released attempts, independent of buy/sell parity')
        self.assertEqual(State(self.root / 'state.json').stopped, 'capacity')

    def test_last_available_effect_exits_and_failed_exit_cannot_exceed_limit(self):
        self.driver.rule.max_orders = 6
        self.assertEqual(self.step(1, (0, 0), last=5, base=120_000_000), 'sell')
        self.assertEqual(self.step(2, (1_000_000, 0), last=6, base=120_000_000), 'capacity_exposure')
        self.assertEqual(self.sent, [('sell', 6, 1)], 'released or failed exit leaves exposure, never effect7')

    def test_pending_exit_waits_past_exit_cutoff_then_stops_flat(self):
        now = self.bar(1)[1] + 1
        self.driver.review_expires_ms = now + 420_000
        self.assertEqual(self.step(1, (0, 0), now_ms=now, base=120_000_000), 'sell')
        now += 210_000
        self.path.write_bytes(seen(now, last=1, pending=1, base=120_000_000))
        self.assertIsNone(self.driver.tick(now), 'already pending exit may reconcile within its authority')
        self.path.write_bytes(seen(now + 1, last=1, base=0))
        self.assertEqual(self.driver.tick(now + 1), 'review_drained')
        self.assertEqual(self.sent, [('sell', 1, 1)])

    def test_flat_drain_is_durable_without_market_data(self):
        now = self.bar(1)[1] + 1
        self.driver.review_expires_ms = now + 420_000
        self.path.write_bytes(seen(now))
        self.assertEqual(self.driver.tick(now), 'review_drained')
        self.assertEqual(State(self.root / 'state.json').stopped, 'review_drained')
        self.assertEqual(self.sent, [])

    def test_hard_expiry_records_pending_without_a_new_candle(self):
        now = self.bar(1)[1] + 1
        self.driver.review_expires_ms = now
        self.path.write_bytes(seen(now, last=2, pending=2, base=120_000_000))
        self.assertEqual(self.driver.tick(now), 'review_expired_pending')
        self.assertEqual(State(self.root / 'state.json').stopped, 'review_expired_pending')
        self.assertEqual(self.sent, [])

    def test_missing_observation_at_deadline_is_unknown_not_flat(self):
        now = self.bar(1)[1] + 1
        self.driver.review_expires_ms = now + 210_000
        self.assertEqual(self.driver.tick(now), 'review_exit_deadline_unknown')
        self.assertEqual(State(self.root / 'state.json').stopped, 'review_exit_deadline_unknown')

    def test_ambiguous_submission_stays_durable_across_restart(self):
        self.accept = None
        self.assertEqual(self.step(1, (0, 1_000_000)), 'submission_unconfirmed')
        again = Driver(RUN, self.path, State(self.root / 'state.json'), self.record, review_expires_ms=EXPIRY)
        self.assertEqual(again.tick(self.bar(1)[1] + 30_000), 'submission_unconfirmed')
        self.assertEqual((again.state.inflight_effect, again.state.sequence), (1, 1))
        self.assertEqual(self.sent, [('buy', 1, 1)])

    def test_submission_exception_preserves_inflight(self):
        def timeout(*args):
            raise subprocess.TimeoutExpired('client', 30)
        self.driver.submit = timeout
        self.assertEqual(self.step(1, (0, 1_000_000)), 'submission_unconfirmed')
        self.assertEqual(State(self.root / 'state.json').inflight_effect, 1)

    def test_main_checks_expiry_when_socket_stalls(self):
        config = self.root / 'driver.json'
        config.write_text(json.dumps(dict(client=['client'], opening_record='opening', kill_epoch=1,
                                         executor_generation=1, incarnation=RUN, observation=str(self.path),
                                         state=str(self.root / 'main-state.json'), max_orders=6, review_expires_ms=1_000_000)))
        self.path.write_bytes(seen(1_000_000, last=1, pending=1, base=120_000_000))
        with patch('sys.argv', ['driver', '--config', str(config)]), \
                patch('velocita_model.shadow.live_swap_driver.time.time_ns', side_effect=iter([500_000_000_000, 500_000_000_000, *([1_000_000_000_000] * 10)])), \
                patch('velocita_model.shadow.live_swap_driver.bootstrap'), \
                patch('velocita_model.shadow.live_swap_driver.ReadOnlyMarketSocket') as socket, \
                patch('velocita_model.shadow.live_swap_driver.time.sleep') as sleep:
            socket.return_value.recv_text.side_effect = TimeoutError('stalled feed')
            self.assertEqual(main(), 0)
            socket.return_value.close.assert_called_once()
            sleep.assert_not_called()
        self.assertEqual(State(self.root / 'main-state.json').stopped, 'review_expired_pending')

    def test_expired_review_cannot_be_extended(self):
        config = self.root / 'driver.json'
        config.write_text(json.dumps(dict(state=str(self.root / 'state.json'), max_orders=6, review_expires_ms=1_000)))
        with self.assertRaisesRegex(ValueError, 'unexpired review'):
            extend(config, 240, now_ms=1_000)
        self.assertEqual(json.loads(config.read_text())['max_orders'], 6)


class SubmitResult(unittest.TestCase):
    def test_only_exact_committed_result_is_definite(self):
        from velocita_model.shadow.live_swap import buy_order
        submit = client_submitter(['client'], 'opening', 1, 1)
        cases = [(0, 'STRATEGY SWAP INTENT side=1 effect=5 amount=12030000 min_receive=110400000 fee_cap=24060\nCOMMITTED sequence=7 index=8 code_name=applied\n', True),
                 (1, 'COMMITTED sequence=7 index=8 code_name=risk_limit\n', False),
                 (3, 'TIMEOUT sequence=7\n', None), (4, 'REFUSED reason=1\n', None),
                 (0, 'COMMITTED sequence=8 index=8 code_name=applied\n', None),
                 (1, 'COMMITTED sequence=7 index=8 code_name=applied\n', None)]
        for code, output, expected in cases:
            with self.subTest(output=output), patch('velocita_model.shadow.live_swap_driver.subprocess.run') as run, \
                    patch('velocita_model.shadow.live_swap_driver.time.time_ns', return_value=1_000_000_000):
                run.return_value = subprocess.CompletedProcess(['client'], code, output, '')
                self.assertIs(submit(buy_order('100', 1_000), 5, 7), expected)

    def test_stale_at_client_send_does_not_launch_a_process(self):
        from velocita_model.shadow.live_swap import buy_order
        submit = client_submitter(['client'], 'opening', 1, 1)
        with patch('velocita_model.shadow.live_swap_driver.time.time_ns', return_value=3_001_000_000), \
                patch('velocita_model.shadow.live_swap_driver.subprocess.run') as run:
            self.assertIs(submit(buy_order('100', 1_000), 5, 7), False)
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
