#!/bin/sh
set -eu
[ $# -eq 1 ] || { echo "usage: $0 VOTER_CONTAINER_NAME" >&2; exit 64; }
others=$(podman ps --format '{{.Names}}' | grep -E '^velocita-(v2-node-[0-9]+|paper-[a-z]+-node)$' | grep -vx "$1" || true)
if [ -n "$others" ]; then
    echo "one-cluster guard: refusing to start $1 while another cluster's voter runs on this host: $others" >&2
    exit 64
fi
