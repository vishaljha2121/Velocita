"""Market-data stream recorder: raw evidence first, bounded storage, honest metrics.

Fixtures follow the True Markets AsyncAPI examples; every expected value is
worked out by hand in the comment next to it, never read back from the code.
"""

from __future__ import annotations

import hashlib
import json
import unittest
from tempfile import TemporaryDirectory

from velocita_model.collectors import market_stream as ms
from velocita_model.storage.sqlite_store import ResearchStore

SECOND = 1_000_000_000
T0 = 1_790_600_000 * SECOND  # 2026-09-28T12:53:20Z


def reference(bid: str, ask: str, mid: str) -> str:
    return json.dumps({"type": "reference_prices", "timestamp": 1790600000, "topics": ["reference_prices"],
                       "data": [{"symbol": "SOL|USD", "mid_price": mid, "bid": bid, "ask": ask,
                                 "sources": [{"source": "coinbase", "bid": bid, "ask": ask, "mid_price": mid,
                                              "weight_pct": "1", "timestamp": "2026-09-28T12:53:18Z"}],
                                 "timestamp": "2026-09-28T12:53:18Z"}]})


def candles(sol_close: str, bonk_close: str = "0.00002") -> str:
    row = {"address": "x", "chain": "solana", "interval": "30s", "open": "1", "high": "2", "low": "0.5"}
    return json.dumps({"type": "price_candles", "timestamp": 1790600000, "topics": ["price_candles"],
                       "data": [dict(row, symbol="SOL", close=sol_close), dict(row, symbol="BONK", close=bonk_close)]})


class Fixture(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = TemporaryDirectory()
        self.store = ResearchStore(f"{self._dir.name}/r.db")
        self.session = self.store.open_session(venue="truemarkets", config_digest="c", schema_digest="s",
                                               code_version="t", clock_synced=True)
        self.recorder = ms.MarketStreamRecorder(
            self.store, self.session,
            topics=["reference_prices", "price_candles", "orderbook.hyperliquid-spot.HYPE-USDC"],
            watch_symbols={"SOL"}, silent_after_s=60)
        self.recorder.on_connected(T0)

    def tearDown(self) -> None:
        self.store.close()
        self._dir.cleanup()

    def rows(self) -> list:
        return self.store.connection.execute(
            "SELECT type, body, body_digest, full_digest, filtered FROM market_message ORDER BY id").fetchall()


class RawEvidence(Fixture):
    def test_reference_prices_are_stored_byte_for_byte(self) -> None:
        text = reference("100.49", "100.51", "100.50")
        self.recorder.handle(text, T0 + SECOND)
        (row,) = self.rows()
        self.assertEqual(row["type"], "reference_prices")
        self.assertEqual(row["body"], text)
        self.assertEqual(row["body_digest"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(row["full_digest"], row["body_digest"])
        self.assertEqual(row["filtered"], 0)

    def test_unparseable_text_is_kept_as_evidence(self) -> None:
        self.recorder.handle("{not json", T0 + SECOND)
        (row,) = self.rows()
        self.assertEqual((row["type"], row["body"]), ("unparseable", "{not json"))
        self.assertIn('velocita_md_parse_errors_total 1\n', self.recorder.metrics(T0 + SECOND))


class CandleStoragePolicy(Fixture):
    def test_only_watched_rows_are_kept_with_the_full_digest(self) -> None:
        text = candles("118.5")
        self.recorder.handle(text, T0 + SECOND)
        (row,) = self.rows()
        body = json.loads(row["body"])
        self.assertEqual([r["symbol"] for r in body["data"]], ["SOL"])
        self.assertEqual(row["full_digest"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(row["filtered"], 1)

    def test_unchanged_watched_rows_are_counted_not_stored(self) -> None:
        self.recorder.handle(candles("118.5", bonk_close="0.1"), T0 + SECOND)
        self.recorder.handle(candles("118.5", bonk_close="0.2"), T0 + 2 * SECOND)  # only BONK moved
        self.recorder.handle(candles("118.6"), T0 + 3 * SECOND)                     # SOL moved
        self.assertEqual(len(self.rows()), 2)
        self.assertIn('velocita_md_messages_total{type="price_candles"} 3\n', self.recorder.metrics(T0 + 3 * SECOND))
        self.assertIn('velocita_md_candles_deduplicated_total 1\n', self.recorder.metrics(T0 + 3 * SECOND))


class Metrics(Fixture):
    def test_reference_spread_and_source_age(self) -> None:
        self.recorder.handle(reference("99", "101", "100"), T0 + SECOND)
        text = self.recorder.metrics(T0 + SECOND)
        # (101 - 99) / 100 (the MID) * 1e4 = 200.000 bp; against the bid it would be 202.020
        self.assertIn('velocita_md_reference_spread_bps{symbol="SOL|USD"} 200.000\n', text)
        self.assertIn('velocita_md_reference_mid{symbol="SOL|USD"} 100\n', text)
        # received 12:53:21, source quote stamped 12:53:18 -> 3 s old
        self.assertIn('velocita_md_reference_source_age_seconds{symbol="SOL|USD",source="coinbase"} 3.000\n', text)

    def test_silent_subscribed_topic_is_flagged_not_hidden(self) -> None:
        self.recorder.handle(reference("1", "1", "1"), T0 + 59 * SECOND)
        text = self.recorder.metrics(T0 + 61 * SECOND)
        self.assertIn('velocita_md_topic_silent{topic="orderbook.hyperliquid-spot.HYPE-USDC"} 1\n', text)
        self.assertIn('velocita_md_topic_silent{topic="reference_prices"} 0\n', text)
        # price_candles never arrived either: silent too.
        self.assertIn('velocita_md_topic_silent{topic="price_candles"} 1\n', text)

    def test_rejected_subscription_is_visible(self) -> None:
        self.recorder.handle(json.dumps({"type": "subscribe", "status": "error", "timestamp": 1790600000,
                                         "topics": ["reference_prices"],
                                         "rejected": [{"topic": "orderbook.x.Y-Z", "reason": "unsupported_channel"}]}),
                             T0 + SECOND)
        self.assertIn('velocita_md_subscribe_rejected{topic="orderbook.x.Y-Z",reason="unsupported_channel"} 1\n',
                      self.recorder.metrics(T0 + SECOND))

    def test_disconnected_stream_reports_down_and_records_a_gap(self) -> None:
        self.recorder.on_disconnected(T0 + 5 * SECOND, "eof")
        self.assertIn("velocita_md_up 0\n", self.recorder.metrics(T0 + 6 * SECOND))
        self.recorder.on_connected(T0 + 9 * SECOND)
        self.assertIn("velocita_md_up 1\n", self.recorder.metrics(T0 + 9 * SECOND))
        gap = self.store.connection.execute(
            "SELECT kind, ended_at_ns IS NOT NULL AS closed, detail FROM data_quality_interval").fetchall()
        self.assertEqual([(g["kind"], g["closed"], g["detail"]) for g in gap], [("disconnect", 1, "eof")])


class Loop(unittest.TestCase):
    """The loop reconnects after a failed connect and after the stream ends,
    publishing metrics atomically each time."""

    def test_reconnect_and_publish(self) -> None:
        with TemporaryDirectory() as directory:
            store = ResearchStore(f"{directory}/r.db")
            session = store.open_session(venue="truemarkets", config_digest="c", schema_digest="s",
                                         code_version="t", clock_synced=True)
            recorder = ms.MarketStreamRecorder(store, session, topics=["reference_prices"], watch_symbols=set())
            attempts: list[str] = []

            class Scripted:
                def __init__(self, messages: list[str]) -> None:
                    self.messages = messages

                def open(self) -> None: ...
                def subscribe(self, topics: list[str]) -> None: attempts.append("subscribe")
                def recv_text(self) -> str | None: return self.messages.pop(0) if self.messages else None
                def close(self) -> None: ...

            sockets = [Scripted([reference("1", "3", "2")]), Scripted([reference("1", "3", "2")])]
            script: list = [OSError("refused"), OSError("refused"), *sockets]

            def factory():
                item = script.pop(0)
                if isinstance(item, Exception):
                    raise item
                return item

            textfile = f"{directory}/md.prom"
            sleeps: list[float] = []
            ms.run_stream(recorder, factory, ["reference_prices"], textfile=textfile,
                          stop=lambda: not script and not any(s.messages for s in sockets),
                          sleep=sleeps.append, max_backoff_s=8)
            self.assertEqual(attempts, ["subscribe", "subscribe"])
            # 1 s, doubling while connects fail, reset to 1 s after a successful session.
            self.assertEqual(sleeps, [1, 2, 1])
            self.assertIn("velocita_md_reconnects_total 3\n", open(textfile).read())
            self.assertEqual(store.connection.execute("SELECT COUNT(*) FROM market_message").fetchone()[0], 2)
            store.close()


if __name__ == "__main__":
    unittest.main()
