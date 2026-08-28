# Raspberry Pi cluster operator runbook

This is the manual learning path for the three-voter V1 cluster. Run one block
at a time and inspect its output. The deployment scripts are the repeatable
version of these steps, not a substitute for understanding them.

## 0. Know the topology

All three voters are separate Linux processes in separate containers, but they
share one Raspberry Pi:

| Node | Client UDP | Peer UDP | Durable volume |
|---|---:|---:|---|
| 1 | 40111 | 40211 | `velocita-raft-node1-data` |
| 2 | 40112 | 40212 | `velocita-raft-node2-data` |
| 3 | 40113 | 40213 | `velocita-raft-node3-data` |

Ports bind only to `127.0.0.1`. That is the loopback interface: other machines
cannot inject unauthenticated Raft packets. Tailscale/LAN is the management
plane used for SSH; Aeron peer traffic stays inside this Pi for V1.

Three logical voters test quorum behavior and survive one container process
stopping. They do not survive failure of the Pi, SD card, power supply, kernel,
or network interface.

## 1. SSH in and identify the machine

From the Mac:

```bash
ssh bhaikapi@vishalkapi.local
```

If mDNS does not resolve, use the LAN or Tailscale IP:

```bash
ssh bhaikapi@192.168.1.X
```

On the Pi, establish facts before changing anything:

```bash
hostnamectl
cat /etc/os-release
uname -m
uname -r
nproc
free -h
df -hT
ip -brief address
ip route
```

Why:

- `/etc/os-release` decides which package manager/instructions apply.
- `uname -m` must show `aarch64` or `arm64` for the intended 64-bit build.
- `free` shows RAM pressure; `df` shows disk capacity and filesystem type.
- `ip address` shows interface state; `ip route` shows where outbound packets go.

Check temperature without installing anything:

```bash
awk '{printf "%.1f C\n", $1/1000}' /sys/class/thermal/thermal_zone0/temp
```

Around 58 C is healthy under ordinary load. We will later alert on sustained
high temperature and throttling, not on one isolated reading.

## 2. Update the host and install operator tools

These commands assume Ubuntu, which is indicated by the existing cloud-init
`system-boot/network-config` layout. Confirm step 1 before running them.

```bash
sudo apt update
sudo apt full-upgrade
sudo apt install -y podman git ca-certificates curl tcpdump
sudo reboot
```

Why:

- `apt update` refreshes package metadata; it installs nothing.
- `full-upgrade` applies compatible OS/security updates and may replace kernel
  packages, which is why we reboot deliberately.
- Podman runs OCI containers without a permanent root daemon.
- Git/CAs/Curl let the image build fetch pinned source securely.
- `tcpdump` gives packet-level evidence when Aeron networking is suspect.

After the reboot, reconnect and verify instead of assuming:

```bash
uptime
podman --version
podman info --debug
stat -fc %T /sys/fs/cgroup
sysctl user.max_user_namespaces
```

Expected: Podman returns normally, the cgroup filesystem is `cgroup2fs`, and
unprivileged user namespaces are enabled. We run rootless Podman as `bhaikapi`;
do not prefix normal `podman` commands with `sudo`, because root and rootless
Podman have different image, container, and volume stores.

## 3. Copy source from the Mac

Open a second terminal on the Mac (not inside the Pi SSH session):

```bash
cd /Users/vishaljha
rsync -av --delete --exclude build/ --exclude .git/ aeron-raft-pi/ \
  bhaikapi@vishalkapi.local:~/aeron-raft-pi/
```

`rsync` transfers only differences on later runs. `--delete` makes the remote
source tree mirror the Mac source tree; it is scoped to `~/aeron-raft-pi/` and
does not touch the separate Podman WAL volumes. Excluding `.git/` keeps the Mac
as the version-control source of truth and avoids copying repository history to
the deployment host.

Back on the Pi:

```bash
cd ~/aeron-raft-pi
pwd
find . -maxdepth 2 -type f | sort
```

`pwd` prevents the classic production mistake of operating in the wrong
directory. The file listing proves which source is actually on the machine.

## 4. Prove DNS, time, and registry access

Container builds need correct time, DNS, HTTPS, and registry/GitHub access:

```bash
timedatectl
getent hosts ghcr.io
getent hosts github.com
curl -I https://github.com
```

If this layer fails, do not debug CMake. Fix time/DNS/routing first. TLS errors
often come from a wrong system clock; name-resolution errors come from DNS.

## 5. Build the ARM64 image

```bash
cd ~/aeron-raft-pi
time podman build --tag localhost/velocita-raft:v1 -f Containerfile .
```

What happens:

1. Podman downloads the ARM64 Ubuntu base image.
2. The builder installs the compiler, CMake, and headers.
3. CMake fetches the exact pinned Aeron and `willemt/raft` commits.
4. Native C/C++ binaries compile on the Pi.
5. `ctest` runs, including a three-node election/replication test.
6. Only runtime binaries and `libstdc++` enter the final image.

The Containerfile defaults to two parallel compiler jobs. On this 4 GiB Pi,
that reduces peak build memory and the chance that Linux's OOM killer terminates
a compiler. Build speed is secondary to a repeatable build.

Inspect the artifact:

```bash
podman images localhost/velocita-raft
podman image inspect localhost/velocita-raft:v1 \
  --format 'arch={{.Architecture}} os={{.Os}} size={{.Size}}'
```

The architecture must be `arm64`. If the build fails, rerun with evidence:

```bash
podman build --no-cache --log-level=debug \
  --tag localhost/velocita-raft:v1 -f Containerfile . 2>&1 | tee build-debug.log
```

Keep the first real compiler error. Later errors are often consequences.

## 6. Understand and create durable storage

Each voter must have its own WAL. Sharing a volume would make three processes
concurrently mutate one Raft identity and corrupt the safety model.

```bash
for id in 1 2 3; do
  podman volume create "velocita-raft-node${id}-data"
done

podman volume ls
podman volume inspect velocita-raft-node1-data
```

A container is replaceable; a named volume is durable state. Stopping or
replacing a container does not delete its volume.

## 7. Start the nodes manually the first time

The full commands live in `deploy/start-pi-cluster.sh`. Read them first:

```bash
sed -n '1,260p' deploy/start-pi-cluster.sh
```

Then start:

```bash
./deploy/start-pi-cluster.sh
sleep 5
```

Important flags:

- `--network host`: Aeron sees real UDP endpoints without container NAT.
- `--shm-size 192m`: each embedded media driver receives private shared memory.
- `--volume ...:U`: mounts one persistent WAL volume and maps ownership to the
  non-root UID in the image.
- `--replace`: replaces only an old container with the same node name; it does
  not remove the named volume.
- `--members 1,2,3`: gives every process the identical static voter set.
- `--peer-target`: maps a Raft node ID to its Aeron UDP destination.

## 8. Verify Linux, container, transport, and Raft layers

Process/container layer:

```bash
podman ps --filter name=velocita-raft
podman top velocita-raft-1
podman stats --no-stream velocita-raft-1 velocita-raft-2 velocita-raft-3
```

All three must be `Up`. `podman top` proves a native `raft-node` process is the
container PID; `stats` exposes CPU, memory, and I/O pressure.

Socket layer:

```bash
ss -lunp | grep -E '4010[1-3]|4011[1-3]|4021[1-3]'
```

Expect UDP listeners on the three request ports and three peer ports. UDP has no
connection state like TCP, so a listener does not prove messages are flowing.

Raft layer:

```bash
for id in 1 2 3; do
  echo "===== node $id ====="
  podman logs --tail 30 "velocita-raft-$id"
done
```

Healthy invariants:

- exactly one `role=leader` and two `role=follower`;
- all nodes converge on the same `term`;
- `commit <= log/current index`;
- `applied <= commit`;
- after traffic settles, all nodes have equal commit, applied, and counter.

Normal operation suppresses per-heartbeat protocol traces to avoid noisy logs
and unnecessary microSD writes. For a short diagnostic run, add
`--raft-trace 1` to a node command; return it to `0` after collecting evidence.

No leader usually means peer packets are not flowing or fewer than two voters
are alive. Multiple leaders in different terms for a moment can occur during a
partition; multiple stable leaders able to commit would be a serious bug.

## 9. Exercise the service

```bash
./deploy/cluster-client.sh increment 5
./deploy/cluster-client.sh get
```

The helper creates one long-lived response subscription and publications to all
three request ports. It retries one stable request ID until a leader returns
`ok`. Follower `not_leader` responses do not end the attempt. A successful
response is emitted only after at least two voters durably hold the log entry
and it has been applied. If the deadline expires, the client reports the outcome
as unknown; a timeout never proves that a command failed to commit.

The helper prints its request ID. To retry an ambiguous operation without
executing it twice, reuse it with
`VELOCITA_REQUEST_ID=<id> ./deploy/cluster-client.sh ...`.

Recheck convergence:

```bash
for id in 1 2 3; do
  podman logs --tail 8 "velocita-raft-$id"
done
```

## 10. Debug from the bottom upward

Use this order so symptoms in an upper layer do not distract from a broken
lower layer.

### Container will not start

```bash
podman ps -a --filter name=velocita-raft
podman inspect velocita-raft-1 --format \
  'status={{.State.Status}} exit={{.State.ExitCode}} error={{.State.Error}}'
podman logs velocita-raft-1
```

Exit `125` commonly means Podman could not create the container. Another exit
code usually came from `raft-node` itself.

### Address already in use

```bash
ss -lunp | grep -E '40101|40111|40112|40113|40211|40212|40213'
podman ps --format '{{.Names}} {{.Ports}}'
```

Find the owner before killing anything. The previous single-node container can
remain on port 40100, but stop it if you want an unambiguous lab:

```bash
podman stop velocita-raft
```

Its old volume remains intact.

### No leader / repeated elections

```bash
for id in 1 2 3; do podman logs --since 2m "velocita-raft-$id"; done
sudo tcpdump -ni lo 'udp portrange 40211-40213'
```

If `tcpdump` shows no peer packets, inspect publications, endpoints, and process
health. If packets flow but messages are rejected, the node log should report a
wire-version, membership, or persistence error.

### Client times out

Check in order:

```bash
podman ps --filter name=velocita-raft
ss -lunp | grep -E '40101|40111|40112|40113'
for id in 1 2 3; do podman logs --tail 20 "velocita-raft-$id"; done
```

A client sent to a stopped node waits for its timeout before the helper tries
the next port. A live lone leader without quorum can accept a proposal into its
local WAL but cannot commit or answer it; that timeout is correct Raft behavior.

### WAL or metadata startup failure

Do not delete the volume. Stop the affected node and inspect it offline:

```bash
podman stop velocita-raft-1
podman run --rm \
  --volume velocita-raft-node1-data:/var/lib/velocita-raft \
  --entrypoint /usr/local/bin/raft-dump \
  localhost/velocita-raft:v1 /var/lib/velocita-raft
```

Preserve the failing data before attempting repair. V1 deliberately fails
closed on a torn/corrupt record; automated repair tooling is a later milestone.

## 11. Run failure drills

After Quadlet and observability are installed, run the bounded online acceptance
suite instead of manually repeating every injection:

```bash
./deploy/run-failure-drills.sh --yes
```

The `--yes` flag makes the disruption explicit. The runner crashes a follower
to verify systemd restart policy, removes the leader to measure election time,
removes quorum to prove that an uncommitted command cannot change the FSM, and
stops each observability service to prove it is not a Raft dependency. It uses
an exit trap to restore every service even when an assertion fails. Evidence is
written outside the repository under
`~/.local/share/velocita/failure-drills/<UTC_TIMESTAMP>/`.

For the V1.1 Phase 2 recovery gate, run the dedicated bounded workload and
same-ID proof:

```bash
cd ~/aeron-raft-pi
./deploy/run-phase2-recovery-drills.sh --yes
```

This keeps the open-loop generator running while it removes the leader, then
removes quorum long enough to force client state to leader unknown. Its exit
trap restores all voters. Evidence is written under
`~/.local/share/velocita/phase2-recovery/<UTC_TIMESTAMP>/`; a passing run must
show exactly-one terminal accounting, bounded pending depth and discovery
attempts, recovery to `known`, zero retries in the final successful samples,
and one state mutation for the same-ID unknown-outcome drill.

The suite never removes a container, volume, WAL, or metadata file. It also
does not reboot the Pi: a process running on that Pi cannot independently
verify the machine's disappearance and return. Keep the reboot/boot-recovery
check as a separate operator drill.

The manual versions follow for learning and targeted diagnosis.

First identify the leader from logs. Then stop one follower:

```bash
podman stop velocita-raft-<FOLLOWER_ID>
./deploy/cluster-client.sh increment 1
```

Two of three voters remain, so the cluster should commit. Restart the follower:

```bash
podman start velocita-raft-<FOLLOWER_ID>
```

It should receive the missing `AppendEntries` and converge.

Next stop the current leader:

```bash
podman stop velocita-raft-<LEADER_ID>
```

The remaining two voters should elect a new leader after the election timeout.
Send another increment, then restart the old leader and verify catch-up.

Finally, in a controlled window, stop two nodes. The survivor must not commit a
new command because one vote is not a majority of three. Restore one node to
restore quorum. This drill teaches the central production truth: health is not
"a process is running"; health is "the system can safely make progress."

## 12. Stop without losing data

```bash
./deploy/stop-pi-cluster.sh
podman volume ls | grep velocita-raft-node
```

The containers stop; the volumes stay. Never remove those volumes as a routine
restart action.

## What comes next

After this manual acceptance run succeeds:

1. add systemd/Quadlet boot management and controlled restart policy;
2. export node, Raft, Aeron, process, disk, and temperature metrics;
3. install Prometheus and Grafana dashboards/alerts;
4. run and document failure drills;
5. use those measurements to define V2 cross-venue arbitrage latency and risk budgets.

Observability follows correctness: dashboards should expose a system whose
states and failure semantics we already understand.

## 13. Migrate manual containers to rootless Quadlet

Quadlet converts declarative `.container` and `.volume` files into generated
systemd user services. The source files live in
`~/.config/containers/systemd`; the generated services are managed with
`systemctl --user` and logged through the user journal.

The three containers belong to `velocita-raft-cluster.target`. A target is a
systemd grouping unit: it lets an operator start, stop, or restart the cluster
as a group while systemd still supervises each voter independently.

The container policy is intentionally bounded:

- `Restart=on-failure`: crashes restart; an intentional clean stop does not.
- five starts per 60 seconds: persistent corruption reaches `failed` instead
  of causing an infinite restart/logging loop.
- 128 PIDs and 192 MiB shared memory per voter. Add a memory ceiling only
  after measuring Aeron mappings, steady-state usage, and startup peak; an
  arbitrary low cgroup limit can kill an otherwise healthy media driver.
- read-only image, no added privileges, and all Linux capabilities dropped.
- journald logging with a per-service rate limit.
- local image only (`Pull=never`), so boot does not depend on a registry.

Rootless user services normally stop when the last login session ends and do
not start at boot. Linger tells logind to keep this user's service manager alive:

```bash
sudo loginctl enable-linger bhaikapi
loginctl show-user bhaikapi --property=Linger
```

Before migration, all three nodes must be converged. Then stop the manual
containers and remove only their disposable container objects:

```bash
./deploy/stop-pi-cluster.sh
podman rm velocita-raft-1 velocita-raft-2 velocita-raft-3
podman volume ls | grep velocita-raft-node
```

Do not add `--volumes` and do not remove the named volumes. The WALs survive the
container migration.

Install and start the declarative units:

```bash
./deploy/install-quadlet.sh
systemctl --user start velocita-raft-cluster.target
sleep 8
systemctl --user status velocita-raft-cluster.target --no-pager
systemctl --user --no-pager --full status \
  velocita-raft-1.service velocita-raft-2.service velocita-raft-3.service
```

Use systemd—not direct `podman stop`—for normal lifecycle operations, so restart
policy and unit state remain coherent:

```bash
systemctl --user stop velocita-raft-2.service
systemctl --user start velocita-raft-2.service
systemctl --user restart velocita-raft-cluster.target
journalctl --user -u velocita-raft-2.service --since -10min --no-pager
```

If a generated service is missing, inspect the generator rather than guessing:

```bash
systemd-analyze --user --generators=true verify velocita-raft-1.service
journalctl --user -b --no-pager | grep -i quadlet
```

After a reboot, verify `Linger=yes`, the target is active, all three generated
services are running, exactly one leader exists, and all indexes converge.

## 14. Export metrics through the shared-memory boundary

The production metrics path follows the book's reference shape. Each voter
creates a versioned fixed-layout page under `/run/user/UID`, which is tmpfs and
is deliberately independent of the durable WAL. Hot updates are bounded,
allocation-free, cache-line-separated atomic operations. A separate exporter
maps all three pages read-only and owns validation, Prometheus formatting, and
HTTP serving.

The page header contains a magic value, schema version, byte size, node ID,
process start time, and writer PID. A mismatched reader rejects the page. Raft
status fields use a seqlock so an exporter cannot combine a new term with old
indexes. Counter fields use relaxed ordering because they do not publish Raft
memory; they are approximate operational views, not consensus evidence.

After rebuilding the image and reinstalling Quadlet files, restart one voter
at a time. Restart followers first and the current leader last. Then start the
exporter and verify:

```bash
systemctl --user is-active velocita-raft-metrics-exporter.service
curl --fail --silent http://127.0.0.1:9200/metrics | sed -n '1,240p'
```

Healthy pages report `velocita_metrics_page_up 1`,
`velocita_metrics_writer_active 1`, and a very small
`velocita_metrics_last_tick_age_seconds`. Exactly one role series is leader,
all indexes converge, queue depths settle, and dropped-message counters remain
zero.

The previous journal-to-textfile timer is a migration fallback, not the final
design. Keep it until the shared-memory exporter passes on the Pi, then disable
it:

```bash
systemctl --user disable --now velocita-raft-observer.timer
```

The full Aeron media-driver view still comes from its CnC file and tooling.
The wrapper counters here answer application-boundary questions such as offer
outcome and queue pressure; CnC answers driver positions, limits, loss, and
transport state.

## 15. Install the bounded local metrics stack

Keep host collection and application collection separate. Ubuntu's native
`prometheus-node-exporter` owns CPU, memory, filesystem, disk, network, and
thermal metrics. The Velocita exporter owns the bounded Raft and Aeron wrapper
schema. Prometheus samples both; Grafana queries Prometheus. None of these
stores is authoritative for committed state.

Install node exporter without optional script collectors, then bind it only to
loopback in `/etc/default/prometheus-node-exporter`:

```bash
sudo apt install --no-install-recommends prometheus-node-exporter
sudoedit /etc/default/prometheus-node-exporter
```

The argument line is:

```text
ARGS="--web.listen-address=127.0.0.1:9100"
```

Restart and prove that temperature is present:

```bash
sudo systemctl restart prometheus-node-exporter
ss -lnt | grep 9100
curl --fail --silent http://127.0.0.1:9100/metrics >/tmp/node.prom
grep node_thermal_zone_temp /tmp/node.prom
```

Install process exporter, then apply the repository-owned bounded matcher:

```bash
sudo apt install --no-install-recommends prometheus-process-exporter
./deploy/install-process-exporter.sh
ss -lnt | grep 9256
curl --fail --silent http://127.0.0.1:9256/metrics >/tmp/process.prom
grep namedprocess_namegroup_num_procs /tmp/process.prom
```

The matcher derives `velocita-raft-1`, `velocita-raft-2`, and
`velocita-raft-3` from the stable `--node-id` argument. It never labels by PID.
Child aggregation, per-thread series, and expensive `smaps` collection are
disabled. The result still exposes group CPU, resident and virtual memory,
minor/major faults, context switches, file descriptors, process count, and
thread count without instrumenting the Raft loop.

Install the repository-controlled observability target:

```bash
./deploy/install-observability.sh
systemctl --user is-active velocita-observability.target
systemctl --user is-active velocita-prometheus.service
systemctl --user is-active velocita-grafana.service
```

The installer pulls pinned multi-architecture images, runs `promtool check
config`, installs the Quadlet and provisioning files, and starts an independent
target. Prometheus has a 512 MiB memory ceiling, seven-day time retention, a
1 GiB size ceiling, and per-scrape sample/label limits. Grafana has a 512 MiB
ceiling. These are measured homelab budgets, not production defaults.

Prometheus and Grafana listen only on loopback. From the Mac, create an SSH
local forward:

```bash
ssh -N -L 3000:127.0.0.1:3000 bhaikapi@100.84.151.116
```

Keep that terminal open and visit `http://127.0.0.1:3000` on the Mac. The first
Grafana login is `admin` / `admin`; change it immediately. The provisioned
dashboard is under the `Velocita` folder. Do not publish port 3000 merely to
avoid the tunnel.

Validate the data path without Grafana:

```bash
curl --fail --silent http://127.0.0.1:9090/-/ready
curl --fail --silent http://127.0.0.1:9090/api/v1/targets >/tmp/targets.json
curl --fail --silent http://127.0.0.1:9090/api/v1/rules >/tmp/rules.json
podman stats --no-stream
df -hT /
```

This is the actual chain under test:

```text
Raft pages -> raft-metrics-exporter --+
                                      +-> Prometheus -> rules -> Grafana
host /proc and /sys -> node exporter -+
host /proc -> process exporter --------+
```

Prometheus records diagnostics over time. It does not turn a metrics sample
into proof that a command committed; the WAL, metadata, request response, and
replay remain the evidence path.

## Observability triage

Start at the failing boundary instead of restarting the complete stack:

```bash
systemctl --user is-active velocita-prometheus.service
systemctl --user is-active velocita-grafana.service
curl --fail --silent http://127.0.0.1:9090/-/ready
curl --fail --silent http://127.0.0.1:9200/metrics >/tmp/raft.prom
curl --fail --silent http://127.0.0.1:9100/metrics >/tmp/node.prom
curl --fail --silent http://127.0.0.1:9256/metrics >/tmp/process.prom
```

If Grafana is down but Prometheus is ready, Raft and metric history are not
implied down. If Prometheus is down, query both exporters directly and verify
Raft through the client before touching voters. If one shared page is missing,
inspect only that node's service and runtime directory. Never restart three
voters because one dashboard panel says `N/A`.

Alert rules are symptom-oriented and contain this runbook reference. They cover
scrape loss, invalid leader count, leader disagreement, stale owner loops,
commit/apply lag, dropped or back-pressured Aeron messages, temperature,
memory, and root-disk headroom. Rules appearing as healthy proves only current
evaluation; failure drills must demonstrate that each important rule fires and
clears as designed.
