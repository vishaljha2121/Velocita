# Verifiable tape runbook

Every command runs from the repository root on the Mac unless it starts with `ssh`. Hosts: voter and recorder 1 on
NUC1 (`velocitas@100.72.127.102`), 2 on the Pi (`velocitas@100.84.151.116`), 3 on NUC2 (`velocitas@100.85.36.22`).
The trading cluster stays halted and is never touched: the tape has its own images, units, ports, data and secrets
(DECISIONS.md D-30).

## 0. Make room (owner)

Builds refuse to start below 4,000,000 KiB free and stop below 3,000,000 KiB (D-33). On 2026-11-11:

| Host | Free | Note |
|---|---|---|
| NUC1 | 0 bytes (100%) | `~/velocita-md` is 60 GB; Prometheus, Grafana and Alertmanager are running on a full disk |
| Pi | 3.1 GB | four `velocita-v2:qualified-*` images of about 727 MB each, plus a dangling 639 MB image |
| NUC2 | 234 GB | fine |

What to remove is the owner's decision. Check afterwards with `ssh HOST 'df -Pk $HOME /var/tmp'`.

## 1. Ship and build (owner approves; runs next to live services)

```sh
C=$(git rev-parse --short=7 HEAD)
for h in velocitas@100.72.127.102 velocitas@100.84.151.116 velocitas@100.85.36.22; do tools/tape-ship.sh HEAD "$h"; done
ssh velocitas@100.85.36.22 "systemd-run --user --slice=velocitabuild.slice --unit=velocita-tape-build-$C --property=Nice=10 \
    /usr/bin/taskset -c 7 /usr/bin/timeout 3600 /bin/sh \$HOME/velocita-tape-release-$C/deploy/tape/run-build-guarded.sh $C"
ssh velocitas@100.72.127.102 "systemd-run --user --slice=velocitabuild.slice --unit=velocita-tape-build-$C --property=Nice=10 \
    /usr/bin/taskset -c 3 /usr/bin/timeout 3600 /bin/sh \$HOME/velocita-tape-release-$C/deploy/tape/run-build-guarded.sh $C"
ssh velocitas@100.84.151.116 "systemd-run --user --slice=velocitabuild.slice --unit=velocita-tape-build-$C --property=Nice=10 \
    /usr/bin/taskset -c 3 /usr/bin/timeout 3600 /bin/sh \$HOME/velocita-tape-release-$C/deploy/tape/run-build-guarded.sh $C"
```

Build one host at a time. What was used on 2026-11-11 (D-37), because NUC1 and the Pi are short of disk:
NUC2 builds natively as above; the Pi's image is built on the Mac from the Pi's own toolchain image and
loaded onto the Pi; NUC1 loads NUC2's image. Stream binary data with `/usr/bin/ssh`, never the shell's
`ssh` function (D-38), and copy through a checksummed file rather than a long pipe:

```sh
/usr/bin/ssh velocitas@100.84.151.116 'podman save --format docker-archive localhost/velocita-dev:current' | docker load
docker build --platform linux/arm64 --build-arg DEV_IMAGE=localhost/velocita-dev:current \
    --build-arg OPENSSL_VERSION=3.5.5-1ubuntu3.7 --build-arg SOURCE_COMMIT=$(git rev-parse HEAD) \
    --build-arg TOOLCHAIN_ID=7796e188c717e65a9d4793a3b8673dde0124b3514a82b5ff3a060fe68728e97b \
    -f deploy/tape/Containerfile -t localhost/velocita-tape:$C .
docker save localhost/velocita-tape:$C | /usr/bin/ssh velocitas@100.84.151.116 'podman load'
/usr/bin/ssh velocitas@100.85.36.22 "podman save --format docker-archive localhost/velocita-tape:$C" > tape-amd64.tar
scp tape-amd64.tar velocitas@100.72.127.102: && /usr/bin/ssh velocitas@100.72.127.102 'podman load -i tape-amd64.tar && rm tape-amd64.tar'
```

Run `docker build` from a `git archive` export, not the working tree. Follow with `ssh HOST "tail -f ~/velocita-tape-release-$C/tape-build-$C.log"`. The image
build runs every `tape-` test inside the builder and fails if any fails. When each finishes:

```sh
ssh HOST "podman tag localhost/velocita-tape:$C localhost/velocita-tape:current && \
    podman image inspect --format '{{index .Labels \"org.velocita.source_commit\"}}' localhost/velocita-tape:current"
```

## 2. Key ceremony, gate G2 (owner, with `!`)

```sh
! tools/tape-key-ceremony.sh master
! tools/tape-key-ceremony.sh heads $C
! tools/tape-key-ceremony.sh fingerprints
```

`master` creates the Keychain item `tape-master` (HMAC master secret, 32 bytes). `heads` generates each voter's
Ed25519 head key on its own host inside the image, keeps the private half in `~/velocita-tape/config` (0600), and
copies the three public keys to every host and to `~/.config/velocita-tape/public` on the Mac. Record the
fingerprints in the results file: they are what a reader checks the published heads against.

## 3. Install and start, gate G3 (owner approves)

```sh
python3 tools/tape-render-configs.py ~/.cache/velocita-tape-deploy/$C
for n in 1 2 3; do
  case $n in 1) h=velocitas@100.72.127.102;; 2) h=velocitas@100.84.151.116;; 3) h=velocitas@100.85.36.22;; esac
  scp ~/.cache/velocita-tape-deploy/$C/host-$n/voter.conf ~/.cache/velocita-tape-deploy/$C/host-$n/recorder.conf "$h":velocita-tape/config/
  ssh "$h" "set -e; R=\$HOME/velocita-tape-release-$C/deploy/tape/systemd; mkdir -p \$HOME/velocita-tape/data/voter \
      \$HOME/.config/user-tmpfiles.d; cp \$R/velocita-tape-tmpfiles.conf \$HOME/.config/user-tmpfiles.d/; \
      systemd-tmpfiles --user --create; cp \$R/velocita-tape-*.service \$HOME/.config/systemd/user/; systemctl --user daemon-reload"
done
! tools/tape-key-ceremony.sh push
for h in velocitas@100.72.127.102 velocitas@100.84.151.116 velocitas@100.85.36.22; do ssh "$h" 'systemctl --user start velocita-tape-voter'; done
sleep 10
for h in velocitas@100.72.127.102 velocitas@100.84.151.116 velocitas@100.85.36.22; do ssh "$h" 'systemctl --user start velocita-tape-recorder velocita-tape-exporter'; done
```

Keep `~/.cache/velocita-tape-deploy/$C/INCARNATION`: the verifier needs it. After a host reboot, run
`! tools/tape-key-ceremony.sh push` before the units can start (the master secret lives only in tmpfs).

Install the watchdog on every host (D-43) before starting the tape:

```sh
ssh HOST "cp ~/velocita-tape-release-$C/deploy/tape/watchdog.sh ~/velocita-tape/ && \
    cp ~/velocita-tape-release-$C/deploy/tape/systemd/velocita-tape-watchdog.service ~/.config/systemd/user/ && \
    systemctl --user daemon-reload && systemctl --user start velocita-tape-watchdog"
```

Never copy large files to a host while the tape runs without a rate limit: `scp -l 4000` (500 KB/s).

## 4. Watch

First, the network budget (D-42). The tape needs tens of KB/s per host; if any host sends more than
200 KB/s over `tailscale0` for a minute, stop the voters and recorders and investigate:

```sh
ssh HOST 'r() { awk "/tailscale0/ {gsub(\":\",\" \"); print \$10}" /proc/net/dev; }; a=$(r); sleep 10; b=$(r); echo $(( (b - a) / 10240 )) KB/s'
```

```sh
ssh HOST 'cat ~/velocita-tape/data/voter/state.json'
curl -s http://100.85.36.22:45301/metrics | grep '^velocita_tape_'
```

Add `deploy/tape/templates/prometheus-tape.yml` to the Prometheus scrape configs on NUC1. Healthy: one leader, all
three `commit_index` within a few entries, 12 books `valid`, update verdicts mostly 3/3, divergent 0, a signed head
every 60 seconds.

## 5. Unplug drill

```sh
ssh velocitas@100.84.151.116 'systemctl --user stop velocita-tape-recorder velocita-tape-voter'
sleep 60
ssh velocitas@100.84.151.116 'systemctl --user start velocita-tape-voter && sleep 5 && systemctl --user start velocita-tape-recorder'
```

Expect: the other two keep committing, verdicts close 2/3 at N ticks, heads keep getting two signatures, the Pi
catches up and its root matches.

## 6. Stop and verify offline (72 hours at most, D-34)

```sh
for h in velocitas@100.72.127.102 velocitas@100.84.151.116 velocitas@100.85.36.22; do
  ssh "$h" 'systemctl --user stop velocita-tape-recorder velocita-tape-exporter velocita-tape-voter'; done
mkdir -p ~/.cache/velocita-tape-runs/$C && cd ~/.cache/velocita-tape-runs/$C
for n in 1 2 3; do case $n in 1) h=velocitas@100.72.127.102;; 2) h=velocitas@100.84.151.116;; 3) h=velocitas@100.85.36.22;; esac
  mkdir -p voter-$n && scp -r "$h":velocita-tape/data/voter/wal.log "$h":velocita-tape/data/voter/heads voter-$n/; done
V=~/.cache/velocita-build/tape-dev/velocita-tape-verify
I=$(cat ~/.cache/velocita-tape-deploy/$C/INCARNATION); K=~/.config/velocita-tape/public
for n in 1 2 3; do $V --wal voter-$n/wal.log --heads voter-$n/heads --incarnation $I --head-every 60 \
    --key $K/head-1.pub.pem --key $K/head-2.pub.pem --key $K/head-3.pub.pem; done
$V --wal voter-1/wal.log --compare-wal voter-2/wal.log --incarnation $I \
    --key $K/head-1.pub.pem --key $K/head-2.pub.pem --key $K/head-3.pub.pem
```

The verifier runs on the Mac from the local build. Recorded data
stays private: it is never committed and never redistributed.

## Upgrading a running tape

Load the new image on every host and tag it `:current`, then restart one voter at a time, followers before
the leader, waiting until each restarted voter's `applied_index` has caught up with the others before the
next. Two voters always remain, so the tape keeps committing; recorders resend anything unacknowledged.
Restart recorders and exporters only if their code changed. The incarnation, keys and data stay as they are.

```sh
ssh HOST 'systemctl --user restart velocita-tape-voter'
ssh HOST 'python3 -c "import json;s=json.load(open(\"velocita-tape/data/voter/state.json\"));print(s[\"leader\"],s[\"applied_index\"])"'
```
