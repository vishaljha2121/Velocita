#!/bin/sh
# Owner-only, one halted startup/seal attempt. No driver or mode change.
set -eu
[ "$(id -un)" = vishal ]
[ "$(hostname)" = bulbazar-NUC ]
SOURCE=/home/vishal/.cache/velocita-build/focused-start-20261015/source
trap 'sudo -k' EXIT
sudo -v
sudo python3 -B "$SOURCE/docs/evidence/automated_launch_2026-10-15/focused-deploy/start_first_run.py" --run
printf '%s\n' 'Startup/seal completed with trading halted and driver stopped. Share output; do not rerun.'
