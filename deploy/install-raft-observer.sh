#!/usr/bin/env bash
set -euo pipefail

libexec_dir="$HOME/.local/libexec/velocita"
user_unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
textfile_dir="${XDG_DATA_HOME:-$HOME/.local/share}/velocita/textfile"

install -d -m 0755 "$libexec_dir" "$user_unit_dir" "$textfile_dir"
install -m 0755 deploy/observability/collect-raft-textfile.sh "$libexec_dir/"
install -m 0644 \
    deploy/systemd-user/velocita-raft-observer.service \
    deploy/systemd-user/velocita-raft-observer.timer \
    "$user_unit_dir/"

systemctl --user daemon-reload
systemctl --user enable --now velocita-raft-observer.timer
systemctl --user start velocita-raft-observer.service

echo "Installed the external Raft observer."
echo "Metrics textfile: $textfile_dir/velocita_raft.prom"
echo "Timer status: systemctl --user status velocita-raft-observer.timer --no-pager"
