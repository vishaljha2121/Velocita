# Alert delivery runbook

How a firing alert reaches your iPhone, how to install it, and what to do when it doesn't.
Everything here is open source and self-hosted on nuc1.

## How it works

```
Prometheus (127.0.0.1:9090)      evaluates the rules in deploy/v2/observability/rules/
      │  firing alerts
      ▼
Alertmanager (127.0.0.1:9093)    groups, repeats, silences
      │  webhook + bearer token
      ▼
ntfy (nuc1 Tailscale IP :2586)   stores the message, renders it (templates/velocita.yml)
      │                    └──►  ntfy.sh: "wake up" only — a message ID and a topic hash
      ▼                                     (Apple delivers the wake-up to the phone)
iPhone ntfy app  ◄── fetches the alert text from nuc1 over Tailscale
```

- **The alert text never leaves your tailnet.** iOS cannot keep a background connection, so an
  instant notification needs one small relay through ntfy.sh. That relay carries a message ID and
  the SHA-256 of the topic URL. If the phone can't reach nuc1 (Tailscale off), the notification
  shows only "New message".
- **Severity → behaviour:**

  | Severity | Push | First sent after | Repeats every |
  |---|---|---|---|
  | `critical` | urgent (priority 5) | 10 s | 1 h |
  | `warning` | normal (priority 3) | 30 s | 4 h |
  | resolved | quiet (priority 2) | – | – |

- **Grouping.** Alerts with the same name become one notification ("VoterDown (+1 more)").
- **Inhibition.**
  - While a host's voter is down, that host's warnings are held back.
  - While quorum is lost, all warnings are held back.

**Files**

| File | What |
|---|---|
| `deploy/v2/alerting/alertmanager.yml` | routing, grouping, the webhook |
| `deploy/v2/alerting/ntfy/server.yml` | ntfy server settings |
| `deploy/v2/alerting/ntfy/templates/velocita.yml` | what the notification says |
| `deploy/v2/systemd/velocita-ntfy.service`, `velocita-v2-alertmanager.service` | the two units |
| `~/.config/velocita/ntfy-publish-token` (nuc1, mode 600, **not in the repo**) | Alertmanager's token |
| podman volume `velocita-ntfy-data` | ntfy's accounts, tokens and 72 h message cache |

The publish token is deliberately a file on disk, not in the tmpfs with the cluster keys (ADR
0010). Alerts must survive a reboot: `VelocitaHostRebooted` is one of the things they report. The
token can only publish to `velocita-*` topics on a server that is reachable from the tailnet only.

## First install (nuc1)

Ship the source as usual (`CLEAN_RESET_RUNBOOK.md` step 1). Then, on nuc1:

1. **Host settings.** Add the base URL to `~/.config/velocita/host.env`:

       echo 'VELOCITA_NTFY_BASE_URL=http://100.72.127.102:2586' >> ~/.config/velocita/host.env

2. **Start ntfy.**

       cp ~/src/aeron-raft-pi/deploy/v2/systemd/velocita-ntfy.service ~/.config/systemd/user/
       cp ~/src/aeron-raft-pi/deploy/v2/systemd/velocita-v2-alertmanager.service ~/.config/systemd/user/
       systemctl --user daemon-reload
       systemctl --user enable --now velocita-ntfy

3. **Create the two accounts.** `owner` is you (read); `alertmanager` is the publisher (write).
   The first command asks for a password: choose one; you type it once into the phone.

       podman exec -it velocita-ntfy ntfy user add owner
       podman exec -e NTFY_PASSWORD="$(head -c 32 /dev/urandom | base64)" velocita-ntfy ntfy user add alertmanager
       podman exec velocita-ntfy ntfy access owner 'velocita-*' ro
       podman exec velocita-ntfy ntfy access alertmanager 'velocita-*' wo

4. **Create the publisher's token and store it.**

       umask 077
       podman exec velocita-ntfy ntfy token add alertmanager | grep -o 'tk_[a-z0-9]*' > ~/.config/velocita/ntfy-publish-token
       test "$(wc -c < ~/.config/velocita/ntfy-publish-token)" -eq 33 && echo token stored

5. **Start Alertmanager and reload Prometheus.**

       systemctl --user enable --now velocita-v2-alertmanager
       systemctl --user restart velocita-v2-prometheus

## Phone setup (once)

1. Install **ntfy** from the App Store. Tailscale must be on.
2. In the app: Settings → **Default server** → `http://100.72.127.102:2586`.
3. Settings → **Users** → Add user: the same server URL, user `owner`, your password.
4. **+** → Subscribe to topic → `velocita-alerts`.
5. iOS Settings → Notifications → ntfy: allow notifications, and allow **Time Sensitive**
   notifications so that critical alerts get through a Focus.

If the app refuses a plain `http://` server, see "HTTPS through Tailscale" below.

## Test it

Send a test alert (on nuc1). It reaches the phone within about 30 seconds, and resolves itself
after 2 minutes:

    curl -s -XPOST http://127.0.0.1:9093/api/v2/alerts -H 'Content-Type: application/json' -d '[{
      "labels": {"alertname": "VelocitaTestAlert", "severity": "warning", "host": "nuc1"},
      "annotations": {"summary": "Test alert from the runbook", "description": "Nothing is wrong."},
      "endsAt": "'"$(date -u -d '+2 minutes' +%Y-%m-%dT%H:%M:%SZ)"'"}]'

Check each hop if nothing arrives:

| Check | Command (nuc1) | Healthy |
|---|---|---|
| Prometheus sees Alertmanager | `curl -s 127.0.0.1:9090/api/v1/alertmanagers` | one `activeAlertmanagers` entry |
| Alertmanager holds the alert | `curl -s 127.0.0.1:9093/api/v2/alerts` | the alert is listed |
| Alertmanager delivered it | `journalctl --user -u velocita-v2-alertmanager -n 20` | no `Notify for alerts failed` |
| ntfy stored it | `curl -s -u owner "http://100.72.127.102:2586/velocita-alerts/json?poll=1"` | the message as JSON |
| Phone can reach nuc1 | open `http://100.72.127.102:2586` in Safari on the phone | the ntfy web page |

A `401` or `403` in the Alertmanager journal means the token file is wrong or the access entry is
missing: repeat steps 3–4. The alert `VelocitaAlertDeliveryFailing` reports the same thing in
Grafana.

## Everyday use

**Silence an alert** you already know about (for example, the Pi is unplugged for 2 hours):

    podman exec velocita-v2-alertmanager amtool --alertmanager.url=http://127.0.0.1:9093 \
        silence add host=pi --duration=2h --comment="Pi power supply swap"
    podman exec velocita-v2-alertmanager amtool --alertmanager.url=http://127.0.0.1:9093 silence query

**See what is firing now:** `curl -s 127.0.0.1:9093/api/v2/alerts | python3 -m json.tool`, or the
Alerts view in Grafana.

**Change what a notification says:** edit `deploy/v2/alerting/ntfy/templates/velocita.yml`, ship
it, and `systemctl --user restart velocita-ntfy`.

**Change the routing or repeat times:** edit `deploy/v2/alerting/alertmanager.yml`, then validate
and restart:

    podman run --rm -v ~/src/aeron-raft-pi/deploy/v2/alerting/alertmanager.yml:/a.yml:ro \
        --entrypoint amtool docker.io/prom/alertmanager:v0.34.1 check-config /a.yml
    systemctl --user restart velocita-v2-alertmanager

**Rotate the publish token:** `podman exec velocita-ntfy ntfy token list alertmanager`, remove the
old one with `ntfy token remove alertmanager TOKEN`, then repeat step 4 and restart Alertmanager.

## HTTPS through Tailscale (only if the app needs it)

Tailscale can put a real certificate in front of ntfy. This needs HTTPS certificates enabled for
the tailnet in the Tailscale admin console, and sudo on nuc1:

    sudo tailscale serve --bg --https=8443 http://100.72.127.102:2586
    tailscale serve status        # prints https://nuc1.<tailnet>.ts.net:8443

Then set `VELOCITA_NTFY_BASE_URL` in `host.env` to that URL, restart `velocita-ntfy`, and use the
same URL in the phone app. The URL in the app and the base URL **must be identical**: the iOS
wake-up is keyed on it.

## The outside heartbeat

Prometheus, Alertmanager and ntfy all run on nuc1. If nuc1 is off, or the house loses power or
internet, none of them can tell you. The heartbeat covers that: nuc1 pings a monitor outside the
house every minute, and the monitor alerts you when the pings **stop**.

**How it works**

- `velocita-heartbeat.timer` runs `deploy/v2/heartbeat.sh` every minute on nuc1.
- The script checks that Prometheus, Alertmanager and ntfy answer locally.
  - All three answer: it requests the ping URL.
  - Any one does not: it requests the ping URL with `/fail`, which makes the monitor alert at once.
- The ping URL is read from `~/.config/velocita/heartbeat-url` (mode 600, not in the repo). It
  never appears in the journal or in a metric.
- The unit does nothing until that file exists.
- It writes `velocita_heartbeat_*` metrics. `VelocitaHeartbeatNotSent` fires when no ping has got
  out for 5 minutes; you will see it once the connection is back.

**The monitor.** Any service that accepts a plain HTTPS GET as a ping, and treats `<url>/fail` as
an explicit failure, works. The recommended one is Healthchecks: it is open source (BSD), and its
hosted service at healthchecks.io has a free tier. It must run outside the house; self-hosting it
on nuc1 would defeat the purpose.

**Set it up (once)**

1. Create an account at healthchecks.io and add a check named `velocita-nuc1`: period **1 minute**,
   grace **3 minutes**.
2. Choose how it reaches you. It cannot use the ntfy server on nuc1: that server is inside the
   tailnet, and it is down in exactly the cases this covers. Use both of:
   - **Email**, which is on by default.
   - **ntfy.sh** (Integrations → ntfy): the public server, with a long random topic name. Then in
     the phone's ntfy app, subscribe to that topic on `https://ntfy.sh`. Only the words "nuc1 is
     down" travel through it.
3. Copy the check's ping URL and store it on nuc1:

       umask 077
       cat > ~/.config/velocita/heartbeat-url      # paste the URL, Enter, then Ctrl-D

4. Enable the timer:

       cp ~/src/aeron-raft-pi/deploy/v2/systemd/velocita-heartbeat.{service,timer} ~/.config/systemd/user/
       systemctl --user daemon-reload
       systemctl --user enable --now velocita-heartbeat.timer

**Test it**

    systemctl --user start velocita-heartbeat.service
    grep -v '^#' ~/velocita-v2/textfile/velocita-heartbeat.prom     # alert_path_ok 1, sent 1

The check turns green on the monitor within a minute. To see the alarm, stop the timer
(`systemctl --user stop velocita-heartbeat.timer`), wait 4 minutes for the notification, and
start it again.

**What it still does not tell you:** *why* nuc1 is unreachable. A power cut, a dead router and a
hung machine all look the same from outside.
