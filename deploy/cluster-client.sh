#!/usr/bin/env bash
set -euo pipefail

image="${VELOCITA_IMAGE:-localhost/velocita-raft:v1}"
if [[ $# -lt 1 ]]; then
    echo "usage: $0 get | increment AMOUNT" >&2
    exit 64
fi

operation="$1"
shift
case "$operation" in
    get)
        client_command=(get)
        ;;
    increment)
        if [[ $# -ne 1 ]]; then
            echo "increment requires exactly one amount" >&2
            exit 64
        fi
        client_command=(increment "$1")
        ;;
    *)
        echo "operation must be get or increment" >&2
        exit 64
        ;;
esac

request_id="${VELOCITA_REQUEST_ID:-$(date +%s%N)}"
echo "request_id=${request_id}"
podman run --rm --network host --shm-size 128m \
    --entrypoint /usr/local/bin/raft-client \
    "$image" "${client_command[@]}" \
    --request-id "$request_id" \
    --members 1,2,3 \
    --request-target "1=aeron:udp?endpoint=127.0.0.1:40111" \
    --request-target "2=aeron:udp?endpoint=127.0.0.1:40112" \
    --request-target "3=aeron:udp?endpoint=127.0.0.1:40113" \
    --response-channel "aeron:udp?endpoint=127.0.0.1:40101"
