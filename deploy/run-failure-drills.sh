#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "${1:-}" != "--yes" || $# -ne 1 ]]; then
    echo "usage: $0 --yes" >&2
    echo "This intentionally stops and crashes supervised V1 services, then restores them." >&2
    exit 64
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
evidence_root="${VELOCITA_DRILL_EVIDENCE_DIR:-${HOME}/.local/share/velocita/failure-drills}"
run_id="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="${evidence_root}/${run_id}"
log_file="${run_dir}/failure-drills.log"
summary_file="${run_dir}/summary.tsv"
mkdir -p "$run_dir"
touch "$log_file" "$summary_file"
exec > >(tee -a "$log_file") 2>&1

raft_units=(
    velocita-raft-1.service
    velocita-raft-2.service
    velocita-raft-3.service
)
metrics_unit="velocita-raft-metrics-exporter.service"
prometheus_unit="velocita-prometheus.service"
grafana_unit="velocita-grafana.service"
exporter_url="http://127.0.0.1:9200/metrics"
prometheus_url="http://127.0.0.1:9090"
cleanup_started=0

timestamp() {
    date -u +%Y-%m-%dT%H:%M:%S.%3NZ
}

log() {
    printf '%s %s\n' "$(timestamp)" "$*"
}

record() {
    printf '%s\t%s\t%s\n' "$1" "$2" "$3" >>"$summary_file"
    log "$1: $2 — $3"
}

die() {
    record "run" "FAIL" "$*"
    exit 1
}

wait_for_active() {
    local unit="$1"
    local timeout_seconds="${2:-30}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if systemctl --user is-active --quiet "$unit"; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_http() {
    local url="$1"
    local timeout_seconds="${2:-30}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if curl --fail --silent --max-time 1 "$url" >/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

metrics_snapshot() {
    curl --fail --silent --max-time 2 "$exporter_url"
}

prometheus_query() {
    local expression="$1"
    curl --fail --silent --max-time 2 --get "${prometheus_url}/api/v1/query" \
        --data-urlencode "query=${expression}"
}

alert_is_firing() {
    local alert_name="$1"
    local response
    response="$(prometheus_query "ALERTS{alertstate=\"firing\",alertname=\"${alert_name}\"}")" || return 1
    grep --quiet "\"alertname\":\"${alert_name}\"" <<<"$response"
}

wait_for_alert() {
    local alert_name="$1"
    local timeout_seconds="${2:-30}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if alert_is_firing "$alert_name"; then
            return 0
        fi
        sleep 0.50
    done
    return 1
}

wait_for_alert_clear() {
    local alert_name="$1"
    local timeout_seconds="${2:-20}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if ! alert_is_firing "$alert_name"; then
            return 0
        fi
        sleep 0.50
    done
    return 1
}

active_leader_from_snapshot() {
    awk '
        function node_id(line, value) {
            value = line
            sub(/^.*node_id="/, "", value)
            sub(/".*$/, "", value)
            return value
        }
        /^velocita_metrics_writer_active\{/ {
            active[node_id($0)] = $2
        }
        /^velocita_raft_role\{/ && /role="leader"/ {
            leader[node_id($0)] = $2
        }
        END {
            count = 0
            selected = ""
            for (id in active) {
                if (active[id] == 1 && leader[id] == 1) {
                    count++
                    selected = id
                }
            }
            if (count == 1) print selected
        }
    '
}

current_leader() {
    metrics_snapshot | active_leader_from_snapshot
}

wait_for_leader_except() {
    local excluded_id="$1"
    local timeout_seconds="${2:-15}"
    local deadline=$((SECONDS + timeout_seconds))
    local leader=""
    while (( SECONDS < deadline )); do
        leader="$(current_leader 2>/dev/null || true)"
        if [[ "$leader" =~ ^[123]$ && "$leader" != "$excluded_id" ]]; then
            printf '%s\n' "$leader"
            return 0
        fi
        sleep 0.10
    done
    return 1
}

metric_for_node() {
    local metric="$1"
    local node_id="$2"
    metrics_snapshot | awk -v metric="$metric" -v node="$node_id" '
        index($1, metric "{") == 1 && index($1, "node_id=\"" node "\"") > 0 {
            print $2
            exit
        }
    '
}

all_nodes_equal() {
    local snapshot
    snapshot="$(metrics_snapshot)" || return 1
    awk '
        function node_id(line, value) {
            value = line
            sub(/^.*node_id="/, "", value)
            sub(/".*$/, "", value)
            return value
        }
        /^velocita_metrics_writer_active\{/ { active[node_id($0)] = $2 }
        /^velocita_raft_log_current_index\{/ { log_index[node_id($0)] = $2 }
        /^velocita_raft_commit_index\{/ { commit_index[node_id($0)] = $2 }
        /^velocita_raft_applied_index\{/ { applied_index[node_id($0)] = $2 }
        /^velocita_raft_counter_value\{/ { counter[node_id($0)] = $2 }
        END {
            for (id = 1; id <= 3; id++) {
                if (active[id] != 1 || !(id in log_index) || !(id in commit_index) ||
                    !(id in applied_index) || !(id in counter)) exit 1
            }
            if (log_index[1] != log_index[2] || log_index[2] != log_index[3]) exit 1
            if (commit_index[1] != commit_index[2] || commit_index[2] != commit_index[3]) exit 1
            if (applied_index[1] != applied_index[2] || applied_index[2] != applied_index[3]) exit 1
            if (counter[1] != counter[2] || counter[2] != counter[3]) exit 1
        }
    ' <<<"$snapshot"
}

wait_for_convergence() {
    local timeout_seconds="${1:-30}"
    local deadline=$((SECONDS + timeout_seconds))
    while (( SECONDS < deadline )); do
        if all_nodes_equal && [[ "$(current_leader 2>/dev/null || true)" =~ ^[123]$ ]]; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

client() {
    (cd "$repo_root" && ./deploy/cluster-client.sh "$@")
}

client_value() {
    sed -n 's/^status=ok .* value=\([-0-9][0-9]*\) .*$/\1/p' | tail -1
}

cleanup() {
    local exit_status=$?
    if (( cleanup_started == 1 )); then
        exit "$exit_status"
    fi
    cleanup_started=1
    trap - EXIT INT TERM
    log "Cleanup: restoring all supervised services"
    systemctl --user reset-failed "${raft_units[@]}" "$metrics_unit" "$prometheus_unit" "$grafana_unit" || true
    systemctl --user start "${raft_units[@]}" "$metrics_unit" "$prometheus_unit" "$grafana_unit" || true
    wait_for_http "$exporter_url" 30 || true
    wait_for_http "${prometheus_url}/-/ready" 45 || true
    wait_for_http "http://127.0.0.1:3000/api/health" 45 || true
    wait_for_convergence 45 || true
    systemctl --user --no-pager --full status \
        "${raft_units[@]}" "$metrics_unit" "$prometheus_unit" "$grafana_unit" \
        >"${run_dir}/final-systemd-status.txt" 2>&1 || true
    metrics_snapshot >"${run_dir}/final-raft-metrics.prom" 2>/dev/null || true
    curl --fail --silent --get "${prometheus_url}/api/v1/query" \
        --data-urlencode 'query=ALERTS{alertstate="firing"}' \
        >"${run_dir}/final-firing-alerts.json" 2>/dev/null || true
    log "Evidence: $run_dir"
    exit "$exit_status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

log "Velocita V1 online failure drills starting"
log "Evidence directory: $run_dir"

for command_name in awk curl date podman sed systemctl tail tee; do
    command -v "$command_name" >/dev/null || die "preflight: required command is missing: $command_name"
done

for unit in "${raft_units[@]}" "$metrics_unit" "$prometheus_unit" "$grafana_unit"; do
    systemctl --user is-active --quiet "$unit" || die "preflight: $unit is not active"
done
wait_for_http "$exporter_url" 5 || die "preflight: Raft metrics exporter is unavailable"
wait_for_http "${prometheus_url}/-/ready" 5 || die "preflight: Prometheus is unavailable"
wait_for_http "http://127.0.0.1:3000/api/health" 5 || die "preflight: Grafana is unavailable"
wait_for_alert_clear VelocitaRaftOwnerLoopStale 20 || die "preflight: stale-owner alert did not clear"
wait_for_alert_clear VelocitaRaftExporterDown 20 || die "preflight: exporter-down alert did not clear"
wait_for_convergence 20 || die "preflight: voters are not converged"

baseline_output="$(client get)" || die "preflight: client get failed"
printf '%s\n' "$baseline_output"
baseline_value="$(printf '%s\n' "$baseline_output" | client_value)"
[[ "$baseline_value" =~ ^-?[0-9]+$ ]] || die "preflight: could not parse baseline counter"
record "preflight" "PASS" "three voters converged; counter=${baseline_value}; leader=$(current_leader)"

# Drill 1: an unexpected process death must be restarted by systemd.
leader="$(current_leader)"
crash_node=1
[[ "$leader" == "1" ]] && crash_node=2
crash_unit="velocita-raft-${crash_node}.service"
old_pid="$(systemctl --user show "$crash_unit" --property=MainPID --value)"
old_restarts="$(systemctl --user show "$crash_unit" --property=NRestarts --value)"
[[ "$old_pid" =~ ^[1-9][0-9]*$ ]] || die "restart-policy: invalid initial PID: ${old_pid}"
[[ "$old_restarts" =~ ^[0-9]+$ ]] || die "restart-policy: invalid initial restart count: ${old_restarts}"
log "Drill restart-policy: SIGKILL node ${crash_node}; pid=${old_pid}; restarts=${old_restarts}"
systemctl --user kill --kill-whom=all --signal=SIGKILL "$crash_unit"
restart_deadline=$((SECONDS + 30))
new_pid=""
while (( SECONDS < restart_deadline )); do
    new_pid="$(systemctl --user show "$crash_unit" --property=MainPID --value)"
    if systemctl --user is-active --quiet "$crash_unit" && [[ "$new_pid" =~ ^[1-9][0-9]*$ && "$new_pid" != "$old_pid" ]]; then
        break
    fi
    sleep 0.25
done
[[ "$new_pid" =~ ^[1-9][0-9]*$ && "$new_pid" != "$old_pid" ]] || die "restart-policy: node ${crash_node} did not receive a new PID"
new_restarts="$(systemctl --user show "$crash_unit" --property=NRestarts --value)"
[[ "$new_restarts" =~ ^[0-9]+$ ]] || die "restart-policy: invalid final restart count: ${new_restarts}"
(( new_restarts > old_restarts )) || die "restart-policy: NRestarts did not increase"
wait_for_convergence 30 || die "restart-policy: cluster did not reconverge"
client get || die "restart-policy: client failed after restart"
record "restart-policy" "PASS" "node=${crash_node}; old_pid=${old_pid}; new_pid=${new_pid}; restarts=${old_restarts}->${new_restarts}"

# Drill 2: loss of the current leader must produce a new leader with the majority.
old_leader="$(current_leader)"
[[ "$old_leader" =~ ^[123]$ ]] || die "leader-failover: no unique live leader before injection"
log "Drill leader-failover: stopping leader node ${old_leader}"
failover_start_ns="$(date +%s%N)"
systemctl --user stop "velocita-raft-${old_leader}.service"
new_leader="$(wait_for_leader_except "$old_leader" 15)" || die "leader-failover: no replacement leader within 15 seconds"
failover_end_ns="$(date +%s%N)"
failover_ms=$(( (failover_end_ns - failover_start_ns) / 1000000 ))
client increment 1 || die "leader-failover: majority could not commit"
systemctl --user start "velocita-raft-${old_leader}.service"
wait_for_active "velocita-raft-${old_leader}.service" 30 || die "leader-failover: old leader service did not start"
wait_for_convergence 30 || die "leader-failover: restored node did not catch up"
record "leader-failover" "PASS" "old_leader=${old_leader}; new_leader=${new_leader}; observed_election_ms=${failover_ms}"

# Drill 3: one voter cannot commit; a same-ID retry resolves the unknown outcome once quorum returns.
quorum_leader="$(current_leader)"
[[ "$quorum_leader" =~ ^[123]$ ]] || die "quorum-loss: no unique live leader before injection"
followers=()
for id in 1 2 3; do
    [[ "$id" == "$quorum_leader" ]] || followers+=("$id")
done
before_output="$(client get)" || die "quorum-loss: baseline get failed"
printf '%s\n' "$before_output"
before_value="$(printf '%s\n' "$before_output" | client_value)"
before_commit="$(metric_for_node velocita_raft_commit_index "$quorum_leader")"
[[ "$before_value" =~ ^-?[0-9]+$ ]] || die "quorum-loss: could not parse baseline counter"
[[ "$before_commit" =~ ^[0-9]+$ ]] || die "quorum-loss: could not parse baseline commit index"
request_id="$(date +%s%N)"
log "Drill quorum-loss: leaving leader ${quorum_leader} alone; stopping nodes ${followers[*]}; request_id=${request_id}"
systemctl --user stop "velocita-raft-${followers[0]}.service" "velocita-raft-${followers[1]}.service"
sleep 2
set +e
unknown_output="$(cd "$repo_root" && VELOCITA_REQUEST_ID="$request_id" ./deploy/cluster-client.sh increment 1 2>&1)"
unknown_status=$?
set -e
printf '%s\n' "$unknown_output"
(( unknown_status != 0 )) || die "quorum-loss: client reported success without a majority"
during_value="$(metric_for_node velocita_raft_counter_value "$quorum_leader")"
during_commit="$(metric_for_node velocita_raft_commit_index "$quorum_leader")"
[[ "$during_value" =~ ^-?[0-9]+$ ]] || die "quorum-loss: could not parse counter without quorum"
[[ "$during_commit" =~ ^[0-9]+$ ]] || die "quorum-loss: could not parse commit index without quorum"
[[ "$during_value" == "$before_value" ]] || die "quorum-loss: state machine changed without quorum (${before_value}->${during_value})"
[[ "$during_commit" == "$before_commit" ]] || die "quorum-loss: commit index changed without quorum (${before_commit}->${during_commit})"
quorum_alert_start_ns="$(date +%s%N)"
wait_for_alert VelocitaRaftOwnerLoopStale 20 || die "quorum-loss: stale-owner alert did not fire within 20 seconds"
quorum_alert_end_ns="$(date +%s%N)"
quorum_alert_ms=$(( (quorum_alert_end_ns - quorum_alert_start_ns) / 1000000 ))
prometheus_query 'ALERTS{alertstate="firing"}' >"${run_dir}/quorum-loss-firing-alerts.json"
systemctl --user start "velocita-raft-${followers[0]}.service"
wait_for_active "velocita-raft-${followers[0]}.service" 30 || die "quorum-loss: first follower did not restart"
sleep 3
retry_output="$(cd "$repo_root" && VELOCITA_REQUEST_ID="$request_id" ./deploy/cluster-client.sh increment 1)" || die "quorum-loss: same-ID retry failed after quorum returned"
printf '%s\n' "$retry_output"
retry_value="$(printf '%s\n' "$retry_output" | client_value)"
[[ "$retry_value" =~ ^-?[0-9]+$ ]] || die "quorum-loss: could not parse retry counter"
expected_value=$((before_value + 1))
[[ "$retry_value" == "$expected_value" ]] || die "quorum-loss: expected counter ${expected_value}, got ${retry_value}"
systemctl --user start "velocita-raft-${followers[1]}.service"
wait_for_convergence 45 || die "quorum-loss: all voters did not reconverge"
record "quorum-loss" "PASS" "commit stayed ${before_commit}; alert_wait_ms=${quorum_alert_ms}; same request_id resolved once; counter=${before_value}->${retry_value}"

# Drill 4: the monitoring control plane must fail independently of Raft.
log "Drill observability-isolation: stopping the Raft metrics exporter"
exporter_stop_ns="$(date +%s%N)"
systemctl --user stop "$metrics_unit"
sleep 1
if curl --fail --silent --max-time 1 "$exporter_url" >/dev/null; then
    die "observability-isolation: metrics endpoint remained reachable after exporter stop"
fi
client get || die "observability-isolation: Raft failed when its metrics exporter stopped"
wait_for_alert VelocitaRaftExporterDown 30 || die "observability-isolation: exporter-down alert did not fire within 30 seconds"
exporter_alert_ns="$(date +%s%N)"
exporter_alert_ms=$(( (exporter_alert_ns - exporter_stop_ns) / 1000000 ))
prometheus_query 'ALERTS{alertstate="firing"}' >"${run_dir}/exporter-down-firing-alerts.json"
systemctl --user start "$metrics_unit"
wait_for_http "$exporter_url" 30 || die "observability-isolation: metrics exporter did not recover"

log "Drill observability-isolation: stopping Grafana"
systemctl --user stop "$grafana_unit"
sleep 1
client get || die "observability-isolation: Raft failed when Grafana stopped"
systemctl --user start "$grafana_unit"
wait_for_http "http://127.0.0.1:3000/api/health" 45 || die "observability-isolation: Grafana did not recover"

log "Drill observability-isolation: stopping Prometheus"
systemctl --user stop "$prometheus_unit"
sleep 1
metrics_snapshot >/dev/null || die "observability-isolation: direct Raft metrics failed when Prometheus stopped"
client get || die "observability-isolation: Raft failed when Prometheus stopped"
systemctl --user start "$prometheus_unit"
wait_for_http "${prometheus_url}/-/ready" 45 || die "observability-isolation: Prometheus did not recover"
record "observability-isolation" "PASS" "Raft remained available through exporter, Grafana, and Prometheus outages; exporter_alert_ms=${exporter_alert_ms}"

wait_for_convergence 30 || die "final: cluster is not converged"
final_output="$(client get)" || die "final: client get failed"
printf '%s\n' "$final_output"
final_value="$(printf '%s\n' "$final_output" | client_value)"
record "final" "PASS" "three voters converged; counter=${final_value}; leader=$(current_leader)"
record "run" "PASS" "all online failure drills passed"
