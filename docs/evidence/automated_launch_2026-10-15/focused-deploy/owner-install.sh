#!/bin/sh
# Owner on NUC1: guarded executable update, caller restoration, read-only observer.
# Never starts the trading services or driver. Stop and inspect any partial failure.
set -eu
B=/home/vishal/.cache/velocita-build/focused-deploy-1f8d7b6
SOURCE=$B/source
TOOLS=$SOURCE/docs/evidence/automated_launch_2026-10-15
[ "$(id -un)" = vishal ]
[ "$(hostname)" = bulbazar-NUC ]
trap 'sudo -k' EXIT
sudo -v
sudo python3 -B "$TOOLS/focused-deploy/upgrade_first_start.py" \
  --bundle "$B/payload" \
  --old-manifest-sha256 f139936f89cae8de526f82c02b41aa3be1b2ec668efdfb7492281bb8d5d2568a \
  --archive /var/lib/velocita-first-start-upgrade-1f8d7b6 --apply
sudo python3 -B "$TOOLS/restore_caller_keys.py" \
  --receiver "$SOURCE/deploy/v2/swap-runtime"
sudo sh "$B/observer/upgrade-observer.sh"
sudo systemctl show velocita-swap-gateway.service velocita-swap-signer.service \
  -p Id -p ActiveState -p MainPID -p ControlPID -p RefuseManualStart
printf '%s\n' 'Installation finished with trading start guards retained. Share output; do not rerun.'
