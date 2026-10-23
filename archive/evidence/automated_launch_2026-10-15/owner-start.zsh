#!/bin/zsh
# Owner Mac, steps 15 and 16: arm, start signer then gateway with trading halted, show the gateway log, seal, upgrade the observer.
# Stops at the first failure; no retry. Trading stays halted (mode 5) throughout.
set -euo pipefail
NUC=vishal@100.72.127.102
B=/home/vishal/.cache/velocita-build/automated-launch-aae119b9
RECV=$B/receiver/deploy/v2/swap-runtime
ssh -t $NUC "set -e
sudo python3 -B $RECV/review_transition.py --bundle $B/transitions/arm --sha256 bc42e3d12fcf63534812dc82ad613bd45f5d3d1c104fc2491fb1f50011cd4521 --apply
sudo systemctl start velocita-swap-signer; sleep 5; systemctl is-active velocita-swap-signer
sudo systemctl start velocita-swap-gateway; sleep 40
sudo journalctl -u velocita-swap-signer -u velocita-swap-gateway --since '-2min' --no-pager -o cat | grep -v '^\s*$' | tail -40
systemctl is-active velocita-swap-signer velocita-swap-gateway
sudo python3 -B $RECV/review_transition.py --bundle $B/transitions/seal --sha256 44d071f894505c6214e434b358f4f2cf706c2411bb1b0e1d6fd116b9fb69237d --apply
sudo sh $B/observer/upgrade-observer.sh
sudo -k"
