#!/bin/sh
set -eu
node=$1
case "$node" in 1) host=nuc1;; 2) host=pi;; 3) host=nuc2;; *) exit 64;; esac
base="$HOME/velocita-indicator-paper"
old="$base/stochastic"
retired="$base/retired/stochastic-19137567-20261007"
prepared="$HOME/stochastic-fresh-c8a6a0a7"
# All previous voters must have been stopped before this script is used.
! systemctl --user is-active --quiet velocita-paper-stochastic-node.service
! systemctl --user is-active --quiet velocita-paper-stochastic-exporter.service
test ! -e "$retired"
mkdir -p "$base/retired"
mv "$old" "$retired"
mkdir -p "$old/config" "$old/data" "$old/edge" "$old/gateway" "$old/setup" "$old/operator"
cp "$retired/config/catalog.bin" "$old/config/catalog.bin"
cp "$prepared/stochastic/$host/node-$node.conf" "$old/config/node-$node.conf"
cp "$prepared/stochastic/profile.json" "$old/config/profile.json"
mkdir "$retired/units-before-reset"
for unit in "$prepared/stochastic/$host/"*.service; do
 name=$(basename "$unit")
 if test -f "$HOME/.config/systemd/user/$name"; then
  cp "$HOME/.config/systemd/user/$name" "$retired/units-before-reset/$name"
 fi
 cp "$unit" "$HOME/.config/systemd/user/$name"
done
if test "$node" = 1; then
 cp "$HOME/.config/systemd/user/velocita-paper-indicator-observer.service" "$retired/units-before-reset/"
 cp "$prepared/velocita-paper-indicator-observer.service" "$HOME/.config/systemd/user/"
 cp "$base/observer.json" "$retired/observer-before-reset.json"
 cp "$prepared/observer.json" "$base/observer.json"
fi
systemctl --user daemon-reload
printf '%s\n' "ARCHIVED $retired" "PREPARED $host incarnation=c8a6a0a78fbf49a099b222124a5d557f"
