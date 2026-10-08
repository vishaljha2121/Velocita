#!/bin/sh
set -eu
node=$1
case "$node" in 1) host=nuc1;; 2) host=pi;; 3) host=nuc2;; *) exit 64;; esac
old_inc=c8a6a0a78fbf49a099b222124a5d557f
new_inc=abd1e149a26d4062ba5bbba2d90b3305
base="$HOME/velocita-indicator-paper"
live="$base/stochastic"
retired="$base/retired/stochastic-c8a6a0a7-20261008"
units="$HOME/.config/systemd/user"
! systemctl --user is-active --quiet velocita-paper-stochastic-node.service
! systemctl --user is-active --quiet velocita-paper-stochastic-exporter.service
if test "$node" = 1; then
 ! systemctl --user is-active --quiet velocita-paper-stochastic-edge.service
 ! systemctl --user is-active --quiet velocita-paper-stochastic-gateway.service
fi
test ! -e "$retired"
podman image exists localhost/velocita-v2:ledger-8a15b98
grep -q "^cluster.incarnation = $old_inc$" "$live/config/node-$node.conf"
mkdir -p "$base/retired"
mv "$live" "$retired"
mkdir -p "$live/config" "$live/data" "$live/edge" "$live/gateway" "$live/setup" "$live/operator" "$retired/units-before"
cp "$retired/config/catalog.bin" "$live/config/catalog.bin"
sed -e "s/$old_inc/$new_inc/g" -e 's/^startup = recover$/startup = bootstrap/' "$retired/config/node-$node.conf" > "$live/config/node-$node.conf"
grep -q '^startup = bootstrap$' "$live/config/node-$node.conf"
grep -q "^cluster.incarnation = $new_inc$" "$live/config/node-$node.conf"
for name in velocita-paper-stochastic-node velocita-paper-stochastic-exporter velocita-paper-stochastic-gateway velocita-paper-stochastic-edge velocita-paper-indicator-observer; do
 unit="$units/$name.service"
 test -f "$unit" || continue
 cp "$unit" "$retired/units-before/$name.service"
 sed -i -e "s/$old_inc/$new_inc/g" \
  -e 's#localhost/velocita-v2:m3-cp081#localhost/velocita-v2:ledger-8a15b98#g' \
  -e 's#localhost/velocita-v2:paper-7297bd5#localhost/velocita-v2:ledger-8a15b98#g' \
  -e 's#localhost/velocita-v2-shadow:paper-cash-d542601#localhost/velocita-v2-shadow:paper-ledger-8a15b98#g' \
  -e 's#localhost/velocita-v2-shadow:paper-cash-9a71762#localhost/velocita-v2-shadow:paper-ledger-8a15b98#g' "$unit"
 ! grep -q "$old_inc" "$unit"
 ! grep -q -e 'm3-cp081' -e 'paper-7297bd5' -e 'paper-cash-' "$unit"
done
if test "$node" = 1; then
 podman image exists localhost/velocita-v2-shadow:paper-ledger-8a15b98
 rm -rf "$units/velocita-paper-stochastic-edge.service.d.retired"
 if test -d "$units/velocita-paper-stochastic-edge.service.d"; then
  cp -r "$units/velocita-paper-stochastic-edge.service.d" "$retired/units-before/"
 fi
 cp "$base/observer.json" "$retired/observer-before.json"
fi
systemctl --user daemon-reload
printf '%s\n' "RETIRED $retired" "STAGED $host incarnation=$new_inc"
