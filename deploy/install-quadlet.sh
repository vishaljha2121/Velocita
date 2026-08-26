#!/usr/bin/env bash
set -euo pipefail

image="${VELOCITA_IMAGE:-localhost/velocita-raft:v1}"
quadlet_dir="${XDG_CONFIG_HOME:-$HOME/.config}/containers/systemd"
user_unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"

if ! podman image exists "$image"; then
    echo "Required local image does not exist: $image" >&2
    exit 1
fi

install -d -m 0755 "$quadlet_dir" "$user_unit_dir"
install -m 0644 deploy/quadlet/*.container deploy/quadlet/*.volume "$quadlet_dir/"
install -m 0644 deploy/systemd-user/velocita-raft-cluster.target "$user_unit_dir/"

systemctl --user daemon-reload
systemctl --user enable velocita-raft-cluster.target

echo "Installed rootless Quadlet files in $quadlet_dir"
echo "Installed cluster target in $user_unit_dir"
echo "Validate before stopping manual containers:"
echo "  systemd-analyze --user --generators=true verify velocita-raft-1.service velocita-raft-2.service velocita-raft-3.service velocita-raft-metrics-exporter.service"
echo "Manage the migrated cluster with: systemctl --user <start|stop|restart> velocita-raft-cluster.target"
