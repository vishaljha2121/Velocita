#!/usr/bin/env bash
set -euo pipefail

image="${VELOCITA_BENCHMARK_IMAGE:-localhost/velocita-raft:bench}"
results_root="${XDG_DATA_HOME:-$HOME/.local/share}/velocita/benchmarks"
rates="25 50 100 200 400"
repetitions=3
warmup_seconds=10
duration_seconds=60
max_in_flight=8192
timeout_ms=3000
retry_base_ms=5
retry_cap_ms=250
response_timeout_ms=250
unconfirmed_admission_limit=1
idle_strategy=yield
cpu_placement=current
stall_every=0
stall_ms=0
run_mode=baseline
confirmed=false

usage() {
    echo "Usage: $0 --yes [--quick|--certification|--coordinated-omission-check]"
    echo "                 [--cpu-placement current|partitioned] [--rates '25 50 100'] [--repetitions N]"
    echo "                 [--warmup-seconds N] [--duration-seconds N]"
}

while (($# > 0)); do
    case "$1" in
        --yes) confirmed=true; shift ;;
        --quick)
            rates="50 100"; repetitions=1; warmup_seconds=3; duration_seconds=15; shift ;;
        --certification)
            rates="25 50 100 200 400 800"; repetitions=5; warmup_seconds=15; duration_seconds=180; shift ;;
        --coordinated-omission-check)
            rates="1000"; repetitions=1; warmup_seconds=3; duration_seconds=15
            stall_every=1000; stall_ms=10; run_mode=stall; shift ;;
        --cpu-placement) cpu_placement="$2"; shift 2 ;;
        --rates) rates="$2"; shift 2 ;;
        --repetitions) repetitions="$2"; shift 2 ;;
        --warmup-seconds) warmup_seconds="$2"; shift 2 ;;
        --duration-seconds) duration_seconds="$2"; shift 2 ;;
        --help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ "$confirmed" != true ]]; then
    echo "Pass --yes after reading this safety boundary:" >&2
    echo "  the script stops the production voter target temporarily and resets only" >&2
    echo "  volumes named velocita-bench-node{1,2,3}-data; production WAL volumes are untouched." >&2
    exit 1
fi

command -v podman >/dev/null || { echo "podman is required" >&2; exit 1; }
podman image exists "$image" || {
    echo "Benchmark image is missing: $image" >&2
    echo "Run ./deploy/build-benchmark-image.sh first." >&2
    exit 1
}

instrumentation="$(podman image inspect "$image" \
    --format '{{ index .Labels "io.velocita.benchmark-instrumentation" }}')"
[[ "$instrumentation" == "ON" ]] || {
    echo "Refusing non-benchmark image; instrumentation label is: $instrumentation" >&2
    exit 1
}

for value in $rates; do
    [[ "$value" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid rate: $value" >&2; exit 1; }
done
[[ "$repetitions" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid repetitions" >&2; exit 1; }
[[ "$cpu_placement" == "current" || "$cpu_placement" == "partitioned" ]] || {
    echo "CPU placement must be current or partitioned" >&2; exit 1;
}

suite_id="$(date -u +%Y%m%dT%H%M%SZ)"
suite_dir="${results_root}/runs/${suite_id}"
mkdir -p "$suite_dir"

production_was_active=false
if systemctl --user is-active --quiet velocita-raft-cluster.target; then
    production_was_active=true
fi

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    set +e
    for id in 1 2 3; do
        podman stop --time 10 "velocita-bench-${id}" >/dev/null 2>&1 || true
        podman rm --force "velocita-bench-${id}" >/dev/null 2>&1 || true
    done
    if [[ "$production_was_active" == true ]]; then
        systemctl --user reset-failed \
            velocita-raft-1.service velocita-raft-2.service velocita-raft-3.service \
            velocita-raft-metrics-exporter.service >/dev/null 2>&1 || true
        systemctl --user start velocita-raft-cluster.target >/dev/null 2>&1 || true
        systemctl --user start \
            velocita-raft-1.service velocita-raft-2.service velocita-raft-3.service \
            velocita-raft-metrics-exporter.service >/dev/null 2>&1 || true
    fi
    echo "Benchmark evidence: $suite_dir"
    exit "$status"
}
trap cleanup EXIT INT TERM

{
    echo "suite_id=${suite_id}"
    echo "started_utc=$(date -u --iso-8601=seconds)"
    echo "image=${image}"
    echo "image_id=$(podman image inspect "$image" --format '{{.Id}}')"
    echo "rates=${rates}"
    echo "repetitions=${repetitions}"
    echo "warmup_seconds=${warmup_seconds}"
    echo "duration_seconds=${duration_seconds}"
    echo "max_in_flight=${max_in_flight}"
    echo "timeout_ms=${timeout_ms}"
    echo "retry_base_ms=${retry_base_ms}"
    echo "retry_cap_ms=${retry_cap_ms}"
    echo "response_timeout_ms=${response_timeout_ms}"
    echo "unconfirmed_admission_limit=${unconfirmed_admission_limit}"
    echo "idle_strategy=${idle_strategy}"
    echo "cpu_placement=${cpu_placement}"
    echo "run_mode=${run_mode}"
    echo "stall_every=${stall_every}"
    echo "stall_ms=${stall_ms}"
    echo "cluster_reset_policy=fresh_cluster_per_repetition"
    uname -a
    nproc
    lscpu
    uptime
    podman version
    if [[ -r /proc/cmdline ]]; then
        echo "kernel_command_line=$(</proc/cmdline)"
    fi
    if [[ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        echo "cpu_governor=$(</sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
    fi
    if [[ -r /sys/class/thermal/thermal_zone0/temp ]]; then
        echo "temperature_millicelsius=$(</sys/class/thermal/thermal_zone0/temp)"
    fi
} >"${suite_dir}/metadata.txt"

echo -e "rate\trepetition\texit_status\tsummary" >"${suite_dir}/suite.tsv"
performance_limits_observed=false

echo "Stopping the production voter target; Prometheus/Grafana remain independent and active."
systemctl --user stop velocita-raft-cluster.target

start_node() {
    local id="$1"
    local peer_one peer_two
    local cpu_arguments=()
    local stall_arguments=()
    case "$id" in
        1) peer_one=2; peer_two=3 ;;
        2) peer_one=1; peer_two=3 ;;
        3) peer_one=1; peer_two=2 ;;
    esac
    if [[ "$cpu_placement" == "partitioned" ]]; then
        cpu_arguments=(--cpuset-cpus "$id")
    fi
    if ((stall_every > 0)); then
        stall_arguments=(--benchmark-stall-every "$stall_every" --benchmark-stall-ms "$stall_ms")
    fi
    podman run --detach \
        --name "velocita-bench-${id}" \
        --network host \
        --shm-size 192m \
        --memory 768m \
        --pids-limit 128 \
        --stop-timeout 15 \
        --read-only \
        --cap-drop all \
        --security-opt no-new-privileges \
        "${cpu_arguments[@]}" \
        --volume "velocita-bench-node${id}-data:/var/lib/velocita-raft:U" \
        "$image" \
        --node-id "$id" \
        --members 1,2,3 \
        --request-channel "aeron:udp?endpoint=127.0.0.1:4011${id}" \
        --response-channel "aeron:udp?endpoint=127.0.0.1:40101" \
        --peer-channel "aeron:udp?endpoint=127.0.0.1:4021${id}" \
        --peer-target "${peer_one}=aeron:udp?endpoint=127.0.0.1:4021${peer_one}" \
        --peer-target "${peer_two}=aeron:udp?endpoint=127.0.0.1:4021${peer_two}" \
        --raft-trace 0 \
        "${stall_arguments[@]}" >/dev/null
}

stop_benchmark_nodes() {
    for id in 1 2 3; do
        podman stop --time 10 "velocita-bench-${id}" >/dev/null 2>&1 || true
        podman rm --force "velocita-bench-${id}" >/dev/null 2>&1 || true
    done
}

reset_benchmark_volumes() {
    for id in 1 2 3; do
        local volume="velocita-bench-node${id}-data"
        if podman volume exists "$volume"; then
            podman volume rm "$volume" >/dev/null
        fi
        podman volume create "$volume" >/dev/null
    done
}

capture_node_logs() {
    local destination="$1"
    for id in 1 2 3; do
        podman logs "velocita-bench-${id}" >"${destination}/node-${id}.log" 2>&1 || true
    done
}

quiesce_benchmark_nodes() {
    # raft-node prints its final role/term/index state while handling SIGTERM.
    # Stop the disposable voters as one bounded operation before copying logs,
    # so every repetition retains its liveness evidence.
    podman stop --time 10 \
        velocita-bench-1 velocita-bench-2 velocita-bench-3 >/dev/null 2>&1 || true
}

prepare_benchmark_cluster() {
    local evidence_dir="$1"
    local probe_output probe_status

    stop_benchmark_nodes
    reset_benchmark_volumes
    for id in 1 2 3; do start_node "$id"; done

    # Node status is held in shared memory rather than printed periodically, so
    # readiness must test the service contract: can one command reach a leader
    # and return a matching successful response?
    sleep 3
    set +e
    probe_output="$(podman run --rm \
        --network host \
        --shm-size 192m \
        --entrypoint /usr/local/bin/raft-client \
        "$image" \
        get \
        --aeron-dir /dev/shm/aeron-benchmark-probe \
        --members 1,2,3 \
        --request-target "1=aeron:udp?endpoint=127.0.0.1:40111" \
        --request-target "2=aeron:udp?endpoint=127.0.0.1:40112" \
        --request-target "3=aeron:udp?endpoint=127.0.0.1:40113" \
        --response-channel "aeron:udp?endpoint=0.0.0.0:40101" 2>&1)"
    probe_status=$?
    set -e
    if ((probe_status != 0)); then
        echo "$probe_output" >&2
        podman ps -a --filter name=velocita-bench \
            --format 'table {{.Names}}\t{{.Status}}' >&2
        capture_node_logs "$evidence_dir"
        for id in 1 2 3; do
            echo "===== BENCHMARK NODE ${id} =====" >&2
            tail -30 "${evidence_dir}/node-${id}.log" >&2
        done
        echo "Benchmark cluster readiness probe failed" >&2
        return 1
    fi
    echo "Benchmark cluster ready: $probe_output"
}

for rate in $rates; do
    for ((repetition = 1; repetition <= repetitions; ++repetition)); do
        run_name="rate-${rate}-rep-${repetition}"
        run_dir="${suite_dir}/${run_name}"
        mkdir -p "$run_dir"
        echo "Resetting the benchmark cluster for an independent repetition."
        prepare_benchmark_cluster "$run_dir"
        echo "Running ${run_name}: open-loop ${rate}/s, ${warmup_seconds}s warmup, ${duration_seconds}s measured"
        set +e
        client_cpu_arguments=()
        if [[ "$cpu_placement" == "partitioned" ]]; then
            client_cpu_arguments=(--cpuset-cpus 0)
        fi
        # The driver owns three request publications plus response images.
        # Aeron term buffers are memory-mapped and can exceed 192 MiB when
        # retries rise during an overloaded/leaderless interval.
        podman run --rm \
            --network host \
            --shm-size 512m \
            --userns keep-id \
            "${client_cpu_arguments[@]}" \
            --volume "${results_root}:/results" \
            --entrypoint /usr/local/bin/raft-bench \
            "$image" \
            --warmup-seconds "$warmup_seconds" \
            --duration-seconds "$duration_seconds" \
            --rate "$rate" \
            --max-in-flight "$max_in_flight" \
            --timeout-ms "$timeout_ms" \
            --retry-base-ms "$retry_base_ms" \
            --retry-cap-ms "$retry_cap_ms" \
            --response-timeout-ms "$response_timeout_ms" \
            --unconfirmed-admission-limit "$unconfirmed_admission_limit" \
            --idle-strategy "$idle_strategy" \
            --operation increment \
            --amount 0 \
            --run-mode "$run_mode" \
            --output-dir "/results/runs/${suite_id}/${run_name}" \
            --prometheus-file /results/latest.prom \
            >"${run_dir}/driver.log" 2>&1
        run_status=$?
        set -e
        quiesce_benchmark_nodes
        capture_node_logs "$run_dir"
        summary="missing"
        if [[ -f "${run_dir}/summary.json" ]]; then
            summary="$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); s=d["scheduled_to_response"]; print("n={},p99_ns={},p999_ns={},timeouts={}".format(s["count"],s["p99_ns"],s["p999_ns"],d["counters"]["steady_timed_out"]))' "${run_dir}/summary.json")"
        fi
        echo -e "${rate}\t${repetition}\t${run_status}\t${summary}" >>"${suite_dir}/suite.tsv"
        cat "${run_dir}/driver.log"
        if ((run_status == 2)); then
            performance_limits_observed=true
            echo "Performance limit observed at ${rate}/s; retaining the result and continuing." >&2
        elif ((run_status != 0)); then
            echo "Benchmark infrastructure failed; preserving evidence and stopping the sweep." >&2
            exit "$run_status"
        fi
        # Give Prometheus one scrape interval to retain this run before latest.prom changes.
        sleep 6
    done
done

for rate in $rates; do
    merge_arguments=()
    for ((repetition = 1; repetition <= repetitions; ++repetition)); do
        merge_arguments+=(--samples "/results/runs/${suite_id}/rate-${rate}-rep-${repetition}/steady-samples.csv")
    done
    podman run --rm \
        --userns keep-id \
        --volume "${results_root}:/results" \
        --entrypoint /usr/local/bin/raft-bench-merge \
        "$image" \
        --output "/results/runs/${suite_id}/rate-${rate}-aggregate.json" \
        "${merge_arguments[@]}"
done

for id in 1 2 3; do
    podman logs "velocita-bench-${id}" >"${suite_dir}/node-${id}.log" 2>&1
done
{
    echo "finished_utc=$(date -u --iso-8601=seconds)"
    if [[ -r /sys/class/thermal/thermal_zone0/temp ]]; then
        echo "final_temperature_millicelsius=$(</sys/class/thermal/thermal_zone0/temp)"
    fi
} >>"${suite_dir}/metadata.txt"

echo "Benchmark suite complete."
column -t -s $'\t' "${suite_dir}/suite.tsv" || cat "${suite_dir}/suite.tsv"
if [[ "$performance_limits_observed" == true ]]; then
    echo "Suite completed successfully and found one or more overload points."
    echo "Exit status 2 in suite.tsv means measured timeouts/rejections, not broken infrastructure."
fi
