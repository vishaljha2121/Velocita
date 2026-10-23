#!/bin/sh
set -eu
node=$1
case "$node" in 1) host=nuc1;; 2) host=pi;; 3) host=nuc2;; *) exit 64;; esac
old_inc=abd1e149a26d4062ba5bbba2d90b3305
new_inc=348e0b2cd4f639ff404df4eeadee0ee5
voter=localhost/velocita-v2:roles-2f5d405
shadow=localhost/velocita-v2-shadow:paper-roles-2f5d405
base="$HOME/velocita-indicator-paper"
live="$base/stochastic"
retired="$base/retired/stochastic-abd1e149-20261009"
units="$HOME/.config/systemd/user"
refuse() { printf '%s\n' "$*" >&2; exit 64; }
require_stopped() {
 state=$(systemctl --user show "$1.service" --property=ActiveState --value) || refuse "Cannot read service state: $1"
 test "$state" = inactive || refuse "Service must be stopped: $1 ($state)"
}
require_absent() {
 if grep -q -e "$1" "$2"; then refuse "Unexpected existing setting: $1 in $2"; else
  code=$?
  test "$code" = 1 || refuse "Cannot inspect $2"
 fi
}
names='velocita-paper-stochastic-node velocita-paper-stochastic-exporter'
if test "$node" = 1; then
 names="$names velocita-paper-stochastic-edge velocita-paper-stochastic-gateway velocita-paper-indicator-observer"
fi
for name in $names; do
 require_stopped "$name"
 test -r "$units/$name.service" || refuse "Missing unit: $name"
 grep -q 'ledger-f55209e' "$units/$name.service" || refuse "Unexpected image in $name"
done
test ! -e "$retired"
podman image exists "$voter"
if test "$node" = 1; then
 podman image exists "$shadow"
 test -r "$base/observer.json"
 require_absent '--require-loss-limit' "$units/velocita-paper-stochastic-gateway.service"
 grep -q -- ' --allow-new-risk' "$units/velocita-paper-stochastic-gateway.service"
fi
test -r "$live/config/catalog.bin"
grep -q "^cluster.incarnation = $old_inc$" "$live/config/node-$node.conf"
grep -q '^startup = recover$' "$live/config/node-$node.conf"
require_absent '^ingress\.' "$live/config/node-$node.conf"
mkdir -p "$base/retired"
mv "$live" "$retired"
mkdir -p "$live/config" "$live/data" "$live/edge" "$live/gateway" "$live/setup" "$live/operator" "$retired/units-before"
cp "$retired/config/catalog.bin" "$live/config/catalog.bin"
sed -e "s/$old_inc/$new_inc/g" -e 's/^startup = recover$/startup = bootstrap/' "$retired/config/node-$node.conf" > "$live/config/node-$node.conf"
printf '%s\n' 'ingress.rate_per_second = 6' 'ingress.burst = 60' >> "$live/config/node-$node.conf"
grep -q '^startup = bootstrap$' "$live/config/node-$node.conf"
grep -q "^cluster.incarnation = $new_inc$" "$live/config/node-$node.conf"
for name in $names; do
 unit="$units/$name.service"
 cp "$unit" "$retired/units-before/$name.service"
 sed -e "s/$old_inc/$new_inc/g" \
  -e "s#localhost/velocita-v2:ledger-f55209e#$voter#g" \
  -e "s#localhost/velocita-v2-shadow:paper-ledger-f55209e#$shadow#g" "$unit" > "$unit.next"
 require_absent "$old_inc" "$unit.next"
 require_absent 'ledger-f55209e' "$unit.next"
 mv "$unit.next" "$unit"
done
if test "$node" = 1; then
 gateway="$units/velocita-paper-stochastic-gateway.service"
 sed -e 's/ --allow-new-risk/ --require-loss-limit --allow-new-risk/' "$gateway" > "$gateway.next"
 mv "$gateway.next" "$gateway"
 grep -q -- '--require-loss-limit --allow-new-risk' "$gateway"
 cp "$base/observer.json" "$retired/observer-before.json"
fi
systemctl --user daemon-reload
printf '%s\n' "RETIRED $retired" "STAGED $host incarnation=$new_inc"
