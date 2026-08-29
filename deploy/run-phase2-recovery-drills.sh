#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "${1:-}" != "--yes" || $# -ne 1 ]]; then
    echo "usage: $0 --yes" >&2
    echo "This stops supervised voters and runs bounded Phase 2 recovery load." >&2
    exit 64
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
production_image="${VELOCITA_IMAGE:-localhost/velocita-raft:v1}"
benchmark_image="${VELOCITA_BENCHMARK_IMAGE:-localhost/velocita-raft:bench}"
evidence_root="${VELOCITA_PHASE2_EVIDENCE_DIR:-${HOME}/.local/share/velocita/phase2-recovery}"
run_id="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="${evidence_root}/${run_id}"
summary_file="${run_dir}/summary.tsv"
driver_name=""
mkdir -p "$run_dir"
printf 'scenario\tresult\tevidence\n' >"$summary_file"

raft_units=(velocita-raft-1.service velocita-raft-2.service velocita-raft-3.service)
exporter_url="http://127.0.0.1:9200/metrics"

record() {
    printf '%s\t%s\t%s\n' "$1" "$2" "$3" | tee -a "$summary_file"
}

die() {
    record run FAIL "$*"
    exit 1
}

metrics_snapshot() {
    curl --fail --silent --max-time 2 "$exporter_url"
}

current_leader() {
    metrics_snapshot | awk '
        function node_id(line, value) {
            value = line
            sub(/^.*node_id="/, "", value)
            sub(/".*$/, "", value)
            return value
        }
        /^velocita_metrics_writer_active\{/ { active[node_id($0)] = $2 }
        /^velocita_raft_role\{/ && /role="leader"/ { leader[node_id($0)] = $2 }
        END {
            count = 0
            for (id in active) if (active[id] == 1 && leader[id] == 1) {
                count++
                selected = id
            }
            if (count == 1) print selected
        }
    '
}

metric_for_node() {
    local metric="$1" node_id="$2"
    metrics_snapshot | awk -v metric="$metric" -v node="$node_id" '
        index($1, metric "{") == 1 && index($1, "node_id=\"" node "\"") > 0 {
            print $2
            exit
        }
    '
}

wait_for_active() {
    local unit="$1" deadline=$((SECONDS + ${2:-30}))
    while (( SECONDS < deadline )); do
        systemctl --user is-active --quiet "$unit" && return 0
        sleep 0.25
    done
    return 1
}

wait_for_leader_except() {
    local excluded="$1" deadline=$((SECONDS + ${2:-20})) leader=""
    while (( SECONDS < deadline )); do
        leader="$(current_leader 2>/dev/null || true)"
        if [[ "$leader" =~ ^[123]$ && "$leader" != "$excluded" ]]; then
            printf '%s\n' "$leader"
            return 0
        fi
        sleep 0.10
    done
    return 1
}

all_nodes_converged() {
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
        /^velocita_raft_log_current_index\{/ { current_index[node_id($0)] = $2 }
        /^velocita_raft_commit_index\{/ { commit[node_id($0)] = $2 }
        /^velocita_raft_applied_index\{/ { applied[node_id($0)] = $2 }
        END {
            for (id = 1; id <= 3; id++)
                if (active[id] != 1 || !(id in current_index) || !(id in commit) || !(id in applied)) exit 1
            if (current_index[1] != current_index[2] || current_index[2] != current_index[3]) exit 1
            if (commit[1] != commit[2] || commit[2] != commit[3]) exit 1
            if (applied[1] != applied[2] || applied[2] != applied[3]) exit 1
        }
    ' <<<"$snapshot"
}

wait_for_convergence() {
    local deadline=$((SECONDS + ${1:-45}))
    while (( SECONDS < deadline )); do
        if all_nodes_converged && [[ "$(current_leader 2>/dev/null || true)" =~ ^[123]$ ]]; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    set +e
    [[ -z "$driver_name" ]] || podman rm --force "$driver_name" >/dev/null 2>&1
    systemctl --user reset-failed "${raft_units[@]}" >/dev/null 2>&1
    systemctl --user start "${raft_units[@]}" >/dev/null 2>&1
    wait_for_convergence 45 || true
    metrics_snapshot >"${run_dir}/final-raft-metrics.prom" 2>/dev/null || true
    systemctl --user --no-pager --full status "${raft_units[@]}" \
        >"${run_dir}/final-systemd-status.txt" 2>&1 || true
    echo "Phase 2 evidence: $run_dir"
    exit "$status"
}
trap cleanup EXIT INT TERM

client() {
    (cd "$repo_root" && VELOCITA_IMAGE="$production_image" ./deploy/cluster-client.sh "$@")
}

client_value() {
    sed -n 's/^status=ok .* value=\([-0-9][0-9]*\) .*$/\1/p' | tail -1
}

start_driver() {
    local scenario="$1" duration="$2"
    local scenario_dir="${run_dir}/${scenario}"
    mkdir -p "$scenario_dir"
    driver_name="velocita-phase2-${scenario}"
    podman rm --force "$driver_name" >/dev/null 2>&1 || true
    podman run --detach \
        --name "$driver_name" \
        --network host \
        --shm-size 512m \
        --userns keep-id \
        --volume "${evidence_root}:/results" \
        --entrypoint /usr/local/bin/raft-bench \
        "$benchmark_image" \
        --warmup-seconds 3 \
        --duration-seconds "$duration" \
        --rate 5 \
        --max-in-flight 64 \
        --timeout-ms 3000 \
        --retry-base-ms 5 \
        --retry-cap-ms 250 \
        --response-timeout-ms 250 \
        --unconfirmed-admission-limit 1 \
        --aeron-dir "/dev/shm/aeron-${driver_name}" \
        --members 1,2,3 \
        --request-target '1=aeron:udp?endpoint=127.0.0.1:40111' \
        --request-target '2=aeron:udp?endpoint=127.0.0.1:40112' \
        --request-target '3=aeron:udp?endpoint=127.0.0.1:40113' \
        --response-channel 'aeron:udp?endpoint=0.0.0.0:40101' \
        --operation increment \
        --amount 0 \
        --output-dir "/results/${run_id}/${scenario}" >/dev/null
}

finish_driver() {
    local scenario="$1" require_rejection="$2" max_amplification="$3"
    local scenario_dir="${run_dir}/${scenario}" wait_status run_status
    set +e
    timeout 45 podman wait "$driver_name" >"${scenario_dir}/container-exit.txt"
    wait_status=$?
    set -e
    ((wait_status == 0)) || die "${scenario}: benchmark driver exceeded its deadline"
    podman logs "$driver_name" >"${scenario_dir}/driver.log" 2>&1 || true
    run_status="$(<"${scenario_dir}/container-exit.txt")"
    [[ "$run_status" == "0" || "$run_status" == "2" ]] || \
        die "${scenario}: benchmark infrastructure exit status ${run_status}"
    podman rm "$driver_name" >/dev/null
    driver_name=""
    python3 - "$scenario_dir/summary.json" "$require_rejection" "$max_amplification" <<'PY'
import csv, json, sys
path, require_rejection, maximum = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
with open(path) as source:
    data = json.load(source)
counters = data["counters"]
amplification = data["rates"]["retry_amplification"]
assert counters["steady_terminal_outcomes"] == counters["steady_scheduled"]
assert counters["steady_completed_ok"] > 0
assert counters["pending_high_watermark"] <= 64
assert amplification <= maximum
assert data["final_leader_state"] == "known"
if require_rejection:
    assert counters["steady_admission_rejected_leader_unknown"] > 0
    assert counters["steady_discovery_probe_attempts"] <= 100
    assert counters["pending_high_watermark"] <= 4
with open(path.replace("summary.json", "steady-samples.csv")) as source:
    rows = list(csv.DictReader(source))
assert rows and all(int(row["retries"]) == 0 for row in rows[-min(5, len(rows)):])
print("scheduled={};completed={};attempts={};admission_rejected={};discovery_probes={};"
      "pending_hwm={};amplification={:.3f};tail_retries=0".format(
          counters["steady_scheduled"], counters["steady_completed_ok"],
          counters["steady_transport_attempts"],
          counters["steady_admission_rejected_leader_unknown"],
          counters["steady_discovery_probe_attempts"], counters["pending_high_watermark"],
          amplification))
PY
}

for command_name in awk curl podman python3 systemctl timeout; do
    command -v "$command_name" >/dev/null || die "preflight: missing ${command_name}"
done
podman image exists "$production_image" || die "preflight: missing ${production_image}"
podman image exists "$benchmark_image" || die "preflight: missing ${benchmark_image}"
instrumentation="$(podman image inspect "$benchmark_image" \
    --format '{{ index .Labels "io.velocita.benchmark-instrumentation" }}')"
[[ "$instrumentation" == "ON" ]] || die "preflight: benchmark image instrumentation=${instrumentation}"
for unit in "${raft_units[@]}" velocita-raft-metrics-exporter.service; do
    systemctl --user is-active --quiet "$unit" || die "preflight: ${unit} is not active"
done
wait_for_convergence 30 || die "preflight: voters are not converged"
baseline="$(client get)" || die "preflight: client get failed"
record preflight PASS "leader=$(current_leader); $(printf '%s' "$baseline" | tail -1)"

# A majority remains available while the old leader is removed under open-loop demand.
old_leader="$(current_leader)"
start_driver leader-failover 15
sleep 5
systemctl --user stop "velocita-raft-${old_leader}.service"
new_leader="$(wait_for_leader_except "$old_leader" 20)" || die "leader-failover: no replacement leader"
sleep 2
systemctl --user start "velocita-raft-${old_leader}.service"
wait_for_active "velocita-raft-${old_leader}.service" 30 || die "leader-failover: old leader did not restart"
failover_result="$(finish_driver leader-failover 0 10.0)" || die "leader-failover: invalid evidence"
wait_for_convergence 45 || die "leader-failover: cluster did not reconverge"
record leader-failover PASS "old=${old_leader};new=${new_leader};${failover_result}"

# Remove the leader and one follower so the client sees an explicit leaderless interval.
old_leader="$(current_leader)"
others=()
for id in 1 2 3; do [[ "$id" == "$old_leader" ]] || others+=("$id"); done
start_driver quorum-loss 20
sleep 5
systemctl --user stop "velocita-raft-${old_leader}.service" "velocita-raft-${others[0]}.service"
sleep 4
systemctl --user start "velocita-raft-${old_leader}.service"
wait_for_active "velocita-raft-${old_leader}.service" 30 || die "quorum-loss: voter did not restart"
new_leader="$(wait_for_leader_except -1 20)" || die "quorum-loss: quorum did not elect a leader"
systemctl --user start "velocita-raft-${others[0]}.service"
quorum_result="$(finish_driver quorum-loss 1 25.0)" || die "quorum-loss: invalid evidence"
wait_for_convergence 45 || die "quorum-loss: cluster did not reconverge"
record quorum-loss PASS "restored_leader=${new_leader};${quorum_result}"

# One remaining voter may append but cannot commit; the same request ID resolves once after restore.
leader="$(current_leader)"
followers=()
for id in 1 2 3; do [[ "$id" == "$leader" ]] || followers+=("$id"); done
before_output="$(client get)" || die "same-id: baseline get failed"
before_value="$(printf '%s\n' "$before_output" | client_value)"
before_commit="$(metric_for_node velocita_raft_commit_index "$leader")"
request_id="$(date +%s%N)"
systemctl --user stop "velocita-raft-${followers[0]}.service" "velocita-raft-${followers[1]}.service"
sleep 1
set +e
unknown_output="$(cd "$repo_root" && VELOCITA_IMAGE="$production_image" \
    VELOCITA_REQUEST_ID="$request_id" ./deploy/cluster-client.sh increment 1 2>&1)"
unknown_status=$?
set -e
((unknown_status != 0)) || die "same-id: command committed without quorum"
[[ "$(metric_for_node velocita_raft_counter_value "$leader")" == "$before_value" ]] || \
    die "same-id: FSM changed without quorum"
[[ "$(metric_for_node velocita_raft_commit_index "$leader")" == "$before_commit" ]] || \
    die "same-id: commit index advanced without quorum"
systemctl --user start "velocita-raft-${followers[0]}.service"
wait_for_active "velocita-raft-${followers[0]}.service" 30 || die "same-id: quorum was not restored"
sleep 3
retry_output="$(cd "$repo_root" && VELOCITA_IMAGE="$production_image" \
    VELOCITA_REQUEST_ID="$request_id" ./deploy/cluster-client.sh increment 1)" || \
    die "same-id: retry did not resolve"
retry_value="$(printf '%s\n' "$retry_output" | client_value)"
[[ "$retry_value" == "$((before_value + 1))" ]] || die "same-id: mutation was not applied exactly once"
systemctl --user start "velocita-raft-${followers[1]}.service"
wait_for_convergence 45 || die "same-id: final cluster did not converge"
printf '%s\n' "$unknown_output" >"${run_dir}/same-id-unknown.txt"
printf '%s\n' "$retry_output" >"${run_dir}/same-id-retry.txt"
record same-id PASS "request_id=${request_id};counter=${before_value}->${retry_value};commit_index_held_at=${before_commit}"

record run PASS "all Phase 2 recovery drills passed"
