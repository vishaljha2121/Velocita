#!/usr/bin/env python3
"""Generates every Velocita V2 Grafana dashboard into grafana/dashboards/.

Five focused dashboards share one link bar. Every panel answers one operator
question. Regenerate after editing (an optional argument writes elsewhere):
    python3 deploy/v2/observability/generate_dashboard.py [out_dir]

The Overview keeps the uid and file name of the former single dashboard
(velocita-v2-cluster). Provisioning sets disableDeletion, so a removed file
would leave its dashboard behind as an unprovisioned stale copy; reusing the
uid updates it in place, and bookmarks and the home dashboard keep working.
"""
import json
import pathlib
import sys

DS = {"type": "prometheus", "uid": "velocita-v2-prometheus"}

OVERVIEW = ("velocita-v2-cluster", "velocita-v2-cluster.json", "Velocita — Overview", "Overview")
RAFT = ("velocita-v2-raft", "velocita-v2-raft.json", "Velocita — Raft & storage", "Raft & storage")
STRATEGY = ("velocita-v2-strategy", "velocita-v2-strategy.json", "Velocita — Strategy (shadow)", "Strategy (shadow)")
PAPER = ("velocita-v2-paper", "velocita-v2-paper.json", "Velocita — Paper trading", "Paper trading")
HOSTS = ("velocita-v2-hosts", "velocita-v2-hosts.json", "Velocita — Hosts", "Hosts")
DASHBOARDS = (OVERVIEW, RAFT, STRATEGY, PAPER, HOSTS)

LINKS = [{"type": "link", "title": short, "url": f"/d/{uid}", "icon": "dashboard", "tooltip": title,
          "targetBlank": False, "keepTime": True, "includeVars": False, "asDropdown": False, "tags": []}
         for uid, _, title, short in DASHBOARDS]

panels = []
_next_id = 1
_y = 0


def start():
    global panels, _next_id, _y
    panels, _next_id, _y = [], 1, 0


def _id():
    global _next_id
    _next_id += 1
    return _next_id - 1


def row(title):
    global _y
    panels.append({"type": "row", "title": title, "id": _id(), "collapsed": False,
                   "gridPos": {"h": 1, "w": 24, "x": 0, "y": _y}, "panels": []})
    _y += 1


def thresholds(*steps):
    return {"mode": "absolute", "steps": [{"color": c, "value": v} for c, v in steps]}


def stat(title, expr, x, w, unit="none", steps=(("green", None),), legend="", description="", h=4, mappings=None,
         decimals=None, no_value="unknown"):
    defaults = {"unit": unit, "thresholds": thresholds(*steps), "color": {"mode": "thresholds"},
                "mappings": mappings or [], "noValue": no_value}
    if decimals is not None:
        defaults["decimals"] = decimals
    panels.append({
        "type": "stat", "title": title, "id": _id(), "datasource": DS, "description": description,
        "gridPos": {"h": h, "w": w, "x": x, "y": _y},
        "targets": [{"expr": expr, "legendFormat": legend, "refId": "A", "instant": True}],
        "fieldConfig": {"defaults": defaults, "overrides": []},
        "options": {"colorMode": "background", "graphMode": "none", "justifyMode": "center",
                    "textMode": "value_and_name" if legend else "value",
                    "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
                    "orientation": "horizontal", "wideLayout": True},
    })


def series(title, targets, x, w, unit="none", h=8, description="", stack=False, min0=True):
    panels.append({
        "type": "timeseries", "title": title, "id": _id(), "datasource": DS, "description": description,
        "gridPos": {"h": h, "w": w, "x": x, "y": _y},
        "targets": [{"expr": e, "legendFormat": l, "refId": chr(65 + i)} for i, (e, l) in enumerate(targets)],
        "fieldConfig": {"defaults": {"unit": unit, "min": 0 if min0 else None, "custom": {
            "lineWidth": 2, "fillOpacity": 10, "showPoints": "never", "spanNulls": False,
            "stacking": {"mode": "normal" if stack else "none"}}}, "overrides": []},
        "options": {"legend": {"displayMode": "list", "placement": "bottom"},
                    "tooltip": {"mode": "multi", "sort": "desc"}},
    })


def bars(title, targets, x, w, unit="none", h=8, description="", decimals=0):
    panels.append({
        "type": "bargauge", "title": title, "id": _id(), "datasource": DS, "description": description,
        "gridPos": {"h": h, "w": w, "x": x, "y": _y},
        "targets": [{"expr": e, "legendFormat": l, "refId": chr(65 + i), "instant": True} for i, (l, e) in enumerate(targets)],
        "fieldConfig": {"defaults": {"unit": unit, "decimals": decimals, "min": 0, "color": {"mode": "fixed", "fixedColor": "blue"},
                                     "thresholds": thresholds(("blue", None))}, "overrides": []},
        "options": {"orientation": "horizontal", "displayMode": "basic", "showUnfilled": True, "valueMode": "color",
                    "namePlacement": "left", "sizing": "auto",
                    "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False}},
    })


def advance(h):
    global _y
    _y += h


def finish(board, description, tags):
    uid, file, title, _ = board
    return file, {
        "uid": uid, "title": title, "description": description,
        "schemaVersion": 41, "version": 1, "editable": False, "refresh": "10s",
        "time": {"from": "now-3h", "to": "now"}, "tags": ["velocita", "v2", *tags],
        "timezone": "browser", "links": LINKS, "panels": panels, "templating": {"list": []},
        "annotations": {"list": []},
    }


ROLE_MAP = [{"type": "value", "options": {"0": {"text": "none"}, "1": {"text": "follower"},
                                          "2": {"text": "candidate", "color": "orange"},
                                          "3": {"text": "LEADER", "color": "blue"}}}]
READY_MAP = [{"type": "value", "options": {"0": {"text": "NOT READY"}, "1": {"text": "READY"}}}]
GATEWAY_UP_MAP = [{"type": "value", "options": {"0": {"text": "DOWN"}, "1": {"text": "UP"}}}]
NEW_RISK_MAP = [{"type": "value", "options": {"0": {"text": "NEW RISK ALLOWED"}, "1": {"text": "NEW RISK STOPPED"}}}]
NEW_RISK_HELP = ("1 = the gateway sends no risk-increasing order. After an election it re-arms by itself once the "
                 "cluster is quiet and the venue reconciles; after an unknown outcome, a conflict, a control change "
                 "or a reconciliation mismatch only a restart (after checking the gateway journal) re-arms it.")


def right_now_row():
    row("Right now: is the cluster safe and able to commit?")
    stat("Voters reporting", 'sum(velocita_v2_up)', 0, 4, steps=(("red", None), ("orange", 2), ("green", 3)),
         description="Voters whose health page is readable, of three. Two are needed to commit: at 2 the cluster "
                     "still commits but one more loss stops it (orange); below 2 nothing commits (red).")
    stat("Leader", 'max(velocita_v2_leader_id)', 4, 4, steps=(("red", None), ("green", 1)),
         description="The voter the followers follow. 'unknown' or -1 means an election is in progress.")
    stat("Term", 'max(velocita_v2_term)', 8, 4,
         description="Raft term. It only changes when an election happens.")
    stat("Elections (last hour)", 'increase(max(velocita_v2_term)[1h:15s])', 12, 4,
         steps=(("green", None), ("orange", 1), ("red", 5)),
         description="How many times the term increased in the last hour. Healthy steady state: 0.")
    stat("Telemetry age", 'max(velocita_v2_telemetry_age_seconds)', 16, 4, unit="s",
         steps=(("green", None), ("orange", 2), ("red", 10)),
         description="Seconds since the stalest voter last published health. Stale data is shown as stale, never as healthy.")
    stat("Failed voters", 'sum(velocita_v2_failed)', 20, 4, steps=(("green", None), ("red", 1)),
         description="Voters that latched a fatal fault and stopped (fail-stop). Must be 0.")
    advance(4)
    stat("Role", 'velocita_v2_role{role="leader"} * 3 + velocita_v2_role{role="candidate"} * 2 + velocita_v2_role{role="follower"}',
         0, 12, legend="node {{node}} ({{host}})", mappings=ROLE_MAP,
         description="Each voter's role: exactly one LEADER and one follower is healthy.")
    stat("Waiting for quorum", 'max(velocita_v2_log_index{kind="current"} - on(host,node) velocita_v2_log_index{kind="commit"})',
         12, 6, steps=(("green", None), ("orange", 1), ("red", 20)),
         description="Entries appended but not yet committed. Stays >0 when the other voter is down or unreachable.")
    stat("Commits (last hour)", 'max(increase(velocita_v2_log_index{kind="commit"}[1h]))', 18, 6,
         description="Log entries committed in the last hour.")
    advance(4)
    stat("Committed index", 'max(velocita_v2_log_index{kind="commit"})', 0, 4,
         description="Highest committed log index on any voter. Raft & storage shows it per voter.")
    stat("Waiting to apply", 'max(velocita_v2_log_index{kind="commit"} - on(host,node) velocita_v2_log_index{kind="applied"})',
         4, 4, steps=(("green", None), ("orange", 1), ("red", 20)),
         description="Committed entries a voter has not applied yet. Healthy: 0.")
    stat("Voters accepting commands", 'sum(velocita_v2_ready)', 8, 4, steps=(("red", None), ("orange", 2), ("green", 3)),
         description="Voters past their recovery barrier, of three.")
    stat("Submitter ready", 'max(velocita_shadow_submitter_ready)', 12, 4,
         steps=(("red", None), ("green", 1)), mappings=READY_MAP,
         description="1 once the policy is installed and the strategy mode is normal.")
    stat("Paper gateway", 'max(velocita_gateway_up)', 16, 4, steps=(("red", None), ("green", 1)), mappings=GATEWAY_UP_MAP,
         description="The paper-trading gateway process. 'unknown' = not deployed or not scraped.")
    stat("New risk", 'max(velocita_gateway_new_risk_stopped)', 20, 4, steps=(("green", None), ("red", 1)),
         mappings=NEW_RISK_MAP, description=NEW_RISK_HELP)
    advance(4)


def alerts_row():
    row("Alerts")
    panels.append({"type": "alertlist", "title": "Firing and pending alerts", "id": _id(),
                   "gridPos": {"h": 8, "w": 24, "x": 0, "y": _y},
                   "options": {"alertListOptions": {"datasource": "velocita-v2-prometheus"},
                               "groupMode": "default", "maxItems": 20, "sortOrder": 1, "viewMode": "list",
                               "stateFilter": {"firing": True, "pending": True, "normal": False, "error": True},
                               "alertInstanceLabelFilter": "", "showInstances": True}})
    advance(8)


def secrets_row():
    row("Secrets: RAM-only, pushed from the Mac after every reboot")
    stat("Secrets present", 'min by (host) (velocita_secret_present)', 0, 12, legend="{{host}}",
         steps=(("red", None), ("green", 1)), mappings=[{"type": "value", "options": {
             "0": {"text": "PUSH SECRETS"}, "1": {"text": "OK"}}}],
         description="Per voter host: are all key files its config names present? Never reads a secret. "
                     "Red: see the VelocitaSecretsMissing alert for the exact push command.")
    stat("Missing secrets", 'count by (host, secret) (velocita_secret_present == 0)', 12, 12,
         legend="{{host}}: {{secret}}", steps=(("red", None),),
         description="Each missing secret by host. Empty ('unknown') means nothing is missing.")
    advance(4)


def overview():
    start()
    right_now_row()
    alerts_row()
    secrets_row()
    return finish(OVERVIEW, "Cluster health at a glance; the link bar opens the detail dashboards. "
                            "Read-only: nothing here grants authority to trade.", ["overview"])


def raft_log_row():
    row("Raft log & compaction")
    stat("Log used (ordinary)", '100 * velocita_v2_retained_log_entries{pool="ordinary"} / '
         'velocita_v2_retained_log_capacity{pool="ordinary"}', 0, 8, unit="percent", legend="{{host}}",
         steps=(("green", None), ("orange", 75), ("red", 100)),
         description="Retained Raft log entries of 8,128. Compaction reclaims only what every voter holds, so this "
                     "climbs only while a voter is not keeping up. At 100% every normal command is refused.")
    stat("Time until full", '(velocita_v2_retained_log_capacity{pool="ordinary"} - '
         'velocita_v2_retained_log_entries{pool="ordinary"}) / '
         'clamp_min(deriv(velocita_v2_retained_log_entries{pool="ordinary"}[30m]), 1e-9)', 8, 8, unit="s",
         legend="{{host}}", steps=(("red", None), ("orange", 6 * 3600), ("green", 24 * 3600)),
         description="Remaining ordinary entries at the last 30 minutes' fill rate.")
    stat("Kill reserve used", 'velocita_v2_retained_log_entries{pool="protected"}', 16, 8, legend="{{host}}",
         steps=(("green", None), ("orange", 32), ("red", 56)),
         description="Of 64 entries kept for operator kill-path commands (kill epoch, executor generation, strategy mode, "
                     "mass cancel). Returned only when compaction reclaims those entries; each submitter start uses one.")
    advance(4)
    series("Commit, compaction floor and snapshot", [
        ('velocita_v2_log_index{kind="commit"}', "{{host}} commit"),
        ('velocita_v2_compaction_floor', "{{host}} floor (every voter holds up to here)"),
        ('velocita_v2_snapshot_index', "{{host}} snapshot (compacted up to here)"),
    ], 0, 12, description="Healthy: the floor follows the commit closely and the snapshot steps up behind it. A flat floor "
                          "while commits continue means some voter is not acknowledging, and nothing can be reclaimed.")
    stat("Stranded voters", 'max(velocita_v2_stranded_voters)', 12, 4, steps=(("green", None), ("red", 1)), h=8,
         description="Voters behind the leader's snapshot: they need repair (snapshot install is M6). Must be 0.")
    stat("Last compaction pause", 'velocita_v2_last_compaction_seconds', 16, 4, unit="s", legend="{{host}}", h=8,
         steps=(("green", None), ("orange", 0.5), ("red", 1)),
         description="Raft makes no progress on a node while it compacts; on a leader, near 1 s risks an election.")
    stat("Compactions", 'velocita_v2_compactions_total', 20, 4, legend="{{host}}", h=8,
         description="Completed compactions per voter since its last restart.")
    advance(8)


def progress_row():
    row("Progress: where is work waiting?")
    series("Log positions per voter", [
        ('velocita_v2_log_index{kind="current"}', "appended node {{node}}"),
        ('velocita_v2_log_index{kind="durable"}', "on disk node {{node}}"),
        ('velocita_v2_log_index{kind="commit"}', "committed node {{node}}"),
        ('velocita_v2_log_index{kind="applied"}', "applied node {{node}}"),
    ], 0, 12, description="Appended → on disk → committed → applied. Lines apart = work waiting at that stage.")
    series("Gaps between stages (entries)", [
        ('velocita_v2_log_index{kind="current"} - on(host,node) velocita_v2_log_index{kind="durable"}', "waiting for disk, node {{node}}"),
        ('velocita_v2_log_index{kind="current"} - on(host,node) velocita_v2_log_index{kind="commit"}', "waiting for quorum, node {{node}}"),
        ('velocita_v2_log_index{kind="commit"} - on(host,node) velocita_v2_log_index{kind="applied"}', "waiting to apply, node {{node}}"),
    ], 12, 12, description="Which stage is holding work up, per voter. Healthy: all near 0.")
    advance(8)
    series("Commit rate", [('rate(velocita_v2_log_index{kind="commit"}[1m]) * 60', "node {{node}}")], 0, 8, unit="short",
           description="Entries committed per minute.")
    series("Owner loop turns per second", [('rate(velocita_v2_owner_turns_total[1m])', "node {{node}}")], 8, 8,
           description="The voter's main loop heartbeat. A drop towards 0 means the loop is stalling.")
    series("Term (elections)", [('velocita_v2_term', "node {{node}}")], 16, 8, min0=False,
           description="Flat line = no elections. Steps = elections (restarts, partitions, slow disks).")
    advance(8)


def refusals_row():
    row("Refusals and pressure: is anything being rejected or backed up?")
    series("Security refusals per minute", [
        ('rate(velocita_v2_auth_refusals_total[5m]) * 60', "authentication, node {{node}}"),
        ('rate(velocita_v2_fence_refusals_total[5m]) * 60', "replay/stale session, node {{node}}"),
    ], 0, 8, description="Forged, foreign or replayed frames. Anything above 0 deserves a look.")
    series("Caller command refusals per minute", [('rate(velocita_v2_ingress_refusals_total[5m]) * 60', "node {{node}}")],
           8, 8, description="Mostly 'not leader' redirects (normal, one per redirected command). A burst of refusals means capacity or impersonation.")
    series("Peer pressure per second", [
        ('rate(velocita_v2_peer_backpressure_total[1m])', "peer unreachable/slow, node {{node}}"),
        ('rate(velocita_v2_peer_busy_holds_total[1m])', "held while disk busy, node {{node}}"),
    ], 16, 8, description="Backpressure = the other voter is down or slow (counted per loop turn). Busy holds = local disk syncs delaying peer frames.")
    advance(8)
    series("Results delivered to callers", [
        ('increase(velocita_v2_results_sent_total[5m])', "sent, node {{node}}"),
        ('increase(velocita_v2_results_dropped_total[5m])', "dropped (no listener), node {{node}}"),
    ], 0, 12, description="Dropped usually means the caller already exited (e.g. it timed out).")
    series("Restarts and secret reloads", [
        ('velocita_v2_session_epoch', "session epoch (restarts), node {{node}}"),
        ('velocita_v2_secret_reloads_total', "secret reloads, node {{node}}"),
        ('velocita_v2_secret_reload_failures_total', "refused reloads, node {{node}}"),
    ], 12, 12, description="Session epoch rises on every process start. Reload failures mean a bad secret push (old keys kept).")
    advance(8)


DISK_WRITE_LATENCY = (
    '1000 * rate(node_disk_write_time_seconds_total{device=~"nvme0n1|mmcblk0|sd[a-z]"}[1m]) / '
    'clamp_min(rate(node_disk_writes_completed_total{device=~"nvme0n1|mmcblk0|sd[a-z]"}[1m]), 1e-9)')
IOWAIT = '100 * avg by (host) (rate(node_cpu_seconds_total{mode="iowait"}[1m]))'
ROOT_FREE = '100 * node_filesystem_avail_bytes{mountpoint="/"} / node_filesystem_size_bytes{mountpoint="/"}'


def wal_disk_row():
    row("Disk under the WAL (the same panels are on Hosts)")
    series("Disk write latency (avg ms per write)", [(DISK_WRITE_LATENCY, "{{host}} {{device}}")],
           0, 8, unit="ms", description="Average time per completed write. The Pi's SD card runs roughly 10-50 ms.")
    series("Waiting on disk (iowait %)", [(IOWAIT, "{{host}}")],
           8, 8, unit="percent", description="High on the Pi = the SD card is the bottleneck.")
    series("Root disk free %", [(ROOT_FREE, "{{host}}")], 16, 8, unit="percent",
           description="The WAL and archive refuse new work before a disk fills (alert below 15%).")
    advance(8)


def soak_row():
    row("Soak test")
    stat("Soak commands OK", 'sum(velocita_soak_commands_total{result="ok"})', 0, 6, steps=(("green", None),))
    stat("Soak failures", 'sum(velocita_soak_commands_total{result!="ok"})', 6, 6, steps=(("green", None), ("red", 1)),
         description="Timeouts or refusals seen by the soak bot. Pass criterion: 0.")
    stat("Soak p99 latency (15m)", 'histogram_quantile(0.99, sum by (le) (rate(velocita_soak_latency_ms_bucket[15m])))',
         12, 6, unit="ms", steps=(("green", None), ("orange", 250), ("red", 1000)))
    stat("Soak bot heartbeat", 'time() - max(velocita_soak_last_update_timestamp_seconds)', 18, 6, unit="s",
         steps=(("green", None), ("orange", 30), ("red", 120)),
         description="Seconds since the soak bot last reported. Large = the bot stopped.")
    advance(4)
    series("Soak round-trip latency (commit, ms)", [
        ('velocita_soak_last_latency_ms', "last command"),
        ('histogram_quantile(0.5, sum by (le) (rate(velocita_soak_latency_ms_bucket[5m])))', "p50 (5m)"),
        ('histogram_quantile(0.99, sum by (le) (rate(velocita_soak_latency_ms_bucket[5m])))', "p99 (5m)"),
    ], 0, 24, unit="ms", description="Soak client start → committed result. Each command is a NEW client process that "
                                     "starts Aeron and connects first, so this is dominated by cold start, not commit latency.")
    advance(8)


def raft():
    start()
    raft_log_row()
    progress_row()
    refusals_row()
    wal_disk_row()
    soak_row()
    return finish(RAFT, "Raft log capacity and compaction, durability and commit progress, elections, refusals, "
                        "the disks under the WAL, and the soak bot.", ["raft"])


def shadow_edge_row():
    row("Shadow edge: live ticks to recorded decisions (never trades)")
    stat("Submitter ready", 'max(velocita_shadow_submitter_ready)', 0, 6,
         steps=(("red", None), ("green", 1)), mappings=READY_MAP,
         description="1 once the policy is installed and the strategy mode is normal.")
    stat("Last decision", 'max(velocita_shadow_last_decision_age_seconds)', 6, 6, unit="s",
         steps=(("green", None), ("orange", 30), ("red", 120)),
         description="Seconds since the cluster last recorded a decision. Ticks arrive every few seconds.")
    stat("Rule agreement", 'sum(velocita_shadow_agreement_total{result="agree"}) / '
         'clamp_min(sum(velocita_shadow_agreement_total), 1)', 12, 6, unit="percentunit",
         steps=(("red", None), ("green", 1)),
         description="Share of decisions where the cluster matched the rule the Python edge predicted. Must be 100%.")
    stat("Edge errors", 'sum(velocita_shadow_errors_total)', 18, 6, steps=(("green", None), ("red", 1)),
         description="Submitter restarts, refused ticks or unparseable output.")
    advance(4)
    series("Tick to decision (ms)", [
        ('histogram_quantile(0.5, sum by (le) (rate(velocita_shadow_tick_to_decision_ms_bucket[5m])))', "p50 (5m)"),
        ('histogram_quantile(0.99, sum by (le) (rate(velocita_shadow_tick_to_decision_ms_bucket[5m])))', "p99 (5m)"),
    ], 0, 12, unit="ms", description="Tick received by the edge -> decision committed by the cluster.")
    series("Where the time goes (average ms per tick, 5m)", [
        ('sum by (hop) (rate(velocita_shadow_hop_ms_sum[5m])) / '
         'scalar(clamp_min(sum(rate(velocita_shadow_tick_to_decision_ms_count[5m])), 1e-9))', "{{hop}}"),
    ], 12, 12, unit="ms", stack=True,
           description="edge = tick received -> submitter read; source_commit = source event committed; "
                       "decision_commit = decision frame committed. Commit hops include the follower durable-ack delay.")
    advance(8)
    series("Decisions by resolution (per minute)", [
        ('sum by (resolution) (60 * rate(velocita_shadow_action_resolutions_total[5m]))', "resolution {{resolution}}"),
    ], 0, 8, description="1 = proposed (a real order would have been sized and bounded); 19 = market data stale; "
                         "29 = notional cap. Effective action is always none in shadow.")
    series("Rule matched (per minute)", [
        ('sum by (rule) (60 * rate(velocita_shadow_rule_matches_total[5m]))', "rule {{rule}}"),
    ], 8, 8, description="rule 0 = trend up (enter long); rule 1 = trend down or flat (exit).")
    series("Market data age at decision (ms)", [('max(velocita_shadow_market_data_age_ms)', "last tick")],
           16, 8, unit="ms", description="Age of the venue's price when the tick reached the edge. The policy refuses above 5 s.")
    advance(8)


def journey_row():
    row("Journey: TrueMarkets tick -> model -> kernel decision -> executor -> TrueMarkets")
    bars("Tick-to-trade funnel (selected time range)", [
        ("1  TM messages received", 'sum(increase(velocita_shadow_messages_total[$__range]))'),
        ("2  New BTC prices sent to the cluster", 'sum(increase(velocita_shadow_messages_total{outcome="tick"}[$__range]))'),
        ("3  Decisions recorded by the kernel", 'sum(increase(velocita_shadow_decisions_total{decision_code="17"}[$__range]))'),
        ("4  Trades proposed by the strategy",
         'sum(increase(velocita_shadow_directives_total{stage="proposed",directive!="no_action"}[$__range]))'),
        ("5  Trades authorized by the kernel",
         'sum(increase(velocita_shadow_directives_total{stage="effective",directive!="no_action"}[$__range])) or vector(0)'),
    ], 0, 16, description="Every stage counted from the edge's metrics for the dashboard's time range. 1 -> 2: True Markets "
                          "re-sends its last price every second; only a newer price is a tick. 3: the cluster committed a "
                          "decision frame (shadow_recorded). 4: the rule matched an enter/exit action and the kernel sized a "
                          "bounded order. 5: what the kernel authorized: always 0 in shadow mode, by design.")
    panels.append({
        "type": "text", "title": "6  Venue executor  ->  7  TrueMarkets", "id": _id(),
        "gridPos": {"h": 8, "w": 8, "x": 16, "y": _y},
        "options": {"mode": "markdown", "content":
            "**Not built yet, so nothing reaches TrueMarkets.**\n\n"
            "- Phase 1 (running): decisions are recorded, never authorized (shadow).\n"
            "- Phase 2: gateway + a TrueMarkets *simulator* (measures tick-to-trade; see Paper trading).\n"
            "- Live orders need M4-M7 and venue keys; no host holds a key.\n\n"
            "A proposal every tick is expected: in shadow no order ever fills, so the "
            "strategy never holds the position it asks for."},
    })
    advance(8)
    series("Journey per minute", [
        ('sum(60 * rate(velocita_shadow_messages_total[5m]))', "TM messages"),
        ('sum(60 * rate(velocita_shadow_messages_total{outcome="tick"}[5m]))', "new prices (ticks)"),
        ('sum(60 * rate(velocita_shadow_decisions_total{decision_code="17"}[5m]))', "decisions recorded"),
        ('sum(60 * rate(velocita_shadow_directives_total{stage="proposed",directive!="no_action"}[5m]))', "trades proposed"),
        ('sum(60 * rate(velocita_shadow_directives_total{stage="effective",directive!="no_action"}[5m])) or vector(0)',
         "trades authorized"),
    ], 0, 12, description="The funnel over time (5 min rate).")
    series("Messages by outcome (per minute)", [
        ('sum by (outcome) (60 * rate(velocita_shadow_messages_total[5m]))', "{{outcome}}"),
    ], 12, 12, stack=True, description="tick = a newer BTC price; resend = the same price again; throttled = newer, but within the "
                                        "edge's minimum tick interval; other = other topics or symbols; no_history = the "
                                        "daily history is stale (should be 0).")
    advance(8)


def shadow_book_row():
    row("Shadow book: $1,000 virtual, filled at measured TrueMarkets costs (never trades)")
    position_map = [{"type": "value", "options": {"0": {"text": "FLAT (USDC)"}, "1": {"text": "LONG (cbBTC)", "color": "blue"}}}]
    stat("Position", 'velocita_shadow_book_position', 0, 6, legend="{{timing}}", mappings=position_map,
         description="daily = acts once per UTC day on the last decision before midnight (like the backtest; this "
                     "one counts for promotion). tick = acts on every committed decision.")
    stat("Equity", 'velocita_shadow_book_equity_usd', 6, 6, unit="currencyUSD", legend="{{timing}}",
         description="Liquidation value: cash plus BTC sold now at the reference price minus the measured sell cost.")
    stat("Realized P&L", 'velocita_shadow_book_realized_pnl_usd', 12, 6, unit="currencyUSD", legend="{{timing}}",
         steps=(("red", None), ("green", 0)))
    stat("Fees paid", 'velocita_shadow_book_fees_usd', 18, 6, unit="currencyUSD", legend="{{timing}}",
         description="Measured all-in cost against the reference price (buy +109.9 bp, sell -123.4 bp on cbBTC).")
    advance(4)
    series("Equity vs buy-and-hold", [
        ('velocita_shadow_book_equity_usd', "{{timing}} book"),
        ('velocita_shadow_hold_equity_usd', "buy and hold (from the first price)"),
    ], 0, 12, unit="currencyUSD", min0=False,
           description="Both start at $1,000. The trend rule is expected to trail holding in a rising market and cut "
                       "drawdowns in a falling one (backtest: +135% vs +169%, max drawdown 57% vs 77%).")
    stat("Round trips", 'velocita_shadow_book_round_trips_total', 12, 6, legend="{{timing}} {{result}}", h=8,
         description="Completed buy-then-sell pairs; a win returned more than it cost, break-even counts as a loss.")
    stat("Orders", 'velocita_shadow_book_trades_total', 18, 6, legend="{{timing}}", h=8,
         description="Simulated fills. The backtest trades ~9.5 times a year on the daily book.")
    advance(8)
    panels.append({
        "type": "table", "title": "Latest simulated fills (newest first)", "id": _id(), "datasource": DS,
        "description": "The last 10 fills per book: time, side and the reference price they were costed against.",
        "gridPos": {"h": 8, "w": 24, "x": 0, "y": _y},
        "targets": [{"expr": 'velocita_shadow_book_fill * 1000', "refId": "A", "instant": True, "format": "table"}],
        "fieldConfig": {"defaults": {}, "overrides": [
            {"matcher": {"id": "byName", "options": "Value"},
             "properties": [{"id": "unit", "value": "dateTimeAsIso"}, {"id": "displayName", "value": "filled at"}]}]},
        "transformations": [{"id": "organize", "options": {
            "excludeByName": {"Time": True, "__name__": True, "job": True, "instance": True, "host": True, "cluster": True},
            "indexByName": {"timing": 0, "n": 1, "side": 2, "price": 3, "Value": 4}}},
            {"id": "sortBy", "options": {"sort": [{"field": "Value", "desc": True}]}}],
        "options": {"showHeader": True},
    })
    advance(8)


def model_row():
    row("Model: BTC trend, SMA 125")
    series("BTC price vs its 125-day average", [
        ('max(velocita_shadow_price_usd)', "BTC (TrueMarkets reference)"),
        ('max(velocita_shadow_price_usd) / (1 + max(velocita_shadow_feature_micros{name="trend"}) / 1e6)',
         "SMA 125 (daily closes + today)"),
    ], 0, 12, unit="currencyUSD", min0=False,
           description="The rule: price above its average -> hold BTC (enter long); at or below -> hold USDC (exit).")
    stat("Signal", 'max(velocita_shadow_feature_micros{name="trend"}) / 1e4', 12, 6, unit="percent",
         steps=(("red", None), ("green", 0.0001)), h=8,
         description="Price vs SMA 125 in percent. Green (> 0) = the strategy wants BTC; red = it wants USDC.")
    series("Other features (percent)", [
        ('max(velocita_shadow_feature_micros{name="ret1"}) / 1e4', "1-day return"),
        ('max(velocita_shadow_feature_micros{name="dev"}) / 1e4', "price vs SMA 20"),
        ('max(velocita_shadow_feature_micros{name="vol"}) / 1e4', "daily volatility"),
    ], 18, 6, unit="percent", min0=False, description="Computed every tick; the trend rule uses only the signal.")
    advance(8)


def strategy():
    start()
    shadow_edge_row()
    journey_row()
    shadow_book_row()
    model_row()
    return finish(STRATEGY, "The shadow strategy: live ticks to recorded decisions, the model's signal and the "
                            "$1,000 virtual book. Nothing here trades.", ["strategy", "shadow"])


STAGES = ("decision_to_authorized", "authorized_to_proof", "proof_to_sent", "sent_to_fill", "fill_to_committed")
CENTS = "suffix:¢"


def _stage_mean(stage, window):
    return (f'sum(increase(velocita_gateway_stage_ms_sum{{stage="{stage}"}}[{window}])) / '
            f'clamp_min(sum(increase(velocita_gateway_stage_ms_count{{stage="{stage}"}}[{window}])), 1e-9)')


def _stage_quantile(q, stage, window):
    return (f'histogram_quantile({q}, sum by (le) '
            f'(rate(velocita_gateway_stage_ms_bucket{{stage="{stage}"}}[{window}])))')


def paper_now_row():
    row("Right now: may the paper gateway take new risk?")
    stat("New risk", 'max(velocita_gateway_new_risk_stopped)', 0, 8, h=6, steps=(("green", None), ("red", 1)),
         mappings=NEW_RISK_MAP, description=NEW_RISK_HELP + " Alert: VelocitaGatewayNewRiskStopped.")
    stat("Gateway", 'max(velocita_gateway_up)', 8, 4, h=6, steps=(("red", None), ("green", 1)), mappings=GATEWAY_UP_MAP,
         description="1 while the gateway process runs. 'unknown' = not deployed, or its metrics are not scraped.")
    stat("Unknown outcomes (range)", 'sum(increase(velocita_gateway_unknown_outcomes_total[$__range]))', 12, 4, h=6,
         steps=(("green", None), ("red", 1)), decimals=0,
         description="Orders whose venue outcome the gateway could not establish. Each one stops new risk. Must be 0.")
    stat("Since last fill", 'time() - (max(velocita_gateway_last_fill_timestamp_seconds) > 0)', 16, 4, h=6, unit="s",
         steps=(("blue", None),), no_value="no fill since gateway start",
         description="Seconds since the last paper fill seen by the running gateway (it resets on restart). The daily "
                     "trend strategy trades a few times a year, so a long gap is normal.")
    stat("Fees paid", '100 * sum(velocita_gateway_fees_usd_total)', 20, 4, h=6, unit=CENTS, decimals=1,
         steps=(("blue", None),), description="All fees charged on paper fills since the gateway's counters began, in US cents.")
    advance(6)


RECONCILIATION_MAP = [{"type": "value", "options": {"0": {"text": "MATCH"}, "1": {"text": "MISMATCH"}}}]


def rearm_row():
    row("Stops, automatic re-arms and reconciliation")
    stat("Venue vs kernel", 'max(velocita_gateway_reconciliation_mismatch)', 0, 6, h=4,
         steps=(("green", None), ("red", 1)), mappings=RECONCILIATION_MAP,
         description="1 = at a comparable cut the paper venue's base differed from the kernel's position (zero-lot "
                     "tolerance). New risk stays stopped until a restart. Alert: VelocitaGatewayReconciliationMismatch.")
    stat("Compared position (lots)", 'max by (source) (velocita_gateway_reconciliation_position_lots)', 6, 6, h=4,
         legend="{{source}}", steps=(("blue", None),),
         description="Kernel and venue position at the last comparable cut; frozen at the values of a mismatch.")
    stat("Re-arms (range)", 'sum(increase(velocita_gateway_rearms_total[$__range]))', 12, 4, h=4, decimals=0,
         steps=(("blue", None),), description="Automatic re-arms after a fence (election or state-read gap).")
    stat("Since clean reconciliation",
         'time() - (max(velocita_gateway_reconciliation_last_match_timestamp_seconds) > 0)', 16, 8, h=4, unit="s",
         steps=(("green", None), ("orange", 300)),
         description="Seconds since the venue's book last matched the kernel at a comparable cut. 'unknown' = none "
                     "since the gateway started (a cut is comparable only with no order open).")
    advance(4)
    series("Stops by cause (per hour)", [
        ('sum by (cause) (increase(velocita_gateway_stops_total[1h]))', "{{cause}}"),
    ], 0, 12, stack=True, description="fence re-arms by itself after the quiet period; every other cause holds new "
                                      "risk until the gateway is restarted (runbook 5.3).")
    series("Why new risk is stopped now", [
        ('max by (cause) (velocita_gateway_stop_cause) > 0', "{{cause}}"),
    ], 12, 12, description="1 per active cause. awaiting_arm = only the arming check is pending (quiet period, an "
                           "open order, or a cut not yet comparable). Alert: VelocitaGatewayAwaitingRearm after 10 m.")
    advance(8)


def tick_to_trade_row():
    row("Tick to trade: decision -> authorized -> proof -> sent -> fill -> committed")
    stat("Tick to trade, mean (range)", ' + '.join(f'({_stage_mean(s, "$__range")})' for s in STAGES),
         0, 4, unit="ms", h=8, steps=(("blue", None),),
         description="Sum of the five stage means over the dashboard's time range: decision committed -> fill "
                     "committed. A stage with no orders in the range adds 0; 'unknown' until every stage has "
                     "seen at least one order.")
    series("Where the time goes (mean ms per order, 5m)", [(_stage_mean(s, "5m"), s) for s in STAGES],
           4, 12, unit="ms", stack=True,
           description="Stacked per-stage means, in the order an order passes through them; the top of the stack is "
                       "the mean tick-to-trade. sent_to_fill is the (simulated) venue.")
    bars("p99 by stage (selected time range)",
         [(s, f'histogram_quantile(0.99, sum by (le) (increase(velocita_gateway_stage_ms_bucket{{stage="{s}"}}[$__range])))')
          for s in STAGES], 16, 8, unit="ms", decimals=1,
         description="99th percentile of each stage over the dashboard's time range, from the stage histogram. "
                     "Percentiles do not add: this is not a p99 of the whole path.")
    advance(8)
    series("Stage latency p50 (5m)", [(_stage_quantile(0.5, s, "5m"), s) for s in STAGES], 0, 12, unit="ms",
           description="Median per stage. Resolution is limited by the histogram's buckets.")
    series("Stage latency p99 (5m)", [(_stage_quantile(0.99, s, "5m"), s) for s in STAGES], 12, 12, unit="ms",
           description="99th percentile per stage. With few orders in 5 minutes this is close to the slowest one.")
    advance(8)


def orders_row():
    row("Orders, fills, rejects and authority")
    stat("Orders sent (range)", 'sum(increase(velocita_gateway_orders_sent_total[$__range]))', 0, 6, decimals=0,
         steps=(("blue", None),))
    stat("Fills (range)", 'sum(increase(velocita_gateway_fills_total[$__range]))', 6, 6, decimals=0,
         steps=(("blue", None),))
    stat("Rejects (range)", 'sum(increase(velocita_gateway_rejects_total[$__range]))', 12, 6, decimals=0,
         steps=(("green", None), ("orange", 1)), description="Orders the (simulated) venue or the gateway refused.")
    stat("Authority requests (range)", 'sum(increase(velocita_gateway_authority_requests_total[$__range]))', 18, 6,
         decimals=0, steps=(("blue", None),), description="Requests the gateway made to the cluster for authority to send.")
    advance(4)
    series("Orders and fills per minute", [
        ('sum by (side) (60 * rate(velocita_gateway_orders_sent_total[5m]))', "sent {{side}}"),
        ('sum by (side) (60 * rate(velocita_gateway_fills_total[5m]))', "filled {{side}}"),
    ], 0, 6, description="Orders the gateway sent to the venue, and fills it received, by side.")
    series("Rejects by reason (per minute)", [
        ('sum by (reason) (60 * rate(velocita_gateway_rejects_total[5m]))', "{{reason}}"),
    ], 6, 6, stack=True, description="Why orders were refused.")
    series("Authority requests by result (per minute)", [
        ('sum by (result) (60 * rate(velocita_gateway_authority_requests_total[5m]))', "{{result}}"),
    ], 12, 6, stack=True, description="The cluster's answers to the gateway's requests for authority.")
    series("Unknown outcomes (per 5 minutes)", [
        ('sum(increase(velocita_gateway_unknown_outcomes_total[5m]))', "unknown outcomes"),
    ], 18, 6, description="Any value above 0 stopped new risk (alert: VelocitaGatewayUnknownOutcome).")
    advance(8)


def position_row():
    row("Position and fees")
    stat("Position (lots)", 'velocita_gateway_position_lots', 0, 8, legend="{{account}} {{instrument}}", h=8,
         steps=(("blue", None),), description="The gateway's paper position per account and instrument, in lots.")
    series("Position over time (lots)", [('velocita_gateway_position_lots', "{{account}} {{instrument}}")],
           8, 8, min0=False, description="Steps at each fill.")
    series("Fees (cents)", [
        ('100 * sum(velocita_gateway_fees_usd_total)', "total"),
        ('100 * sum(increase(velocita_gateway_fees_usd_total[1h]))', "last hour"),
    ], 16, 8, unit=CENTS, description="Cumulative paper fees and fees in the last hour, in US cents.")
    advance(8)


def gauge(title, expr, x, w, unit="percent", h=6, description="", maximum=100, steps=(("green", None),)):
    panels.append({
        "type": "gauge", "title": title, "id": _id(), "datasource": DS, "description": description,
        "gridPos": {"h": h, "w": w, "x": x, "y": _y},
        "targets": [{"expr": expr, "refId": "A", "instant": True}],
        "fieldConfig": {"defaults": {"unit": unit, "min": 0, "max": maximum, "thresholds": thresholds(*steps),
                                     "color": {"mode": "thresholds"}, "noValue": "unknown"}, "overrides": []},
        "options": {"showThresholdMarkers": True, "showThresholdLabels": False,
                    "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False}},
    })


WEEK_VOLUME = 'max(velocita_paper_week_volume_usd)'
WEEK_LOSS = 'max(velocita_paper_week_loss_usd)'
WEEK_PNL = 'max(velocita_paper_week_pnl_usd)'
WEEK_FEES = 'max(velocita_paper_week_fees_usd)'


def volume_row():
    row("Volume strategies: this ISO week against the loss budget")
    week = ("ISO week to date (UTC, from Monday 00:00), from the edge's paper book: fills the kernel's position "
            "confirmed, costed at the paper venue's fees.")
    stat("Volume this week", WEEK_VOLUME, 0, 4, unit="currencyUSD", h=6, steps=(("blue", None),),
         description="Filled notional, buys plus sells. " + week)
    stat("Fees this week", '100 * max(velocita_paper_week_fees_usd)', 4, 4, unit=CENTS, h=6, decimals=1,
         steps=(("blue", None),), description="Paper venue fees on this week's fills, in US cents. " + week)
    stat("Net P&L this week", 'max(velocita_paper_week_pnl_usd)', 8, 4, unit="currencyUSD", h=6, decimals=2,
         steps=(("red", None), ("green", 0)),
         description="Realized: each buy's fee at the buy, each sell's price move and fee at the sell. " + week)
    stat("Volume per $ lost", f'{WEEK_VOLUME} / ({WEEK_LOSS} > 0)', 12, 4, h=6, decimals=0,
         steps=(("blue", None),),
         description="Dollars of volume bought with each dollar lost this week: the leaderboard's efficiency. "
                     "'unknown' while the week has no loss.")
    stat("Trades per hour (1h)", '3600 * sum(rate(velocita_gateway_fills_total[1h]))', 16, 4, h=6, decimals=1,
         steps=(("blue", None),), description="Paper fills per hour over the last hour, from the gateway.")
    gauge("Loss budget used", f'100 * {WEEK_LOSS} / max(velocita_paper_weekly_loss_cap_usd)', 20, 4,
          steps=(("green", None), ("orange", 75), ("red", 100)),
          description="This week's realized loss as a share of --weekly-loss-cap-usd (default $12). Above 100 % "
                      "the edge stops buys (exits only) until Monday 00:00 UTC.")
    advance(6)
    series("Weekly volume: observed vs expected", [
        (WEEK_VOLUME, "observed (ISO week to date)"),
        ('max(velocita_paper_expected_week_volume_usd{bound="low"})', "expected low"),
        ('max(velocita_paper_expected_week_volume_usd{bound="high"})', "expected high"),
        ('sum(increase(velocita_gateway_volume_usd_total[7d]))', "gateway fills, last 7 days"),
    ], 0, 8, unit="currencyUSD",
           description="The paper run against the backtest's prediction interval (the edge's "
                       "--expected-week-volume-usd LOW,HIGH; absent until configured). Observed restarts at 0 each "
                       "Monday; the gateway line is a rolling 7 days.")
    series("Weekly loss: observed vs expected and the cap", [
        (WEEK_LOSS, "observed (ISO week to date)"),
        ('max(velocita_paper_expected_week_loss_usd{bound="low"})', "expected low"),
        ('max(velocita_paper_expected_week_loss_usd{bound="high"})', "expected high"),
        ('max(velocita_paper_weekly_loss_cap_usd)', "cap (buys stop above it)"),
    ], 8, 8, unit="currencyUSD",
           description="Loss = max(0, -net P&L of the week). Expected: the edge's --expected-week-loss-usd LOW,HIGH.")
    series("Buys stopped and withheld", [
        ('max by (reason) (velocita_paper_buys_stopped)', "stopped: {{reason}}"),
        ('sum by (reason) (increase(velocita_paper_buys_withheld_total[1h]))', "withheld per hour: {{reason}}"),
    ], 16, 8, description="stopped = 1 while the edge sends exits_only (loss_budget: the week's loss is above the "
                          "cap; insufficient_balance: flat and the book's cash cannot pay a clip plus fee). "
                          "withheld = buys the strategy proposed while flat that were never sent.")
    advance(8)
    series("Volume by side (USD per hour, 1h)", [
        ('sum by (side) (3600 * rate(velocita_gateway_volume_usd_total[1h]))', "{{side}}"),
    ], 0, 12, unit="currencyUSD", stack=True, description="Filled notional per hour from the gateway's fills.")
    series("Fees, last 7 days (cents)", [
        ('100 * sum(increase(velocita_gateway_fees_usd_total[7d]))', "gateway, rolling 7 days"),
    ], 12, 12, unit=CENTS, description="Venue-reported paper fees over a rolling week, in US cents.")
    advance(8)
    stat("Gross P&L this week (before fees)", f'{WEEK_PNL} + {WEEK_FEES}', 0, 6, unit="currencyUSD", h=8,
         decimals=2, steps=(("red", None), ("green", 0)),
         description="Net P&L plus fees: the realized price-move result of this week's closed round trips, before "
                     "the paper venue's fees. Positive = the strategy's timing made money before costs. " + week)
    series("P&L this week: gross, fees, net", [
        (f'{WEEK_PNL} + {WEEK_FEES}', "gross (before fees)"),
        (f'-{WEEK_FEES}', "fees"),
        (WEEK_PNL, "net"),
    ], 6, 18, unit="currencyUSD",
           description="Net = gross - fees. ISO week to date; all three restart at 0 each Monday 00:00 UTC.")
    advance(8)


def paper():
    start()
    paper_now_row()
    rearm_row()
    tick_to_trade_row()
    orders_row()
    position_row()
    volume_row()
    return finish(PAPER, "Phase 2 paper trading: the gateway against the TrueMarkets simulator. Tick-to-trade by "
                         "stage, orders, fills, position and fees. No real order is sent.", ["paper", "gateway"])


def machines_row():
    row("Machines: NUC and Pi")
    series("CPU busy %", [('100 * (1 - avg by (host) (rate(node_cpu_seconds_total{mode="idle"}[1m])))', "{{host}}")],
           0, 8, unit="percent")
    series("Waiting on disk (iowait %)", [(IOWAIT, "{{host}}")],
           8, 8, unit="percent", description="High on the Pi = the SD card is the bottleneck.")
    series("Memory available", [('node_memory_MemAvailable_bytes', "{{host}}")], 16, 8, unit="bytes")
    advance(8)
    series("Disk write latency (avg ms per write)", [(DISK_WRITE_LATENCY, "{{host}} {{device}}")],
           0, 8, unit="ms", description="Average time per completed write. The Pi's SD card runs roughly 10-50 ms.")
    series("Root disk free %", [(ROOT_FREE, "{{host}}")], 8, 8, unit="percent")
    series("Temperature", [('max by (host) (node_thermal_zone_temp)', "{{host}} thermal zone"),
                           ('max by (host) (node_hwmon_temp_celsius)', "{{host}} hwmon")], 16, 8, unit="celsius", min0=False)
    advance(8)
    series("Tailscale traffic", [('rate(node_network_receive_bytes_total{device="tailscale0"}[1m])', "{{host}} rx"),
                                 ('rate(node_network_transmit_bytes_total{device="tailscale0"}[1m])', "{{host}} tx")],
           0, 12, unit="Bps")
    stat("Uptime", 'time() - node_boot_time_seconds', 12, 12, unit="s", legend="{{host}}", h=8,
         description="A reset here means the machine rebooted.")
    advance(8)


def hosts():
    start()
    machines_row()
    return finish(HOSTS, "The NUCs and the Pi from node-exporter: CPU, memory, disks, temperature, network.", ["hosts"])


def build():
    return dict(f() for f in (overview, raft, strategy, paper, hosts))


def main(argv):
    out_dir = pathlib.Path(argv[1]) if len(argv) > 1 else pathlib.Path(__file__).parent / "grafana" / "dashboards"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, dashboard in build().items():
        out = out_dir / name
        out.write_text(json.dumps(dashboard, indent=1) + "\n")
        print(f"wrote {out} with {len(dashboard['panels'])} panels")


if __name__ == "__main__":
    main(sys.argv)
