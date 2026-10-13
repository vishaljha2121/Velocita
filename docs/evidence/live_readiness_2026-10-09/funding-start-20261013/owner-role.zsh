#!/bin/zsh
set -euo pipefail
cd "$HOME/aeron-raft-pi"
C="$HOME/.cache/velocita-build/swap-cutover-20261013/mac-make/velocita-v2-client"
S="$HOME/.cache/velocita-operator/b79e65be1ea14e219016ec48ea02efef"
A=(--caller-key-id 9 --caller-id 7 --key-stdin
 --incarnation b79e65be1ea14e219016ec48ea02efef
 --session-file "$S/client.session" --response-endpoint 100.91.80.64:44400
 --node 1=100.72.127.102:44301 --node 2=100.84.151.116:44302 --node 3=100.85.36.22:44303
 --aeron-dir "$S/aeron")
if [[ "${1:-}" == --dry-run ]]; then
 "$C" "${A[@]}" --dry-run --sequence 4 assign-role 22 gateway
elif [[ "${1:-}" == --run ]]; then
 tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 4 assign-role 22 gateway
else
 exit 64
fi
