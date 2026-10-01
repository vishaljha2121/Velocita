# Clean reset runbook: a fresh cluster with paper trading

This is the exact sequence used for the 2026-10-01 reset (tag `v2-cp078-reset2`, incarnation
`f63eafc2…`). It produced a healthy cluster at term 1 and a first churn round trip within
minutes.

A clean reset **wipes the cluster's history**: positions, ledgers and decisions all start
fresh. The old state is moved into `~/velocita-v2/retired/`, never deleted.

Use it when:
- the kernel bytes or the peer wire change (a new schema or peer version), or
- the paper data is corrupt.

Hosts and voter numbers: **nuc1 = voter 1** (it also runs the edge, the gateway, Prometheus
and Grafana), **vishalkapi (Pi) = voter 2**, **nuc2 = voter 3**. The `velocitas` user's UID
is 1003 on the NUCs and 1004 on the Pi.

## Pitfalls we already hit (read first)

- **Never use rsync from the Mac to these hosts.** macOS `openrsync` exits 0 and writes
  nothing (finding 45). Use the `git archive` + `tar` method in step 1.
- **Disable macOS metadata when making the tar:** use `COPYFILE_DISABLE=1 tar --no-xattrs`.
  Otherwise `._*` files break the tests that glob `*.json` and `*.service`.
- **The Mac runs zsh, which doesn't split `$VAR` into words.** Use the arrays shown below.
  `~` inside quotes doesn't expand; use `$HOME`.
- **Delete a build log before rebuilding.** Killing `podman build` leaves the container's
  test step running, and it keeps writing into the old log.
- **After any host reboot, push the keys again** (step 0). `/run/user` is wiped on boot, and
  the voter exits 64 with `secret refused`.
- **Pre-vote (CP-076) changed the peer wire.** Voters on different builds refuse each other.
  Upgrade all three together; never do a rolling restart across a wire change.

## 0. Prerequisites

Keys, on the Mac in `~/aeron-raft-pi`. Push them only if they are missing on a host; check
with `ssh velocitas@HOST 'ls /run/user/$(id -u)/velocita-secrets'`:

    for hu in nuc1:1003 vishalkapi:1004 nuc2:1003; do h=${hu%%:*}; u=${hu##*:}
      for n in link-1-2 link-1-3 link-2-3 caller-9 caller-10 caller-11 caller-12 caller-13; do
        tools/velocita-secrets push $n velocitas@$h /run/user/$u/velocita-secrets/$n.key
      done
    done

Each host needs only its own link keys: nuc1 needs 1-2 and 1-3, the Pi 1-2 and 2-3, nuc2 1-3
and 2-3. A push the host does not use is harmless.

Disk space: the Pi needs a few GB free, so check `df -h /`. Old images can be removed with
`podman rmi <tag>`; always keep `m3` and one rollback tag.

## 1. Ship the exact commit to all three hosts (on the Mac)

    cd ~/aeron-raft-pi
    T=<tag-or-commit>                     # e.g. v2-cp078-reset2
    E=~/.cache/velocita-work/export-$T
    rm -rf $E && mkdir -p $E
    git archive $T | tar -x -C $E
    git rev-parse $T^{commit} > $E/SOURCE_COMMIT
    (cd $E && find . -type f | LC_ALL=C sort | xargs shasum -a 256 | shasum -a 256 | cut -c1-16)   # note this hash

    for h in nuc1 nuc2 vishalkapi; do
      COPYFILE_DISABLE=1 tar --no-xattrs -C $E -cf - . | ssh velocitas@$h '
        set -e; cd ~/src; rm -rf aeron-raft-pi.new aeron-raft-pi.old; mkdir aeron-raft-pi.new
        tar -xf - -C aeron-raft-pi.new 2>/dev/null
        mv aeron-raft-pi aeron-raft-pi.old; mv aeron-raft-pi.new aeron-raft-pi; cd aeron-raft-pi
        echo "$(hostname) $(find . -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -c1-16) $(cut -c1-7 SOURCE_COMMIT)"'
    done

**Check:** all three hosts print the same hash as the Mac, and the same commit.

## 2. Build the images on all three hosts (in parallel)

Use `J=3` on the Pi and `J=4` on the NUCs. nuc1 also builds the shadow (edge) image.

    for h in nuc1 nuc2 vishalkapi; do
      J=4; [ $h = vishalkapi ] && J=3
      CC="-v \$HOME/.cache/velocita-ccache:/ccache"      # compiler cache: only changed files recompile
      B="podman build $CC --build-arg BUILD_JOBS=$J -f deploy/v2/Containerfile -t localhost/velocita-v2:NEW ."
      [ $h = nuc1 ] && B="$B && podman build $CC --build-arg BUILD_JOBS=$J --target shadow -f deploy/v2/Containerfile -t localhost/velocita-v2-shadow:NEW ."
      ssh -f -n velocitas@$h "mkdir -p ~/.cache/velocita-ccache; cd ~/src/aeron-raft-pi && rm -f ~/build.log ~/build.rc && setsid nohup sh -c '($B) > ~/build.log 2>&1; echo \$? > ~/build.rc' </dev/null >/dev/null 2>&1 &"
    done

Replace `NEW` with a tag such as `m3-p4`.

Follow a build with `ssh velocitas@HOST 'tail -f ~/build.log'`.

**Done:** `ssh velocitas@HOST 'cat ~/build.rc; grep "tests passed" ~/build.log | tail -1'`
must show `0` and `100% tests passed`. Every build runs the full test suite in the container,
as root.

## 3. Stop everything and retire the old cluster

Set `OLD` to the first 8 hex digits of the current incarnation:
`grep incarnation ~/velocita-v2/config/node-1.conf` on nuc1.

    OLD=xxxxxxxx

    # nuc1 services
    ssh velocitas@nuc1 "systemctl --user stop velocita-v2-shadow-edge velocita-v2-gateway
      R=~/velocita-v2/retired; S=~/velocita-v2/shadow
      cp \$S/shadow.env \$R/shadow-$OLD.env
      mv \$S/state \$R/shadow-state-$OLD; mkdir -p \$S/state
      mv ~/velocita-v2/gateway/state \$R/gateway-state-$OLD; mkdir -p ~/velocita-v2/gateway/state"

    # every voter
    for hn in nuc1:1 vishalkapi:2 nuc2:3; do h=${hn%%:*}; n=${hn##*:}
      ssh velocitas@$h "systemctl --user stop velocita-v2-node@$n
        mv ~/velocita-v2/data/node-$n ~/velocita-v2/retired/node-$n-$OLD
        cp ~/velocita-v2/config/node-$n.conf ~/velocita-v2/retired/node-$n-$OLD.conf"
    done

## 4. New incarnation, configs, images and units

    INC=$(openssl rand -hex 16); echo $INC          # keep it: steps 5-9 use it
    ROOT=167b67a7954d3d5c895070062e5917cb7820fb1f1a0d9c7e754b54f8871d7526   # catalog root (unchanged unless the catalog changes)

    for hn in nuc1:1 vishalkapi:2 nuc2:3; do h=${hn%%:*}; n=${hn##*:}
      ssh velocitas@$h "set -e
        sed -e 's/@INCARNATION@/$INC/g' -e 's/@CATALOG_ROOT@/$ROOT/g' \
          ~/src/aeron-raft-pi/deploy/v2/three-host/node-$n.conf.template > ~/velocita-v2/config/node-$n.conf.new
        grep -qE '@[A-Z_]+@' ~/velocita-v2/config/node-$n.conf.new && { echo UNFILLED; exit 1; }
        mv ~/velocita-v2/config/node-$n.conf.new ~/velocita-v2/config/node-$n.conf
        podman tag localhost/velocita-v2:m3 localhost/velocita-v2:m3-pre-NEW
        podman tag localhost/velocita-v2:NEW localhost/velocita-v2:m3"
    done

    # nuc1 only: the edge image and the unit files
    ssh velocitas@nuc1 'podman tag localhost/velocita-v2-shadow:m3 localhost/velocita-v2-shadow:m3-pre-NEW
      podman tag localhost/velocita-v2-shadow:NEW localhost/velocita-v2-shadow:m3
      for f in velocita-v2-gateway.service velocita-v2-shadow-edge.service velocita-v2-shadow-setup.service; do
        cp ~/src/aeron-raft-pi/deploy/v2/systemd/$f ~/.config/systemd/user/$f; done
      systemctl --user daemon-reload; systemctl --user reset-failed velocita-v2-shadow-edge || true'

Check whether any other installed unit differs from the repo:

    ssh velocitas@HOST 'cd ~/.config/systemd/user; for f in velocita*; do s=~/src/aeron-raft-pi/deploy/v2/systemd/$f; [ -e $s ] && { cmp -s $f $s || echo DIFFERS $f; }; done'

## 5. Start the voters, then switch them to recover

    for hn in nuc1:1 vishalkapi:2 nuc2:3; do h=${hn%%:*}; n=${hn##*:}
      ssh velocitas@$h "systemctl --user start velocita-v2-node@$n"; done

    # status, on any host (N = its voter number)
    ssh velocitas@nuc1 'podman exec velocita-v2-node-1 sh -c "velocita-v2-status \$(find /var/lib/velocita-v2 /dev/shm -maxdepth 3 -name \"*health*\" -type f | head -1)"'

**Check:** all three show `ready=1 failed=0`, the same `term` and `leader`, and `commit=0`.

The Pi can take about two minutes to start its container.

Then switch every voter to recover, so a restart never re-bootstraps:

    for hn in nuc1:1 vishalkapi:2 nuc2:3; do h=${hn%%:*}; n=${hn##*:}
      ssh velocitas@$h "sed -i 's/^startup = bootstrap/startup = recover/' ~/velocita-v2/config/node-$n.conf"; done

## 6. The operator ceremony (on the Mac, zsh)

    cd ~/aeron-raft-pi
    C=~/.cache/velocita-build/release/velocita-v2-client      # a Mac build of the SAME commit
    A=(--caller-key-id 9 --caller-id 7 --key-stdin --incarnation $INC
       --session-file $HOME/.velocita/client.session --response-endpoint 100.91.80.64:42400
       --node 1=100.72.127.102:42301 --node 2=100.84.151.116:42302 --node 3=100.85.36.22:42303
       --aeron-dir /tmp/velocita-client)
    tools/velocita-secrets client-key caller-9 | $C $A --sequence 1 kill-epoch 1
    tools/velocita-secrets client-key caller-9 | $C $A --sequence 2 executor-generation 1
    tools/velocita-secrets client-key caller-9 | $C $A --sequence 3 install-catalog $ROOT 1 1

**Check:** each prints `COMMITTED … code_name=applied`.

If the Mac's Tailscale IP changed (`tailscale ip -4`), fix `--response-endpoint`.

## 7. Choose the strategy and run setup (nuc1)

Edit `~/velocita-v2/shadow/shadow.env`:

    VELOCITA_INCARNATION=<INC>
    VELOCITA_MODE_ARGS=                          # shadow until the gateway is armed
    VELOCITA_GATEWAY_RISK_ARGS=                  # unarmed for now
    VELOCITA_STRATEGY_ARGS=--strategy churn --period-s 60 --order-quantity-lots 12 --weekly-loss-cap-usd 12 --expected-week-volume-usd 1011,1030 --expected-week-loss-usd 11.81,12.00

Other strategies are in `docs/PAPER_TRADING_RUNBOOK.md` §7.3. Leave `VELOCITA_STRATEGY_ARGS`
empty for the trend strategy.

    systemctl --user start velocita-v2-shadow-setup
    journalctl --user -u velocita-v2-shadow-setup | grep SETUP     # ... strategy_mode=1
    grep -E '^(# generated|policy\.|features|rule|order\.quantity)' ~/velocita-v2/shadow/state/trend.program

## 8. Risk limits (on the Mac; same `C` and `A` as step 6)

    tools/velocita-secrets client-key caller-9 | $C $A --sequence 4 --dry-run risk-limits \
      account=1 instrument=1 risk_policy_version=1 expected_risk_version=0 kill_epoch=1 executor_generation=1 \
      max_order_lots=12 max_reserved_lots=12 max_order_notional_micros=25000000 max_reserved_exposure_micros=25000000 \
      initial_margin_ppm=1000000 maximum_fee_ppm=15000 max_position_lots=12 allow_short=0 reason_code=<YYYYMMDD>

Read the summary. Then run the same line again without `--dry-run`.

**Check:** `COMMITTED … risk_version=1`.

A `BrokenPipeError` after a dry run is harmless: the dry run never reads the key.

## 9. Paper venue state file, then arm the gateway (nuc1)

One-shot init: $25 and 0 BTC, bound to this cluster. It refuses if the cluster holds a
position.

    INC=<INC>
    podman run --rm --name velocita-v2-gateway-init --network host --userns=keep-id --read-only \
      --security-opt no-new-privileges --cap-drop all --pids-limit 64 --memory 512m --shm-size 256m \
      -e AERON_TERM_BUFFER_LENGTH=1048576 \
      -v ~/velocita-v2/config/catalog.bin:/etc/velocita/catalog.bin:ro -v ~/velocita-v2/gateway/state:/state \
      -v ~/velocita-v2/textfile:/textfile \
      -v /run/user/1003/velocita-secrets/caller-13.key:/run/velocita-secrets/caller-13.key:ro \
      --entrypoint /usr/local/bin/velocita-v2-gateway localhost/velocita-v2:m3 \
      --venue paper --catalog /etc/velocita/catalog.bin --caller-key-id 13 --caller-id 22 \
      --key-file /run/velocita-secrets/caller-13.key --incarnation $INC \
      --session-file /state/gateway.session --state-file /state/gateway.state --paper-state-file /state/paper.state \
      --response-endpoint 100.72.127.102:42413 \
      --node 1=100.72.127.102:42301 --node 2=100.84.151.116:42302 --node 3=100.85.36.22:42303 \
      --aeron-dir /dev/shm/velocita-gateway-aeron --metrics-textfile /textfile/velocita-gateway.prom \
      --paper-state-init --paper-quote-usd 25

**Check:** `PAPER_STATE created … mode=fresh base_lots=0 quote_micros=25000000`.

Arm and start:

    sed -i 's/^VELOCITA_GATEWAY_RISK_ARGS=.*/VELOCITA_GATEWAY_RISK_ARGS=--freshness-ms 2000 --allow-new-risk/' ~/velocita-v2/shadow/shadow.env
    systemctl --user restart velocita-v2-prometheus velocita-v2-grafana    # load new rules and dashboards
    systemctl --user start velocita-v2-gateway
    journalctl --user -u velocita-v2-gateway -o cat | grep -E '^(ARMED|STOP|RECONCILIATION)'

**Check:** `ARMED new_risk term=… kernel_lots=0 venue_lots=0`.

## 10. Start paper trading (nuc1)

    sed -i 's/^VELOCITA_MODE_ARGS=.*/VELOCITA_MODE_ARGS=--mode paper/' ~/velocita-v2/shadow/shadow.env
    systemctl --user start velocita-v2-shadow-edge       # or restart, if it is running

Verify after a few minutes (churn completes a round trip about every 2 minutes):

    grep -hE '^velocita_(shadow_(ticks|errors)_total|paper_(authorizations|fills)_total|paper_week_(volume|loss)_usd)' ~/velocita-v2/textfile/velocita-shadow.prom
    grep -hE '^velocita_gateway_(new_risk_stopped|fills_total|position_lots|venue_quote_usd|reconciliation_mismatch|unknown_outcomes_total)' ~/velocita-v2/textfile/velocita-gateway.prom

**Expected:**
- `errors_total 0`;
- buy and sell fills both increasing;
- `reconciliation_mismatch 0`;
- `unknown_outcomes_total 0`;
- `new_risk_stopped 0`;
- the cluster term stays flat.

## Rollback

Every step keeps the previous state:
- images `m3-pre-NEW`;
- `~/velocita-v2/retired/node-N-OLD` and `node-N-OLD.conf`;
- `shadow-state-OLD`, `gateway-state-OLD` and `shadow-OLD.env`;
- `~/src/aeron-raft-pi.old`.

To go back:
1. Stop all units.
2. Restore those directories and config files to their original names.
3. Retag `m3-pre-NEW` as `m3`.
4. Restore the old unit files from the old source tree.
5. Start the voters (their configs say `recover`), then the gateway and the edge.

## Halting at any time

From the Mac, `$C $A --sequence N strategy-mode halted 1 1` stops all new risk. On nuc1,
`systemctl --user stop velocita-v2-gateway` stops sending entirely.
