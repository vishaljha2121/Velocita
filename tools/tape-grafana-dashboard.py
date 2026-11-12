#!/usr/bin/env python3
"""Writes the verifiable tape's Grafana dashboard (deploy/tape/observability/dashboards/velocita-tape.json)."""
import json
from pathlib import Path

DS = {"type": "prometheus", "uid": "velocita-tape-prometheus"}
MACHINES = {"nuc1": "#3987e5", "pi": "#d95926", "nuc2": "#199e70"}
RECORDERS = {"1": "nuc1", "2": "pi", "3": "nuc2"}
GOOD, WARNING, SERIOUS, CRITICAL = "#0ca30c", "#fab219", "#ec835a", "#d03b3b"
OUTCOMES = [("agree_3_of_3", "3 of 3 agree", GOOD), ("agree_2_of_3", "2 of 3 agree", WARNING),
            ("only_1_of_3", "seen by 1 only", SERIOUS), ("divergent", "divergent", CRITICAL)]

panels = []
next_id = 1


def place(panel, w, h, x, y):
    global next_id
    panel.update({"id": next_id, "gridPos": {"w": w, "h": h, "x": x, "y": y}, "datasource": DS})
    next_id += 1
    panels.append(panel)


def target(expr, legend="", ref="A", instant=False):
    t = {"refId": ref, "expr": expr, "legendFormat": legend, "datasource": DS}
    if instant:
        t["instant"], t["range"] = True, False
    return t


def name_color(name, color, display=None):
    props = [{"id": "color", "value": {"mode": "fixed", "fixedColor": color}}]
    if display:
        props.append({"id": "displayName", "value": display})
    return {"matcher": {"id": "byName", "options": name}, "properties": props}


def row(title, y):
    global next_id
    panels.append({"id": next_id, "type": "row", "title": title, "collapsed": False,
                   "gridPos": {"w": 24, "h": 1, "x": 0, "y": y}, "panels": []})
    next_id += 1


def stat(title, expr, desc, x, y, w=4, unit="none", steps=None, mappings=None, decimals=0):
    steps = steps or [{"color": "text", "value": None}]
    place({"type": "stat", "title": title, "description": desc, "targets": [target(expr, instant=True)],
           "options": {"reduceOptions": {"calcs": ["lastNotNull"], "values": False}, "colorMode": "value",
                       "graphMode": "none", "textMode": "value", "justifyMode": "center"},
           "fieldConfig": {"defaults": {"unit": unit, "decimals": decimals, "mappings": mappings or [],
                                        "thresholds": {"mode": "absolute", "steps": steps}}, "overrides": []}},
          w, 4, x, y)


def series(title, targets, desc, x, y, w=12, h=8, unit="none", overrides=None, stack=False, min0=True,
           thresholds=None, decimals=None):
    defaults = {"unit": unit, "custom": {"lineWidth": 2, "fillOpacity": 25 if stack else 0, "showPoints": "never",
                                         "stacking": {"mode": "normal" if stack else "none"},
                                         "spanNulls": False, "axisSoftMin": 0 if min0 else None}}
    if decimals is not None:
        defaults["decimals"] = decimals
    if thresholds:
        defaults["thresholds"] = {"mode": "absolute", "steps": thresholds}
        defaults["custom"]["thresholdsStyle"] = {"mode": "dashed"}
    place({"type": "timeseries", "title": title, "description": desc, "targets": targets,
           "options": {"legend": {"displayMode": "list", "placement": "bottom", "showLegend": True},
                       "tooltip": {"mode": "multi", "sort": "desc"}},
           "fieldConfig": {"defaults": defaults, "overrides": overrides or []}}, w, h, x, y)


machine_overrides = [name_color(m, c) for m, c in MACHINES.items()]
recorder_overrides = [name_color(f"recorder {r}", MACHINES[m], f"{m} recorder") for r, m in RECORDERS.items()]
outcome_overrides = [name_color(o, c, label) for o, label, c in OUTCOMES]

row("Now", 0)
stat("Books valid", 'min(sum by (host) (velocita_tape_book_state{state="valid",source="log"}))',
     "Order books every voter currently holds as valid, out of 12. Lower means a book is waiting for its next snapshot.",
     0, 1, steps=[{"color": SERIOUS, "value": None}, {"color": GOOD, "value": 12}])
stat("Leader", "max(velocita_tape_raft_leader_id)", "The voter that orders the log right now.", 4, 1,
     mappings=[{"type": "value", "options": {"1": {"text": "nuc1", "color": MACHINES["nuc1"]},
                                            "2": {"text": "pi", "color": MACHINES["pi"]},
                                            "3": {"text": "nuc2", "color": MACHINES["nuc2"]},
                                            "-1": {"text": "electing", "color": WARNING}}}])
stat("Raft term", "max(velocita_tape_raft_term)", "Increases by one at every election. A steady value means a stable leader.", 8, 1)
stat("Ticks", "max(velocita_tape_last_tick)", "One tick per second of replicated time. It should climb steadily.", 12, 1)
stat("Signed tree heads", "max(velocita_tape_published_heads)",
     "Merkle tree heads published with at least two voter signatures (one a minute).", 16, 1)
stat("Divergent updates", 'max(velocita_tape_verdicts_total{kind="update",outcome="divergent"})',
     "Updates where recorders received different bytes for the same message. Should stay at 0.", 20, 1,
     steps=[{"color": GOOD, "value": None}, {"color": CRITICAL, "value": 1}])

row("Agreement between the three vantage points", 5)
series("Update verdicts per minute",
       [target(f'max(rate(velocita_tape_verdicts_total{{kind="update",outcome="{o}"}}[2m])) * 60', o, chr(65 + i))
        for i, (o, _, _) in enumerate(OUTCOMES)],
       "Every published update gets one verdict: how many of the three recorders saw identical bytes before it closed.",
       0, 6, overrides=outcome_overrides, stack=True, unit="short")
series("Observations committed per recorder",
       [target("max by (recorder) (rate(velocita_tape_recorder_requests_total[2m]))", "recorder {{recorder}}")],
       "Frames each machine's recorder got into the log, per second. The three lines should track each other.",
       12, 6, overrides=recorder_overrides, unit="short", decimals=2)
series("Arrival skew between recorders",
       [target("histogram_quantile(0.5, max by (le) (rate(velocita_tape_vantage_skew_seconds_bucket[5m])))", "median", "A"),
        target("histogram_quantile(0.99, max by (le) (rate(velocita_tape_vantage_skew_seconds_bucket[5m])))", "p99", "B")],
       "Time between the first and last recorder receiving the same update.", 0, 14, unit="s",
       overrides=[name_color("median", "#3987e5"), name_color("p99", "#9085e9")])
series("Late copies per minute", [target("max(rate(velocita_tape_late_copies_total[5m])) * 60", "late copies")],
       "Copies that arrived after their verdict had closed (they are kept, but do not change the verdict).",
       12, 14, unit="short", overrides=[name_color("late copies", MACHINES["pi"])])

row("Order books", 22)
place({"type": "state-timeline", "title": "Book state per symbol",
       "description": "Green while the reconstructed book is valid; red while it waits for a snapshot after a gap or divergence.",
       "targets": [target('max by (symbol) (velocita_tape_book_state{state="valid",source="log"})', "{{symbol}}")],
       "options": {"showValue": "never", "rowHeight": 0.8, "mergeValues": True, "legend": {"showLegend": False},
                   "tooltip": {"mode": "single"}},
       "fieldConfig": {"defaults": {"mappings": [{"type": "value", "options": {
           "1": {"text": "valid", "color": GOOD}, "0": {"text": "not valid", "color": CRITICAL}}}],
           "color": {"mode": "thresholds"}, "thresholds": {"mode": "absolute", "steps": [{"color": CRITICAL, "value": None},
                                                                                        {"color": GOOD, "value": 1}]}},
           "overrides": []}}, 16, 10, 0, 23)
place({"type": "bargauge", "title": "Longest silence between DEPTH messages",
       "description": "The longest gap ever seen between two DEPTH messages for each book. Long gaps are normal outside US market hours.",
       "targets": [target("max by (symbol) (velocita_tape_book_max_silence_seconds)", "{{symbol}}", instant=True)],
       "options": {"orientation": "horizontal", "displayMode": "basic", "showUnfilled": True,
                   "reduceOptions": {"calcs": ["lastNotNull"], "values": False}},
       "fieldConfig": {"defaults": {"unit": "s", "min": 0, "color": {"mode": "fixed", "fixedColor": "#3987e5"},
                                    "thresholds": {"mode": "absolute", "steps": [{"color": "#3987e5", "value": None}]}},
                       "overrides": []}}, 8, 10, 16, 23)

row("Cluster and network", 33)
series("Log entries committed per second", [target("rate(velocita_tape_raft_commit_index[1m])", "{{host}}")],
       "Each voter's commit rate. All three should overlap.", 0, 34, w=8, overrides=machine_overrides, decimals=1)
series("Entries behind the leader",
       [target("scalar(max(velocita_tape_raft_commit_index)) - velocita_tape_raft_commit_index", "{{host}}")],
       "How far each voter's committed log trails the furthest one.", 8, 34, w=8, overrides=machine_overrides)
series("Tailscale send per host",
       [target('rate(node_network_transmit_bytes_total{device="tailscale0"}[1m])', "{{host}}")],
       "Everything each host sends over the tailnet. The dashed line is the watchdog's limit (600 KB/s for 60 s stops the tape).",
       16, 34, w=8, unit="Bps", overrides=machine_overrides,
       thresholds=[{"color": "transparent", "value": None}, {"color": CRITICAL, "value": 614400}])
series("Recorder events per 10 minutes",
       [target("sum by (event) (max by (event, recorder) (increase(velocita_tape_recorder_events_total[10m])))", "{{event}}")],
       "Gaps, resubscriptions, reconnects and dead connections the recorders reported.", 0, 42, w=8, unit="short",
       overrides=[name_color("resubscribe", "#3987e5"), name_color("gap", "#d95926"), name_color("reconnect", "#199e70"),
                  name_color("dead_connection", "#c98500"), name_color("overflow", "#d55181"),
                  name_color("regression", "#9085e9"), name_color("session_start", "#008300"), name_color("oversize", "#e66767")])
series("Rejected frames and refused submissions",
       [target("rate(velocita_tape_rejected_frames_total[5m])", "{{host}} rejected", "A"),
        target("rate(velocita_tape_refused_submissions_total[5m])", "{{host}} refused", "B")],
       "Frames that failed authentication and submissions a voter refused. Should stay at 0.", 8, 42, w=8,
       unit="short", decimals=2)
series("Free disk per host", [target('min by (host) (node_filesystem_avail_bytes{mountpoint="/"})', "{{host}}")],
       "Root filesystem space left. The tape's log grows about 300 MB a day.", 16, 42, w=8, unit="bytes",
       overrides=machine_overrides)

def process_overrides():
    out = []
    for m, c in MACHINES.items():
        out.append(name_color(f"{m} voter", c))
        rec = name_color(f"{m} recorder", c)
        rec["properties"].append({"id": "custom.lineStyle", "value": {"fill": "dash", "dash": [10, 6]}})
        out.append(rec)
    return out


row("Aeron transport (what the tape itself puts on the wire)", 50)
series("Aeron bytes sent",
       [target("sum by (host, process) (rate(velocita_tape_aeron_bytes_sent_total[1m]))", "{{host}} {{process}}")],
       "Bytes each tape process handed to the network, including retransmissions. Solid: voter. Dashed: recorder.",
       0, 51, w=8, unit="Bps", overrides=process_overrides())
series("Tape share of Tailscale send",
       [target('sum by (host) (rate(velocita_tape_aeron_bytes_sent_total[1m])) / on (host) '
               'rate(node_network_transmit_bytes_total{device="tailscale0"}[1m])', "{{host}}")],
       "Fraction of each host's tailnet traffic that came from the tape. Near 1 during a flood means the tape is the sender; "
       "near 0 means something else is.", 8, 51, w=8, unit="percentunit", overrides=machine_overrides, decimals=0)
series("Retransmitted bytes",
       [target("sum by (host, process) (rate(velocita_tape_aeron_retransmitted_bytes_total[1m]))", "{{host}} {{process}}")],
       "Bytes Aeron sent again because a receiver reported loss. Spikes here mean the Wi-Fi dropped packets.",
       16, 51, w=8, unit="Bps", overrides=process_overrides())
series("NAKs received",
       [target("sum by (host, process) (rate(velocita_tape_aeron_naks_received_total[1m])) * 60", "{{host}} {{process}}")],
       "Loss reports from receivers, per minute. Each one can trigger a retransmission.", 0, 59, w=8, unit="short",
       overrides=process_overrides())
series("Transport trouble per 10 minutes",
       [target("sum by (host) (increase(velocita_tape_aeron_back_pressure_events_total[10m]))", "{{host}} back-pressure", "A"),
        target("sum by (host) (increase(velocita_tape_link_dropped_total[10m]))", "{{host}} dropped sends", "B"),
        target("sum by (host) (increase(velocita_tape_aeron_loss_gap_fills_total[10m]))", "{{host}} gap fills", "C"),
        target("sum by (host) (increase(velocita_tape_aeron_errors_total[10m]))", "{{host}} errors", "D")],
       "Back-pressure (a peer not keeping up), sends the tape dropped because Aeron refused them, gaps Aeron gave up on, "
       "and driver errors.", 8, 59, w=8, unit="short")
series("Recorder frames received per second",
       [target("rate(velocita_tape_recorder_frames_total[2m])", "{{host}} recorder")],
       "Frames each recorder's WebSocket delivered, before anything reaches the tape. Compare with "
       "\"Observations committed per recorder\" to see whether frames are lost on the way in.", 16, 59, w=8, unit="short",
       overrides=process_overrides(), decimals=2)

dashboard = {
    "uid": "velocita-tape", "title": "Verifiable tape", "tags": ["velocita", "tape"], "timezone": "utc",
    "editable": False, "graphTooltip": 1, "refresh": "10s", "schemaVersion": 39,
    "time": {"from": "now-3h", "to": "now"}, "panels": panels,
}
out = Path(__file__).resolve().parent.parent / "deploy/tape/observability/dashboards/velocita-tape.json"
out.write_text(json.dumps(dashboard, indent=1) + "\n")
print(f"wrote {out} with {sum(1 for p in panels if p['type'] != 'row')} panels")
