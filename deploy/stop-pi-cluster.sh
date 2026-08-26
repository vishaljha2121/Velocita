#!/usr/bin/env bash
set -euo pipefail

podman stop velocita-raft-1 velocita-raft-2 velocita-raft-3
echo "Cluster stopped. The three named WAL volumes were preserved."
