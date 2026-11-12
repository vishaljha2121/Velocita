#!/bin/sh
# Stops the tape on this host if Tailscale send stays above LIMIT KB/s for SAMPLES consecutive 10 s samples,
# then restarts it once the link has been quiet for the cool-down, which doubles on each trip up to an hour.
set -u
limit=${1:-600}
samples=${2:-6}
cooldown_start=${3:-300}
cooldown_max=${4:-3600}
cooldown=$cooldown_start
over=0
stopped_at=0
quiet=0
last_trip=-100000
ctl=${TAPE_WATCHDOG_SYSTEMCTL:-systemctl}
tick=${TAPE_WATCHDOG_SLEEP:-10}
read_tx() {
    if [ -n "${TAPE_WATCHDOG_READ:-}" ]; then "$TAPE_WATCHDOG_READ"; return; fi
    awk '/tailscale0:/ { sub(/.*:/, ""); split($0, f, " "); print f[9] }' /proc/net/dev
}
prev=$(read_tx)
t=0
while :; do
    sleep "$tick"
    now=$(read_tx)
    [ -n "$now" ] || exit 0
    rate=$(( (now - prev) / 10240 ))
    prev=$now
    t=$((t + 10))
    if [ "$stopped_at" -eq 0 ]; then
        if [ "$rate" -gt "$limit" ]; then over=$((over + 1)); else over=0; fi
        if [ "$over" -ge "$samples" ]; then
            if [ $((t - last_trip)) -lt 3600 ]; then cooldown=$((cooldown * 2)); else cooldown=$cooldown_start; fi
            [ "$cooldown" -gt "$cooldown_max" ] && cooldown=$cooldown_max
            last_trip=$t
            echo "tape watchdog: tailscale0 sent ${rate} KB/s for $((samples * 10)) s; stopping the tape on $(hostname) for at least ${cooldown} s"
            mkdir -p "$HOME/velocita-tape/data"
            date -u +%Y-%m-%dT%H:%M:%SZ > "$HOME/velocita-tape/data/watchdog-tripped"
            $ctl --user stop velocita-tape-recorder.service velocita-tape-voter.service
            stopped_at=$t
            over=0
            quiet=0
        fi
        continue
    fi
    if [ "$rate" -lt $((limit / 4)) ]; then quiet=$((quiet + 10)); else quiet=0; fi
    if [ $((t - stopped_at)) -ge "$cooldown" ] && [ "$quiet" -ge 300 ]; then
        echo "tape watchdog: link quiet for ${quiet} s; restarting the tape on $(hostname)"
        $ctl --user start velocita-tape-voter.service
        sleep "$tick"
        $ctl --user start velocita-tape-recorder.service
        prev=$(read_tx)
        stopped_at=0
    fi
done
