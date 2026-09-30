#!/usr/bin/env bash
# velocita-soak: steady low-rate authenticated commands against the shadow
# cluster, with periodic health samples from every voter. Runs on the Mac.
#
#   tools/velocita-soak.sh FIRST_SEQUENCE FIRST_KILL_EPOCH COUNT INTERVAL_SECONDS
#
# Pass criteria (decided before the run, M3 soak):
#   every command COMMITTED with code=0; zero elections (the term never changes);
#   auth_refusals and fence_refusals stay 0; every voter ends with
#   commit == applied == durable at the same index.
# The secret goes Keychain -> pipe -> client stdin; it is never on a command line.
set -euo pipefail

first_sequence=${1:?first sequence}
first_value=${2:?first kill epoch}
count=${3:?count}
interval=${4:?interval seconds}

client=${VELOCITA_CLIENT:-$HOME/bin/velocita-v2-client}
incarnation=${VELOCITA_INCARNATION:?set VELOCITA_INCARNATION to the running cluster incarnation}
log=${VELOCITA_SOAK_LOG:-$HOME/velocita-soak-$(date +%Y%m%d-%H%M%S).log}
here=$(cd "$(dirname "$0")" && pwd)

now_ms() { perl -MTime::HiRes=time -e 'printf "%.0f\n", time*1000'; }

health() {
    for target in "velocitas@100.72.127.102 1" "velocitas@100.84.151.116 2" "velocitas@100.85.36.22 3"; do
        set -- $target
        printf '%s health node-%s %s\n' "$(date +%T)" "$2" \
            "$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$1" \
               "podman exec velocita-v2-node-$2 velocita-v2-status /var/lib/velocita-v2/node-$2/health.page" 2>&1)"
    done
}

echo "soak log: $log"
health | tee -a "$log"
for ((i = 0; i < count; i++)); do
    sequence=$((first_sequence + i))
    value=$((first_value + i))
    started=$(now_ms)
    if out=$("$here/velocita-secrets" client-key caller-9 | "$client" --key-stdin \
            --caller-key-id 9 --caller-id 7 --incarnation "$incarnation" \
            --session-file "$HOME/.velocita/client-session.bin" \
            --response-endpoint 100.91.80.64:42400 \
            --node 1=100.72.127.102:42301 --node 3=100.85.36.22:42303 --node 2=100.84.151.116:42302 \
            --aeron-dir /tmp/velocita-client-aeron \
            --sequence "$sequence" --timeout-ms 20000 kill-epoch "$value" 2>&1); then :; fi
    elapsed=$(( $(now_ms) - started ))
    printf '%s cmd seq=%s value=%s ms=%s %s\n' "$(date +%T)" "$sequence" "$value" "$elapsed" "$out" | tee -a "$log"
    if (( (i + 1) % 6 == 0 )); then health | tee -a "$log"; fi
    sleep "$interval"
done
health | tee -a "$log"

echo "== summary ($log)"
committed=$(grep -c ' cmd .*COMMITTED.* code=0 ' "$log" || true)
failures=$(grep ' cmd ' "$log" | grep -vc 'COMMITTED.* code=0 ' || true)
terms=$(grep -o ' term=[0-9]*' "$log" | sort -u | tr '\n' ' ')
echo "commands committed ok: $committed / $count   failures: $failures"
echo "distinct terms seen: $terms  (one value = zero elections)"
grep ' cmd ' "$log" | sed -E 's/.* ms=([0-9]+).*/\1/' | sort -n | awk '
    {v[NR]=$1} END {if (NR) printf "client round trip ms: median=%s p99=%s max=%s\n", v[int((NR+1)/2)], v[int(NR*0.99)>0?int(NR*0.99):1], v[NR]}'
