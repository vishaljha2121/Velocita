"""Shadow edge: live ticks -> features -> submitter -> decisions and metrics.

Oracles: hand arithmetic for units and latencies; `rules.features` (already
tested) for the feature vector; hand-written program text.
"""

from __future__ import annotations

import hashlib
import json
import sys
import textwrap
import unittest
from datetime import date, timedelta
from decimal import Decimal
from pathlib import Path
from tempfile import TemporaryDirectory

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
        # 83000.505 USD at 0.01 USD per tick = 8300050.5 ticks -> 8300050
        self.assertEqual(edge.price_ticks(Decimal("83000.505"), Decimal("0.01")), 8_300_050)
        self.assertEqual(edge.price_ticks(Decimal("1"), Decimal("0.3")), 3)

    def test_rfc3339_to_ns(self) -> None:
        self.assertEqual(edge.rfc3339_ns("2026-09-28T12:00:00.25Z"), 1_790_596_800 * NS + 250_000_000)


class CrossLanguageParity(unittest.TestCase):
    """Constants copied from C++ must match the header they came from."""

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
        # trend(125): feature 0 (trend) > 0 -> enter_long; <= 0 -> exit
        self.assertIn("rule.1 = 0 gt 0 enter_long\nrule.2 = 0 le 0 exit\n", text)
        # The model digest names the exact research program (strategy and parameter).
        expected = hashlib.sha256(b"velocita.rules.v1 trend 125").hexdigest()
        self.assertIn(f"policy.model_digest = {expected}\n", text)


class Ticks(unittest.TestCase):
    def test_tick_from_a_reference_message(self) -> None:
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        raw = reference("300.00", "2026-07-20T12:00:00Z")  # the day after the last close (2026-07-19)
        tick = f.tick(raw, receive_ns=1_784_548_800 * NS + 3 * NS, tick_id=7)
        self.assertIsNotNone(tick)
        # Oracle: the tested research feature function on history + today's provisional close.
        closes = [c for _, c in history(200)] + [300.0]
        bars = DailyBars(tuple(date(2026, 1, 1) + timedelta(days=i) for i in range(201)), ("BTC",), (tuple(closes),), None)
        expected = rules.features(bars, 0, 200, rules.trend(125)[1])
        self.assertEqual(tick.features[:4], list(expected))
        self.assertEqual(tick.features[4], 30_000)                                 # 300.00 / 0.01
        self.assertEqual(tick.evidence, hashlib.sha256(raw.encode()).hexdigest())  # raw message digest
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
        # Our clock 2 s behind the source's stamp: the frame's market-data age is 0, not -2 s.
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        tick = f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=1_784_548_800 * NS - 2 * NS, tick_id=1)
        self.assertEqual(tick.age_ns, 0)

    def test_stale_history_is_refused_not_guessed(self) -> None:
        # Last close 2026-07-19; a tick on 2026-07-21 would count the wrong days in every window.
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"))
        self.assertIsNone(f.tick(reference("300", "2026-07-21T12:00:00Z"), receive_ns=0, tick_id=1))

    def test_only_a_new_observation_is_a_tick(self) -> None:
        # True Markets re-sends the last reference price every second and refreshes
        # it about every 12 s (measured 2026-09-28): a resend is not a new source
        # event, and an older stamp arriving late is not one either.
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
        # A message that yields no tick (other symbol) does not move the high-water stamp.
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
                 (reference("300", "2026-07-22T12:00:00Z"), "no_history")]  # history ends 2026-07-19
        for i, (raw, outcome) in enumerate(cases):
            f.tick(raw, receive_ns=at, tick_id=i)
            self.assertEqual(f.last_outcome, outcome, raw[:60])

    def test_a_minimum_interval_forwards_at_most_one_price_per_interval(self) -> None:
        # Each forwarded tick costs two retained Raft log entries, and nothing
        # reclaims the log yet: at one tick per 30 s the log lasts ~34 h.
        f = edge.FeatureSource("trend", 125, closes=history(200), usd_per_tick=Decimal("0.01"),
                               min_interval_ns=30 * NS)
        at = 1_784_548_800 * NS
        self.assertIsNotNone(f.tick(reference("300", "2026-07-20T12:00:00Z"), receive_ns=at, tick_id=1))
        self.assertIsNone(f.tick(reference("301", "2026-07-20T12:00:10Z"), receive_ns=at + 10 * NS, tick_id=2))
        self.assertEqual(f.last_outcome, "throttled")
        # The throttled price was never forwarded, so its later resend is still
        # the freshest observation and goes out once the interval has passed.
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
        # received 1.0000 s, read by the submitter 1.0005 s, source committed 1.004 s, decision 1.009 s:
        # tick-to-decision 9 ms = edge 0.5 + source commit 3.5 + decision commit 5.0
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
        # The kernel's own view: the strategy proposed place_limit (2), shadow authorized no_action (1).
        m = edge.EdgeMetrics(shadow_recorded_code=14)
        m.observe(edge.parse_decision(self.LINE), tick_receive_ns=1_000_000_000, expected_rule=0)
        m.observe(edge.parse_decision(self.LINE.replace("effective=1", "effective=9")), tick_receive_ns=1_000_000_000,
                  expected_rule=0)
        text = m.render(now_ns=0)
        self.assertIn('velocita_shadow_directives_total{stage="proposed",directive="place_limit"} 2\n', text)
        self.assertIn('velocita_shadow_directives_total{stage="effective",directive="no_action"} 1\n', text)
        # An unknown code is shown, not dropped.
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
            # After the run the submitter has exited: ready reads 0, and one start was counted.
            self.assertIn("velocita_shadow_submitter_ready 0\n", text)
            self.assertIn("velocita_shadow_submitter_starts_total 1\n", text)
            # Every message is accounted for: 3 new prices, 1 resend, 1 other topic.
            self.assertIn('velocita_shadow_messages_total{outcome="tick"} 3\n', text)
            self.assertIn('velocita_shadow_messages_total{outcome="resend"} 1\n', text)
            self.assertIn('velocita_shadow_messages_total{outcome="other"} 1\n', text)
            # The model's last view: price 40000 ticks x $0.01, features from the tested research function.
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
            # Paper decisions are `action` (1), not shadow_recorded: the tick book still buys once at 400.
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
            # Buy at 400; the stale exit at 410 is ignored; enter at 420 changes nothing.
            self.assertEqual(book.books["tick"].trades, 1)
            self.assertEqual(book.books["tick"].fills[0].price, Decimal("400.00"))
            text = textfile.read_text()
            self.assertIn('velocita_shadow_book_trades_total{timing="tick"} 1\n', text)
            self.assertIn('velocita_shadow_book_position{timing="tick"} 1\n', text)


if __name__ == "__main__":
    unittest.main()
