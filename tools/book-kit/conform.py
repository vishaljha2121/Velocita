#!/usr/bin/env python3
"""Plays a recorded DEPTH stream to an order-book client and checks every book it reports valid against the tape."""
import argparse
import hashlib
import json
import os
import select
import shlex
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_book import BookBuilder, atoms  # noqa: E402

SCENARIOS = ("clean", "drop", "duplicate", "late-snapshot", "swap")


def book_hash(symbol, next_seqnum, bids, asks):
    out = b"velocita-tape/v1/book" + bytes([len(symbol.encode())]) + symbol.encode() + struct.pack("<Q", next_seqnum)
    for side in (bids, asks):
        out += struct.pack("<H", len(side))
        for price, qty, count in side:
            out += struct.pack("<qqI", price, qty, count)
    return hashlib.sha256(out).hexdigest()


def load_key(path):
    key = {}
    with open(path) as f:
        for line in f:
            r = line.rstrip("\n").split("\t")
            if r[0] == "K":
                key.setdefault((r[2], int(r[3])), set()).add(r[4])
    return key


def parse(frame):
    m = json.loads(frame)
    symbol = (m.get("data") or {}).get("symbol") or m.get("item")
    return symbol, m.get("update"), int(m["seqnum"])


def inject(frames, scenario):
    """Returns the frames to play and, for a dropped update, the (symbol, seqnum) that went missing."""
    if scenario == "clean":
        return frames, None
    meta = [parse(f) for f in frames]
    for i, (symbol, kind, seqnum) in enumerate(meta):
        if kind != "UPDATE" or i < 1:
            continue
        later = [j for j in range(i + 1, len(meta)) if meta[j][0] == symbol]
        if not later or meta[later[0]][1] != "UPDATE" or meta[later[0]][2] != seqnum + 1:
            continue
        snaps = [j for j in later if meta[j][1] == "SNAPSHOT_COMPLETE" and meta[j][2] > seqnum + 1]
        if not snaps:
            continue
        if scenario == "drop":
            return frames[:i] + frames[i + 1:], (symbol, seqnum)
        if scenario == "duplicate":
            return frames[:i + 1] + [frames[i]] + frames[i + 1:], None
        if scenario == "swap":
            k = later[0]
            out = list(frames)
            out[i], out[k] = out[k], out[i]
            return out, None
        if scenario == "late-snapshot":
            first = [j for j in range(i) if meta[j][0] == symbol and meta[j][1] in ("SNAPSHOT", "SNAPSHOT_COMPLETE")]
            if not first:
                continue
            s = meta[first[0]][2]
            old = [frames[j] for j in first if meta[j][2] == s]
            return frames[:i + 1] + old + frames[i + 1:], None
    raise SystemExit(f"no place in this stream for the {scenario} scenario")


class Client:
    def __init__(self, command, timeout):
        self.timeout = timeout
        self.proc = subprocess.Popen(shlex.split(command), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def ask(self, frame):
        try:
            self.proc.stdin.write(json.dumps({"frame": frame}, separators=(",", ":")) + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            return None, "the client exited"
        ready, _, _ = select.select([self.proc.stdout], [], [], self.timeout)
        if not ready:
            return None, f"no answer within {self.timeout} s"
        line = self.proc.stdout.readline()
        if not line:
            return None, "the client exited"
        try:
            answer = json.loads(line)
            if not isinstance(answer, dict):
                raise ValueError
            return answer, None
        except ValueError:
            return None, "the answer is not a JSON object"

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()


def first_difference(claimed, expected):
    for side in ("bids", "asks"):
        a, b = claimed[side], expected[side]
        for k in range(max(len(a), len(b))):
            if k >= len(a) or k >= len(b) or a[k] != b[k]:
                return {"side": side, "level": k, "client": a[k] if k < len(a) else None,
                        "expected": b[k] if k < len(b) else None}
    return None


def run(frames, key, command, scenario, timeout):
    plays, missing = inject(frames, scenario)
    client = Client(command, timeout)
    witness = BookBuilder()
    skipping, last_next, gap_open = set(), {}, {}
    if missing:
        gap_open[missing[0]] = missing[1]
    counts = {"frames": 0, "valid": 0, "checked": 0, "unchecked": 0, "building": 0, "stale": 0, "resubscribes": 0}
    failure = None
    try:
        for frame in plays:
            symbol, kind, seqnum = parse(frame)
            if symbol in skipping:
                if kind != "SNAPSHOT":
                    continue
                skipping.discard(symbol)
            counts["frames"] += 1
            reference = witness.on_frame(frame)
            answer, problem = client.ask(frame)
            if problem:
                failure = {"reason": problem, "symbol": symbol, "seqnum": seqnum}
                break
            if symbol in gap_open and kind == "SNAPSHOT_COMPLETE" and seqnum > gap_open[symbol]:
                del gap_open[symbol]
            state = answer.get("state")
            if answer.get("symbol") != symbol and state != "ignored":
                failure = {"reason": "answered for a different book", "symbol": symbol, "seqnum": seqnum}
                break
            if state == "stale":
                counts["stale"] += 1
                if answer.get("resubscribe"):
                    counts["resubscribes"] += 1
                    skipping.add(symbol)
                continue
            if state == "building":
                counts["building"] += 1
            if state != "valid":
                continue
            counts["valid"] += 1
            try:
                position = int(answer["next_seqnum"])
                bids = [(atoms(p), atoms(q), int(c)) for p, q, c in answer["bids"]]
                asks = [(atoms(p), atoms(q), int(c)) for p, q, c in answer["asks"]]
            except (KeyError, ValueError, TypeError):
                failure = {"reason": "a valid answer without a readable book", "symbol": symbol, "seqnum": seqnum}
                break
            if symbol in gap_open and position > gap_open[symbol]:
                failure = {"reason": "valid after a missing update, before a new snapshot", "symbol": symbol,
                           "next_seqnum": position, "missing": gap_open[symbol]}
                break
            if position < last_next.get(symbol, 0):
                failure = {"reason": "the book went backwards", "symbol": symbol, "next_seqnum": position,
                           "previous": last_next[symbol]}
                break
            last_next[symbol] = position
            expected = key.get((symbol, position))
            if expected is None:
                counts["unchecked"] += 1
                continue
            counts["checked"] += 1
            if book_hash(symbol, position, bids, asks) not in expected:
                failure = {"reason": "wrong book", "symbol": symbol, "next_seqnum": position}
                if reference.get("state") == "valid" and reference.get("next_seqnum") == str(position):
                    failure["first_difference"] = first_difference(answer, reference)
                break
    finally:
        client.close()
    return {"scenario": scenario, "passed": failure is None, "first_failure": failure, "counts": counts,
            "missing": {"symbol": missing[0], "seqnum": missing[1]} if missing else None}


def from_tape(args):
    """Runs the verifier on a segment; uses its frames and answer key only if the segment verifies."""
    work = Path(tempfile.mkdtemp(prefix="book-kit-"))
    frames, rows = work / "frames.txt", work / "rows.tsv"
    cmd = [args.verify, "--wal", str(args.tape / "wal.log"), "--heads", str(args.tape / "heads"), "--incarnation",
           str(args.incarnation), "--head-every", str(args.head_every), "--frames", str(args.recorder), str(frames),
           "--extract", str(rows)]
    for k in args.key_file:
        cmd += ["--key", str(k)]
    out = subprocess.run(cmd, capture_output=True, text=True)
    try:
        verdict = json.loads(out.stdout.strip().splitlines()[-1]) if out.stdout.strip() else {}
    except ValueError:
        verdict = {}
    if out.returncode != 0 or not verdict.get("ok"):
        raise SystemExit("the segment did not verify, so there is no answer key: " + (out.stdout.strip() or out.stderr.strip())[:300])
    return frames, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--client", required=True, help="command that starts the client")
    ap.add_argument("--scenario", choices=SCENARIOS, action="append")
    ap.add_argument("--frames", type=Path, help="one DEPTH frame per line")
    ap.add_argument("--key", type=Path, help="answer key: K rows from velocita-tape-verify --extract")
    ap.add_argument("--tape", type=Path, help="a segment directory (wal.log, heads); verified before use")
    ap.add_argument("--recorder", type=int, default=1)
    ap.add_argument("--incarnation", type=int)
    ap.add_argument("--head-every", type=int, default=60)
    ap.add_argument("--key-file", type=Path, action="append", default=[], help="voter public key (with --tape)")
    ap.add_argument("--verify", default=os.environ.get("TAPE_VERIFY", "velocita-tape-verify"))
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    if args.tape:
        frames_path, key_path = from_tape(args)
    elif args.frames and args.key:
        frames_path, key_path = args.frames, args.key
    else:
        ap.error("give --tape, or --frames and --key")
    frames = [line.rstrip("\n") for line in open(frames_path) if line.strip()]
    key = load_key(key_path)
    results = [run(frames, key, args.client, s, args.timeout) for s in (args.scenario or ["clean"])]
    report = {"client": args.client, "passed": all(r["passed"] for r in results), "scenarios": results}
    text = json.dumps(report, indent=1, sort_keys=True) + "\n"
    if args.report:
        args.report.write_text(text)
    sys.stdout.write(text)
    sys.exit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
