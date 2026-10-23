#!/usr/bin/env bash
set -euo pipefail

first_sequence=${1:?first sequence}
first_value=${2:?first kill epoch}
count=${3:?count}
interval=${4:?interval seconds}

incarnation=${VELOCITA_INCARNATION:?set VELOCITA_INCARNATION to the running cluster incarnation}
secrets=/run/user/$(id -u)/velocita-secrets
work=$HOME/velocita-soak
textfile=$HOME/velocita-v2/textfile
log=$work/soak-$(date +%Y%m%d-%H%M%S).log
mkdir -p "$work" "$textfile"
[[ -f $secrets/caller-10.key ]] || { echo "missing $secrets/caller-10.key: push it from the Mac" >&2; exit 64; }

buckets=(5 10 20 50 100 250 500 1000 2500 5000)
declare -a bucket_counts
for i in "${!buckets[@]}"; do bucket_counts[$i]=0; done
ok=0; timeout_n=0; refused=0; other=0; latency_sum=0; latency_count=0; last_latency=0

publish() {
    local tmp=$textfile/.velocita-soak.prom.tmp j
    {
        echo "# TYPE velocita_soak_commands_total counter"
        echo "velocita_soak_commands_total{result=\"ok\"} $ok"
        echo "velocita_soak_commands_total{result=\"timeout\"} $timeout_n"
        echo "velocita_soak_commands_total{result=\"refused\"} $refused"
        echo "velocita_soak_commands_total{result=\"other\"} $other"
        echo "# TYPE velocita_soak_last_latency_ms gauge"
        echo "velocita_soak_last_latency_ms $last_latency"
        echo "# TYPE velocita_soak_latency_ms histogram"
        for j in "${!buckets[@]}"; do echo "velocita_soak_latency_ms_bucket{le=\"${buckets[$j]}\"} ${bucket_counts[$j]}"; done
        echo "velocita_soak_latency_ms_bucket{le=\"+Inf\"} $latency_count"
        echo "velocita_soak_latency_ms_sum $latency_sum"
        echo "velocita_soak_latency_ms_count $latency_count"
        echo "# TYPE velocita_soak_last_update_timestamp_seconds gauge"
        echo "velocita_soak_last_update_timestamp_seconds $(date +%s)"
    } > "$tmp"
    mv -f "$tmp" "$textfile/velocita-soak.prom"   # atomic: the collector never reads half a file
}

echo "soak log: $log"
publish
for ((i = 0; i < count; i++)); do
    sequence=$((first_sequence + i)); value=$((first_value + i))
    set +e
    out=$(podman run --rm --network host --userns=keep-id --read-only \
            -e AERON_TERM_BUFFER_LENGTH=1048576 \
            -v "$secrets":/run/velocita-secrets:ro -v "$work":/soak \
            --entrypoint /usr/local/bin/velocita-v2-client localhost/velocita-v2:m3 \
            --caller-key-id 10 --caller-id 8 --key-file /run/velocita-secrets/caller-10.key \
            --incarnation "$incarnation" --session-file /soak/client-session.bin \
            --response-endpoint 100.72.127.102:42410 \
            --node 1=100.72.127.102:42301 --node 3=100.85.36.22:42303 --node 2=100.84.151.116:42302 \
            --aeron-dir /tmp/velocita-client-aeron \
            --sequence "$sequence" --timeout-ms 20000 kill-epoch "$value" 2>&1)
    rc=$?
    set -e
    case $rc in
        0) ok=$((ok + 1))
           last_latency=$(sed -nE 's/.*latency_ms=([0-9]+).*/\1/p' <<<"$out")
           last_latency=${last_latency:-0}
           latency_sum=$((latency_sum + last_latency)); latency_count=$((latency_count + 1))
           for b in "${!buckets[@]}"; do (( last_latency <= buckets[b] )) && bucket_counts[$b]=$((bucket_counts[b] + 1)); done ;;
        3) timeout_n=$((timeout_n + 1)) ;;
        4) refused=$((refused + 1)) ;;
        *) other=$((other + 1)) ;;
    esac
    printf '%s seq=%s value=%s rc=%s %s\n' "$(date +%T)" "$sequence" "$value" "$rc" "$out" >> "$log"
    publish
    sleep "$interval"
done
echo "done: ok=$ok timeout=$timeout_n refused=$refused other=$other (log $log)"
