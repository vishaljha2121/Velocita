#!/bin/zsh
# Owner-run only, from the Mac. No live venue access.
set -euo pipefail
cd "$HOME/aeron-raft-pi"
C="$HOME/.cache/velocita-build/stochastic-1s-20261007/velocita-v2-client"
S="$HOME/.cache/velocita-operator/c8a6a0a78fbf49a099b222124a5d557f"
mkdir -p "$S"
A=(--caller-key-id 9 --caller-id 7 --key-stdin
   --incarnation c8a6a0a78fbf49a099b222124a5d557f
   --session-file "$S/client.session" --response-endpoint 100.91.80.64:44400
   --node 1=100.72.127.102:44301 --node 2=100.84.151.116:44302 --node 3=100.85.36.22:44303
   --aeron-dir "$S/aeron")
# Stop on any failed command. Do not automatically retry a partial ceremony.
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 1 kill-epoch 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 2 executor-generation 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 3 install-catalog 3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe 1 1
tools/velocita-secrets client-key caller-9 | "$C" "${A[@]}" --sequence 4 risk-limits \
  account=2 instrument=2 risk_policy_version=1 expected_risk_version=0 kill_epoch=1 executor_generation=1 \
  max_order_lots=12 max_reserved_lots=12 max_order_notional_micros=25000000 max_reserved_exposure_micros=25000000 \
  initial_margin_ppm=1000000 maximum_fee_ppm=3000 max_position_lots=12 allow_short=0 reason_code=20261007
