# x86-64 NUC operator runbook

Updated: 2026-09-11

Companion to `PI_OPERATOR_RUNBOOK.md`. Written from an actual bring-up rather
than from expectation: every prerequisite below was discovered by a gate failing,
and the order is the order that avoids locking yourself out.

The NUC is the x86-64 half of the ADR 0002 two-machine topology. Its gate is a
LAYOUT AND CAPACITY gate more than a functional one - the question it answers is
whether `TradingState` still fits 81,920 bytes under x86-64 padding and alignment,
because the state ceiling is what makes the sixteen-conflict saturation bound of
architecture §10.5.3 immovable.

## What the gate needs, and what it does not

Needs `podman`, `git`, `sed`, `uname`. That is all.

It does NOT need a host C++ toolchain or CMake. Everything builds inside the
container, and the gate refuses to run if it finds a host toolchain but no podman,
because building outside the container would measure a different thing.

## 1. Network before headless, always

The single rule that prevents a lockout: a machine you will only reach remotely
must prove its network comes up WITHOUT a graphical login before you remove the
graphical login.

On Ubuntu Desktop a wifi connection created through the GNOME UI is often
user-scoped, with its secret in the GNOME keyring, which only unlocks at graphical
login. Boot to `multi-user.target` with such a profile and the machine has no
network and no way back except a monitor.

```sh
nmcli connection show --active
sudo grep -H -E 'permissions|^password|password-flags|psk|eap=' \
  /etc/NetworkManager/system-connections/<profile>.nmconnection
```

Required:
- `permissions=` empty or absent - absent means all users, which is fine;
- the secret present IN THE FILE (`psk=` for WPA2-PSK, `password=` for 802.1X);
- NO `password-flags=1` or `psk-flags=1` - flag 1 is agent-owned, i.e. keyring
  only, i.e. dead headless.

For enterprise 802.1X (campus `eduroam` / `nyu` style):

```sh
sudo nmcli connection modify "<profile>" \
  connection.permissions "" \
  802-1x.password-flags 0 \
  802-1x.password 'THE_PASSWORD'
sudo nmcli -s -g 802-1x.password connection show "<profile>"   # must echo back
```

That last command is the definitive check. A `systemctl restart NetworkManager`
test is NOT conclusive while a desktop session is alive, because the still-running
secret agent will happily supply a keyring-owned credential that would be
unavailable at boot.

Make the choice deterministic if several profiles autoconnect - wired highest, so
a cable always wins:

```sh
sudo nmcli connection modify <wired>   connection.autoconnect-priority 200
sudo nmcli connection modify <main-ap> connection.autoconnect-priority 100
sudo nmcli connection modify <backup>  connection.autoconnect-priority 50
```

## 2. Remote access

```sh
sudo apt install -y openssh-server podman git
sudo systemctl enable --now ssh

curl -fsSL https://tailscale.com/install.sh | sudo sh
sudo tailscale up --operator="$USER"
tailscale ip -4
```

`tailscale up` prints a login URL - open it on another machine; the NUC needs no
browser.

Then in the admin console, **disable key expiry for this node**. Tailscale node
keys expire by default around 180 days, and when that happens a headless machine
silently drops off the tailnet and wants a browser login it cannot give you. The
same applies to any campus password the wifi profile depends on: both are time
bombs on a machine with no monitor, and both argue for a wired connection where
one is available.

## 3. cgroup delegation for rootless podman

Ubuntu does not delegate the `cpu` controller to user sessions, and the gate
constrains CPU deliberately - the limit is part of the constrained-environment
evidence, so the fix belongs on the host rather than in a looser gate.

```sh
cat /sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/cgroup.controllers
# before: memory pids

sudo mkdir -p /etc/systemd/system/user@.service.d
sudo tee /etc/systemd/system/user@.service.d/delegate.conf >/dev/null <<'EOF'
[Service]
Delegate=cpu cpuset io memory pids
EOF
sudo systemctl daemon-reload
sudo reboot

# after: cpuset cpu io memory pids
```

Delegation applies only to NEW user sessions and lingering ones are easy to miss,
so reboot rather than trying to log out cleanly. Without this the gate fails with
`OCI runtime error: the requested cgroup controller 'cpu' is not available`.

## 4. Headless

```sh
sudo systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
sudo systemctl set-default multi-user.target
sudo reboot
```

The suspend masking is not optional on a desktop install: it will suspend on idle
and vanish from the network, which looks exactly like a dead machine.

Reconnect over SSH afterwards and confirm `systemctl get-default`, `nmcli device
status`, and `tailscale status`. That reboot IS the test - passing it is what
makes the machine genuinely headless rather than nominally so.

In BIOS, when a monitor is next attached: set **restore on AC power loss**, and
disable deep S5 sleep if present. Without it the NUC sits in standby after an
outage.

## 5. Accounts

If a second person will run services on the box, give yourself a separate account
first and verify it before changing anything:

```sh
sudo adduser <you>
sudo usermod -aG sudo <you>
sudo chmod 750 /home/<you>
sudo sed -i 's/^DIR_MODE=.*/DIR_MODE=0750/' /etc/adduser.conf
```

Be clear-eyed about what this buys: if the other account keeps `sudo`, nothing in
your home is protected from it. Separation is a security boundary only if the
service account is unprivileged.

Podman is rootless and per-user, so container storage lives in
`~/.local/share/containers` and each account rebuilds the base image from scratch.

## 6. Running the gate

```sh
# from the development host
nuc_sync_dir=$(mktemp -d /tmp/velocita-nuc-sync.XXXXXX)
git clone --no-local . "$nuc_sync_dir"
git -C "$nuc_sync_dir" checkout --detach <accepted-commit>
git -C "$nuc_sync_dir" status --short          # MUST be empty
rsync -av --delete --exclude 'build/' --exclude 'build-*/' \
  "$nuc_sync_dir/" <you>@<nuc>:~/aeron-raft-pi-phase5-nuc/
```

```sh
# on the NUC: CHECK BEFORE YOU RUN, against a value from outside this tree
cd ~/aeron-raft-pi-phase5-nuc
git log --oneline -1
{ sha256sum CMakeLists.txt;
  find include/velocita/v2 src tests -maxdepth 1 -type f \
    \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum; } | sha256sum
```

Compare that against the root written in the handoff, the tag message, or the
acceptance record - NOT against the pin inside `deploy/`. This step is not
ceremony, and skipping it has already produced a PASS on the wrong candidate.

WHY THE GATE CANNOT DO THIS FOR YOU. `VELOCITA_EXPECTED_SOURCE_ROOT` lives in
`deploy/run-v2-phase5-nuc-gate.sh`, which travels INSIDE the tree. A stale tree
therefore carries its own stale pin, the two agree, and the gate passes - because
it was asked whether the tree matches its pin, and it does. A PASS proves THIS
TREE MATCHES ITS OWN PIN; it never proves THIS IS THE CANDIDATE YOU MEANT. The
root check catches tampering and transfer corruption, which break that internal
agreement. It cannot catch the wrong tree, which preserves it.

This bites hardest when the machine has more than one account. Each has its own
`~/aeron-raft-pi-phase5-nuc` and its own rootless podman storage, so a tree
synced for one user is invisible to the other and can sit at an old commit
indefinitely.

```sh
# on the NUC, no sudo - rootless podman
BUILD_JOBS=4 ./deploy/run-v2-phase5-nuc-gate.sh --yes
```

No power preflight: a NUC has no firmware `throttled` interface, and the gate
disables that check rather than fabricating a value.

Evidence lands in `~/.local/share/velocita/v2-phase5-nuc-gates/<timestamp>/`.

## Reading the evidence

```sh
d=$(ls -1dt ~/.local/share/velocita/v2-phase5-nuc-gates/*/ | head -1)
cat "$d/summary.tsv"; cat "$d/source-root.sha256"
grep -hE "tests passed" "$d"/{release,instrumented,sanitized}-tests.txt
grep -E "trading_state_size|state_snapshot_max|ubsan|machine=" "$d/metadata.txt"
```

Check three things, not one:

1. the gate-computed root in `source-root.sha256` equals the value you hold
   outside the tree;
2. the suite counts are 29 / 26 / 26; and
3. `ubsan_compile_recovery` and `ubsan_negative_probe_exit_status` are PRESENT,
   with a nonzero probe status.

The third is the sharpest staleness detector available, and the easiest to skim
past, because a stale gate script does not print those keys WRONG - it does not
print them at all. Absence in a grep whose other lines look correct reads as
nothing rather than as a finding.

## Recomputing the source root by hand

```sh
{ sha256sum CMakeLists.txt;
  find include/velocita/v2 src tests -maxdepth 1 -type f \
    \( -name 'v2_*' -o -name 'v2*.cpp' -o -name '*.hpp' \) -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum; } | sha256sum
```

`LC_ALL=C` is required, not optional: manifest ORDER decides the root and
COLLATION decides manifest order, so a UTF-8 locale produces a different value for
identical content. On macOS substitute `shasum -a 256`.

If the gate reports a root mismatch, the tree moved. Re-pin deliberately; never
edit the expected root to make a run pass.

## What this target found that ARM64 could not

Recorded because it is the argument for having a second architecture at all.

- The container image was pinned `FROM --platform=linux/arm64`, so an x86-64 host
  pulled an ARM64 base and ran it emulated - measuring the wrong architecture and
  then failing the gate's own dpkg-architecture check. The Phase 3D NUC gate had
  existed since that line was written and recorded no evidence anywhere.
- GCC enables LeakSanitizer by default on x86-64 and not on aarch64, and LSan
  needs `ptrace` which `--cap-drop ALL` denies. Every suite died before executing
  a test, while the identical ARM64 run passed because LSan was never active - so
  "Debug ASan+UBSan" had always meant different things on the two targets.
- Rootless podman cgroup delegation, above.

None was a defect in the code. All three were defects in how the code was being
verified, and each was invisible from a single architecture.
