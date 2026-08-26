#!/usr/bin/env bash
set -euo pipefail

image="${VELOCITA_IMAGE:-localhost/velocita-raft:v1}"
members="1,2,3"
response_channel="aeron:udp?endpoint=127.0.0.1:40101"

for id in 1 2 3; do
    podman volume exists "velocita-raft-node${id}-data" ||
        podman volume create "velocita-raft-node${id}-data" >/dev/null
done

podman run --detach --replace \
    --name velocita-raft-1 \
    --network host \
    --shm-size 192m \
    --stop-timeout 15 \
    --volume velocita-raft-node1-data:/var/lib/velocita-raft:U \
    "$image" \
    --node-id 1 --members "$members" \
    --request-channel "aeron:udp?endpoint=127.0.0.1:40111" \
    --response-channel "$response_channel" \
    --peer-channel "aeron:udp?endpoint=127.0.0.1:40211" \
    --peer-target "2=aeron:udp?endpoint=127.0.0.1:40212" \
    --peer-target "3=aeron:udp?endpoint=127.0.0.1:40213" >/dev/null

podman run --detach --replace \
    --name velocita-raft-2 \
    --network host \
    --shm-size 192m \
    --stop-timeout 15 \
    --volume velocita-raft-node2-data:/var/lib/velocita-raft:U \
    "$image" \
    --node-id 2 --members "$members" \
    --request-channel "aeron:udp?endpoint=127.0.0.1:40112" \
    --response-channel "$response_channel" \
    --peer-channel "aeron:udp?endpoint=127.0.0.1:40212" \
    --peer-target "1=aeron:udp?endpoint=127.0.0.1:40211" \
    --peer-target "3=aeron:udp?endpoint=127.0.0.1:40213" >/dev/null

podman run --detach --replace \
    --name velocita-raft-3 \
    --network host \
    --shm-size 192m \
    --stop-timeout 15 \
    --volume velocita-raft-node3-data:/var/lib/velocita-raft:U \
    "$image" \
    --node-id 3 --members "$members" \
    --request-channel "aeron:udp?endpoint=127.0.0.1:40113" \
    --response-channel "$response_channel" \
    --peer-channel "aeron:udp?endpoint=127.0.0.1:40213" \
    --peer-target "1=aeron:udp?endpoint=127.0.0.1:40211" \
    --peer-target "2=aeron:udp?endpoint=127.0.0.1:40212" >/dev/null

echo "Started three Raft voters. Wait five seconds, then run:"
echo "  podman logs --tail 20 velocita-raft-1"
echo "  podman logs --tail 20 velocita-raft-2"
echo "  podman logs --tail 20 velocita-raft-3"
