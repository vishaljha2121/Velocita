#!/bin/sh
# Owner sudo: publish the validated gateway snapshot for the strategy driver and poll every 5 s; archives the previous files.
set -eu
B=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test "$(id -u)" = 0
test "$(hostname)" = bulbazar-NUC
cd "$B"
sha256sum -c observer.sha256
A=/opt/velocita/live-observer/before-automated-launch
test ! -e "$A"
test -d /home/velocitas/velocita-v2/textfile
install -d -o root -g root -m 0700 "$A"
cp -p /opt/velocita/live-observer/live_observer.py "$A/live_observer.py"
cp -p /etc/systemd/system/velocita-live-observer.timer "$A/velocita-live-observer.timer"
install -o root -g root -m 0444 live_observer.py /opt/velocita/live-observer/live_observer.py.pending
install -o root -g root -m 0444 velocita-live-observer.timer /etc/systemd/system/velocita-live-observer.timer.pending
mv /opt/velocita/live-observer/live_observer.py.pending /opt/velocita/live-observer/live_observer.py
mv /etc/systemd/system/velocita-live-observer.timer.pending /etc/systemd/system/velocita-live-observer.timer
systemctl daemon-reload
systemctl restart velocita-live-observer.timer
systemctl show --no-pager velocita-live-observer.timer -p ActiveState -p SubState
