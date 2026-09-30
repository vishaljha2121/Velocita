# M3 deployment log (owner-operated)

The owner runs every command; the assistant prepares files and explains.
No venue, credential or trading action. Started 2026-09-25.

## NUC facts (Stage 1, observed)

| Fact | Value | Why it matters |
|---|---|---|
| OS / arch | Ubuntu 22.04.5 LTS, x86_64 | x86-64 layout already gated |
| CPU / RAM / swap | 4 cores, 7.5 GiB, 2 GiB swapfile | Resource envelope for E3-P5 |
| Root disk | NVMe `/dev/nvme0n1p2`, ext4 `rw,relatime` (ordered journaling) | fsync semantics for E3-P1 |
| `/run` | tmpfs | Secrets home |
| Host toolchain | CMake 3.22 (repo needs 3.30), podman 3.4.4 (no quadlet) | Build and run in a container; plain systemd user units |
| Addresses | LAN 10.21.38.192; Tailscale 100.72.127.102 | Use Tailscale (stable, WireGuard-encrypted) |
| Mac LAN address | changes under DHCP (10.21.103.136 → 10.21.124.226) | Response endpoint must use the Mac's Tailscale address |

## Pi facts (Stage 6 discovery, observed 2026-09-25)

| Fact | Value | Why it matters |
|---|---|---|
| Host / login | `vishalkapi`, admin `bhaikapi` (key SSH installed via ssh-copy-id) | |
| OS / arch | Ubuntu 26.04 LTS, aarch64 | Same release as the build image |
| CPU / RAM / swap | 4 cores, 3.9 GiB (2.4 GiB used, 1.5 GiB available), no swap | Build parallelism must be limited; find what uses memory |
| Root disk | SD card `/dev/mmcblk0p2`, ext4 `rw,relatime`, 11 GB free | fsync latency risk (V1 lesson): measure before trusting (E3-P1) |
| `/run` | tmpfs | Secrets home |
| podman | 5.7.0 | Same systemd unit as the NUC works |
| Tailscale | 100.84.151.116 | Voter link over WireGuard |
| Throttle/temperature | `vcgencmd` needs privileges on Ubuntu | Clean-power evidence (`throttled=0x0`) required for Pi gates |

## nuc2 facts (added 2026-09-25; role not yet decided)

| Fact | Value |
|---|---|
| Hostname / admin | `nuc2`; admin `vishal` (uid 1001, sudo, Mac key only); lab account `hello-robot` (uid 1000, sudo) still present |
| Hardware / OS | Ubuntu 22.04.4 x86-64, 8 cores, 15 GiB, Kingston KC2500 500 GB NVMe (ext4); podman 3.4.4 (same as NUC) |
| Service user | `velocitas` (uid 1003, lingering, Mac key only); `%t/velocita-secrets` tmpfiles rule; rootless podman on overlay |
| Shared-machine risk | `hello-robot` authorizes 8 other people's SSH keys and is a sudoer, so they can reach root. This must be resolved before nuc2 holds secrets |
| Tailscale | 100.85.36.22 (re-joined to the correct tailnet; `--accept-routes` left off) |
| Network | NYU 802.1X wifi (`wpa-eap`, TTLS/MSCHAPv2). The password was keyring-only (empty in the system profile) and is now stored system-wide with `password-flags 0`. This breaks silently when the NYU password changes; a wired link is preferred. |
| Boot | ssh and tailscaled enabled; sleep/suspend/hibernate masked |
| Quiesced | Disabled by the operator: anydesk (was listening on 0.0.0.0:7070), cups, avahi, bluetooth, ModemManager, kerneloops, openvpn, podman-auto-update/restart; snap cups; masked the velocitas user's audio/desktop units. Listening now: sshd 22, tailscaled, local resolver. Robot data kept in place; no robot processes run headless |
| Pending | lock `hello-robot` (remove the 8 foreign keys, remove from sudo, expire the account; data kept); BIOS "After Power Failure: Power On" + power-cut test |

## Decisions

- Service user `velocitas` (uid 1003): unprivileged, lingering, key-only SSH.
  Secrets go in `/run/user/1003/velocita-secrets` (tmpfs, 0700).
- Node runs as a rootless podman container (`deploy/v2/Containerfile`,
  `deploy/v2/systemd/velocita-v2-node@.service`) with a read-only root, all
  capabilities dropped, no-new-privileges, host networking, and `--userns=keep-id`.
- The image build runs the full Release suite natively: part of E-L on the NUC.
- M3 shadow catalog = the test fixture catalog (fictional venue 9), written by
  `velocita-v2-shadow-catalog`. It is not a venue contract; M5 replaces it.

## Stage status

- [x] Stage 1: discovery
- [x] Stage 2: service user, linger, runtime tmpfs, key SSH (verified from the Mac)
- [x] Stage 3: image `localhost/velocita-v2:m3` (ID ef91b316ff5a, 115 MB) built on the NUC from source root `1aa6946f…b28a`; Release suite 108/108 native on x86-64 Ubuntu 22.04 host kernel (2026-09-25)
- [x] Stage 4: `link-1-2` and `caller-9` created in the Mac Keychain and pushed into `/run/user/1003/velocita-secrets` (0600, 32 bytes each); tmpfiles rule recreates the empty directory at boot
- [x] Stage 5: both voters running under `velocita-v2-node@{1,2}` (2026-09-25 13:27 EDT).
  Cluster incarnation `4dbcc4d361cb9cd7245368b892d08188` (identifier, not a
  secret). Shadow catalog root `a17bf680…3c64`. Config digests: node 1
  `d39a6b7e…dbe`, node 2 `b2f729a6…f95`. Voter 1 leader, term 1, zero auth
  refusals; configs switched to `recover` after the first start. First
  authenticated command from the Mac over Tailscale COMMITTED (index 1,
  kill_epoch 1); both voters commit=applied=durable=1. Redirect proven live
  (sequence 2 via voter 2: `ingress_refusals=1`).
  - Drill 1, planned stop of voter 2: command TIMEOUT (exit 3); leader
    current=10 commit=2 durable=10. Voter 2 restarted (epoch 2), caught up from
    its own log to commit=10 with no snapshot. The retry committed at index 11
    with state_version=3: 9 log copies, applied once.
  - Drill 2, SIGKILL of voter 2: exit 137, systemd restart after 6 s, READY
    epoch 3, caught up to commit=applied=11.
  - Drill 3, NUC reboot (14:07 EDT): the tmpfiles rule recreated an EMPTY
    secrets directory; both enabled units failed closed with exit 64 ("secret
    refused … code=1", missing) and did not loop. After a push from the Mac
    Keychain plus reset-failed/start, both recovered from disk to
    commit=applied=durable=11 with new epochs (voter 1 → 2, voter 2 → 4), term
    2, voter 2 leader. Sequence 4 then COMMITTED at index 12, term 2,
    state_version 4, via redirect to voter 2.
- [ ] Stage 6: voter 2 on the Pi. Owner decisions (2026-09-25): stop (not
  delete) V1 on the Pi, done: `velocita-raft-cluster.target` and
  `velocita-observability.target` disabled and stopped, all 19 volumes kept,
  available memory 1.5 → 3.4 GiB. FRESH two-host cluster with a new
  incarnation; the NUC rehearsal is archived, not reused. Both hosts build the
  same source root `5b9b2f94b824ea0beb7bd0c5cb079d6afffb91527e1058dd065fb47784da2d54`.
  Templates in `deploy/v2/two-host/`. deployment_mode stays `laboratory` until
  M2b (backup-copy transport) exists.

## Findings

1. **First NUC image build failed (2026-09-25): GCC 15 only.**
   `tests/v2_archive_publication_fault_tests.cpp` defines `syscall` as a fault
   macro and then includes the production archive source. libstdc++ 15 calls
   `syscall(SYS_futex, ...)` inside `<thread>` → `<semaphore>` →
   `atomic_timed_wait.h`, so the macro rewrote a standard-library call. Apple
   libc++ does not, so the Mac never saw it. Fix (test only): include
   `wal_arena.hpp`, `<atomic>`, `<thread>` and `<vector>` before the macros. A
   GCC 15 syntax scan of every source in an Ubuntu 26.04 container found no
   other error. This is why target-native builds are required, not optional.
   Source root after the fix: `31f07c0631472b7b0a4f2d46d9463e934a7d408b526a2458dc27bf3d63bf5d82`.

2. **Second NUC build: 107/108 passed; `v2-node-health-tests` failed on
   liveness, not safety.** No torn snapshot was ever observed. The test's writer
   republished in a loop with no gap, and on the NUC's x86 scheduler a
   continuously writing seqlock writer starved the reader (fewer than 1,000
   consistent reads out of 200,000). The node publishes every 100 ms, so this
   cannot happen in production. Fix (test only): the writer pauses briefly
   between publishes, still thousands per second. Both seqlock mutants still
   fail "torn health snapshot observed" on Linux (Docker, GCC 15) and macOS;
   TSan clean. Lesson: a liveness threshold must match a real workload.

3. **The first live command exposed an overstated claim.** Voter 2 was listed
   first but showed `ingress_refusals=0`: the client kept `--node` in a sorted
   map and always asked the lowest id first, so no redirect happened. The
   CP-057 process test claimed "client asks the follower first" but only did so
   when voter 2 led; in its recorded run voter 1 led, so the redirect was never
   exercised at process level (the in-process node tests do cover it). Fix: the
   client honours `--node` order, and the process test asserts the follower's
   refusal counter. The old ordering fails "the follower was never asked"
   3/3. Lesson: check the counter that proves an event happened, not only the
   final outcome.

4. **Retry amplification (efficiency, not safety).** The client resends every
   500 ms even after `admitted`, so a 4 s outage left 8 duplicate entries. A
   long outage could fill the 8,192-entry retained log and trigger (safe)
   capacity refusals. FIXED RED-first: the process test now bounds log growth
   during a 3 s outage to at most 2 entries; the old client failed it with 6
   (`evidence/m3_nuc_2026-09-25/client-resend-red.txt`). The client resends
   every 500 ms until admitted, then every 5 s (covers a leader change).
5. **`backpressure` counts retry turns, not frames** (~1,000 per second while
   the peer is down). M3a should expose "peer unreachable for N s" instead.

6. **A never-published health page read as a healthy all-zero status.** The
   first Pi reading, taken before the node's first 100 ms publish, printed
   `node=0 role=none ready=0` with zero counters. Roadmap M3a forbids treating
   missing samples as zeros. Fixed RED-first: sequence 0 means never published,
   so the reader reports unknown (`evidence/m3_nuc_2026-09-25/health-unknown-red.txt`).
7. **First two-host election (2026-09-25 ~19:10 EDT).** Cluster incarnation
   `815d5841231dba86b9767b143f6e0455`. NUC started first and campaigned alone
   up to term ~22; after the Pi started, voter 1 (NUC) won at term 25 over
   Tailscale (direct path after a first DERP packet). The Pi logged
   `busy_holds=22`: frames held, not dropped, while its hard state was syncing
   (the M2 liveness fix working on real hardware). A config-template swap
   (voter 1's config generated on the Pi and vice versa) was caught before start.

8. **First two-host commit (2026-09-25).** Sequence 1 from the Mac, listing
   the Pi first: the Pi redirected (`ingress_refusals=1`), and voter 1 on the
   NUC committed at index 1, term 25, state_version 1. Both hosts
   commit=applied=durable=1, zero authentication refusals. Two voters on two
   physical hosts, over WireGuard (Tailscale), authenticated, one frozen
   candidate (source root `5b9b2f94…`; NUC image 54bdf5f289cf, Pi image
   9a50e0e72af6).

9. **Two-host drills (2026-09-25).**
   - Pi voter stopped: command TIMEOUT (exit 3). NUC log grew by ONE entry
     (1 → 2), versus 8 before the client fix. Pi restarted with epoch 2 and
     caught up over Tailscale from the NUC log (commit=2). The resend committed
     at index 3 with state_version 2 (deduplicated).
   - One-way partition (iptables drop of UDP 42202 on the Pi's tailscale0):
     command TIMEOUT; the Pi campaigned alone to term 73 (hears nothing,
     replies dropped). After healing, voter 1 leads again at term 100: the Pi's
     inflated term forced a step-down and re-election. After the heal the Pi
     showed current=4 commit=3: the old-term entry waited for a current-term
     entry (the approved no-leader-no-op cost, observed live).
   - Operator typo became a bonus test: sequence 3 sent `kill-epoch 2` again.
     The reducer refused it deterministically (`code=11`
     non_monotonic_kill_epoch), committed as an auditable result
     (state_version 3 records the receipt), client exit 1 (committed, not
     applied).
10. **Term inflation on heal (known Raft behaviour; no PreVote in the pinned
    library).** A partitioned voter's rising term disrupts the healthy leader
    when the link returns: a brief election, never a safety issue. With two
    voters the cluster is unavailable during a partition anyway. PreVote would
    remove the disruption; deferred (a Raft library change needs its own review).

11. **The storage benchmark had rotted.** Its per-record `payload_digest` set
    only byte 0 to `index % 256`, so record 256 had an all-zero digest, which the
    codec refuses ("benchmark command refused" on the NUC). Nothing ran the
    benchmark, so the later non-zero-digest rule broke it silently. Fixed, and
    added `v2-storage-benchmark-smoke-tests` to CTest so it cannot rot again.
    The benchmark is a measurement tool, not part of the voter candidate: the
    voters keep running the images built from `5b9b2f94…`.

12. **E3-P1 disk sync latency (storage benchmark, 3 runs each, 2,000 records).**

    | Write+sync | NUC NVMe (ext4) | Pi SD card "SD32G" 08/2024 (ext4) |
    |---|---|---|
    | median | 0.39 ms | 9.3–9.7 ms |
    | p99 | 0.84–0.85 ms | 46–49 ms |
    | max | 10.8–17.6 ms | 82–454 ms |
    | V2 owner loop median / max | 0.22 µs / ~1.0 ms | 0.17–0.19 µs / 51–57 ms |

    The off-thread writer keeps the owner loop at sub-microsecond medians on
    both. With two voters, commit latency is bounded by the Pi's sync: about
    10 ms typical, about 50 ms p99, up to about 0.45 s worst. Pi full native
    suite: 109/109 on ARM64 (`~/velocita-v2-pi-builder.log`, builder
    3afad7c2b41d); NUC benchmark from builder 01f780d817f plus the fixed benchmark.
13. **E3-P2 (unplanned): leadership NOT stable while the SD card is
    saturated.** While the benchmark saturated the Pi's SD card (~60% iowait,
    up to 9 blocked processes), the live cluster went from term 100 to 166:
    66 elections in about 5 minutes. Safety held: both hosts
    commit=applied=durable=6, failed=0, zero auth refusals, voter 1 re-won
    each time. Pi `busy_holds` rose from 22 to 88,483 (its kernel was busy
    with slow persistence almost continuously). Hypothesis, UNVERIFIED: held
    heartbeats plus slow term/vote syncs make the Pi campaign repeatedly, and
    each campaign's own sync delays the next heartbeats. Next: reproduce
    in-process with a slow parked worker; count elections by cause in health
    (M3a); move the Pi voter's data to a USB 3 SSD; consider PreVote.

14. **E3-P2 root cause found and fixed with no hardware purchase (owner
    decision: use existing hardware).** Aeron's term buffers were mmap'd files
    on the data volume, so on the Pi they lived on the SD card; messaging
    stalled behind the card's writeback, heartbeats arrived late, and the Pi
    campaigned repeatedly. Fix (unit file only): Aeron dir in `/dev/shm`
    (`--shm-size 256m`), `AERON_TERM_BUFFER_LENGTH=1048576` (verified the pinned
    driver reads it at context init). Idle SD writes: 113/min before, 22/min
    after. A/B under the same benchmark saturation:

    | | Aeron on SD | Aeron in RAM |
    |---|---|---|
    | Pi role | follower | follower (also tested as leader) |
    | worst SD sync | 454 ms | 421 ms (leader run 72 ms) |
    | elections during saturation | 66 | 0 (both roles) |
    | Pi `busy_holds` | 88,483 | 0 |

    The first hypothesis (a kernel busy with slow hard-state syncs) was WRONG:
    the held frames were a symptom of the elections, not their cause. Testing
    on the hardware settled it before any wrong code change. The first rerun
    was confounded (the Pi had become leader) and was repeated with the Pi as
    follower. Operating rule kept anyway: no disk-heavy work on the Pi while
    its voter runs. The USB SSD stays a pre-unattended-canary consideration
    (sync latency ~10 ms median / ~48 ms p99 bounds commit latency), not a blocker.

15. **M3a dashboard slice built (2026-09-25).**
    - `velocita-v2-exporter` (RED-first renderer; 3 mutants killed:
      unknown-as-zeros, unclamped age, role off-by-one): an unknown voter
      exports only `velocita_v2_up 0`, never healthy zeros.
    - Client prints `latency_ms` (first send → committed result).
    - Prometheus (NUC, 127.0.0.1) scrapes both hosts' voter exporters (:9410)
      and host node-exporters (:9110) over Tailscale. Grafana listens on the
      NUC's Tailscale address only (Mac and iPhone through Tailscale). 11 alert
      rules (promtool-valid); notification delivery NOT configured (owner
      chooses the channel). Dashboard generated from
      `deploy/v2/observability/generate_dashboard.py`: 39 panels, all 46
      queries parsed by Prometheus v3.13.2, provisioning verified in Grafana 13.1.3.
    - NUC soak bot (`tools/velocita-soak-nuc.sh`) with its own caller key
      (caller 10 = CallerId 8), results to the NUC, metrics through the
      textfile collector.
    - Source root `6fc1164f30001648caf5ca1a4227efe572629cc3001ea98f32b0d3f60ec68e8f`;
      Mac Release 110/110.

16. **Elections still happen under a full Pi build, and after it.** During the
    ~1 h Pi image build with voter 2 running, the term went 168 → 207 (39
    elections; Pi busy_holds rose to 45,978). After it, the NUC restart
    election took ~14 terms and ~20 s. The Aeron fix removed the benchmark
    storm, not this. Hypothesis, UNVERIFIED: gigabytes of build writeback delay
    the durable term/vote syncs Raft needs before replying. Next: instrument
    leader-contact age, the slowest owner turn, hard-state sync latency and
    elections started. The rule "no builds on the Pi while its voter runs" is
    now enforced, not advisory.
17. Units used `podman volume create --ignore`, which does not exist in the
    NUC's podman 3.4: Prometheus/Grafana failed in ExecStartPre. Removed
    (`podman run` creates named volumes itself). Lesson: test units against
    the target's podman version, not a newer one.

18. **Monitoring live (2026-09-25 ~17:33 EDT).** Exporters on both hosts
    (voter :9410, host :9110 on Tailscale addresses); Prometheus on the NUC
    reports all 5 targets `up` (prometheus, velocita-v2 nuc/pi, node nuc/pi);
    Grafana 13.1.3 on 100.72.127.102:3000 (Tailscale only). Both voters on the
    new image (NUC dbfbd428f02d, Pi 0fa7a9fe90c2; source root `6fc1164f…`;
    110/110 native on each), term 228, NUC leader. Hardening note for later:
    Grafana contacts the internet on first start to install bundled plugins;
    consider disabling that for an offline-by-default deployment.

19. **First NUC soak run INVALID: the bot resent one command forever.**
    `publish()` reused the main loop's variable `i` for its bucket loop, so every
    metrics write reset the counter: sequence 11 / kill-epoch 14 was resent
    about 40 times and the run would never end. The cluster behaved correctly
    (each resend deduplicated; state_version stayed 6). Fixed (locals in
    publish) and tested with a fake podman: sequences 12–16, clean exit,
    correct counters. Bot stopped by the operator.
20. **Elections during that run: term 228 → 234 around 17:42 EDT**, while the
    NUC was also pulling Grafana/Prometheus images and starting them. It fails
    the soak's zero-election criterion; cause not yet known (see finding 16).
    Next soak is a data-gathering run with the dashboard recording host
    context; election diagnostics metrics are the next code change.
21. **Second NUC soak (bot on the NUC, 540 × 10 s, started 17:49 EDT
    2026-09-25): commands PASS, elections NOT YET ATTRIBUTED.** Read on
    2026-09-26 21:46 EDT from the exporters (Prometheus listens on 127.0.0.1
    only and SSH to the NUC was blocked, see below):
    - soak bot: ok 540 / timeout 0 / refused 0 / other 0.
    - both voters: current = commit = applied = durable = 616, term 263,
      NUC leader, Pi follower, ready, not failed; auth and fence refusals 0;
      results sent 610 each, dropped 0.
    - client round trip (histogram buckets): median 100–250 ms, p99
      500–1000 ms, max ≤ 2.5 s, mean 283 ms. This is NOT commit latency: each
      command is a fresh client process that starts Aeron and connects before
      sending (cold start dominates).
    - term went 234 (17:42) → 263 by the read, 26 h after the soak ended.
      Whether any of those 29 elections fall inside the soak window needs
      Prometheus history (`changes(velocita_v2_term[...])` over 17:49–19:25).
      Until then the zero-election criterion is UNKNOWN, not passed.
    - NUC `ingress_refusals_total` 686 (Pi 0) while the bot saw 0 refusals:
      probably resends refused as duplicates after an admission response was
      lost at client cold start. Unverified; check before trusting the counter
      as an alert source.
    - Access finding: `tailscale set --ssh=false` did not take effect on the
      NUC. Tailscale SSH still owns port 22 there, and its policy is in
      `check` mode: after the check period, key SSH blocks on a browser login
      URL. Unattended scripts must not depend on it; turn it off.
22. **Three-voter code ready (2026-09-27, CP-059); not yet deployed.** A
    RED-first test exposed that one unreachable follower stalled all peer
    traffic, so a three-voter cluster could not have survived a dead voter:
    it would stop committing once Aeron's liveness timeout marked the peer
    unreachable. Fixed and mutation-covered (`evidence/three_voter_2026-09-27/`).
    Deployment inputs: `deploy/v2/three-host/`, Prometheus targets for nuc2
    (host labels now `nuc1`, `pi`, `nuc2`; old series keep `nuc`), quorum
    alerts, soak bots requiring `VELOCITA_INCARNATION`. The running two-host
    cluster is unchanged and still on the old image.
23. **Soak (finding 21) PASSES: zero elections in the soak window**
    (Prometheus: term 234 at 17:49 and at 19:29:56, `changes()` = 0). All pass
    criteria met: 540/540 committed, 0 elections, 0 auth/fence refusals,
    voters converged.
24. **Root cause of the recurring elections: wifi carrier loss (not the SD
    card, not load).** After the soak, the term rose in bursts of 12–14 every
    ~8 h 24 min (09-25 22:31, 09-26 06:56, 15:20, 23:44, 09-27 08:08). At
    08:08:30–40 nuc1's `wlp0s20f3` changed carrier twice while nuc1's term went
    277 → 290 (it alone kept starting elections, no PreVote); at 08:14:45 the
    Pi's `wlan0` changed carrier and its term went 290 → 299. Both hosts are
    on NYU wifi; the wired ports (`eno1`, `eth0`) are unused. Period matches
    802.1X re-authentication or a DHCP renewal; which one is unconfirmed
    (system journal needs admin). Fix: wire every voter. Three voters also
    absorb one host's drop, but a returning voter's inflated term still
    forces one extra election (finding 10).
25. **nuc1 access (2026-09-27).** Tailscale SSH is now off on nuc1 (plain key
    SSH works for `velocitas`). A `vishal` account already existed there (uid
    1002, created 2026-09-25), so the earlier `adduser` chain stopped at its
    first command: vishal has no Mac key yet.
26. **Owner decision (2026-09-27): stay on wifi, no Ethernet.** Known cost:
    each host's link reset (~every 8 h 24 min) costs an election when that
    host leads, plus one extra election when it rejoins with an inflated term.
    Three voters keep committing through any single host's reset. nuc2 `ufw`
    is inactive (no firewall between voters). nuc1 `vishal` now has the Mac key.
    Rebuild plan: new image tag `localhost/velocita-v2:m3-3v` on every host,
    running image kept as `:m3-2v`; tags switch only at the three-host cutover.
27. **M4 market-data recorder live on nuc1 (2026-09-28).** `velocita-md-stream`
    user unit (python:3.12-slim, model source read-only, no secrets) records the
    True Markets public WebSocket into `~/velocita-md/stream.db` and publishes
    `velocita-md.prom` via the node-exporter textfile directory. First read:
    up=1, price_candles 61, reference_prices 30, trending 6, surging 7; the
    orderbook topic is subscribed but delivers nothing (D-7).
28. **Hypothesis for the election bursts (finding 24), from a lab defect
    (2026-09-28).** willemt/raft randomizes election timeouts with rand(), and
    nothing seeded it, so every voter process drew the SAME timeouts. After a
    link reset the voters would retry elections in lock-step until timing noise
    separated them: consistent with bursts of 12-14 terms per wifi drop instead
    of 1-2. Fixed in the node binary (CP-063). VERIFY after redeploying: term
    increase per wifi reset should fall to about 1-2.
29. **True Markets reference-price cadence (2026-09-28).** The
    `reference_prices` topic delivers a message every ~1 s, but BTC|USD's
    `timestamp` advances only every few seconds (a 60 s sample: one value
    repeated for ~12 s; age at receipt p50 4.3 s, p90 14.3 s). The edge now
    forwards only a newer observation (CP-064), so each source event is a new
    price and its age at receipt is ~0.3 s.
30. **Step 5 deploys on two voters (nuc1 + Pi) while nuc2 is offline.** Fresh
    cluster (new incarnation) with the True Markets shadow catalog; the old
    two-host cluster (incarnation 4dbcc4d3...) and its soak state are retired.
    Images: running `:m3` kept as `:m3-2v`; new build tagged `:m3-3v` and `:m3`
    (the unit runs `:m3`; rollback = retag `:m3-2v` as `:m3`).
31. **Phase 1 shadow loop LIVE on nuc1 + Pi (2026-09-28 16:03 EDT).** Fresh
    two-voter cluster, incarnation 8d1fd769b484ba5dc55456b762c69139, images
    `:m3-3v` (nuc1 e9d1057c4f9, Pi cb656be114d1, shadow 5964052bbaa; source
    ade92b70...3712; 116/116 on both architectures; nuc1's rebuild after a
    test-only change produced a byte-identical image). The TrueMarkets catalog
    root 167b67a7...7526 came out identical on x86 and ARM. Operator ceremony
    committed at indexes 1-3 (147-198 ms from the Mac). `velocita-v2-shadow-edge`
    first reading, 87 live ticks: 87 decisions recorded (`shadow_recorded`), all
    87 `proposed` (a bounded 0.0001 BTC shadow order), 87/87 Python-vs-cluster
    agreement, 0 errors, 1 submitter start. Tick-to-decision mean 356 ms, all
    87 within 250-500 ms; per hop: edge 0.7 ms, source-event commit 140 ms,
    decision-frame commit 215 ms (follower durable ack waits for the next
    heartbeat, CP-063). Market data mean age 0.32 s. Both voters commit ==
    applied == durable (182 = 3 ceremony + 5 setup + 2 x 87 ticks); term 101
    unchanged, so no elections since start. The fresh cluster reached term 101
    because nuc1 retried elections alone for ~2 min before the Pi started (no
    pre-vote). nuc1's auth_refusals=21 and backpressure=115932 date from that
    window (the old Pi voter's frames, then the absent peer) and have not
    grown since.
32. **Unplanned drill: nuc1 power loss (2026-09-28, boot 16:37:48 EDT).**
    Mid-build power drop. On boot the RAM-only secrets were gone (by design:
    never on disk), so voter 1 refused to start (`secret refused ...
    link-1-2.key`, exit 64, no restart loop) and the shadow edge's unit
    assertion held it down: fail-closed. The Pi alone went candidate and
    committed nothing (term 101 -> 331, commit = durable = 1674 frozen).
    Latent bug exposed: the promtool test file sat in `rules/`, which
    Prometheus globs as rule files; Prometheus had not re-read the directory
    since it was added (CP-059), so it only failed on this boot. Moved to
    `rule-tests/`; `tests/test_observability_layout.py` now fails if any
    loaded file is not a rule-group file. Recovery evidence: see finding 33.
33. **Power-loss recovery complete (2026-09-28).** Secrets re-pushed from the
    Mac; voter 1 restarted at 16:44:38 with `startup = recover` (session
    epoch 1 -> 2, as designed), rejoined, and one election followed at the
    Pi's inflated term (331 -> 398; the Pi now leads). Both voters then
    reported commit = applied = durable = 3257 (up from the 1674 frozen during
    the outage), no latched failure, auth and fence refusals 0. The shadow
    edge resumed on the new image (funnel: 59 messages -> 23 new prices ->
    23 decisions -> 23 proposed place_limit -> 23 effective no_action).
    Prometheus restarted cleanly on the moved rule tests; the secrets check
    (CP-066) is installed on nuc1 and the Pi and reports 4/4 present on both.
    Not yet shown: the alert firing on a real missing secret (the secrets
    were pushed before the check was installed).
34. **Latency with the Pi leading (2026-09-28, 848 live ticks after the
    rejoin).** Tick-to-decision mean 391 ms (nuc1 leading, finding 31: 356
    ms): edge 0.5 ms, source-event commit 153 ms (was 140), decision-frame
    commit 237 ms (was 215). 848/848 agreement, 0 errors, 1 submitter start.
    With two voters both copies must be durable either way, so the SD-card
    sync is on the path in both layouts; the extra ~35 ms is consistent with
    the edge on nuc1 now crossing Tailscale to a leader on the Pi and the
    Pi's slower leader-side work. Cause not isolated.
35. **nuc2 outage cause: a system freeze, not wifi (2026-09-28).** One boot
    ran from Sep 25 18:23 until the owner's plug-cycle at 16:37 on Sep 28 (no
    shutdown/reboot between, `last -x`). The system journal stops at 07:17:01
    on Sep 28 (the hourly 08:17 cron entry never appears) with no panic or
    kernel error: a hard freeze between 07:17 and 08:17, then ~9 h powered but
    silent. Wifi was healthy before it: the 05:14:59 802.1X re-auth failed on
    one AP (`IEEE8021X_FAILED`) and succeeded on another 13 s later. The `nyu`
    profile is system-wide with the secret in the system profile, so it
    connects without a login. nuc2 is a NUC11PAHi5, BIOS 0043 (2022-02-21),
    kernel 6.8.0-85; no watchdog device registered and systemd
    RuntimeWatchdog off, so nothing could reset the hang. Both NUCs booted
    within 7 s of each other at 16:37 (nuc1 16:37:48, nuc2 16:37:55). The
    owner confirms the NUCs are on SEPARATE power sources, so the common cause
    is upstream (a dip on a shared building circuit) or physical handling
    during nuc2's replug; not a shared strip. Cause not determined.
    The `nyu` 802.1X profile does not validate the RADIUS certificate
    (server seen: CN=rad.nyu.edu via InCommon/USERTrust).
36. **nuc2 crash recovery set up (2026-09-28).** `kernel.panic = 10` +
    `softlockup_panic = 1`: a deliberate sysrq crash at ~17:46 came back
    unattended at 17:47:09. Hardware watchdog: Ubuntu's kernel blacklist
    (`/lib/modprobe.d/blacklist_linux-hwe-6.8_*.conf`, `blacklist iTCO_wdt`)
    stops the driver loading at boot, `modules-load.d` honors it, and an
    initramfs `/etc/initramfs-tools/modules` entry was not loaded either (no
    iTCO line in the boot's kernel log although the module was in the image).
    An explicit `modprobe iTCO_wdt` works, and systemd 249 then opens the
    device by itself: state=active, timeout 30 s, timeleft 30, used by 1.
    Chosen: `deploy/v2/host/velocita-watchdog@.service` (modprobe by name at
    boot) + `velocita-watchdog.conf` (RuntimeWatchdogSec=30s). nuc2 wifi:
    `nyu` retries forever at priority 10; six stale profiles no longer
    autoconnect. Reboot verified 18:04:30 -> 18:04:57: iTCO_wdt state=active,
    timeleft 30, unit active, with no manual step; the unused initramfs entry
    was removed. Pending: the same setup on nuc1 and the Pi.
37. **THREE VOTERS LIVE: nuc1 + Pi + nuc2 (2026-09-28 18:24 EDT).** nuc2
    image built natively, 118/118 (2bba12172a20, source c81ba6e9...; nuc1 and
    the Pi keep their ade92b70 images: the C++ is identical, only Python,
    deploy files and test registration changed since). Retired the two-voter
    cluster 8d1fd769 at commit = durable = 5107 on both voters (data moved to
    `~/velocita-v2/retired/`, not deleted). New incarnation
    8d9d7c9548c25e99f25dc3fe8a731b31 from the three-host templates; new
    secrets link-1-3, link-2-3; catalog.bin byte-identical on all three hosts
    (root 167b67a7...). First election succeeded at term 1 (nuc2 leads).
    Ceremony committed at indexes 1-3 (from=3, 470-740 ms from the Mac,
    including the redirect from voter 1). Shadow edge on the new cluster,
    voters from `VELOCITA_NODES` in shadow.env: first 19 ticks -> 18
    recorded + 1 stale, 19/19 agreement, 0 errors, mean tick-to-decision
    348 ms. All three voters commit = applied = durable (50 at the check),
    no refusals; Prometheus: 3/3 voter and 3/3 host targets up, no alerts.
    Operator slip, recovered: my first start command passed the node number
    through a remote `$0`, starting a bogus `velocita-v2-node@bash` on each
    host; it had no config and exited 64 with nothing written (data
    directories verified), and was cleared with reset-failed before the real
    start.
38. **Early durable acks rolled out (CP-068, 2026-09-28 19:14-19:17 EDT).**
    Native builds 119/119 on nuc1 (57b9757d7b4e), Pi (3a35e742e9ff) and nuc2
    (cb0d0b5e7337), source f77d81bd...; previous images kept as `:m3-pre-ack`.
    Rolling restart, followers first: nuc1 and the Pi rejoined on term 1 with
    no election; restarting the leader (nuc2) cost exactly one election (term
    1 -> 2, the Pi won). The shadow edge rode through all three restarts: 0
    errors, 0 disagreements, one submitter start. Live mean tick-to-decision:
    353 ms before (644 ticks, nuc2 leading, old binaries); ~41 ms over 33
    ticks once both followers had the fix while nuc2 (NVMe) still led; 122.8
    ms at steady state with the Pi leading (59 ticks: source 60, decision 62).
    The Pi-leader cost is its SD card on the apply path: a leader applies,
    and so answers the client, only after its own copy and the commit index
    are durable, and followers learn the commit only on the next append.
    Prometheus: no alerts.
    Leadership moved to a NUC by restarting the Pi voter (19:22 EDT): one
    election, term 2 -> 3, nuc1 won; edge 0 errors. Steady state with nuc1
    leading (59 ticks): mean 39.0 ms (edge 0.5, source commit 19.9, decision
    commit 18.5), p50 36.5 ms, p99 166 ms; 0 disagreements, no alerts. Live
    improvement 353 -> 39 ms (9x). Open: nothing prefers a NUC leader, so a
    later election can hand leadership back to the Pi (~123 ms).
39. **Shadow edge stopped at 20:35:58: the retained Raft log is full
    (2026-09-28).** The node never compacts (by design until snapshot
    install / catch-up exists: "fullness stops admission, never evicts
    history"), and the retained log holds 8,192 entries: 8,128 ordinary + 64
    reserved for operator control. The three-voter cluster started 18:24 and
    reached index 8,132 at ~2 entries per tick (source event + decision
    frame), about 2 h 10 min. Since then every normal-lane command is refused
    as retry-later; the submitter reports it as a timeout (`ERROR setup
    step=0 code=-1 refusal=-1`) and the edge restarts it every ~90 s (34
    starts by 21:20). Voters healthy throughout (term 3, commit = applied =
    durable = 8132); ingress refusals are the visible symptom (nuc1 2,010,
    Pi 1,811). Operator-control commands still have their 64 reserved
    entries, so kill/halt remain available. Not related to CP-068. Missing:
    an alert and a dashboard gauge for retained-log fill; the dashboard
    showed only "submitter not ready".
40. **A voter behind a compacted prefix disrupted the whole cluster (found in
    a lab test, 2026-09-29, before any compaction runs live).** willemt sends
    nothing to a peer whose next entry lies inside the snapshot
    (RAFT_ERR_NEEDS_SNAPSHOT), and `raft_send_appendentries_all` returned at
    the first such peer, so the peers after it got no heartbeats either. The
    stranded voter timed out, campaigned with ever higher terms, and pulled
    the others off a healthy leader (an uncommitted entry was lost; a
    three-voter lab cluster reached term 4 within 4 simulated seconds). Fixed
    in the pinned patch (CP-070): the loop serves every peer, and a stranded
    peer gets an empty append at the snapshot point from the periodic
    heartbeat, which keeps it a quiet follower. The leader reports it as
    `stranded_voters`. It still needs a snapshot install to catch up (M6); the
    compaction watermark (C2-C3) is what keeps healthy voters from being
    stranded in the first place.
41. **Compacted voters could not elect a new leader (found in C4 testing,
    2026-09-29, before deployment).** With compaction on, the real-process
    shadow scenario stalled after its leader was SIGKILLed; in-process the two
    survivors campaigned forever. Upstream willemt: `raft_get_last_log_term`
    returns 0 when the last entry lives only in the snapshot, so a candidate
    whose log was compacted to its end advertised last-log term 0, and every
    voter (whose own check does fall back to the snapshot term) refused it.
    Fixed in the pinned patch (CP-073) by the same fallback. Proven both ways:
    kernel-level and node-level tests fail without it and pass with it.
42. **Compaction and the shadow book deployed (C5, 2026-09-29 15:40 EDT).**
    Images built natively 119/119 on all three hosts from source
    17220d21... (nuc1 f21094b10fec + shadow 472d8a1d2074, Pi 67612899bd77,
    nuc2 f22a77fa9acc; previous kept as :m3-pre-c). The V2 peer body version
    changed, so a fresh cluster: retired 98313aff (moved to retired/), new
    incarnation 453345d0ac2193a09958e96cf367da07, elected at term 1 (nuc2).
    Ceremony: 45 ms for the first command from the Mac. Edge with the ledger:
    3/3 agreement, 0 errors; the tick book bought at $83,526 (equity $976.60 =
    liquidation value after the measured round-trip cost), the daily book
    waits for UTC midnight; /state/ledger.db written. Compaction floor 14 =
    commit on all three voters, snapshot 0 (threshold 2,048 entries: first
    compaction expected in ~8.5 h at 4 entries a minute), 0 stranded, no
    alerts. The 30 s tick throttle stays until the first live compaction's
    pause is measured (VelocitaCompactionPauseLong watches it).
43. **Finding 28's hypothesis disproved: the +12-term bursts are a returning
    voter, not repeated elections (2026-09-29).** Two bursts in the first 4 h
    of cluster 453345d0: at 17:51 nuc1's wifi changed carrier twice and nuc1
    ALONE climbed from term 1 to 14 while the others stayed at 1; at 19:18
    nuc2's wifi changed carrier once and the same happened (14 -> 26). An
    isolated voter keeps campaigning, raising its own term each timeout; when
    it rejoins, its higher term forces ONE leadership change (nuc1, then
    nuc2, won). The cluster committed throughout on two voters and the edge
    saw 0 errors. The seeded election jitter (CP-063) is correct but cannot
    prevent this; the standard fix is Raft pre-vote (thesis section 9.6): a
    candidate first asks whether it could win, without raising its term, and
    voters that still hear a leader say no.

44. **Phase 2 deployed; first paper trade (CP-075, 2026-09-30).** A fresh
    cluster was created from tag `v2-cp075-phase2-paper` (commit `5f20fdb`).
    - Incarnation `5ebc6359dd25d63ff02df0ee9e78875c`.
    - Images: nuc1 `069ca3b759dc` (shadow image `fe0591f45acd`), Pi
      `8e4e7ecca2ff`, nuc2 `32a0566e4c5b`. Each built with 122/122 tests.
      The previous images are kept as `:m3-pre-p2`.
    - The old cluster `453345d0` is retired under `~/velocita-v2/retired/`.

    Order of events:
    1. The ceremony from the Mac (kill epoch 1, executor generation 1,
       catalog `167b67…`).
    2. `velocita-v2-shadow-setup` as operator CallerId 9.
    3. `risk-limits` from the Mac, at index 10 / risk version 1: $25 per
       order and exposure, 25-lot position cap, long-only, 1.5% fee hold,
       reason 20260930.
    4. The shadow edge as CallerId 21.
    5. The gateway (CallerId 22): first unarmed, then armed with
       `--freshness-ms 2000 --allow-new-risk`.
    6. The edge switched to `--mode paper`.

    The first paper tick bought 25 lots (0.00025 BTC) at about $84,409,
    FOK-filled by PaperVenue, with a fee of $0.242676:
    ceil(10 × 84,409 × 25 × 115 bp) micros, which is exact.
    - Tick 2 was refused `stale_risk_state`. The edge re-synced to
      position version 2 and position 25.
    - Ticks 3 and 4 were refused `position_limit_exceeded`.
    - One order and one fill in total, 0 errors, and the term stayed at 49
      throughout.

45. **Deploy tooling failures (fixed in procedure; recorded in memory).**
    - **macOS openrsync wrote nothing to the hosts.** It exited 0 with a
      `user@host:path` target, and instead created local directories named
      `velocitas@<host>aft-pi` in the repo. The hosts then rebuilt the OLD
      source from cache and tagged it as the new image. Detected by the image
      IDs and the test count (119, not 122).
    - **macOS tar added AppleDouble `._*` files**, which broke the tests that
      glob `*.json` / `*.service`.
    - **Procedure now:**
      1. `git archive`, then `COPYFILE_DISABLE=1 tar --no-xattrs` over ssh,
         swapping the directory into place.
      2. Check `SOURCE_COMMIT` plus a whole-tree sha256 against the export
         before building.
    - **Killing `podman build` leaves its test step running in the build
      container.** That step keeps writing into the log. Delete the log
      before rebuilding, never just truncate it.

46. **Setup and gateway units were OOM-killed at 256 MB** (exit 137,
    `OOMKilled=true`). They run the same submitter/Aeron client as the edge,
    which has 512 MB. Both units are now 512 MB.

47. **Election bursts on the new cluster (open).** The term went 2 → 8 → 23 → 49
    in three bursts of 15–45 s, at 11:50:30, 12:06:30 and 12:10:00:
    - +6 while the setup container was OOM-killed;
    - +15 while Prometheus, Grafana and md-stream restarted on nuc1;
    - +24 at a gateway container start.

    During each burst, Prometheus on nuc1 also missed another host's
    exporter. Between bursts the cluster is stable: 2 h idle, edge only,
    unarmed gateway state reads at 4/s, and armed gateway plus paper mode.

    Ruled out:
    - CPU (load about 1.2 of 4 cores);
    - firewall changes (every container uses host networking);
    - gateway steady-state traffic.

    Suspects: IO or memory pressure on nuc1 stalling its voter, or a brief
    network stall, turned into many terms by the missing pre-vote
    (finding 43). Consequence for paper trading: the gateway stops new risk
    on every election and stays stopped until restarted (manual on day 1).
