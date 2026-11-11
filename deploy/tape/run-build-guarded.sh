#!/bin/sh
# Builds the tape image on a host while keeping the disk safe for the services already running there.
set -u
[ $# -eq 1 ] || { echo "usage: $0 TAG" >&2; exit 64; }
tag=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
log="$here/tape-build-$tag.log"
start_kib=4000000
stop_kib=3000000
free_kib() { df -Pk "$HOME" /var/tmp | awk 'NR > 1 { print $4 }' | sort -n | head -1; }
free=$(free_kib)
if [ "$free" -lt "$start_kib" ]; then
    echo "tape build: refused, only $free KiB free (needs $start_kib)" | tee "$log" >&2
    exit 75
fi
sh "$here/deploy/tape/build-image.sh" "$tag" > "$log" 2>&1 &
build=$!
trap 'kill "$build" 2>/dev/null; wait "$build" 2>/dev/null; exit 130' INT TERM
while kill -0 "$build" 2>/dev/null; do
    free=$(free_kib)
    if [ "$free" -lt "$stop_kib" ]; then
        echo "tape build: stopped, free disk fell to $free KiB" >> "$log"
        pkill -TERM -P "$build" 2>/dev/null
        kill "$build" 2>/dev/null
        wait "$build" 2>/dev/null
        exit 75
    fi
    sleep 10
done
wait "$build"
status=$?
echo "tape build: exit $status" >> "$log"
exit "$status"
