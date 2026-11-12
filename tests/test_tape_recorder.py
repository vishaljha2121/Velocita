"""Recorder against the scripted WebSocket fixture (REC-1 to REC-7). Expectations come from the fixture's own log."""
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tape_oracles.reference import decode  # noqa: E402

RECORDER = os.environ.get("TAPE_RECORDER")
FIXTURE_PYTHON = os.environ.get("TAPE_FIXTURE_PYTHON", sys.executable)
FIXTURE = Path(__file__).resolve().parent / "tape_oracles" / "fixture_ws.py"
SYM = "NVDAC-USDC"


def frame(seq, kind="UPDATE", symbol=SYM, channel="DEPTH", bids=(), pad=""):
    if kind == "SNAPSHOT_COMPLETE":
        return json.dumps({"seqnum": str(seq), "channel": channel, "update": kind, "item": symbol}, separators=(",", ":"))
    data = {"symbol": symbol, "bids": [{"price": p, "qty": q, "order_count": "1"} for p, q in bids], "asks": []}
    if pad:
        data["pad"] = pad
    return json.dumps({"seqnum": str(seq), "channel": channel, "update": kind, "data": data}, separators=(",", ":"))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def fixture_available():
    try:
        return subprocess.run([FIXTURE_PYTHON, "-c", "import websockets"], capture_output=True).returncode == 0
    except OSError:
        return False


class Run:
    def __init__(self, steps, run_ms=3000, extra_config="", recorder_args=()):
        self.dir = Path(tempfile.mkdtemp(prefix="tape-rec-"))
        port = free_port()
        (self.dir / "script.json").write_text(json.dumps(steps))
        (self.dir / "recorder.conf").write_text(
            f"recorder_id=2\nurl=ws://127.0.0.1:{port}/v1/cefi/market\nsymbols={SYM}\nrefresh_ms=0\n" + extra_config)
        fixture = subprocess.Popen([FIXTURE_PYTHON, "-I", str(FIXTURE), str(port), str(self.dir / "script.json"),
                                    str(self.dir / "log.jsonl")], stdout=subprocess.PIPE, text=True)
        try:
            assert fixture.stdout.readline().strip() == "ready"
            out = subprocess.run([RECORDER, "--config", str(self.dir / "recorder.conf"), "--sink", str(self.dir / "sink.bin"),
                                  "--run-ms", str(run_ms), *recorder_args], capture_output=True, text=True, timeout=60)
            self.stderr = out.stderr
        finally:
            fixture.terminate()
            fixture.wait()
            fixture.stdout.close()
        self.entries = []
        data = (self.dir / "sink.bin").read_bytes()
        at = 0
        while at < len(data):
            (n,) = struct.unpack_from("<I", data, at)
            at += 4
            self.entries.append(decode(data[at:at + n]))
            at += n
        self.log = [json.loads(line) for line in (self.dir / "log.jsonl").read_text().splitlines()] if (self.dir / "log.jsonl").exists() else []

    def sent(self):
        return [m["raw"] for m in self.log if m["dir"] == "out"]

    def received(self):
        return [json.loads(m["raw"]) for m in self.log if m["dir"] == "in"]

    def observations(self):
        out, parts = [], {}
        for e in self.entries:
            if e["type"] == 1:
                out.append(e)
            elif e["type"] == 2:
                parts.setdefault(e["first_request"], b"")
                parts[e["first_request"]] += e["chunk"]
                if e["part_index"] == e["part_count"] - 1:
                    out.append(decode(parts.pop(e["first_request"])))
        return [o for o in out if o["kind"] != "CONTROL"]

    def events(self, kind):
        return [e for e in self.entries if e["type"] == 3 and e["kind"] == kind]


class FixtureIsAvailable(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("TAPE_REQUIRE_FIXTURE") == "1", "the fixture is optional here")
    def test_the_recorder_tests_can_run(self):
        self.assertTrue(RECORDER, "TAPE_RECORDER is not set")
        self.assertTrue(fixture_available(), f"{FIXTURE_PYTHON} cannot import websockets")


@unittest.skipUnless(RECORDER and fixture_available(), "requires the recorder binary and the websockets library")
class RecorderTests(unittest.TestCase):
    def test_bytes_and_hashes_are_exactly_what_was_sent(self):
        frames = [frame(1, "SNAPSHOT"), frame(1, "SNAPSHOT_COMPLETE"),
                  '{ "seqnum" : "1", "channel":"DEPTH","update":"UPDATE","data":{"symbol":"NVDAC-USDC","bids":[{"price":"182.4000","qty":"1","order_count":"1"}],"asks":[],"note":"café ☃"}}',
                  frame(2, pad="y" * 9000)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames])
        obs = r.observations()
        self.assertEqual([o["payload"].decode() for o in obs], r.sent())
        self.assertTrue(all(hashlib.sha256(o["payload"]).digest() == o["hash"] for o in obs))
        self.assertTrue(any(e["type"] == 2 for e in r.entries), "the 9 KB frame was split into parts")

    def test_duplicates_reordering_and_malformed_frames_are_all_kept(self):
        frames = [frame(10, "SNAPSHOT"), frame(10, "SNAPSHOT_COMPLETE"), frame(10), frame(10), frame(12), frame(11), "{not json"]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames])
        obs = r.observations()
        self.assertEqual([o["payload"].decode() for o in obs], frames)
        self.assertEqual(obs[-1]["kind"], "UNPARSED")
        self.assertEqual([e["request"] for e in r.entries], list(range(1, len(r.entries) + 1)))

    def test_gap_is_recorded_and_only_that_stream_resubscribes(self):
        frames = [frame(100, "SNAPSHOT"), frame(100, "SNAPSHOT_COMPLETE"), frame(100), frame(101), frame(103)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames] + [{"await_subscribe": 3}])
        gaps = r.events(2)
        self.assertEqual([(g["symbol"], g["expected"], g["received"]) for g in gaps], [(SYM, 102, 103)])
        requests = r.received()
        self.assertEqual([(q["type"], q["channels"], q["item_names"]) for q in requests[1:3]],
                         [("UNSUBSCRIBE_NO_AUTH", ["DEPTH"], [SYM]), ("SUBSCRIBE_NO_AUTH", ["DEPTH"], [SYM])])
        self.assertEqual(len(r.events(7)), 1)

    def test_resync_waits_for_each_reply_and_the_stream_ends_up_subscribed(self):
        frames = [frame(100, "SNAPSHOT"), frame(100, "SNAPSHOT_COMPLETE"), frame(100), frame(101), frame(103)]
        r = Run([{"busy_ms": 2000}, {"await_subscribe": 1}, {"sleep": 2.2}] + [{"send": f} for f in frames] +
                [{"await_subscribe": 3}, {"sleep": 2.5}], run_ms=9000)
        replies = [json.loads(m["raw"]) for m in r.log if m["dir"] == "ctl"]
        self.assertFalse([x for x in replies if x["status"] == "ERROR"], "no request was sent while another was pending")
        last = replies[-1]["subscriptions"]
        self.assertIn(SYM, next(s["item_names"] for s in last if s["channel"] == "DEPTH"))

    def test_a_rejected_request_is_retried_until_the_server_accepts_it(self):
        frames = [frame(100, "SNAPSHOT"), frame(100, "SNAPSHOT_COMPLETE"), frame(100), frame(101), frame(103)]
        r = Run([{"busy_ms": 100, "busy_after_reply_ms": 300}, {"await_subscribe": 1}, {"sleep": 0.6}] +
                [{"send": f} for f in frames] + [{"sleep": 2.0}], run_ms=4500)
        replies = [json.loads(m["raw"]) for m in r.log if m["dir"] == "ctl"]
        self.assertTrue([x for x in replies if x["status"] == "ERROR"], "the server rejected a request")
        accepted = [x for x in replies if x["status"] != "ERROR"]
        self.assertIn(SYM, next(s["item_names"] for s in accepted[-1]["subscriptions"] if s["channel"] == "DEPTH"))
        self.assertEqual([q["type"] for q in r.received()][-1], "SUBSCRIBE_NO_AUTH")

    def test_regression_is_recorded_without_resubscribing(self):
        frames = [frame(100, "SNAPSHOT"), frame(100, "SNAPSHOT_COMPLETE"), frame(100), frame(101), frame(100)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames])
        self.assertEqual([(e["expected"], e["received"]) for e in r.events(3)], [(102, 100)])
        self.assertEqual(len(r.received()), 1)

    def test_silence_ends_the_connection_and_heartbeats_keep_it(self):
        silent = Run([{"await_subscribe": 1}, {"send": frame(1, "SNAPSHOT")}, {"sleep": 30}], run_ms=4000,
                     extra_config="dead_after_ms=1500\nbackoff_start_ms=200\n")
        dead = silent.events(5)
        self.assertGreaterEqual(len(dead), 1)
        last_frame = [m["t"] for m in silent.log if m["dir"] == "out"][0]
        self.assertGreaterEqual(dead[0]["utc"] - last_frame, 1_400_000_000)
        self.assertLess(dead[0]["utc"] - last_frame, 2_600_000_000)
        self.assertGreaterEqual(len(silent.events(6)), 1, "reconnected after the dead connection")
        heartbeat = '{"channel":"HEARTBEAT","update":"UPDATE","timestamp":"1"}'
        alive = Run([{"await_subscribe": 1}, {"repeat": {"send": heartbeat}, "times": 1}] +
                    [{"sleep": 0.5}, {"send": heartbeat}] * 8, run_ms=4500, extra_config="dead_after_ms=1500\n")
        self.assertEqual(alive.events(5), [])

    def test_every_subscription_asks_for_heartbeats_and_only_no_login_requests_are_sent(self):
        frames = [frame(100, "SNAPSHOT"), frame(100, "SNAPSHOT_COMPLETE"), frame(100), frame(102)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames] + [{"await_subscribe": 3}])
        requests = r.received()
        self.assertIn("HEARTBEAT", requests[0]["channels"])
        for q in requests:
            self.assertIn(q["type"], ("SUBSCRIBE_NO_AUTH", "UNSUBSCRIBE_NO_AUTH"))
            self.assertEqual(set(q), {"type", "item_names", "channels", "timestamp"})

    def test_full_ring_records_one_overflow_with_the_exact_range(self):
        frames = [frame(1, "SNAPSHOT"), frame(1, "SNAPSHOT_COMPLETE")] + [frame(s) for s in range(1, 301)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames] + [{"sleep": 2}] +
                [{"send": frame(s)} for s in range(301, 311)],
                run_ms=6000, extra_config="ring_capacity=40\nunacked_capacity=20\n",
                recorder_args=("--sink-stall-after", "5", "--sink-stall-ms", "1500"))
        overflow = r.events(4)
        self.assertEqual(len(overflow), 1)
        recorded = {o["seqnum"] for o in r.observations() if o["kind"] == "UPDATE"}
        lost = set(range(1, 301)) - recorded
        self.assertEqual((overflow[0]["expected"], overflow[0]["received"]), (min(lost), max(lost)))
        self.assertEqual([e["request"] for e in r.entries], list(range(1, len(r.entries) + 1)))

    def test_receive_times_are_monotonic_and_utc_is_recorded(self):
        frames = [frame(s) for s in range(1, 50)]
        r = Run([{"await_subscribe": 1}] + [{"send": f} for f in frames])
        obs = r.observations()
        monos = [o["mono"] for o in obs]
        self.assertEqual(monos, sorted(monos))
        self.assertTrue(all(o["utc"] > 1_700_000_000_000_000_000 for o in obs))


if __name__ == "__main__":
    unittest.main()
