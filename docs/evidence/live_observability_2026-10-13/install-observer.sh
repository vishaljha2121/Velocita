#!/bin/sh
set -eu
B=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test "$(id -u)" = 0
test "$(hostname)" = bulbazar-NUC
cd "$B"
sha256sum -c observer.sha256
test ! -e /opt/velocita/live-observer
test ! -e /etc/systemd/system/velocita-live-observer.service
test ! -e /etc/systemd/system/velocita-live-observer.timer
test -d /home/velocitas/velocita-v2/textfile
install -d -o root -g root -m 0755 /opt/velocita/live-observer
install -o root -g root -m 0444 live_observer.py /opt/velocita/live-observer/live_observer.py
install -o root -g root -m 0444 velocita-live-observer.service /etc/systemd/system/velocita-live-observer.service
install -o root -g root -m 0444 velocita-live-observer.timer /etc/systemd/system/velocita-live-observer.timer
systemctl daemon-reload
systemctl start velocita-live-observer.service
systemctl enable --now velocita-live-observer.timer
systemctl show --no-pager velocita-live-observer.service velocita-live-observer.timer -p Id -p ActiveState -p SubState -p ExecMainStatus
