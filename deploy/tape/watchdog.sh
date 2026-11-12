#!/bin/sh
# Stops the tape on this host if Tailscale send stays above LIMIT KB/s for SAMPLES consecutive 10 s samples.
set -u
limit=${1:-600}
samples=${2:-6}
over=0
read_tx() { awk '/tailscale0:/ { sub(/.*:/, ""); split($0, f, " "); print f[9] }' /proc/net/dev; }
prev=$(read_tx)
while :; do
    sleep 10
    now=$(read_tx)
    rate=$(( (now - prev) / 10240 ))
    prev=$now
    if [ "$rate" -gt "$limit" ]; then over=$((over + 1)); else over=0; fi
    if [ "$over" -ge "$samples" ]; then
        echo "tape watchdog: tailscale0 sent ${rate} KB/s for $((samples * 10)) s; stopping the tape on $(hostname)"
        mkdir -p "$HOME/velocita-tape/data"
        date -u +%Y-%m-%dT%H:%M:%SZ > "$HOME/velocita-tape/data/watchdog-tripped"
        systemctl --user stop velocita-tape-recorder.service velocita-tape-voter.service
        over=0
    fi
done
