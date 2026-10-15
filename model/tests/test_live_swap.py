"""Live swap decisions against the independent 2026-10-09 replay and hand-computed order sizes."""
import importlib.util
import math
from pathlib import Path
import sys
import unittest

from velocita_model.shadow.live_swap import Account, LiveRule, Order, Stop, buy_order, sell_order

REPLAY = Path(__file__).resolve().parents[2]/'docs/evidence/stochastic_rule_replay_2026-10-09/replay.py'


def replay_module():
    spec = importlib.util.spec_from_file_location('replay_oracle', REPLAY)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def bars(count, start_s=1_700_000_000):
    price, rows = 100.0, []
    for n in range(count):
        drift = 0.02 * math.sin(n / 700) + 0.006 * math.sin(n / 53)
        noise = ((n * 7919) % 101 - 50) / 5000
        o = price
        price = max(1.0, price + drift + noise)
        h, l = max(o, price) + 0.01, min(o, price) - 0.01
        start = (start_s + n) * 1000
        rows.append((start, start + 999, f'{o:.4f}', f'{h:.4f}', f'{l:.4f}', f'{price:.4f}'))
    return rows


def account(base=0, quote=10**9, pnl=0, orders=0, pending=False):
    return Account(base, quote, pnl, orders, pending)


class Parity(unittest.TestCase):
    def test_entries_and_exits_match_the_replay_oracle(self):
        rows = bars(30_000)
        trips, resets, held = replay_module().replay(rows)
        rule, base, last_exit, entry, live = LiveRule(), 0, None, None, []
        for row in rows:
            stamp = (row[1] + 1) // 1000
            order = rule.bar(*row, account(base=base), row[0], last_exit)
            if isinstance(order, Order) and order.side == 'buy':
                base, entry = 120_000_000, stamp
            elif isinstance(order, Order) and order.side == 'sell':
                live.append((entry, stamp))
                base, last_exit = 0, stamp
        self.assertGreaterEqual(len(trips), 5, 'synthetic series must exercise the rule')
        self.assertEqual(live, [(t[0], t[1]) for t in trips], 'live entries and exits equal the replay')
        self.assertEqual(bool(base), bool(held), 'open position at the end agrees')


class Sizes(unittest.TestCase):
    def test_buy_spends_whole_cents_for_twelve_hundredths_sol(self):
        order = buy_order('105.08', 1_000)
        self.assertEqual(order, Order('buy', 12_640_000, 110_400_000, 25_280, 1_000, 11_000))
        self.assertGreaterEqual((order.amount - order.fee_cap) * 1000, 120_000_000 * 105.08, 'principal buys at least 0.12 SOL')

    def test_sell_sells_the_whole_position_with_floor_and_fee_margin(self):
        self.assertEqual(sell_order(120_000_000, '105.08', 2_000), Order('sell', 120_000_000, 11_853_024, 26_786, 2_000, 12_000))


class Gating(unittest.TestCase):
    def warmed(self, base=0):
        rule, rows = LiveRule(), bars(1_300)
        for row in rows:
            rule.bar(*row, account(base=base, pending=True), row[0], None)
        return rule, rows[-1]

    def next_bar(self, row, shift=1):
        start = row[0] + 1000 * shift
        return (start, start + 999, *row[2:])

    def test_pending_effect_waits(self):
        rule, row = self.warmed()
        rule.previous = (0, 1_000_000)
        self.assertIsNone(rule.bar(*self.next_bar(row), account(pending=True), 0, None))

    def test_entry_buys_when_flat_and_exit_sells_when_held(self):
        rule, row = self.warmed()
        rule.previous = (0, 1_000_000)
        self.assertEqual(rule.bar(*self.next_bar(row), account(), 5, None).side, 'buy')
        rule.previous = (1_000_000, 0)
        sold = rule.bar(*self.next_bar(row, 2), account(base=120_000_000), 6, None)
        self.assertEqual((sold.side, sold.amount), ('sell', 120_000_000))
        rule.previous = (1_000_000, 0)
        self.assertIsNone(rule.bar(*self.next_bar(row, 3), account(), 7, None), 'no exit while flat')
        rule.previous = (0, 1_000_000)
        self.assertIsNone(rule.bar(*self.next_bar(row, 4), account(base=1), 8, None), 'no entry while held')

    def test_reentry_waits_sixty_seconds_after_an_exit(self):
        rule, row = self.warmed()
        bar = self.next_bar(row)
        exited = (bar[1] + 1) // 1000 - 59
        rule.previous = (0, 1_000_000)
        self.assertIsNone(rule.bar(*bar, account(), 0, exited))
        rule.previous = (0, 1_000_000)
        self.assertEqual(rule.bar(*self.next_bar(row, 2), account(), 0, exited).side, 'buy')

    def test_stops_exit_a_position_then_end_the_run(self):
        for change, reason in ((dict(pnl=-10_000_000), 'drawdown'), (dict(orders=240), 'capacity')):
            rule, row = self.warmed()
            self.assertEqual(rule.bar(*self.next_bar(row), account(base=5, **change), 0, None).side, 'sell', reason)
            self.assertEqual(rule.bar(*self.next_bar(row, 2), account(**change), 0, None), Stop(reason))
        rule, row = self.warmed()
        self.assertIsNone(rule.bar(*self.next_bar(row), account(pnl=-9_999_999, orders=239), 0, None), 'just inside both limits')

    def test_flat_run_stops_when_cash_cannot_buy_the_clip(self):
        rule, row = self.warmed()
        self.assertEqual(rule.bar(*self.next_bar(row), account(quote=12_000_000), 0, None), Stop('insufficient_balance'))

    def test_protective_stop_sells_even_before_warmup(self):
        rule = LiveRule()
        row = bars(1)[0]
        self.assertEqual(rule.bar(*row, account(base=7, pnl=-10_000_000), 0, None).side, 'sell')
        self.assertIsNone(LiveRule().bar(*row, account(base=7), 0, None))

    def test_gap_resets_indicators(self):
        rule, row = self.warmed()
        rule.bar(*self.next_bar(row, 5), account(), 0, None)
        self.assertEqual(rule.indicators.count, 1)


if __name__ == '__main__':
    unittest.main()
