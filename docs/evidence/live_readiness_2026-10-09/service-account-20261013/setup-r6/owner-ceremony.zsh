#!/bin/zsh
# Approved read-only cluster bootstrap: no trading, risk-limit or funding command.
set -euo pipefail
cd "$HOME/aeron-raft-pi"
C="$HOME/.cache/velocita-build/swap-cutover-20261013/mac-make/velocita-v2-client"
S="$HOME/.cache/velocita-operator/b79e65be1ea14e219016ec48ea02efef"
umask 077
mkdir "$S"
A=(--caller-key-id 9 --caller-id 7 --key-stdin
 --incarnation b79e65be1ea14e219016ec48ea02efef
 --session-file "$S/client.session" --response-endpoint 100.91.80.64:44400
 --node 1=100.72.127.102:44301 --node 2=100.84.151.116:44302 --node 3=100.85.36.22:44303
 --aeron-dir "$S/aeron")
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 1 kill-epoch 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 2 executor-generation 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 3 install-catalog 3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe 1 1
