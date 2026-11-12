#!/usr/bin/env python3
"""Feed-quality report from the tape's archived segments: verifies each segment, extracts its events with the
voters' own reducer (velocita-tape-verify --extract), and writes summary.json and report.html. Aggregates only."""
import argparse
import gzip
import html
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

UNKNOWN_CLOCK = 4294967295
FLAGS = ["crossed", "locked", "ebbo_mismatch", "trade_outside_book", "trade_outside_band", "snapshot_differs",
         "late_copy", "late_divergence", "stale_snapshot"]
OUTCOMES = ["3/3", "2/3", "1/3", "divergent"]
EVENTS = ["gap", "resubscribe", "reconnect", "dead_connection", "overflow", "regression"]
GAP_SECONDS = 5.0
DEAD_SECONDS = 30.0


def percentile(hist, q):
    total = sum(hist.values())
    if total == 0:
        return None
    rank = q * (total - 1)
    seen = 0
    for value in sorted(hist):
        seen += hist[value]
        if seen > rank:
            return value
    return max(hist)


def us_market_hours(utc_ns):
    t = datetime.fromtimestamp(utc_ns / 1e9, timezone.utc)
    minutes = t.hour * 60 + t.minute
    return t.weekday() < 5 and 14 * 60 + 30 <= minutes < 21 * 60


class Book:
    def __init__(self):
        self.outcomes = Counter()
        self.events = Counter()
        self.flags = Counter()
        self.cadence_ms = Counter()
        self.last_first_arrival = None
        self.silences_over_dead = 0
        self.longest_silence_ms = 0
        self.spread = {"market_hours": Counter(), "off_hours": Counter()}
        self.spread_weight = {"market_hours": 0.0, "off_hours": 0.0}
        self.spread_sum = {"market_hours": 0.0, "off_hours": 0.0}
        self.top_bid = Counter()
        self.top_ask = Counter()
        self.last_top = None


class Report:
    def __init__(self):
        self.books = defaultdict(Book)
        self.spread_ms = Counter()
        self.clock_errors = []
        self.clock_unknown = False
        self.segments = []
        self.covered_seconds = 0.0
        self.coverage_gaps = []
        self.prev_tick_utc = None
        self.outages = []
        self.periods = defaultdict(lambda: {"outcomes": Counter(), "spread_ms": Counter(), "recorded": 0.0})

    def fold(self, rows_path):
        last_tick_utc = self.prev_tick_utc
        first_tick_utc = None
        with open(rows_path) as f:
            for line in f:
                if not line.startswith("T\t"):
                    continue
                utc = int(line.rstrip("\n").split("\t")[3])
                if first_tick_utc is None:
                    first_tick_utc = utc
                if last_tick_utc is not None:
                    delta = (utc - last_tick_utc) / 1e9
                    if 0 < delta <= GAP_SECONDS:
                        self.covered_seconds += delta
                        self.periods[utc // 3_600_000_000_000]["recorded"] += delta
                    elif delta > GAP_SECONDS:
                        self.coverage_gaps.append({"from_utc": iso(last_tick_utc), "seconds": round(delta, 1)})
                        self.outages.append((last_tick_utc, utc))
                last_tick_utc = utc
        with open(rows_path) as f:
            for line in f:
                r = line.rstrip("\n").split("\t")
                kind = r[0]
                if kind == "T":
                    continue
                elif kind == "O":
                    err = int(r[8])
                    if err == UNKNOWN_CLOCK:
                        self.clock_unknown = True
                    else:
                        self.clock_errors.append(err)
                    flags = int(r[10])
                    for bit, name in enumerate(FLAGS):
                        if flags & (1 << bit):
                            self.books[r[5] or "-"].flags[name] += 1
                elif kind == "E":
                    if r[3] in EVENTS and r[5]:
                        self.books[r[5]].events[r[3]] += 1
                elif kind == "C":
                    symbol, channel, update_kind, outcome = r[2], r[3], r[4], r[6]
                    if update_kind != "UPDATE":
                        continue
                    book = self.books[symbol]
                    book.outcomes[outcome] += 1
                    times = [int(x) for x in r[9:12] if int(x) != 0]
                    period = self.periods[min(times) // 3_600_000_000_000] if times else None
                    if period is not None:
                        period["outcomes"][outcome] += 1
                    if len(times) >= 2:
                        self.spread_ms[(max(times) - min(times)) // 1_000_000] += 1
                        if period is not None:
                            period["spread_ms"][(max(times) - min(times)) // 1_000_000] += 1
                    if channel == "DEPTH" and times:
                        first = min(times)
                        spans_outage = book.last_first_arrival is not None and any(
                            s < first and e > book.last_first_arrival for s, e in self.outages)
                        if book.last_first_arrival is not None and first > book.last_first_arrival and not spans_outage:
                            gap_ms = (first - book.last_first_arrival) // 1_000_000
                            book.cadence_ms[gap_ms] += 1
                            book.longest_silence_ms = max(book.longest_silence_ms, gap_ms)
                            if gap_ms > DEAD_SECONDS * 1000:
                                book.silences_over_dead += 1
                        book.last_first_arrival = first if book.last_first_arrival is None else max(book.last_first_arrival, first)
                elif kind == "B":
                    self.fold_top(r)
        for book in self.books.values():
            book.last_top = None
        self.prev_tick_utc = last_tick_utc
        return first_tick_utc, last_tick_utc

    def fold_top(self, r):
        symbol, state, utc = r[2], r[3], int(r[8])
        book = self.books[symbol]
        if book.last_top is not None:
            prev_utc, prev_spread, prev_hours = book.last_top
            weight = min(max(utc - prev_utc, 0) / 1e9, 60.0)
            if prev_spread is not None and weight > 0:
                book.spread[prev_hours][round(prev_spread, 1)] += weight
                book.spread_weight[prev_hours] += weight
                book.spread_sum[prev_hours] += weight * prev_spread
        spread = None
        if state == "valid" and r[4] != "-" and r[6] != "-":
            bid, ask = int(r[4]), int(r[6])
            if bid > 0 and ask > 0:
                spread = (ask - bid) / ((ask + bid) / 2) * 1e4
                book.top_bid[int(r[5]) / 1e9] += 1
                book.top_ask[int(r[7]) / 1e9] += 1
        book.last_top = (utc, spread, "market_hours" if us_market_hours(utc) else "off_hours")

    def summary(self):
        books = {}
        totals = Counter()
        for symbol in sorted(s for s in self.books if s and s != "-"):
            b = self.books[symbol]
            totals.update(b.outcomes)
            closed = sum(b.outcomes.values())
            books[symbol] = {
                "updates": {o: b.outcomes.get(o, 0) for o in OUTCOMES} | {"closed": closed},
                "events": {e: b.events.get(e, 0) for e in EVENTS},
                "flags": {f: b.flags.get(f, 0) for f in FLAGS},
                "cadence_ms": {"p50": percentile(b.cadence_ms, 0.5), "p90": percentile(b.cadence_ms, 0.9),
                               "longest": b.longest_silence_ms or None},
                "silences_over_30s": b.silences_over_dead,
                "spread_bp": {h: {"mean": round(b.spread_sum[h] / b.spread_weight[h], 2) if b.spread_weight[h] else None,
                                  "median": percentile(Counter({k: round(v * 1000) for k, v in b.spread[h].items()}), 0.5),
                                  "seconds": round(b.spread_weight[h], 1)} for h in ("market_hours", "off_hours")},
                "top_size": {"bid_median": percentile(b.top_bid, 0.5), "ask_median": percentile(b.top_ask, 0.5)},
            }
        closed = sum(totals.values())
        used = [s for s in self.segments if s["status"] == "used"]
        periods = []
        for hour in sorted(self.periods):
            p = self.periods[hour]
            n = sum(p["outcomes"].values())
            periods.append({"hour_utc": iso(hour * 3_600_000_000_000), "recorded_minutes": round(p["recorded"] / 60, 1),
                            "updates": {o: p["outcomes"].get(o, 0) for o in OUTCOMES} | {"closed": n},
                            "share_3_of_3": round(p["outcomes"].get("3/3", 0) / n, 4) if n else None,
                            "arrival_spread_ms": {"p50": percentile(p["spread_ms"], 0.5), "p99": percentile(p["spread_ms"], 0.99)}})
        return {
            "coverage": {"segments_used": len(used), "segments_excluded": len(self.segments) - len(used),
                         "recorded_hours": round(self.covered_seconds / 3600, 3), "gaps": self.coverage_gaps},
            "completeness": {"updates": {o: totals.get(o, 0) for o in OUTCOMES} | {"closed": closed},
                             "share_3_of_3": round(totals.get("3/3", 0) / closed, 4) if closed else None},
            "timeliness": {"arrival_spread_ms": {"p50": percentile(self.spread_ms, 0.5), "p90": percentile(self.spread_ms, 0.9),
                                                 "p99": percentile(self.spread_ms, 0.99), "max": max(self.spread_ms) if self.spread_ms else None,
                                                 "updates": sum(self.spread_ms.values())},
                           "clock_error_bound_ms": round(max(self.clock_errors) / 1000, 3) if self.clock_errors else None,
                           "clock_error_unknown_on_some_hosts": self.clock_unknown},
            "books": books,
            "periods": periods,
            "segments": self.segments,
        }


def iso(utc_ns):
    return datetime.fromtimestamp(utc_ns / 1e9, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def head_file(seg, tick):
    return seg / "heads" / f"head-{tick}.json"


def first_tick_utc(rows):
    with open(rows) as f:
        for line in f:
            if line.startswith("T\t"):
                return int(line.rstrip("\n").split("\t")[3])
    return None


def run(args):
    report = Report()
    segments = [p for p in args.segments.iterdir() if p.is_dir() and p.name.isdigit()]
    scratch_root = Path(tempfile.mkdtemp(prefix="tape-report-", dir=args.scratch))
    try:
        verified = []
        for seg in sorted(segments, key=lambda p: p.name):
            incarnation = int(seg.name)
            entry = {"incarnation": incarnation, "status": "excluded"}
            scratch = scratch_root / seg.name
            scratch.mkdir()
            wal = scratch / "wal.log"
            rows = scratch / "rows.tsv"
            try:
                if (seg / "wal.log.gz").exists():
                    with gzip.open(seg / "wal.log.gz", "rb") as src, open(wal, "wb") as dst:
                        shutil.copyfileobj(src, dst, 1 << 20)
                elif (seg / "wal.log").exists():
                    shutil.copyfile(seg / "wal.log", wal)
                else:
                    entry["reason"] = "no log"
            except (OSError, EOFError) as e:
                entry["reason"] = f"log unreadable: {e.__class__.__name__}"
            result = {}
            if "reason" not in entry:
                cmd = args.verify + ["--wal", str(wal), "--heads", str(seg / "heads"), "--incarnation", str(incarnation),
                                     "--head-every", str(args.head_every), "--extract", str(rows)]
                for k in args.key:
                    cmd += ["--key", str(k)]
                out = subprocess.run(cmd, capture_output=True, text=True)
                wal.unlink(missing_ok=True)
                last = out.stdout.strip().splitlines()[-1] if out.stdout.strip() else "{}"
                try:
                    result = json.loads(last)
                except ValueError:
                    result = {}
                if out.returncode != 0 or not result.get("ok"):
                    detail = result.get("check") or result.get("error") or (out.stderr.strip().splitlines() or [""])[-1]
                    entry["reason"] = "verify failed" + (f": {detail[:120]}" if detail else "")
                elif result.get("heads_verified", 0) < 1:
                    entry["reason"] = "no signed head"
            start_utc = first_tick_utc(rows) if "reason" not in entry else None
            verified.append((start_utc, seg, entry, result, rows))
        verified.sort(key=lambda v: (v[0] is None, v[0] or 0, v[1].name))
        previous_used = None
        for start_utc, seg, entry, result, rows in verified:
            report.segments.append(entry)
            if "reason" in entry:
                continue
            link = result.get("previous_segment")
            if previous_used is not None:
                ok = False
                if link and link.get("incarnation") == int(previous_used.name):
                    hf = head_file(previous_used, link.get("tick"))
                    if hf.exists():
                        h = json.loads(hf.read_text())
                        ok = (h.get("tick") == link["tick"] and h.get("tree_size") == link["tree_size"]
                              and h.get("root") == link["root"])
                if not ok:
                    entry["reason"] = "link"
                    continue
            first_utc, last_utc = report.fold(rows)
            rows.unlink(missing_ok=True)
            heads = sorted(int(p.stem[5:]) for p in (seg / "heads").glob("head-*.json"))
            last_head = json.loads(head_file(seg, heads[-1]).read_text()) if heads else {}
            entry.update({"status": "used", "entries": result.get("entries"), "last_head_tick": last_head.get("tick"),
                          "tree_size": last_head.get("tree_size"), "root": last_head.get("root"),
                          "heads_verified": result.get("heads_verified"),
                          "first_tick_utc": iso(first_utc) if first_utc else None, "last_tick_utc": iso(last_utc) if last_utc else None,
                          "linked_to": link.get("incarnation") if link else None})
            previous_used = seg
    finally:
        shutil.rmtree(scratch_root, ignore_errors=True)
    return report.summary()


def render(summary, keys_hint):
    e = html.escape
    def cell(v):
        return "–" if v is None else e(str(v))
    c = summary["completeness"]["updates"]
    t = summary["timeliness"]
    rows_books = "".join(
        f"<tr><th>{e(sym)}</th><td>{b['updates']['closed']}</td><td>{b['updates']['3/3']}</td><td>{b['updates']['2/3']}</td>"
        f"<td>{b['updates']['1/3']}</td><td>{b['updates']['divergent']}</td><td>{b['events']['gap']}</td>"
        f"<td>{b['events']['resubscribe']}</td></tr>" for sym, b in summary["books"].items())
    rows_consistency = "".join(
        f"<tr><th>{e(sym)}</th>" + "".join(f"<td>{b['flags'][f]}</td>" for f in ("crossed", "locked", "trade_outside_book", "snapshot_differs", "stale_snapshot")) + "</tr>"
        for sym, b in summary["books"].items())
    rows_time = "".join(
        f"<tr><th>{e(sym)}</th><td>{cell(b['cadence_ms']['p50'])}</td><td>{cell(b['cadence_ms']['p90'])}</td>"
        f"<td>{cell(b['cadence_ms']['longest'])}</td><td>{b['silences_over_30s']}</td></tr>" for sym, b in summary["books"].items())
    rows_shape = "".join(
        f"<tr><th>{e(sym)}</th><td>{cell(b['spread_bp']['market_hours']['mean'])}</td><td>{cell(b['spread_bp']['off_hours']['mean'])}</td>"
        f"<td>{cell(b['top_size']['bid_median'])}</td><td>{cell(b['top_size']['ask_median'])}</td></tr>" for sym, b in summary["books"].items())
    def pct(v):
        return "–" if v is None else f"{v * 100:.1f}%"
    rows_periods = "".join(
        f"<tr><th>{e(p['hour_utc'][:13].replace('T', ' '))}:00</th><td>{p['recorded_minutes']}</td><td>{p['updates']['closed']}</td>"
        f"<td>{pct(p['share_3_of_3'])}</td><td>{p['updates']['2/3']}</td>"
        f"<td>{p['updates']['1/3']}</td><td>{p['updates']['divergent']}</td><td>{cell(p['arrival_spread_ms']['p50'])}</td>"
        f"<td>{cell(p['arrival_spread_ms']['p99'])}</td></tr>" for p in summary["periods"])
    rows_segments = "".join(
        f"<tr><td>{s['incarnation']}</td><td>{e(s['status'])}</td><td>{cell(s.get('reason'))}</td><td>{cell(s.get('first_tick_utc'))}</td>"
        f"<td>{cell(s.get('last_tick_utc'))}</td><td>{cell(s.get('last_head_tick'))}</td><td>{cell(s.get('tree_size'))}</td>"
        f"<td class='mono'>{cell((s.get('root') or '')[:16] or None)}</td></tr>" for s in summary["segments"])
    gaps = summary["coverage"]["gaps"]
    gap_text = ", ".join(f"{g['from_utc']} ({g['seconds']} s)" for g in gaps[:20]) or "none"
    share = summary["completeness"]["share_3_of_3"]
    bound = t["clock_error_bound_ms"]
    bound_text = (f"{bound} ms" if bound is not None else "unknown") + (" (unknown on some hosts)" if t["clock_error_unknown_on_some_hosts"] and bound is not None else "")
    return f"""<title>Feed Quality Report</title>
<meta name="claude:viewport-fit-sides" content="cover">
<style>
:root {{ --bg:#f6f7f9; --panel:#ffffff; --fg:#17202a; --muted:#5b6673; --rule:#d9dee5; --accent:#1f5fa8;
  --font-body: "IBM Plex Sans", system-ui, sans-serif; --font-mono: "IBM Plex Mono", ui-monospace, monospace; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#11161c; --panel:#18202a; --fg:#e6ebf1; --muted:#9aa6b4; --rule:#2a3542; --accent:#7fb0ea; color-scheme: dark; }} }}
:root[data-theme="dark"] {{ --bg:#11161c; --panel:#18202a; --fg:#e6ebf1; --muted:#9aa6b4; --rule:#2a3542; --accent:#7fb0ea; color-scheme: dark; }}
body {{ background: var(--bg); color: var(--fg); font-family: var(--font-body); line-height: 1.5;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 24px; }}
main {{ max-width: 980px; margin: 0 auto; display: grid; gap: 28px; }}
h1 {{ margin: 0; font-size: 1.8rem; text-wrap: balance; }} h2 {{ margin: 0 0 8px; font-size: 1.15rem; }}
p {{ margin: 0; max-width: 70ch; color: var(--muted); }}
.tiles {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; }}
.tile {{ background: var(--panel); border: 1px solid var(--rule); border-radius: 6px; padding: 12px 14px; }}
.tile b {{ display: block; font-size: 1.5rem; font-variant-numeric: tabular-nums; }} .tile span {{ color: var(--muted); font-size: .85rem; }}
.wrap {{ overflow-x: auto; }} table {{ border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; font-size: .9rem; }}
th, td {{ text-align: right; padding: 6px 8px; border-bottom: 1px solid var(--rule); white-space: nowrap; }}
th:first-child, td:first-child {{ text-align: left; }} thead th {{ color: var(--muted); font-weight: 600; }}
.mono, code {{ font-family: var(--font-mono); font-size: .85rem; }}
</style>
<main>
<header><h1>Feed Quality Report</h1><p>Twelve Orderbook Arena books as seen by three independent recorders, from the verified tape. Aggregates only.</p></header>
<section><div class="tiles">
<div class="tile"><b>{summary['coverage']['recorded_hours']} h</b><span>recorded ({summary['coverage']['segments_used']} segments used, {summary['coverage']['segments_excluded']} excluded)</span></div>
<div class="tile"><b>{'–' if share is None else f'{share * 100:.2f}%'}</b><span>updates identical at all three recorders</span></div>
<div class="tile"><b>{c['divergent']}</b><span>divergent updates (different bytes for one message)</span></div>
<div class="tile"><b>{cell(t['arrival_spread_ms']['p50'])} ms</b><span>median arrival spread across recorders (p99 {cell(t['arrival_spread_ms']['p99'])} ms)</span></div>
</div></section>
<section><h2>Completeness</h2><p>Every update gets one verdict: how many recorders received identical bytes before it closed. Closed {c['closed']}: 3 of 3 {c['3/3']}, 2 of 3 {c['2/3']}, 1 of 3 {c['1/3']}, divergent {c['divergent']}.</p>
<div class="wrap"><table><thead><tr><th>Book</th><th>Updates</th><th>3 of 3</th><th>2 of 3</th><th>1 of 3</th><th>Divergent</th><th>Gaps</th><th>Resubscribes</th></tr></thead><tbody>{rows_books}</tbody></table></div></section>
<section><h2>By hour (UTC)</h2><p>Updates grouped by the hour of their first arrival, with the minutes the tape was actually recording in that hour.</p>
<div class="wrap"><table><thead><tr><th>Hour</th><th>Recorded min</th><th>Updates</th><th>3 of 3</th><th>2 of 3</th><th>1 of 3</th><th>Divergent</th><th>Spread p50 ms</th><th>Spread p99 ms</th></tr></thead><tbody>{rows_periods}</tbody></table></div></section>
<section><h2>Consistency</h2><p>Integrity checks on the rebuilt books, counted per observation.</p>
<div class="wrap"><table><thead><tr><th>Book</th><th>Crossed</th><th>Locked</th><th>Trade outside book</th><th>Snapshot differs</th><th>Stale snapshot</th></tr></thead><tbody>{rows_consistency}</tbody></table></div></section>
<section><h2>Timeliness and liveness</h2><p>Time between consecutive DEPTH updates by first arrival (ms). The feed documents about 1 s batching for no-login subscribers and a 30 s silence rule for dead connections. Arrival spread p50 {cell(t['arrival_spread_ms']['p50'])} ms, p90 {cell(t['arrival_spread_ms']['p90'])} ms, p99 {cell(t['arrival_spread_ms']['p99'])} ms, max {cell(t['arrival_spread_ms']['max'])} ms over {t['arrival_spread_ms']['updates']} updates seen by at least two recorders; clock error bound {e(bound_text)}.</p>
<div class="wrap"><table><thead><tr><th>Book</th><th>Gap p50</th><th>Gap p90</th><th>Longest</th><th>Silences over 30 s</th></tr></thead><tbody>{rows_time}</tbody></table></div></section>
<section><h2>Book shape</h2><p>Time-weighted mean spread in basis points, US market hours (14:30 to 21:00 UTC, weekdays) and off-hours, and median top-of-book size.</p>
<div class="wrap"><table><thead><tr><th>Book</th><th>Spread, market hours</th><th>Spread, off-hours</th><th>Top bid size</th><th>Top ask size</th></tr></thead><tbody>{rows_shape}</tbody></table></div></section>
<section><h2>Coverage and how to check</h2><p>Gaps when the tape was not recording: {e(gap_text)}. Each segment below can be re-verified with <code>velocita-tape-verify --wal SEGMENT/wal.log --heads SEGMENT/heads --incarnation N --head-every 60 {e(keys_hint)}</code>.</p>
<div class="wrap"><table><thead><tr><th>Segment</th><th>Status</th><th>Reason</th><th>First tick</th><th>Last tick</th><th>Last head</th><th>Tree size</th><th>Root</th></tr></thead><tbody>{rows_segments}</tbody></table></div></section>
</main>
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--segments", type=Path, required=True, help="directory of archived segments (segments/<incarnation>/)")
    p.add_argument("--verify", required=True, help="command that runs velocita-tape-verify, as one shell-quoted string")
    p.add_argument("--key", type=Path, action="append", required=True)
    p.add_argument("--head-every", type=int, default=60)
    p.add_argument("--scratch", type=Path, default=None)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    a.verify = shlex.split(a.verify)
    a.out.mkdir(parents=True, exist_ok=True)
    summary = run(a)
    (a.out / "summary.json").write_text(json.dumps(summary, sort_keys=True, indent=1) + "\n")
    (a.out / "report.html").write_text(render(summary, " ".join(f"--key {k.name}" for k in a.key)))
    print(json.dumps({"segments": len(summary["segments"]), "used": summary["coverage"]["segments_used"],
                      "updates": summary["completeness"]["updates"]["closed"]}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
