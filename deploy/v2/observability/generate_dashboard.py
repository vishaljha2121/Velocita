#!/usr/bin/env python3
"""Generates every Velocita V2 Grafana dashboard into grafana/dashboards/.

Five focused dashboards share one link bar. Every panel answers one operator
question. Regenerate after editing (an optional argument writes elsewhere):
    python3 deploy/v2/observability/generate_dashboard.py [out_dir]

A paper run that keeps its own Prometheus label is generated with
    python3 deploy/v2/observability/generate_dashboard.py out_dir --paper-run RUN
which scopes every cluster, gateway and strategy query to that run and also
writes the matching prometheus.yml into out_dir.

The Overview keeps the uid and file name of the former single dashboard
(velocita-v2-cluster). Provisioning sets disableDeletion, so a removed file
would leave its dashboard behind as an unprovisioned stale copy; reusing the
uid updates it in place, and bookmarks and the home dashboard keep working.
"""
import json
import pathlib
import re
import sys

DS = {"type": "prometheus", "uid": "velocita-v2-prometheus"}

OVERVIEW = ("velocita-v2-cluster", "velocita-v2-cluster.json", "Velocita — Overview", "Overview")
RAFT = ("velocita-v2-raft", "velocita-v2-raft.json", "Velocita — Raft & storage", "Raft & storage")
STRATEGY = ("velocita-v2-strategy", "velocita-v2-strategy.json", "Velocita — Strategy edge", "Strategy edge")
PAPER = ("velocita-v2-paper", "velocita-v2-paper.json", "Velocita — Paper trading", "Paper trading")
HOSTS = ("velocita-v2-hosts", "velocita-v2-hosts.json", "Velocita — Hosts", "Hosts")
LIVE = ("velocita-v2-live", "velocita-v2-live.json", "Velocita — Live account & execution", "Live trading")
DASHBOARDS = (OVERVIEW, RAFT, STRATEGY, PAPER, LIVE, HOSTS)

LINKS = [{"type": "link", "title": short, "url": f"/d/{uid}", "icon": "dashboard", "tooltip": title,
          "targetBlank": False, "keepTime": True, "includeVars": False, "asDropdown": False, "tags": []}
         for uid, _, title, short in DASHBOARDS]

panels = []
_next_id = 1
_y = 0
_paper_run = None
SCOPED = re.compile(r'\b(velocita_(?:v2|shadow|gateway|indicator)_[a-zA-Z0-9_]+)(\{[^}]*\})?')


def scoped(expr):
    def active(match):
        metric, labels = match.group(1), match.group(2)
        rest = [part for part in (labels[1:-1].split(',') if labels else []) if part and part != 'strategy="stochastic"']
        run = [f'paper_run="{_paper_run}"'] if _paper_run else []
        return metric + '{' + ','.join(['strategy="stochastic"', *run, *rest]) + '}'
    return SCOPED.sub(active, expr)


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


def note(title, content, x, w, h):
    panels.append({"type": "text", "id": _id(), "title": title, "gridPos": {"x": x, "y": _y, "w": w, "h": h},
                   "options": {"mode": "markdown", "content": content}})


def advance(h):
    global _y
    _y += h


def finish(board, description, tags):
    uid, file, title, _ = board
    for panel in panels:
        for target in panel.get('targets', []):
            target['expr'] = scoped(target['expr'])
    description = ('Supervised live swap. ' if board == LIVE else 'Active cluster: stochastic paper only. ') + description
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
READY_MAP = [{"type": "value", "options": {"0": {"text": "NOT READY"}, "1": {"text": "READY"}, "2": {"text": "COMPLETED", "color": "blue"}}}]
RUN_MAP = [{"type":"value","options":{"0":{"text":"Open (see health)","color":"blue"},"1":{"text":"Completed","color":"blue"},"2":{"text":"Needs finalization","color":"orange"}}}]
GATEWAY_UP_MAP = [{"type": "value", "options": {"0": {"text": "DOWN"}, "1": {"text": "UP"}}}]
NEW_RISK_MAP = [{"type": "value", "options": {"0": {"text": "NEW RISK ALLOWED"}, "1": {"text": "NEW RISK STOPPED"}}}]
NEW_RISK_HELP = ("1 = the gateway sends no risk-increasing order. After an election it re-arms by itself once the "
                 "cluster is quiet and the venue reconciles; after an unknown outcome, a conflict, a control change "
                 "or a reconciliation mismatch only a restart (after checking the gateway journal) re-arms it.")
ARMED_HELP = " An armed gateway alone does not imply an active strategy; check session status and submitter."


def right_now_row():
    row("Right now: is the cluster safe and able to commit?")
    stat("Voters reporting", 'sum(velocita_v2_up)', 0, 4, steps=(("red", None), ("orange", 2), ("green", 3)),
         description="Voters whose health page is readable, of three. Two are needed to commit: at 2 the cluster "
                     "still commits but one more loss stops it (orange); below 2 nothing commits (red).")
    stat("Leader", 'max(velocita_v2_leader_id)', 4, 4, steps=(("red", None), ("green", 1)),
         description="The voter the followers follow. 'unknown' or -1 means an election is in progress.")
    stat("Term", 'max(velocita_v2_term)', 8, 4,
         description="Raft term. It only changes when an election happens.")
    stat("Elections (last hour)", 'max(velocita_v2_term) - min_over_time(max(velocita_v2_term)[1h:15s])', 12, 4,
         steps=(("green", None), ("orange", 1), ("red", 5)),
         description="How many times the term increased in the last hour. Healthy steady state: 0.")
    stat("Telemetry age", 'max(velocita_v2_telemetry_age_seconds)', 16, 4, unit="s",
         steps=(("green", None), ("orange", 2), ("red", 10)),
         description="Seconds since the stalest voter last published health. Stale data is shown as stale, never as healthy.")
    stat("Failed voters", 'sum(velocita_v2_failed)', 20, 4, steps=(("green", None), ("red", 1)),
         description="Voters that latched a fatal fault and stopped (fail-stop). Must be 0.")
    advance(4)
    stat("Role", 'max by (host, node) (velocita_v2_role{role="leader"} * 3 or velocita_v2_role{role="candidate"} * 2 '
         'or velocita_v2_role{role="follower"})',
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
    stat("Submitter ready", '((max(velocita_indicator_run_state{strategy="stochastic"}) == 1) * 0 + 2) or max(velocita_shadow_submitter_ready)', 12, 4,
         steps=(("red", None), ("green", 1)), mappings=READY_MAP,
         description="1 once the policy is installed and the strategy mode is normal.")
    stat("Paper gateway", 'max(velocita_gateway_up)', 16, 4, steps=(("red", None), ("green", 1)), mappings=GATEWAY_UP_MAP,
         description="The paper-trading gateway process. 'unknown' = not deployed or not scraped.")
    stat("New risk", 'max(velocita_gateway_new_risk_stopped)', 20, 4, steps=(("green", None), ("red", 1)),
         mappings=NEW_RISK_MAP, description=NEW_RISK_HELP + ARMED_HELP)
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
    stat("Compactions", 'velocita_v2_compactions_total', 20, 4, legend="{{host}}", h=4,
         description="Completed compactions per voter since its last restart.")
    advance(4)
    stat("Policy slots used", 'max(velocita_v2_policies)', 20, 4,
         steps=(("green", None), ("orange", 6), ("red", 8)),
         description="Of 8. Every published policy version keeps a slot for the life of the incarnation; at 8 a "
                     "new strategy version is refused. Alert: VelocitaPolicySlotsLow at 6.")
    advance(4)


ARCHIVE_USED = ('100 * max by (host) (velocita_v2_archive_bytes / (velocita_v2_archive_byte_limit - 16777216) '
                'or velocita_v2_archive_segments / (velocita_v2_archive_segment_limit - 4))')
ARCHIVE_DAYS = ('min by (host) (((velocita_v2_archive_byte_limit - 16777216) - velocita_v2_archive_bytes) / '
                'clamp_min(deriv(velocita_v2_archive_bytes[6h]), 1e-9) '
                'or ((velocita_v2_archive_segment_limit - 4) - velocita_v2_archive_segments) / '
                'clamp_min(deriv(velocita_v2_archive_segments[6h]), 1e-9))')


def archive_row():
    row("Archive: the compacted log, kept on disk under a fixed budget")
    stat("Archive used (ordinary budget)", ARCHIVE_USED, 0, 6, unit="percent", legend="{{host}}", decimals=1,
         steps=(("green", None), ("orange", 70), ("red", 90)),
         description="Bytes or catalog rows used, whichever is closer to its limit. At 100% every ordinary command "
                     "is refused, including a strategy exit; the operator kill path keeps 16 MiB and 4 rows. "
                     "Alerts: VelocitaArchiveFilling at 70%, VelocitaArchiveNearlyFull at 90%.")
    stat("Days until the archive is full", ARCHIVE_DAYS, 6, 6, unit="s", legend="{{host}}",
         steps=(("red", None), ("orange", 3 * 86400), ("green", 10 * 86400)), no_value="Not growing",
         description="Remaining ordinary budget at the last six hours' growth. A run with no end date needs a new "
                     "incarnation or a larger budget before this reaches zero.")
    series("Archive size", [('velocita_v2_archive_bytes', "{{host}} bytes"),
                            ('velocita_v2_archive_byte_limit - 16777216', "{{host}} ordinary budget")],
           12, 6, unit="bytes", description="Grows by one segment at each compaction; it is never trimmed.")
    series("Archive segments", [('velocita_v2_archive_segments', "{{host}} segments"),
                                ('velocita_v2_archive_segment_limit - 4', "{{host}} ordinary rows")],
           18, 6, description="One catalog row per compaction.")
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


SOAK_FRESH = 'and on() (time() - max(velocita_soak_last_update_timestamp_seconds) < 600)'
SOAK_IDLE = "Soak bot not running"


def soak_row():
    row("Soak test (blank unless the soak bot reported in the last 10 minutes)")
    stat("Soak commands OK", f'sum(velocita_soak_commands_total{{result="ok"}}) {SOAK_FRESH}', 0, 6,
         steps=(("green", None),), no_value=SOAK_IDLE)
    stat("Soak failures", f'sum(velocita_soak_commands_total{{result!="ok"}}) {SOAK_FRESH}', 6, 6,
         steps=(("green", None), ("red", 1)), no_value=SOAK_IDLE,
         description="Timeouts or refusals seen by the soak bot. Pass criterion: 0.")
    stat("Soak p99 latency (15m)",
         f'histogram_quantile(0.99, sum by (le) (rate(velocita_soak_latency_ms_bucket[15m]))) {SOAK_FRESH}',
         12, 6, unit="ms", steps=(("green", None), ("orange", 250), ("red", 1000)), no_value=SOAK_IDLE)
    stat("Soak bot heartbeat", 'time() - max(velocita_soak_last_update_timestamp_seconds)', 18, 6, unit="s",
         steps=(("green", None), ("orange", 30), ("red", 120)),
         description="Seconds since the soak bot last reported. Large = the bot stopped; the other soak panels then go blank.")
    advance(4)
    series("Soak round-trip latency (commit, ms)", [
        (f'velocita_soak_last_latency_ms {SOAK_FRESH}', "last command"),
        (f'histogram_quantile(0.5, sum by (le) (rate(velocita_soak_latency_ms_bucket[5m]))) {SOAK_FRESH}', "p50 (5m)"),
        (f'histogram_quantile(0.99, sum by (le) (rate(velocita_soak_latency_ms_bucket[5m]))) {SOAK_FRESH}', "p99 (5m)"),
    ], 0, 24, unit="ms", description="Soak client start to committed result. Each command is a new client process that starts Aeron "
                                     "and connects first, so this is dominated by cold start, not commit latency.")
    advance(8)


def raft():
    start()
    raft_log_row()
    archive_row()
    progress_row()
    refusals_row()
    wal_disk_row()
    soak_row()
    return finish(RAFT, "Raft log capacity and compaction, durability and commit progress, elections, refusals, "
                        "the disks under the WAL, and the soak bot.", ["raft"])


def edge_row():
    row("Strategy edge: one-second bars to committed decisions")
    stat("Submitter ready", '((max(velocita_indicator_run_state{strategy="stochastic"}) == 1) * 0 + 2) or max(velocita_shadow_submitter_ready)', 0, 6,
         steps=(("red", None), ("green", 1)), mappings=READY_MAP,
         description="1 once the policy is installed and the strategy mode is normal.")
    stat("Last decision", 'max(velocita_shadow_last_decision_age_seconds)', 6, 6, unit="s",
         steps=(("green", None), ("orange", 45), ("red", 90)),
         description="Seconds since the cluster last recorded a decision. The edge sends a bar when it has a signal "
                     "or a stop, and otherwise one every 30 seconds.")
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
    ], 0, 12, unit="ms", description="Payload delivered to Python to the committed decision reply at the local C++ "
                                     "submitter. Includes indicator and journal work and two Raft commits; excludes "
                                     "authorization and fills. Bucket interpolation, not exact percentiles.")
    series("Where the time goes (average ms per tick, 5m)", [
        ('sum by (hop) (rate(velocita_shadow_hop_ms_sum[5m])) / '
         'scalar(clamp_min(sum(rate(velocita_shadow_tick_to_decision_ms_count[5m])), 1e-9))', "{{hop}}"),
    ], 12, 12, unit="ms", stack=True,
           description="edge = tick received to submitter read; source_commit = source event committed; "
                       "decision_commit = decision frame committed. Commit hops include the follower durable-ack delay.")
    advance(8)
    series("Decisions by resolution (per minute)", [
        ('sum by (resolution) (60 * rate(velocita_shadow_action_resolutions_total[5m]))', "resolution {{resolution}}"),
    ], 0, 8, description="0 = no action; 1 = an order was proposed, sized and bounded.")
    series("Rule matched (per minute)", [
        ('sum by (rule) (60 * rate(velocita_shadow_rule_matches_total[5m]))', "rule {{rule}}"),
    ], 8, 8, description="rule 0 = exit; rule 1 = enter; 65535 = no rule matched (hold).")
    series("Market data age at decision (ms)", [('max(velocita_shadow_market_data_age_ms)', "last tick")],
           16, 8, unit="ms", description="Bar close to arrival at the edge. The edge drops a bar older than 2 s and "
                                         "the policy refuses one too.")
    advance(8)


def journey_row():
    row("Journey: public one-second bar -> edge -> kernel decision -> paper gateway")
    bars("Bar-to-order funnel (selected time range)", [
        ("1  Market messages handled", 'sum(increase(velocita_shadow_messages_total[$__range]))'),
        ("2  Closed bars sent to the cluster", 'sum(increase(velocita_shadow_messages_total{outcome="forwarded"}[$__range]))'),
        ("3  Decisions committed by the kernel", 'sum(increase(velocita_shadow_decisions_total[$__range]))'),
        ("4  Order requests (each order is asked twice)",
         'sum(increase(velocita_shadow_directives_total{stage="proposed",directive!="no_action"}[$__range])) or vector(0)'),
        ("5  Orders authorized by the kernel",
         'sum(increase(velocita_indicator_paper_authorizations_total{code="applied"}[$__range])) or vector(0)'),
    ], 0, 16, description="Counted from the edge's metrics over the dashboard's time range. Prometheus increase "
                          "extrapolates, so small counts can show fractions. The kernel refuses the first request "
                          "for every order and asks the edge to re-read its position, so 4 is about twice 5.")
    note("After authorization", "Authorized orders go to the paper gateway, which fills them in a simulator. "
         "See *Paper trading* for orders, fills, position and fees. **Nothing reaches a venue.**", 16, 8, 8)
    advance(8)
    series("Journey per minute", [
        ('sum(60 * rate(velocita_shadow_messages_total[5m]))', "market messages"),
        ('sum(60 * rate(velocita_shadow_messages_total{outcome="forwarded"}[5m]))', "bars sent"),
        ('sum(60 * rate(velocita_shadow_decisions_total[5m]))', "decisions committed"),
        ('sum(60 * rate(velocita_shadow_directives_total{stage="proposed",directive!="no_action"}[5m])) or vector(0)', "order requests"),
        ('sum(60 * rate(velocita_indicator_paper_authorizations_total{code="applied"}[5m])) or vector(0)', "orders authorized"),
    ], 0, 12, description="The funnel over time (5 min rate).")
    series("Messages by outcome (per minute)", [
        ('sum by (outcome) (60 * rate(velocita_shadow_messages_total[5m]))', "{{outcome}}"),
    ], 12, 12, stack=True, description="forwarded = sent to the cluster; quiet = nothing to act on, kept local "
                                        "(one bar in 30 s is still sent); stale = older than 2 s; throttled = a second "
                                        "bar in the same wall-clock second; duplicate = already handled; warming_up = "
                                        "indicators not ready; invalid = rejected frame or a bar ahead of the local "
                                        "clock (alert: VelocitaPaperBadMarketData).")
    advance(8)


def signal_row():
    row("Signal and price")
    series("Signal flags sent to the kernel", [
        ('max(velocita_shadow_feature_micros{name="entry_flag"}) / 1e6', "entry"),
        ('max(velocita_shadow_feature_micros{name="exit_flag"}) / 1e6', "exit"),
    ], 0, 12, description="1 = the edge asked for an entry or an exit on the last forwarded bar. Sampled at the "
                          "scrape interval, so a one-second flag can fall between samples; the ledger is exact.")
    series("SOL proxy close", [('max(velocita_shadow_price_usd)', "Binance SOLUSDT one-second close")],
           12, 12, unit="currencyUSD", min0=False,
           description="A public price proxy. It is not a True Markets executable quote.")
    advance(8)


def strategy():
    start()
    edge_row()
    journey_row()
    signal_row()
    return finish(STRATEGY, "The strategy edge: one-second bars to committed decisions, the funnel to authorized "
                            "paper orders, and the signal it sends. Paper only.", ["strategy", "edge"])


STAGES = ("authorized_to_proof", "proof_to_sent", "sent_to_fill", "fill_to_committed")
CENTS = "suffix:¢"


def _stage_mean(stage, window):
    return (f'sum(increase(velocita_gateway_stage_ms_sum{{stage="{stage}"}}[{window}])) / '
            f'(sum(increase(velocita_gateway_stage_ms_count{{stage="{stage}"}}[{window}])) > 0)')


def _stage_samples(stage, window):
    return f'sum(increase(velocita_gateway_stage_ms_count{{stage="{stage}"}}[{window}]))'


def _stage_quantile(q, stage, window):
    return (f'histogram_quantile({q}, sum by (le) '
            f'(increase(velocita_gateway_stage_ms_bucket{{stage="{stage}"}}[{window}]))) '
            f'and on() ({_stage_samples(stage, window)} >= 100)')


def paper_now_row():
    row("Right now: may the paper gateway take new risk?")
    stat("New risk", 'max(velocita_gateway_new_risk_stopped)', 0, 8, h=6, steps=(("green", None), ("red", 1)),
         mappings=NEW_RISK_MAP, description=NEW_RISK_HELP + " Alert: VelocitaGatewayNewRiskStopped." + ARMED_HELP)
    stat("Gateway", 'max(velocita_gateway_up)', 8, 4, h=6, steps=(("red", None), ("green", 1)), mappings=GATEWAY_UP_MAP,
         description="1 while the gateway process runs. 'unknown' = not deployed, or its metrics are not scraped.")
    stat("Unknown outcomes (range)", 'sum(increase(velocita_gateway_unknown_outcomes_total[$__range]))', 12, 4, h=6,
         steps=(("green", None), ("red", 1)), decimals=0,
         description="Orders whose venue outcome the gateway could not establish. Each one stops new risk. Must be 0.")
    stat("Since last fill", 'time() - (max(velocita_gateway_last_fill_timestamp_seconds) > 0)', 16, 4, h=6, unit="s",
         steps=(("blue", None),), no_value="no fill since gateway start",
         description="Seconds since the last paper fill seen by the running gateway (it resets on restart).")
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
    row("Stochastic gateway: observed effect -> proof -> send -> fill -> committed")
    stat("Sum of gateway stage means", ' + '.join(f'({_stage_mean(s, "$__range")})' for s in STAGES),
         0, 4, unit="ms", h=8, steps=(("blue", None),), no_value="No fill samples",
         description="Four gateway-clock stage means. Starts when the gateway first observes a deliverable effect. "
                     "Excludes tick processing, decision authorization and the wait before gateway observation. "
                     "Stages may have different sample counts across a window or restart; this sum is not a paired end-to-end latency.")
    series("Where the time goes (mean ms per order, 5m)", [(_stage_mean(s, "5m"), s) for s in STAGES],
           4, 12, unit="ms", stack=True,
           description="Independent gateway-stage means. Their sum is not full tick-to-trade latency. sent_to_fill is the simulated venue.")
    bars("p99 by stage (selected range, 100+ orders)", [(s, _stage_quantile(0.99, s, "$__range")) for s in STAGES],
         16, 8, unit="ms", decimals=1,
         description="99th percentile of each stage over the dashboard's time range, from the stage histogram. "
                     "Shown only for a stage with at least 100 orders in the range. Percentiles do not add.")
    advance(8)
    series("Orders per stage (selected range)", [(_stage_samples(s, "$__range"), s) for s in STAGES], 0, 8,
           description="How many orders each stage timing is based on. Below 100 the percentile panels stay empty.")
    series("Stage latency p50 (selected range, 100+ orders)", [(_stage_quantile(0.5, s, "$__range"), s) for s in STAGES],
           8, 8, unit="ms", description="Median per stage. Resolution is limited by the histogram's buckets.")
    series("Stage latency p99 (selected range, 100+ orders)", [(_stage_quantile(0.99, s, "$__range"), s) for s in STAGES],
           16, 8, unit="ms", description="99th percentile per stage, from bucket interpolation.")
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
    stat("Position (lots)", 'velocita_gateway_position_lots', 0, 6, legend="{{account}} {{instrument}}", h=8,
         steps=(("blue", None),), description="The gateway's paper position per account and instrument, in lots.")
    series("Position over time (lots)", [('velocita_gateway_position_lots', "{{account}} {{instrument}}")],
           6, 6, min0=False, description="Steps at each fill.")
    series("Fees (cents)", [
        ('100 * sum(velocita_gateway_fees_usd_total)', "total"),
        ('100 * sum(increase(velocita_gateway_fees_usd_total[1h]))', "last hour"),
    ], 12, 6, unit=CENTS, description="Cumulative paper fees and fees in the last hour, in US cents.")
    series("Volume by side (USD per hour, 1h)", [
        ('sum by (side) (3600 * rate(velocita_gateway_volume_usd_total[1h]))', "{{side}}"),
    ], 18, 6, unit="currencyUSD", description="Filled notional per hour by side, from the gateway's counters.")
    advance(8)


def kernel_ledger_row():
    row("Kernel ledger: what the replicated kernel booked from fills, and its loss limit")
    account = 'max(velocita_v2_account_%s_micros) / 1e6'
    stat("Kernel realized P&L", account % "realized_pnl", 0, 4, unit="currencyUSD", decimals=4,
         steps=(("red", None), ("green", 0)),
         description="After fees, since this incarnation's account was created. Every voter applies the same "
                     "fills, so they agree; the largest is shown.")
    stat("Kernel fees booked", account % "fees_paid", 4, 4, unit="currencyUSD", decimals=4, steps=(("blue", None),),
         description="Each fill's fee is checked against the fee cap before it is booked.")
    stat("Kernel net cash from fills", account % "cash", 8, 4, unit="currencyUSD", decimals=4, steps=(("blue", None),),
         description="Sells minus buys minus fees. Venue cash should equal funding plus this for a session "
                     "that began with its incarnation.")
    stat("Loss limit used",
         '100 * max(-velocita_v2_account_window_realized_pnl_micros) / '
         '(max(velocita_v2_account_max_window_loss_micros) > 0)', 12, 4, unit="percent", decimals=0,
         steps=(("green", None), ("orange", 80), ("red", 100)), no_value="No loss limit set",
         description="Realized loss since the operator last reset it, against the kernel's limit. At 100% the "
                     "kernel refuses orders that add risk and still authorizes orders that close the position. "
                     "Alerts: VelocitaLossLimitNear at 80%, VelocitaLossLimitReached at 100%.")
    stat("Loss limit", account % "max_window_loss", 16, 4, unit="currencyUSD", decimals=2, steps=(("blue", None),),
         description="Set by the operator's risk-limits command. 0 means no limit.")
    stat("Kernel fees minus venue fees",
         'max(velocita_v2_account_fees_paid_micros) - max(velocita_indicator_variant_fees_usd{strategy="stochastic"}) * 1e6',
         20, 4, decimals=0, steps=(("red", None), ("green", -1), ("red", 2)),
         description="In micro-dollars; must be 0 for a session that began with its incarnation. "
                     "Alert: VelocitaLedgerMismatch.")
    advance(4)


def indicator_paper_rows():
    for strategy in ('stochastic',):
        title='Stochastic one-second session'
        row(f'{title} — isolated $25 virtual account')
        content=(
            '**Ongoing paper session: one-second completed bars; SOL/cash; 0.12 SOL orders.**\n'
            '\n'
            '```text\n'
            'ENTER: K crosses above D, K >= 60, EMA300 > EMA1200;\n'
            '       all conditions hold for 3 seconds.\n'
            'EXIT:  K < D and K <= 40 for 3 seconds.\n'
            'Wait 60 seconds after an exit before re-entry.\n'
            'STOP:  while flat, cash cannot cover 0.12 SOL plus its fee.\n'
            '```\n'
            '\n'
            'Stochastic range 900 seconds; K smoothing 10 seconds; D smoothing 30 seconds.\n'
            'Signals use the previous completed second. Public Binance SOLUSDT is a price\n'
            'proxy; simulated FOK fills use the following close. **20 bp per side, fee-only**;\n'
            'buy-side symmetry is assumed. This is not proof of True Markets execution or alpha.\n'
            '\n'
            '**No scheduled time cutoff.** Continues from **$24.869170**, preserving the\n'
            'original $25 funding and all four prior fills. Session-only metrics exclude\n'
            'those prior fills; lifetime and 7/14-day panels include them. One cluster only.\n'
            'The $10 drawdown, 240 lifetime-order, reconciliation and storage guards remain.\n'
            'While invested in SOL, low USDC does not block selling. A finalized flat session\n'
            'keeps its balances and fills visible even after the edge stops.')
        panels.append({'type':'text','id':_id(),'title':f'{title}: rules and assumptions',
            'gridPos':{'x':0,'y':_y,'w':24,'h':10},'options':{'mode':'markdown','content':content}})
        advance(10)
        selector=f'{{strategy="{strategy}"}}'
        stat('Session-only net P&L (20 bp)',f'velocita_indicator_variant_net_pnl_usd{selector}',0,6,unit='currencyUSD',decimals=3)
        stat('Session-only volume',f'velocita_indicator_variant_volume_usd{selector}',6,6,unit='currencyUSD',decimals=2)
        stat('Session-only fees',f'velocita_indicator_variant_fees_usd{selector}',12,6,unit='currencyUSD',decimals=3)
        stat('Session-only fills',f'velocita_indicator_variant_order_count{selector}',18,6,decimals=0)
        advance(4)
        stat('Session status',f'velocita_indicator_run_state{selector}',0,4,mappings=RUN_MAP,
             steps=(("red",None),("green",1)),
             description='Completed requires flat inventory and a checked final ledger. Running window alone is not a health assertion.')
        stat('Equity after exit cost (account history; new fills 20 bp)',f'velocita_indicator_equity_usd{selector}',4,4,unit='currencyUSD',decimals=2)
        stat('Net P&L since funding (account history; new fills 20 bp)',f'velocita_indicator_net_pnl_usd{selector}',8,4,unit='currencyUSD',decimals=2,
             steps=(("red",None),("green",0)),
             description='Account lifetime includes old 30 bp fills and new 20 bp fee-only fills. Session-only panels isolate the new rules; buy-side 20 bp remains assumed.')
        stat('Cash available',f'velocita_indicator_cash_usd{selector}',12,4,unit='currencyUSD',decimals=2)
        stat('SOL inventory',f'velocita_indicator_position_sol{selector}',16,4,decimals=2)
        stat('Age of last recorded candle',f'velocita_indicator_price_age_seconds{selector}',20,4,unit='s',
             steps=(("green",None),("red",5)),
             description='Naturally increases once the session is completed. No fresh price is needed to value a flat final account.')
        advance(4)
        measured='Historical route stress case: same fills re-priced at the September 28 100/98 bp quotes; not the current 20 bp CLI service fee.'
        stat('P&L under historical 100/98 bp stress',f'velocita_indicator_measured_cost_net_pnl_usd{selector}',0,4,unit='currencyUSD',decimals=2,
             steps=(("red",None),("green",0)),description=measured)
        stat('Volume per $1 lost (account history; new fills 20 bp)',f'velocita_indicator_volume_per_usd_lost{{strategy="{strategy}",cost="scenario"}}',4,4,
             unit='currencyUSD',decimals=0,no_value='No net loss',description='Total buy and sell volume divided by the loss so far, as simulated. Empty while the account is not at a loss.')
        stat('Volume per $1 lost (100/98 bp stress)',f'velocita_indicator_volume_per_usd_lost{{strategy="{strategy}",cost="measured"}}',8,4,
             unit='currencyUSD',decimals=0,no_value='No net loss',description=measured+' Plain churn achieves about $101 on SOL.')
        stat('Drawdown stop used',f'100 * velocita_indicator_drawdown_usd{selector} / 10',12,4,unit='percent',decimals=0,
             steps=(("green",None),("orange",50),("red",80)),description='Share of the $10 drawdown that stops new buys and requests an exit.')
        unjoined='No exact signal-to-fill join is currently instrumented for this one-second session. The configured delay is one completed bar; do not infer a measurement from fill timestamps.'
        for x,name in ((16,'mean'),(20,'max')):
            stat(f'Bar close to fill ({name})',f'velocita_indicator_bar_close_to_fill_seconds{{strategy="{strategy}",stat="{name}"}}',x,4,unit='s',decimals=1,
                 no_value='Not instrumented',description=unjoined)
        advance(4)
        series('SOL price with confirmed entries / exits',[
            (f'velocita_indicator_price_usd{selector}','SOL proxy close'),
            (f'max_over_time(velocita_indicator_fill_marker_usd{{strategy="{strategy}",side="buy"}}[$__interval])','Entry'),
            (f'max_over_time(velocita_indicator_fill_marker_usd{{strategy="{strategy}",side="sell"}}[$__interval])','Exit'),
            (f'velocita_indicator_ema_usd{{strategy="{strategy}",span="300"}}','EMA 300 s'),
            (f'velocita_indicator_ema_usd{{strategy="{strategy}",span="1200"}}','EMA 1200 s')],
            0,14,unit='currencyUSD',min0=False,
            description='Markers are confirmed simulated fills, never raw signals. Their chart time has scrape/bucket resolution; exact confirmation times are retained in the ledger. Missed historical times are unknown, not invented. Zoom in to inspect close entries/exits.')
        for name,color in (('Entry','green'),('Exit','red')):
            panels[-1]['fieldConfig']['overrides'].append({'matcher':{'id':'byName','options':name},
                'properties':[{'id':'custom.drawStyle','value':'points'}, {'id':'custom.showPoints','value':'always'},
                    {'id':'custom.pointSize','value':9},{'id':'custom.lineWidth','value':0},
                    {'id':'color','value':{'mode':'fixed','fixedColor':color}}]})
        series('Equity and drawdown',[(f'velocita_indicator_equity_usd{selector}','Equity'),
            (f'velocita_indicator_drawdown_usd{selector}','Drawdown')],14,10,unit='currencyUSD',min0=False)
        advance(8)
        series('Stochastic K and D',[
            (f'velocita_indicator_stochastic{{strategy="{strategy}",line="k"}}','K'),
            (f'velocita_indicator_stochastic{{strategy="{strategy}",line="d"}}','D'),
            ('vector(60)','60: entry needs K at or above'),
            ('vector(40)','40: exit needs K at or below')],
            0,24,h=6,
            description='Entry: K crosses above D with K at 60 or more while EMA 300 s is above EMA 1200 s (price chart), held for 3 seconds. '
                        'Exit: K below D and K at 40 or less for 3 seconds. Sampled at the scrape interval, so a one-second cross can fall between points; the ledger is exact.')
        advance(6)
        metric=f'velocita_indicator_shadow_tick_to_decision_ms'
        count=f'sum(increase({metric}_count{{strategy="{strategy}"}}[$__range]))'
        stat('Decision samples (range estimate)',count,0,6,decimals=1,
             description='Prometheus increase extrapolates between scrapes and handles process counter resets; this is an estimated count, not an exact event ledger.')
        stat('Mean tick to decision',f'sum(increase({metric}_sum{{strategy="{strategy}"}}[$__range])) / ({count} > 0)',6,6,unit='ms',decimals=3,
             description='Payload delivered to Python through receipt of the committed decision at the local C++ submitter. Includes indicators, durable journal and two Raft commits. Excludes HTTP fetch, authorization and fills. Same-host wall clock; clock steps can distort durations.')
        stat('p99 estimate (100+ samples)',f'histogram_quantile(0.99, sum by (le) (increase({metric}_bucket{{strategy="{strategy}"}}[$__range]))) and on() ({count} >= 100)',12,6,unit='ms',decimals=2,no_value='Need 100 samples',
             description='Bucket interpolation, not an exact observed percentile. Hidden below an estimated 100 samples. Five-minute decisions do not characterize high-load tail latency.')
        stat('Rule disagreements',f'velocita_indicator_shadow_agreement_total{{strategy="{strategy}",result="disagree"}}',18,6,decimals=0)
        advance(4)
        series('Tick to decision: mean by component (30m)',[
            (f'sum by (hop) (increase(velocita_indicator_shadow_hop_ms_sum{{strategy="{strategy}"}}[30m])) / scalar(sum(increase({metric}_count{{strategy="{strategy}"}}[30m])) > 0)','{{hop}}')],
            0,24,unit='ms',stack=True,description='edge: feature/journal work and IPC; source_commit and decision_commit: request to committed reply, including networking, scheduling, quorum and durable storage. Not pure kernel execution time.')
        advance(8)
        for offset,days in ((0,7),(12,14)):
            sel=f'{{strategy="{strategy}",days="{days}"}}'
            for j,(label,metric,unit) in enumerate((('Volume','period_volume_usd','currencyUSD'),
                    ('Net P&L','period_pnl_usd','currencyUSD'),('Fees','period_fees_usd','currencyUSD'),
                    ('Round trips','period_completed_trades','none'))):
                stat(f'{days}d {label}',f'velocita_indicator_{metric}{sel}',offset+3*j,3,unit=unit,decimals=2,
                    description='Trailing window, or since funding when the account is younger. No annualization. Unknown historical fill times are excluded from timed volume/fees/trade counts.')
        advance(4)
        series('Volume accumulated',[(f'velocita_indicator_volume_usd_total{selector}','Executed buy + sell notional')],
            0,8,unit='currencyUSD')
        series('Buy stops',[(f'velocita_indicator_buy_stop{selector}','{{reason}}'),
            (f'velocita_indicator_gateway_new_risk_stopped{selector}','Gateway stopped')],8,8,
            description='Stops are independent for each account. Loss/capacity/duration stops persist across restart.')
        series('Orders used / capacity',[(f'velocita_indicator_order_count{selector}','Orders'),
            (f'velocita_indicator_order_capacity{selector}','Buy-stop capacity')],16,8)
        advance(8)


def paper():
    start()
    paper_now_row()
    panels[0]['title'] = 'Active stochastic paper cluster: gateway status'
    indicator_paper_rows()
    kernel_ledger_row()
    rearm_row()
    tick_to_trade_row()
    orders_row()
    position_row()
    stat('Final stop reason', 'velocita_indicator_completed_reason{strategy="stochastic"}', 0, 24, h=3,
         steps=(("red", None), ("green", 1)), legend='{{reason}}', no_value='None (not finalized)',
         description='A checked final snapshot records why the strategy stopped.')
    panels[-1]['options']['textMode'] = 'name'
    advance(3)
    return finish(PAPER, "Paper trading: the gateway against the fill-or-kill simulator. The session's account, "
                         "gateway stages, orders, fills, position and fees. No real order is sent.", ["paper", "gateway"])


def live():
    start()
    fresh=' and on() (time() - velocita_live_observer_observed_seconds < 45) and on() (velocita_live_observer_up == 1)'
    def q(name):return '(velocita_live_'+name+'{run="$live_run"}'+fresh+')'
    state_map=[{"type":"value","options":{"0":{"text":"STOPPED","color":"orange"},"1":{"text":"RUNNING","color":"green"}}}]
    phase_map=[{"type":"value","options":{"1":{"text":"IDLE","color":"blue"},"2":{"text":"STARTED / UNRESOLVED","color":"orange"},"3":{"text":"RECORDED RESULT","color":"blue"}}}]
    row('Live operation — service readiness is not trading authorization')
    stat('Observer healthy', 'velocita_live_observer_up and (time() - velocita_live_observer_observed_seconds < 45)',0,4,steps=(("red",None),("green",1)))
    stat('Gateway',q('gateway_active'),4,4,mappings=state_map)
    stat('Protected signer',q('signer_active'),8,4,mappings=state_map)
    stat('Review current',q('review_current'),12,4,mappings=[{"type":"value","options":{"0":{"text":"EXPIRED","color":"orange"},"1":{"text":"CURRENT","color":"green"}}}])
    stat('Review time remaining','clamp_min(velocita_live_review_expires_seconds{run="$live_run"} - time(), 0)'+fresh,16,4,unit='s')
    stat('Cluster trading mode','max(velocita_v2_strategy_mode)',20,4,mappings=[{"type":"value","options":{"1":{"text":"NORMAL","color":"orange"},"2":{"text":"NO NEW RISK"},"3":{"text":"CANCEL ONLY"},"4":{"text":"REDUCE ONLY"},"5":{"text":"HALTED","color":"blue"},"6":{"text":"FLATTEN"}}}])
    advance(4)
    row('Account snapshot — last verified venue balances, not a live wallet poll')
    stat('Last reviewed USDC',q('reviewed_usdc'),0,8,unit='suffix: USDC',decimals=6)
    stat('Last reviewed SOL',q('reviewed_sol'),8,8,unit='suffix: SOL',decimals=9)
    stat('Account evidence age','time() - velocita_live_account_observed_seconds{run="$live_run"}'+fresh,16,8,unit='s',steps=(("green",None),("orange",60),("red",900)))
    advance(4)
    series('Reviewed USDC snapshots',[(q('reviewed_usdc'),'Verified snapshot')],0,12,unit='suffix: USDC',description='Only changes when reviewed account history is replaced. Sampling this observer does not refresh the wallet evidence.')
    series('Reviewed SOL snapshots',[(q('reviewed_sol'),'Verified snapshot')],12,12,unit='suffix: SOL',description='No inferred fill amounts, proxy marks or paper balance. See account evidence age.')
    advance(8)
    row('Order lifecycle — durable journal evidence')
    for i,(key,title) in enumerate([('prepare','Quote preparation'),('protected_signing','Protected signing'),('submission','Venue submission')]):
        stat(title,q(key+'_phase'),i*8,8,mappings=phase_map,description='1 idle; 2 started; 3 recorded result. A prepared quote, signature or submission result is not proof of a settled fill.')
    advance(4)
    stat('Latest effect ID',q('submission_effect'),0,6,decimals=0)
    stat('Submission journal committed index',q('submission_index'),6,6,decimals=0)
    stat('Gateway exit status',q('gateway_exit_status'),12,6,steps=(("green",None),("orange",1)))
    stat('Signer exit status',q('signer_exit_status'),18,6,steps=(("green",None),("orange",1)))
    advance(4)
    series('Durable lifecycle phases',[(q(k+'_phase'),v) for k,v in [('prepare','Quote'),('signing_request','Signing request'),('protected_signing','Protected signer'),('submission','Submission')]],0,16)
    series('Automatic service restarts',[(q('gateway_restarts'),'Gateway'),(q('signer_restarts'),'Signer')],16,8,description='Services currently use Restart=no. A stopped service does not imply a failed trade.')
    advance(8)
    row('Costs, limits and settlement interpretation')
    stat('Configured maximum buy output deduction',q('repayment_cap_sol'),0,12,unit='suffix: SOL',decimals=9,description='Conservative signer allowance. It is not an observed fee or expected cost.')
    stat('Last account evidence time',q('account_observed_seconds')+' * 1000',12,12,unit='dateTimeAsIso')
    advance(4)
    panels.append({"id":_id(),"type":"text","title":"How to read this supervised MVP","gridPos":{"x":0,"y":_y,"w":24,"h":9},"options":{"mode":"markdown","content":
        "**Scope:** one separately authorized $1 total-debit SOL buy; $0.002 explicit fee cap; $0.50 realized-loss limit. No automatic strategy, retries, second trade or sell. The loss limit is not a guaranteed market stop-loss.\n\n"
        "**Cost evidence:** 20 bp explicit fee plus an observed 0.000550840 SOL buy-output deduction on the prior owner trade. The configured 0.002039280 SOL maximum is a conservative allowance with zero assumed sponsor credit. These are different quantities; neither establishes future route costs.\n\n"
        "**Balances:** last reviewed TM/on-chain account snapshot. The age above advances even while the observer is healthy. Deposits or trades after that timestamp are not reflected until reconciliation refreshes the evidence.\n\n"
        "**Settlement:** this view reports durable local stages, not economic settlement. Actual received SOL, final fees, fill price, trade markers and P&L require a verified settlement record. They are deliberately not synthesized from a quote or a submitted transaction. No settled live trade has yet been established by this cluster.\n\n"
        "**Expiry:** account review expiry stops this supervised runtime. Preserve all recovery journals; never initialize again to restart. Paper metrics remain on the separate Paper trading tab."}})
    advance(9)
    name,board=finish(LIVE,'Read-only service status, reviewed account snapshots, durable order progress and cost boundaries.',['live','account','execution'])
    board['templating']['list']=[{"name":"live_run","label":"Live incarnation","type":"query","datasource":DS,"query":"label_values(velocita_live_reviewed_usdc, run)","refresh":1,"multi":False,"includeAll":False,"current":{"text":"b79e65be1ea14e219016ec48ea02efef","value":"b79e65be1ea14e219016ec48ea02efef"}}]
    return name,board


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
    return dict(f() for f in (overview, raft, strategy, paper, live, hosts))


def prometheus_config():
    lines = (pathlib.Path(__file__).parent / "prometheus.yml").read_text().splitlines()
    marked = [n for n, line in enumerate(lines) if line.rstrip().endswith("# run-scoped")]
    if not marked:
        raise SystemExit("prometheus.yml has no run-scoped targets")
    for n in marked:
        body = lines[n][:lines[n].rindex("#")].rstrip()
        if not body.endswith("}"):
            raise SystemExit(f"prometheus.yml line {n + 1}: a run-scoped line must end its labels with a brace")
        lines[n] = f"{body[:-1]}, paper_run: {_paper_run}}}"
    return "\n".join(lines) + "\n"


def main(argv):
    global _paper_run
    args = list(argv[1:])
    if "--paper-run" in args:
        at = args.index("--paper-run")
        if at + 1 >= len(args) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", args[at + 1]):
            raise SystemExit("--paper-run takes letters, digits, dot, dash and underscore")
        _paper_run = args[at + 1]
        del args[at:at + 2]
    out_dir = pathlib.Path(args[0]) if args else pathlib.Path(__file__).parent / "grafana" / "dashboards"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, dashboard in build().items():
        out = out_dir / name
        out.write_text(json.dumps(dashboard, indent=1) + "\n")
        print(f"wrote {out} with {len(dashboard['panels'])} panels")
    if _paper_run:
        (out_dir / "prometheus.yml").write_text(prometheus_config())
        print(f"wrote {out_dir / 'prometheus.yml'} for paper run {_paper_run}")


if __name__ == "__main__":
    main(sys.argv)
