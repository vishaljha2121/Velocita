#!/bin/sh
set -eu
[ $# -eq 2 ] || { echo "usage: $0 URL_FILE TEXTFILE" >&2; exit 64; }
url=$(cat "$1")
textfile=$2
now=$(date +%s)

reachable() { curl -fsS -m 5 -o /dev/null "$1" 2>/dev/null; }

alert_path_ok=1
reachable http://127.0.0.1:9090/-/ready || alert_path_ok=0
reachable http://127.0.0.1:9093/-/ready || alert_path_ok=0
reachable "${VELOCITA_NTFY_BASE_URL}/v1/health" || alert_path_ok=0

target=$url
[ "$alert_path_ok" = 1 ] || target=$url/fail

last_sent=$(sed -n 's/^velocita_heartbeat_last_sent_timestamp_seconds //p' "$textfile" 2>/dev/null || true)
last_sent=${last_sent:-0}
sent=0
if curl -fsS -m 10 --retry 2 -o /dev/null "$target" 2>/dev/null; then
    sent=1
    last_sent=$now
fi

tmp=$textfile.tmp
{
    echo "# HELP velocita_heartbeat_alert_path_ok 1 when Prometheus, Alertmanager and ntfy all answered on this host."
    echo "# TYPE velocita_heartbeat_alert_path_ok gauge"
    echo "velocita_heartbeat_alert_path_ok $alert_path_ok"
    echo "# HELP velocita_heartbeat_sent 1 when the last ping reached the outside monitor."
    echo "# TYPE velocita_heartbeat_sent gauge"
    echo "velocita_heartbeat_sent $sent"
    echo "# HELP velocita_heartbeat_last_sent_timestamp_seconds When a ping last reached the outside monitor."
    echo "# TYPE velocita_heartbeat_last_sent_timestamp_seconds gauge"
    echo "velocita_heartbeat_last_sent_timestamp_seconds $last_sent"
} > "$tmp"
mv "$tmp" "$textfile"
