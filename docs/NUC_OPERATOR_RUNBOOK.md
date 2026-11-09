# x86-64 NUC operator runbook

Host preparation for a NUC voter, written from an actual bring-up: every prerequisite below was discovered
by a build or start failing, and the order is the order that avoids locking yourself out.

Releases are built and qualified on the host inside the pinned toolchain image: see
[`TOOLCHAIN.md`](TOOLCHAIN.md) (`deploy/v2/build-image.sh` builds and runs the full CTest suite natively).
Bringing up a cluster on prepared hosts is [`CLEAN_RESET_RUNBOOK.md`](CLEAN_RESET_RUNBOOK.md).

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
