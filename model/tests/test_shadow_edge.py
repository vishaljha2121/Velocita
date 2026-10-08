"""Shadow edge: live ticks -> features -> submitter -> decisions and metrics.

Oracles: hand arithmetic for units and latencies; `rules.features` (already
tested) for the feature vector; hand-written program text.
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import sys
import textwrap
import time
import unittest
from datetime import date, timedelta
from decimal import Decimal
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Callable

from velocita_model.research import rules
from velocita_model.research.bars import DailyBars
from velocita_model.shadow import edge

NS = 1_000_000_000


def reference(mid: str, stamp: str = "2026-09-28T12:00:00Z", symbol: str = "BTC|USD") -> str:
    return json.dumps({"type": "reference_prices", "timestamp": 1790600000, "topics": ["reference_prices"],
                       "data": [{"symbol": "ETH|USD", "mid_price": "1", "bid": "1", "ask": "1", "sources": [],
                                 "timestamp": stamp},
                                {"symbol": symbol, "mid_price": mid, "bid": mid, "ask": mid, "sources": [],
                                 "timestamp": stamp}]})


def history(n: int = 200, start: float = 100.0) -> list[tuple[date, float]]:
    return [(date(2026, 1, 1) + timedelta(days=i), start + i) for i in range(n)]


class Units(unittest.TestCase):
    def test_price_ticks_are_exact_and_round_toward_zero(self) -> None:
        self.assertEqual(edge.price_ticks(Decimal("83000.505"), Decimal("0.01")), 8_300_050)
        self.assertEqual(edge.price_ticks(Decimal("1"), Decimal("0.3")), 3)

    def test_rfc3339_to_ns(self) -> None:
        self.assertEqual(edge.rfc3339_ns("2026-09-28T12:00:00.25Z"), 1_790_596_800 * NS + 250_000_000)


class CrossLanguageParity(unittest.TestCase):
    def _enum(self, name: str, header_file: str = "kernel.hpp") -> dict[str, int]:
        import re
        header = (Path(__file__).resolve().parents[2] / "include" / "velocita" / "v2" / header_file).read_text()
        body = header[header.index(f"enum class {name}"):]
        body = body[:body.index("};")]
        values, current = {}, -1
        for line in body.splitlines()[1:]:
            line = line.split("//")[0].strip().rstrip(",")
            match = re.match(r"(\w+)\s*=\s*(\d+)$", line)
            if match:
                current = int(match.group(2))
                values[match.group(1)] = current
            elif re.match(r"^\w+$", line):
                current += 1
                values[line] = current
        return values

    def test_shadow_recorded_matches_the_kernel(self) -> None:
        self.assertEqual(edge.SHADOW_RECORDED, self._enum("DecisionCode")["shadow_recorded"])

    def test_action_matches_the_kernel(self) -> None:
        self.assertEqual(edge.ACTION, self._enum("DecisionCode")["action"])

    def test_proposed_matches_the_resolver(self) -> None:
        values = self._enum("ActionResolutionCode", "action_resolver.hpp")
        self.assertEqual(edge.PROPOSED, values["proposed"])

    def test_directive_names_match_the_kernel(self) -> None:
        self.assertEqual(edge.DIRECTIVES, {v: k for k, v in self._enum("Directive", "contracts.hpp").items()})

    def test_feature_names_follow_the_rule_schema(self) -> None:
        self.assertEqual([edge.FEATURE_NAMES[i] for i in (rules.TREND, rules.RET1, rules.VOL, rules.DEV)],
                         ["trend", "ret1", "vol", "dev"])


class ProgramFile(unittest.TestCase):
    def test_trend_program_text(self) -> None:
        catalog = edge.CatalogRow(root="ab" * 32, account=5, venue=9, instrument=4, instrument_spec_version=7,
                                  venue_capabilities_version=8)
        text = edge.program_text("trend", 125, catalog, policy_id=21, policy_version=1, feature_schema_id=31,
                                 valid_from_ns=1000, valid_until_ns=2000, order_quantity_lots=1,
                                 max_notional_micros=10_000, max_age_ns=5 * NS, source_id=41,
                                 time_in_force="gtc", post_only="required")
        self.assertIn("catalog.root = " + "ab" * 32 + "\n", text)
        self.assertIn("features.generic = 4\n", text)
        self.assertIn("rule.1 = 0 gt 0 enter_long\nrule.2 = 0 le 0 exit\n", text)
        expected = hashlib.sha256(b"velocita.rules.v1 trend 125").hexdigest()
        self.assertIn(f"policy.model_digest = {expected}\n", text)


def candles(rows, ts_s: int) -> str:
    return json.dumps({"type": "price_candles", "timestamp": ts_s, "topics": ["price_candles"],
                       "data": [{"symbol": sym, "chain": chain, "interval": iv, "close": close,
                                 "open": close, "high": close, "low": close, "address": "x", "asset_id": "y"}
                                for sym, chain, iv, close in rows]})


class CandleTicks(unittest.TestCase):
    def source(self) -> edge.FeatureSource:
        return edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"), symbol="SOL",
                                  price_source="candles", price_chain="solana")

    def test_the_spot_30s_close_is_the_tick(self) -> None:
        ts = 1_784_548_800
        raw = candles([("SOL", "hypercore-perp", "30s", "118.445"),
                       ("SOL", "solana", "1h", "117.000"),
                       ("SOL", "solana", "30s", "118.578198444149"),
                       ("HYPE", "solana", "30s", "40.0")], ts)
        tick = self.source().tick(raw, receive_ns=ts * NS + 2 * NS, tick_id=3)
        self.assertIsNotNone(tick)
        self.assertEqual(tick.features[-1], 11_857)
        self.assertEqual(tick.event_ns, ts * NS)
        self.assertEqual(tick.age_ns, 2 * NS)
        closes = [c for _, c in history(200)] + [118.578198444149]
        bars = DailyBars(tuple(date(2026, 1, 1) + timedelta(days=i) for i in range(201)), ("SOL",), (tuple(closes),), None)
        self.assertEqual(tick.features[:4], list(rules.features(bars, 0, 200, rules.trend(125)[1])))

    def test_only_the_traded_chain_counts(self) -> None:
        ts = 1_784_548_800
        f = self.source()
        self.assertIsNone(f.tick(candles([("SOL", "hypercore-perp", "30s", "118.445")], ts), receive_ns=ts * NS, tick_id=1))
        self.assertIsNone(f.tick(candles([("SOL", "solana", "4h", "118.0")], ts), receive_ns=ts * NS, tick_id=2))
        self.assertIsNone(f.tick(reference("118.5", "2026-07-20T12:00:00Z", symbol="SOL"), receive_ns=ts * NS, tick_id=3))

    def test_a_restamped_message_is_a_resend(self) -> None:
        ts = 1_784_548_800
        f = self.source()
        self.assertIsNotNone(f.tick(candles([("SOL", "solana", "30s", "118.5")], ts), receive_ns=ts * NS, tick_id=1))
        self.assertIsNone(f.tick(candles([("SOL", "solana", "30s", "118.6")], ts), receive_ns=ts * NS + 1, tick_id=2))
        self.assertEqual(f.last_outcome, "resend")

    def test_candles_need_a_chain(self) -> None:
        with self.assertRaises(ValueError):
            edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"), symbol="SOL",
                               price_source="candles")


class Ticks(unittest.TestCase):
    def test_tick_from_a_reference_message(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        raw = reference("300.00", "2026-07-20T12:00:00Z")
        tick = f.tick(raw, receive_ns=1_784_548_800 * NS + 3 * NS, tick_id=7)
        self.assertIsNotNone(tick)
        closes = [c for _, c in history(200)] + [300.0]
        bars = DailyBars(tuple(date(2026, 1, 1) + timedelta(days=i) for i in range(201)), ("BTC",), (tuple(closes),), None)
        expected = rules.features(bars, 0, 200, rules.trend(125)[1])
        self.assertEqual(tick.features[:4], list(expected))
        self.assertEqual(tick.features[4], 30_000)
        self.assertEqual(tick.evidence, hashlib.sha256(raw.encode()).hexdigest())
        self.assertEqual(tick.event_ns, 1_784_548_800 * NS)
        self.assertEqual(tick.age_ns, 3 * NS)
        self.assertEqual(tick.expected_rule, rules.first_match(rules.trend(125)[0], expected))
        self.assertEqual(tick.line(), f"TICK id=7 event_ns={1_784_548_800 * NS} receive_ns={1_784_548_800 * NS + 3 * NS} "
                                      f"age_ns={3 * NS} features={','.join(map(str, tick.features))} evidence={tick.evidence}")

    def test_messages_without_the_symbol_give_no_tick(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        self.assertIsNone(f.tick(reference("1", symbol="SOL|USD"), receive_ns=0, tick_id=1))
        self.assertIsNone(f.tick(json.dumps({"type": "price_candles", "data": []}), receive_ns=0, tick_id=1))

    def test_clock_skew_never_gives_a_negative_age(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        tick = f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=1_784_548_800 * NS - 2 * NS, tick_id=1)
        self.assertEqual(tick.age_ns, 0)

    def test_stale_history_is_refused_not_guessed(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        self.assertIsNone(f.tick(reference("300", "2026-07-21T12:00:00Z"), receive_ns=0, tick_id=1))

    def test_only_a_new_observation_is_a_tick(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        at = 1_784_548_800 * NS
        first = f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=at, tick_id=1)
        self.assertIsNotNone(first)
        self.assertIsNone(f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=at + NS, tick_id=2))
        self.assertIsNone(f.tick(reference("301", "2026-07-20T12:00:00Z"), receive_ns=at + 2 * NS, tick_id=3))
        self.assertIsNone(f.tick(reference("299", "2026-07-20T11:59:59Z"), receive_ns=at + 3 * NS, tick_id=4))
        newer = f.tick(reference("302", "2026-07-20T12:00:12Z"), receive_ns=at + 12 * NS + NS // 4, tick_id=5)
        self.assertIsNotNone(newer)
        self.assertEqual((newer.features[4], newer.age_ns), (30_200, NS // 4))
        self.assertIsNone(f.tick(reference("1", "2026-07-20T12:00:30Z", symbol="SOL|USD"), receive_ns=0, tick_id=6))
        self.assertIsNotNone(f.tick(reference("303", "2026-07-20T12:00:24Z"), receive_ns=at + 24 * NS, tick_id=7))

    def test_every_message_has_an_outcome(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        at = 1_784_548_800 * NS
        cases = [(reference("300", "2026-07-20T12:00:00Z"), "tick"),
                 (reference("300", "2026-07-20T12:00:00Z"), "resend"),
                 (reference("1", symbol="SOL|USD"), "other"),
                 (json.dumps({"type": "price_candles", "data": []}), "other"),
                 ("not json", "other"),
                 (reference("300", "2026-07-22T12:00:00Z"), "no_history")]
        for i, (raw, outcome) in enumerate(cases):
            f.tick(raw, receive_ns=at, tick_id=i)
            self.assertEqual(f.last_outcome, outcome, raw[:60])

    def test_a_minimum_interval_forwards_at_most_one_price_per_interval(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"),
                               min_interval_ns=30 * NS)
        at = 1_784_548_800 * NS
        self.assertIsNotNone(f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=at, tick_id=1))
        self.assertIsNone(f.tick(reference("301", "2026-07-20T12:00:10Z"), receive_ns=at + 10 * NS, tick_id=2))
        self.assertEqual(f.last_outcome, "throttled")
        resend = f.tick(reference("301", "2026-07-20T12:00:10Z"), receive_ns=at + 30 * NS, tick_id=3)
        self.assertIsNotNone(resend)
        self.assertEqual(resend.features[4], 30_100)
        self.assertIsNone(f.tick(reference("302", "2026-07-20T12:00:40Z"), receive_ns=at + 59 * NS, tick_id=4))
        self.assertIsNotNone(f.tick(reference("302", "2026-07-20T12:00:40Z"), receive_ns=at + 60 * NS, tick_id=5))

    def test_short_history_gives_no_tick(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(50), usd_per_tick=Decimal("0.01"))
        self.assertIsNone(f.tick(reference("300"), receive_ns=0, tick_id=1))


class Decisions(unittest.TestCase):
    LINE = ("DECISION id=7 source_seq=3 decision=4294967299 code=0 decision_code=14 rule=0 proposed=2 effective=1 "
            "from=2 t_read_ns=1000500000 t_source_ns=1004000000 t_decision_ns=1009000000")

    def test_decision_line_and_metrics(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        d = edge.parse_decision(self.LINE)
        m.observe(d, tick_receive_ns=1_000_000_000, expected_rule=0)
        text = m.render(now_ns=1_010_000_000)
        self.assertIn("velocita_shadow_tick_to_decision_ms_sum 9.000\n", text)
        self.assertIn('velocita_shadow_hop_ms_sum{hop="edge"} 0.500\n', text)
        self.assertIn('velocita_shadow_hop_ms_sum{hop="source_commit"} 3.500\n', text)
        self.assertIn('velocita_shadow_hop_ms_sum{hop="decision_commit"} 5.000\n', text)
        self.assertIn('velocita_shadow_decisions_total{decision_code="14"} 1\n', text)
        self.assertIn('velocita_shadow_rule_matches_total{rule="0"} 1\n', text)
        self.assertIn('velocita_shadow_agreement_total{result="agree"} 1\n', text)
        self.assertIn('velocita_shadow_tick_to_decision_ms_bucket{le="10"} 1\n', text)
        self.assertIn('velocita_shadow_tick_to_decision_ms_bucket{le="5"} 0\n', text)

    def test_proposed_and_effective_directives_are_counted(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        m.observe(edge.parse_decision(self.LINE), tick_receive_ns=1_000_000_000, expected_rule=0)
        m.observe(edge.parse_decision(self.LINE.replace("effective=1", "effective=9")), tick_receive_ns=1_000_000_000,
                  expected_rule=0)
        text = m.render(now_ns=0)
        self.assertIn('velocita_shadow_directives_total{stage="proposed",directive="place_limit"} 2\n', text)
        self.assertIn('velocita_shadow_directives_total{stage="effective",directive="no_action"} 1\n', text)
        self.assertIn('velocita_shadow_directives_total{stage="effective",directive="code_9"} 1\n', text)

    def test_resolution_and_market_data_age_are_reported(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        m.observe(edge.parse_decision(self.LINE + " resolution=4"), tick_receive_ns=1_000_000_000, expected_rule=0,
                  age_ns=6_500_000_000)
        text = m.render(now_ns=0)
        self.assertIn('velocita_shadow_action_resolutions_total{resolution="4"} 1\n', text)
        self.assertIn("velocita_shadow_market_data_age_ms 6500.000\n", text)

    def test_a_disagreement_is_counted_not_hidden(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        m.observe(edge.parse_decision(self.LINE), tick_receive_ns=1_000_000_000, expected_rule=1)
        self.assertIn('velocita_shadow_agreement_total{result="disagree"} 1\n', m.render(now_ns=0))

    def test_no_match_is_the_evaluator_sentinel(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        line = self.LINE.replace("rule=0", "rule=65535")
        m.observe(edge.parse_decision(line), tick_receive_ns=1_000_000_000, expected_rule=None)
        self.assertIn('velocita_shadow_agreement_total{result="agree"} 1\n', m.render(now_ns=0))


class PaperDecisions(unittest.TestCase):
    LINE = ("DECISION id=7 source_seq=3 decision=4294967299 code=0 decision_code=1 rule=0 proposed=2 effective=2 "
            "resolution=1 from=2 t_read_ns=1000500000 t_source_ns=1004000000 t_decision_ns=1009000000 "
            "risk_version=2 position_version=5 authorization=applied")

    def test_the_authorization_name_is_kept_as_text(self) -> None:
        d = edge.parse_decision(self.LINE)
        self.assertEqual((d["authorization"], d["risk_version"], d["position_version"]), ("applied", 2, 5))

    def test_authorizations_are_counted_by_code_and_none_is_not_one(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=17)
        for line in (self.LINE, self.LINE.replace("=applied", "=order_slot_occupied"),
                     self.LINE.replace("=applied", "=order_slot_occupied"), self.LINE.replace("=applied", "=none")):
            m.observe(edge.parse_decision(line), tick_receive_ns=1_000_000_000, expected_rule=0)
        text = m.render(now_ns=0)
        self.assertIn('velocita_paper_authorizations_total{code="applied"} 1\n', text)
        self.assertIn('velocita_paper_authorizations_total{code="order_slot_occupied"} 2\n', text)
        self.assertNotIn('code="none"', text)
        self.assertIn("velocita_paper_risk_version 2\n", text)
        self.assertIn("velocita_paper_position_version 5\n", text)
        self.assertIn("velocita_paper_resyncs_total 0\n", text)

    def test_a_resync_is_counted_with_the_reported_position(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=17)
        line = self.LINE.replace("=applied", "=stale_risk_state") + " resync=1 position_lots=25"
        m.observe(edge.parse_decision(line), tick_receive_ns=1_000_000_000, expected_rule=0)
        text = m.render(now_ns=0)
        self.assertIn("velocita_paper_resyncs_total 1\n", text)
        self.assertIn("velocita_paper_position_lots 25\n", text)
        self.assertIn('velocita_paper_authorizations_total{code="stale_risk_state"} 1\n', text)

    def test_a_shadow_line_exports_no_paper_series(self) -> None:
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        m.observe(edge.parse_decision(Decisions.LINE), tick_receive_ns=1_000_000_000, expected_rule=0)
        self.assertNotIn("velocita_paper_", m.render(now_ns=0))


class CommandLine(unittest.TestCase):
    ARGS = ["shadow-edge", "--usd-per-tick", "1", "--catalog-root", "ab" * 32, "--catalog-row", "1,20,1,1,1",
            "--max-notional-micros", "25000000"]

    def test_the_book_defaults_to_the_owners_25_dollar_budget(self) -> None:
        from velocita_model import cli
        args = cli.build_parser().parse_args(self.ARGS + ["--program-out", "/dev/null"])
        self.assertEqual(Decimal(args.book_usd), Decimal(25))

    def test_program_only_writes_the_program_and_starts_nothing(self) -> None:
        from velocita_model import cli
        with TemporaryDirectory() as d:
            out = Path(d) / "trend.program"
            args = cli.build_parser().parse_args(self.ARGS + ["--program-out", str(out), "--program-only",
                                                              "--order-quantity-lots", "25"])
            self.assertEqual(args.handler(args), 0)
            text = out.read_text()
            self.assertIn("order.quantity_lots = 25\n", text)
            self.assertIn("order.max_notional_micros = 25000000\n", text)


FAKE_SUBMITTER = textwrap.dedent('''
    import sys
    print("READY generation=1 first_sequence=1 kill_epoch=1 executor_generation=1 strategy_mode=1", flush=True)
    seq = 0
    for line in sys.stdin:
        if not line.startswith("TICK"):
            continue
        seq += 1
        f = dict(p.split("=", 1) for p in line.split()[1:])
        trend = int(f["features"].split(",")[0])
        rule = 0 if trend > 0 else 1
        print(f"DECISION id={f['id']} source_seq={seq} decision={seq} code=0 decision_code=14 rule={rule} "
              f"proposed=2 effective=1 from=1 t_read_ns={int(f['receive_ns']) + 1000} "
              f"t_source_ns={int(f['receive_ns']) + 2000000} t_decision_ns={int(f['receive_ns']) + 5000000}", flush=True)
''')


class EndToEndWithAFakeSubmitter(unittest.TestCase):
    def test_a_quiet_message_still_refreshes_the_metrics_file(self) -> None:
        with TemporaryDirectory() as d:
            fake = Path(d) / "fake_submitter.py"
            fake.write_text(FAKE_SUBMITTER)
            textfile = Path(d) / "shadow.prom"
            seen = []

            class Quiet:
                feature_names = ("flag",)
                usd_per_tick = Decimal("0.01")
                last_outcome = "quiet"
                def tick(self, raw, *, receive_ns, tick_id):
                    self.last_outcome = raw
                    return None

            messages = ["duplicate", "quiet", None]

            class Socket:
                def open(self): ...
                def subscribe(self, topics): ...
                def recv_text(self):
                    seen.append(textfile.read_text().count('outcome="quiet"') if textfile.exists() else -1)
                    return messages.pop(0)
                def close(self): ...

            edge.run_once(lambda: Socket(), [sys.executable, str(fake)], Quiet(), textfile=str(textfile),
                          shadow_recorded_code=14, clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__)
            self.assertEqual(seen, [0, 0, 1])

    def test_messages_become_decisions_and_metrics(self) -> None:
        with TemporaryDirectory() as d:
            fake = Path(d) / "fake_submitter.py"
            fake.write_text(FAKE_SUBMITTER)
            messages = [reference("400.0", "2026-07-20T12:00:0%dZ" % i) for i in range(3)]
            messages = messages[:1] + [messages[0], json.dumps({"type": "trending"})] + messages[1:] + [None]

            class Socket:
                def open(self): ...
                def subscribe(self, topics): self.topics = topics
                def recv_text(self): return messages.pop(0)
                def close(self): ...

            textfile = Path(d) / "shadow.prom"
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            result = edge.run_once(lambda: Socket(), [sys.executable, str(fake)], source, textfile=str(textfile),
                                   shadow_recorded_code=14, clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__)
            text = textfile.read_text()
            self.assertEqual(result.decisions, 3)
            self.assertIn('velocita_shadow_decisions_total{decision_code="14"} 3\n', text)
            self.assertIn('velocita_shadow_agreement_total{result="agree"} 3\n', text)
            self.assertIn("velocita_shadow_submitter_ready 0\n", text)
            self.assertIn("velocita_shadow_submitter_starts_total 1\n", text)
            self.assertIn('velocita_shadow_messages_total{outcome="tick"} 3\n', text)
            self.assertIn('velocita_shadow_messages_total{outcome="resend"} 1\n', text)
            self.assertIn('velocita_shadow_messages_total{outcome="other"} 1\n', text)
            self.assertIn("velocita_shadow_price_usd 400.00\n", text)
            closes = [c for _, c in history(200)] + [400.0]
            bars = DailyBars(tuple(date(2026, 1, 1) + timedelta(days=i) for i in range(201)), ("BTC",),
                             (tuple(closes),), None)
            expected = rules.features(bars, 0, 200, rules.trend(125)[1])
            self.assertIn(f'velocita_shadow_feature_micros{{name="trend"}} {expected[0]}\n', text)
            self.assertIn(f'velocita_shadow_feature_micros{{name="dev"}} {expected[3]}\n', text)


LEDGER_SUBMITTER = textwrap.dedent('''
    import sys
    print("READY generation=1 first_sequence=1 kill_epoch=1 executor_generation=1 strategy_mode=1", flush=True)
    seq = 0
    for line in sys.stdin:
        if not line.startswith("TICK"):
            continue
        seq += 1
        f = dict(p.split("=", 1) for p in line.split()[1:])
        # Decision 2 is an exit the kernel could not size (market data stale).
        rule, resolution = (1, 19) if seq == 2 else (0, 1)
        print(f"DECISION id={f['id']} source_seq={seq} decision={seq} code=0 decision_code=14 rule={rule} "
              f"proposed=2 effective=1 from=1 resolution={resolution} t_read_ns={int(f['receive_ns']) + 1000} "
              f"t_source_ns={int(f['receive_ns']) + 2000000} t_decision_ns={int(f['receive_ns']) + 5000000}", flush=True)
''')


PAPER_SUBMITTER = textwrap.dedent('''
    import sys
    print("READY generation=1 first_sequence=1 mode=paper caller_id=21", flush=True)
    seq = 0
    names = ["applied", "order_slot_occupied", "none"]
    for line in sys.stdin:
        if not line.startswith("TICK"):
            continue
        seq += 1
        f = dict(p.split("=", 1) for p in line.split()[1:])
        print(f"DECISION id={f['id']} source_seq={seq} decision={seq} code=0 decision_code=1 rule=0 "
              f"proposed=2 effective=2 from=1 resolution=1 t_read_ns={int(f['receive_ns']) + 1000} "
              f"t_source_ns={int(f['receive_ns']) + 2000000} t_decision_ns={int(f['receive_ns']) + 5000000} "
              f"risk_version=1 position_version=1 authorization={names[seq - 1]}", flush=True)
''')


class PaperEndToEnd(unittest.TestCase):
    def test_paper_decisions_move_the_book_and_count_authorizations(self) -> None:
        from velocita_model.research.rules import Costs
        from velocita_model.shadow import ledger as ledger_module
        with TemporaryDirectory() as d:
            fake = Path(d) / "paper_submitter.py"
            fake.write_text(PAPER_SUBMITTER)
            messages = [reference(p, "2026-07-20T12:00:0%dZ" % i) for i, p in enumerate(("400", "410", "420"))] + [None]

            class Socket:
                def open(self): ...
                def subscribe(self, topics): self.topics = topics
                def recv_text(self): return messages.pop(0)
                def close(self): ...

            textfile = Path(d) / "shadow.prom"
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            book = ledger_module.Ledger(Costs(buy_bps=100, sell_bps=100), Decimal(25))
            edge.run_once(lambda: Socket(), [sys.executable, str(fake)], source, textfile=str(textfile),
                          shadow_recorded_code=17, clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__,
                          ledger=book)
            self.assertEqual(book.books["tick"].trades, 1)
            self.assertEqual(book.books["tick"].fills[0].price, Decimal("400.00"))
            text = textfile.read_text()
            self.assertIn('velocita_paper_authorizations_total{code="applied"} 1\n', text)
            self.assertIn('velocita_paper_authorizations_total{code="order_slot_occupied"} 1\n', text)
            self.assertIn("velocita_paper_mode 1\n", text)


class LedgerFromCommittedDecisions(unittest.TestCase):
    def test_only_proposed_decisions_move_the_book(self) -> None:
        from velocita_model.research.rules import Costs
        from velocita_model.shadow import ledger as ledger_module
        with TemporaryDirectory() as d:
            fake = Path(d) / "ledger_submitter.py"
            fake.write_text(LEDGER_SUBMITTER)
            messages = [reference(p, "2026-07-20T12:00:0%dZ" % i) for i, p in enumerate(("400", "410", "420"))] + [None]

            class Socket:
                def open(self): ...
                def subscribe(self, topics): self.topics = topics
                def recv_text(self): return messages.pop(0)
                def close(self): ...

            textfile = Path(d) / "shadow.prom"
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            book = ledger_module.Ledger(Costs(buy_bps=100, sell_bps=100), Decimal(1000))
            edge.run_once(lambda: Socket(), [sys.executable, str(fake)], source, textfile=str(textfile),
                          shadow_recorded_code=14, clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__,
                          ledger=book)
            self.assertEqual(book.books["tick"].trades, 1)
            self.assertEqual(book.books["tick"].fills[0].price, Decimal("400.00"))
            text = textfile.read_text()
            self.assertIn('velocita_shadow_book_trades_total{timing="tick"} 1\n', text)
            self.assertIn('velocita_shadow_book_position{timing="tick"} 1\n', text)


class Backoff(unittest.TestCase):
    def test_the_delay_doubles_from_one_second_and_is_capped(self) -> None:
        top = [edge.backoff_delay(n, lambda: 1.0) for n in range(1, 9)]
        self.assertEqual(top, [1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 60.0, 60.0])
        self.assertEqual(edge.backoff_delay(10_000, lambda: 1.0), 60.0)

    def test_jitter_never_waits_less_than_half_the_step(self) -> None:
        self.assertEqual([edge.backoff_delay(n, lambda: 0.0) for n in (1, 3, 7)], [0.5, 2.0, 30.0])
        self.assertEqual(edge.backoff_delay(2, lambda: 0.5), 1.5)


class SubmitterErrorLabels(unittest.TestCase):
    def test_reason_and_code_are_taken_from_the_reply_and_bounded(self) -> None:
        metrics = edge.EdgeMetrics(shadow_recorded_code=17)
        for line in ("ERROR id=1 reason=source code=14", "ERROR id=2 reason=source code=14",
                     "ERROR id=3 reason=bad_tick", "ERROR id=4 reason=decision",
                     'ERROR id=5 reason=x"} 1 code=14', "ERROR id=6 reason=source code=123456789",
                     "something unexpected"):
            metrics.observe_submitter_error(line)
        self.assertEqual(metrics.errors, 7)
        text = metrics.render(now_ns=0)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="source",code="14"} 2\n', text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="bad_tick",code="none"} 1\n', text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="decision",code="none"} 1\n', text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="other",code="14"} 1\n', text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="source",code="none"} 1\n', text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="other",code="none"} 1\n', text)
        self.assertIn("velocita_shadow_errors_total 7\n", text)


def _scripted(connections: list) -> Callable[[], object]:
    script = connections

    class Socket:
        def __init__(self) -> None:
            self.plan = script.pop(0)

        def open(self) -> None:
            if isinstance(self.plan, BaseException):
                raise self.plan

        def subscribe(self, topics) -> None: ...

        def recv_text(self):
            if not self.plan:
                return None
            item = self.plan.pop(0)
            if isinstance(item, BaseException):
                raise item
            return item() if callable(item) else item

        def close(self) -> None: ...

    return Socket


class MarketDataOutages(unittest.TestCase):
    def test_refusals_and_drops_reconnect_with_one_submitter_and_bounded_backoff(self) -> None:
        from velocita_model.adapters.websocket import HandshakeRefused, ProtocolError
        with TemporaryDirectory() as d:
            fake = Path(d) / "fake_submitter.py"
            fake.write_text(FAKE_SUBMITTER)
            textfile = Path(d) / "shadow.prom"
            seen_connected: list[bool] = []

            def while_connected() -> str:
                seen_connected.append("velocita_shadow_market_data_connected 1\n" in textfile.read_text())
                return reference("401.0", "2026-07-20T12:00:02Z")

            connections = [HandshakeRefused(503, "handshake refused: 'HTTP/1.1 503 Service Unavailable'"),
                           HandshakeRefused(503, "handshake refused: 'HTTP/1.1 503 Service Unavailable'"),
                           ConnectionResetError("reset by peer"),
                           [reference("400.0", "2026-07-20T12:00:01Z"), TimeoutError("timed out")],
                           ProtocolError("frame exceeds the configured bound"),
                           [while_connected],
                           [reference("402.0", "2026-07-20T12:00:03Z")]]
            sleeps: list[float] = []
            stderr = io.StringIO()
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            with contextlib.redirect_stderr(stderr):
                result = edge.run_once(_scripted(connections), [sys.executable, str(fake)], source,
                                       textfile=str(textfile), shadow_recorded_code=14,
                                       clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__,
                                       reconnect=lambda: bool(connections), sleep=sleeps.append,
                                       jitter=lambda: 1.0)
            text = textfile.read_text()
            self.assertEqual(result.decisions, 3)
            self.assertIn("velocita_shadow_submitter_starts_total 1\n", text)
            self.assertIn('velocita_shadow_decisions_total{decision_code="14"} 3\n', text)
            self.assertIn('velocita_shadow_market_data_errors_total{kind="http_5xx"} 2\n', text)
            self.assertIn('velocita_shadow_market_data_errors_total{kind="connection"} 1\n', text)
            self.assertIn('velocita_shadow_market_data_errors_total{kind="timeout"} 1\n', text)
            self.assertIn('velocita_shadow_market_data_errors_total{kind="protocol"} 1\n', text)
            self.assertIn('velocita_shadow_market_data_errors_total{kind="closed"} 1\n', text)
            self.assertIn("velocita_shadow_market_data_connected 0\n", text)
            self.assertEqual(seen_connected, [True])
            self.assertEqual(sleeps, [1.0, 2.0, 4.0, 1.0, 2.0, 1.0])
            self.assertEqual(result.reason, "market data ended")
            self.assertIn("HTTP/1.1 503 Service Unavailable", stderr.getvalue())

    def test_without_reconnect_a_refused_handshake_ends_the_run_without_raising(self) -> None:
        from velocita_model.adapters.websocket import HandshakeRefused
        with TemporaryDirectory() as d:
            fake = Path(d) / "fake_submitter.py"
            fake.write_text(FAKE_SUBMITTER)
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            with contextlib.redirect_stderr(io.StringIO()):
                result = edge.run_once(_scripted([HandshakeRefused(503, "handshake refused")]),
                                       [sys.executable, str(fake)], source, textfile=None, shadow_recorded_code=14)
            self.assertEqual(result.decisions, 0)
            self.assertIn("http_5xx", result.reason)


EXITING_SUBMITTER = textwrap.dedent('''
    import sys
    print("READY generation=400 first_sequence=1 mode=shadow caller_id=21", flush=True)
    for line in sys.stdin:
        if line.startswith("TICK"):
            f = dict(p.split("=", 1) for p in line.split()[1:])
            print(f"ERROR id={f['id']} reason=source code=14", flush=True)
            sys.exit(3)
''')

DEAF_SUBMITTER = textwrap.dedent('''
    import os, time
    print("READY generation=1 first_sequence=1 mode=shadow caller_id=21", flush=True)
    os.close(0)
    time.sleep(2)
''')


class SubmitterEnds(unittest.TestCase):
    def _run(self, script: str, messages: list) -> tuple[edge.RunResult, str, str]:
        with TemporaryDirectory() as d:
            fake = Path(d) / "submitter.py"
            fake.write_text(script)
            textfile = Path(d) / "shadow.prom"
            source = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                result = edge.run_once(_scripted([messages]), [sys.executable, str(fake)], source,
                                       textfile=str(textfile), shadow_recorded_code=14,
                                       clock_ns=iter(range(10**18, 10**18 + 10**9, 10**6)).__next__)
            return result, textfile.read_text(), stderr.getvalue()

    def test_an_exited_submitter_is_not_written_to_and_its_reply_is_logged(self) -> None:
        def after_exit() -> str:
            time.sleep(0.5)
            return reference("401.0", "2026-07-20T12:00:02Z")

        result, text, stderr = self._run(EXITING_SUBMITTER, [reference("400.0", "2026-07-20T12:00:01Z"),
                                                             after_exit, reference("402.0", "2026-07-20T12:00:03Z")])
        self.assertEqual(result.reason, "submitter exited with code 3")
        self.assertIn("velocita_shadow_ticks_total 1\n", text)
        self.assertIn('velocita_shadow_submitter_errors_total{reason="source",code="14"} 1\n', text)
        self.assertIn("ERROR id=1 reason=source code=14\n", stderr)

    def test_a_closed_submitter_input_ends_the_run_instead_of_raising(self) -> None:
        def after_close() -> str:
            time.sleep(0.5)
            return reference("400.0", "2026-07-20T12:00:01Z")

        result, text, _ = self._run(DEAF_SUBMITTER, [after_close])
        self.assertTrue(result.reason.startswith("submitter closed its input"), result.reason)
        self.assertIn("velocita_shadow_ticks_total 0\n", text)


class _Stop(BaseException):
    pass


class EdgeCommandSurvivesOutages(unittest.TestCase):
    def test_the_command_keeps_running_and_keeps_its_submitter(self) -> None:
        from datetime import datetime, timezone
        from unittest import mock

        from velocita_model import cli
        from velocita_model.adapters import websocket
        from velocita_model.research import bars as bars_module
        now = datetime.now(timezone.utc)
        today = now.date()
        days = [today - timedelta(days=200 - i) for i in range(200)]
        loads: list[int] = []

        def load(*_args, **_kwargs):
            loads.append(1)
            if len(loads) == 1:
                raise OSError("history host unreachable")
            return DailyBars(tuple(days), ("X",), (tuple(100.0 + i for i in range(200)),), None)

        stamp = lambda s: now.replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%S") + f".{s:06d}Z"
        refused = websocket.HandshakeRefused(503, "handshake refused: 'HTTP/1.1 503 Service Unavailable'")
        plans = [refused, refused, [reference("400.0", stamp(1)), reference("401.0", stamp(2))]]

        class Market:
            def __init__(self, host: str, path: str) -> None:
                if not plans:
                    raise _Stop()
                self.plan = plans.pop(0)

            def open(self) -> None:
                if isinstance(self.plan, BaseException):
                    raise self.plan

            def subscribe(self, topics) -> None: ...
            def recv_text(self): return self.plan.pop(0) if self.plan else None
            def close(self) -> None: ...

        with TemporaryDirectory() as d:
            fake = Path(d) / "fake_submitter.py"
            fake.write_text(FAKE_SUBMITTER)
            textfile = Path(d) / "shadow.prom"
            args = cli.build_parser().parse_args(CommandLine.ARGS + [
                "--program-out", str(Path(d) / "trend.program"), "--textfile", str(textfile),
                "--history-cache", d, "--", sys.executable, str(fake)])
            sleeps: list[float] = []
            with mock.patch.object(bars_module, "load_binance_daily_api", load), \
                    mock.patch.object(websocket, "ReadOnlyMarketSocket", Market), \
                    mock.patch("time.sleep", sleeps.append), contextlib.redirect_stderr(io.StringIO()) as err, \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(_Stop):
                    args.handler(args)
            text = textfile.read_text()
        self.assertEqual(len(loads), 2, "the failed history fetch was not retried")
        self.assertIn("history host unreachable", err.getvalue())
        self.assertIn("velocita_shadow_submitter_starts_total 1\n", text)
        self.assertIn('velocita_shadow_decisions_total{decision_code="14"} 2\n', text)
        self.assertIn('velocita_shadow_market_data_errors_total{kind="http_5xx"} 2\n', text)
        self.assertIn('velocita_shadow_market_data_errors_total{kind="closed"} 1\n', text)
        backoffs = [s for s in sleeps if s >= 0.5]
        self.assertTrue(len(backoffs) == 4 and all(s <= 60 for s in backoffs), backoffs)


if __name__ == "__main__":
    unittest.main()
