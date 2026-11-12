#!/bin/sh
# Ends the current tape segment and starts the next one on this host. Every host runs this at the same
# interval boundary; the new incarnation is the boundary's Unix time, so all three agree without talking.
# The new segment's first entry records the previous segment's last signed head (D-55).
set -eu
interval=${TAPE_ROTATE_INTERVAL:-14400}
keep=${TAPE_ROTATE_KEEP:-2}
head_wait=${TAPE_ROTATE_HEAD_WAIT:-75}
base=${TAPE_HOME:-$HOME/velocita-tape}
ctl=${TAPE_ROTATE_SYSTEMCTL:-systemctl}
now=${TAPE_ROTATE_NOW:-$(date +%s)}
config=$base/config
data=$base/data
container_data=/var/lib/velocita-tape

incarnation=$(( now - now % interval ))
current=$(sed -n 's/^incarnation=//p' "$config/voter.conf")
if [ "$current" = "$incarnation" ]; then
    echo "tape rotate: already on segment $incarnation"
    exit 0
fi
if [ "$current" -gt "$incarnation" ] && [ $(( current % interval )) -eq 0 ]; then
    echo "tape rotate: current segment $current is newer than $incarnation; refusing" >&2
    exit 65
fi

echo "tape rotate: ending segment $current, starting $incarnation"
$ctl --user stop velocita-tape-recorder.service
sleep "$head_wait"
$ctl --user stop velocita-tape-voter.service

segment=$data/segments/$current
mkdir -p "$data/segments"
[ ! -e "$segment" ] || { echo "tape rotate: $segment already exists" >&2; exit 65; }
mv "$data/voter" "$segment"
mkdir -p "$data/voter"
last=$(ls "$segment/heads" 2>/dev/null | sed -n 's/^head-\([0-9]*\)\.json$/\1/p' | sort -n | tail -1)

for f in voter.conf recorder.conf; do
    sed -e "s/^incarnation=.*/incarnation=$incarnation/" -e '/^previous_head_file=/d' "$config/$f" > "$config/$f.next"
done
if [ -n "$last" ]; then
    echo "previous_head_file=$container_data/segments/$current/heads/head-$last.json" >> "$config/voter.conf.next"
    echo "tape rotate: segment $current ends at signed head $last"
else
    echo "tape rotate: segment $current has no signed head; the next segment starts unlinked" >&2
fi
mv "$config/voter.conf.next" "$config/voter.conf"
mv "$config/recorder.conf.next" "$config/recorder.conf"

$ctl --user start velocita-tape-voter.service
sleep "${TAPE_ROTATE_START_WAIT:-20}"
$ctl --user start velocita-tape-recorder.service
echo "tape rotate: segment $incarnation started"

[ ! -f "$segment/wal.log" ] || nice -n 19 gzip -6 "$segment/wal.log"
if [ "$keep" -gt 0 ]; then
    ls "$data/segments" | grep -E '^[0-9]+$' | sort -n | awk -v keep="$keep" '{ s[NR] = $0 } END { for (i = 1; i <= NR - keep; i++) print s[i] }' |
        while read -r old; do
            [ "$old" = "$current" ] && continue
            rm -rf "${data:?}/segments/$old"
            echo "tape rotate: removed local copy of segment $old"
        done
fi
