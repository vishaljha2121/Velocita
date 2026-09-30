# E1/E2/E3 sign-off plan: what remains, and which runs close each exit

Date: 2026-09-24 (CP-051). Status: owner-requested plan; it approves no bytes,
device action, venue contact or deployment. Every machine run below needs
explicit operator authorization at the time it is run.

Topology assumed: MVP two-voter pilot, one voter per host (NUC + Pi). The
three-voter online installer is out of scope here (see `CURRENT_HANDOFF.md`
CP-051); its tracked RED `tests/v2_snapshot_catchup_red.cpp` does not block
these sign-offs.

## 1. What "signed off" means

An exit is signed off only when ALL of these hold for ONE frozen candidate:

1. The candidate source root from `deploy/source-manifest.sh` is recorded in an
   independently held review record before any run; every run below reports
   that same root. A run of a different root counts for nothing.
2. Library evidence passes on four platforms: macOS host (Release + ASan/UBSan),
   ARM64 container, NUC (x86-64) and Pi (ARM64), via the existing gate scripts.
3. The process-level runs below pass on the physical NUC + Pi pair with the M2
   node binary: real processes, real network, real storage.
4. Raw logs, host facts (kernel, filesystem, mount options, storage device,
   temperature/throttle on Pi) and measured numbers are saved under
   `docs/evidence/`, with pass/fail against the criteria written BEFORE the run.
5. The owner signs the record. A green library suite never substitutes for a
   missing process or machine run.

## 2. Shared prerequisites

| Prerequisite | Why it is needed | Status |
|---|---|---|
| M2 startable node (`velocita-v2-node`) | Every process/machine run needs a real voter process | NEXT |
| Authenticated peer transport between NUC and Pi | Process runs must not use the test fixture's simulated authentication | Part of M2 |
| Authenticated archive copy NUC→Pi (backup destination) | E2 independent-copy evidence across hosts | Part of M2/M3 |
| M3a minimal health output (commit/durable/applied gaps, WAL latency, disk headroom) | Runs must show which stage waited and why, not only pass/fail | M3a; minimum counters during M2 |
| Fault-injection kit on each host (link drop, disk delay, disk error, disk full) | E1/E3 drills | To script in M3 |

Fault-injection options, least invasive first: `nft`/`iptables` drops for
partitions; `cgroup v2 io.max` for slow disk; a loop-device filesystem, which
can be filled to test ENOSPC; `dm-delay`/`dm-error` on a scratch loop device for
latency and EIO. Never inject faults on a filesystem holding anything but the
test data directory.

## 3. E1: an intact voter catches up safely

Native evidence (done): `v2-two-voter-catchup-tests` covers a partitioned peer
and a peer that loses power after an unsynced acknowledgement, and is
mutation-covered. Existing recovery suites cover restart replay.

| # | Run | Where | Pass criteria |
|---|---|---|---|
| E1-L | Library gates incl. `v2-two-voter-catchup-tests` | host, container, NUC, Pi | All pass on the frozen root |
| E1-P1 | SIGKILL the follower mid-stream under steady load; restart it | NUC+Pi, both directions (NUC leader, then Pi leader) | Follower rejoins from its own log; both digests equal; no committed index lost; leader commit never exceeds the follower's durable index while it is down |
| E1-P2 | Partition the link for longer than several election timeouts while proposals arrive; heal | NUC+Pi | New commits stop during the partition (fail-closed); after heal plus one current-term command, both reach the same digest; no snapshot transfer requested |
| E1-P3 | Leader compaction during a partition | NUC+Pi | Compaction cut ≤ peer durable index at every sample; peer catches up afterwards |
| E1-P4 | Physical power cut of the Pi during writes (operator pulls power) | Pi | Pi recovers with a durable prefix ≥ everything the NUC counted as committed; digests converge; `throttled=0x0` recorded before and after |
| E1-P5 | Same power cut on the NUC | NUC | As E1-P4 |
| E1-R | Lost-storage repair: wipe one voter's data, run the offline repair | NUC+Pi | Requires the lost-voter repair implementation (bytes still need review). Without it, the wiped voter must NOT be able to rejoin by re-bootstrapping; that refusal currently cannot be detected locally, so this row stays OPEN until repair lands |

Measure and record: catch-up time versus backlog length (100 / 1,000 / 8,000
entries), and time to first commit after heal.

E1-R must close before a live canary (M7), not before M3 shadow deployment.

## 4. E2: compaction preserves verified history

Native evidence (done): local archive publication, catalog, quotas, fault
suites, fresh destination verification (CP-048); compaction in `two_host_pilot`
mode refuses reclamation without independent evidence.

| # | Run | Where | Pass criteria |
|---|---|---|---|
| E2-L | Library gates (archive, compaction, destination suites) | host, container, NUC, Pi | All pass on the frozen root |
| E2-P1 | Compaction in `two_host_pilot` mode with the Pi as authenticated backup destination | NUC→Pi | WAL prefix reclaimed only after the Pi verifies the exact archive bytes; restart reloads from snapshot + archive |
| E2-P2 | Backup destination unavailable / corrupted / stale during compaction | NUC→Pi | Reclamation refused; retained WAL keeps every record; health output shows the reason |
| E2-P3 | Restore drill: rebuild the complete committed history from the archive + the backup copy on the other host | both | Replayed digest equals the live digest; every archived record verifies |
| E2-P4 | Archive quota exhaustion on real disk sizes | both | Admission refuses before disk-full; protected control lane still admits kill/cancel within its reserve |
| E2-P5 | Power cut during archive publication | Pi and NUC | Recovery either keeps the old catalog or the complete new one; never a partial catalog accepted |

Record: archive bytes per 1,000 commands, verification time per segment, free
space on both hosts before and after.

## 5. E3: persistence does not block the owner or expose early success

Native evidence (done): worker-backed hard state, suffix truncation, compaction
and history recovery; WAL target handoff; persistence/crash models and fault
suites; no owner-thread fsync.

| # | Run | Where | Pass criteria |
|---|---|---|---|
| E3-L | Library gates incl. async persistence/model suites | host, container, NUC, Pi | All pass on the frozen root |
| E3-P1 | fsync / fdatasync latency profile of the actual data device | NUC, Pi | p50/p99/p99.9/max recorded for block sizes the WAL writes; storage device, filesystem and mount options recorded |
| E3-P2 | Slow disk (io.max throttle) under load | each host in turn | Owner loop keeps polling health (measured loop-period p99 within budget); no vote/ACK/apply/result/proof before durability; leadership stable (V1 lesson: SD stalls destabilised it) |
| E3-P3 | Disk error (dm-error) during WAL write and during hard-state save | each host | Storage latch trips; node stops serving; health reports failed-storage; no success returned for the failed write; restart recovers the durable prefix |
| E3-P4 | Disk full (loop fs) | each host | Fail-closed refusal before corruption; clear reason in health; recovery after space is freed |
| E3-P5 | Resource envelope under sustained load | NUC, Pi | Peak RSS measured against the bounded design (the integrated peak was never measured); CPU, temperature, throttle recorded |
| E3-T | Whole-process ThreadSanitizer run of the node under load | host | No race reports. The current TSan covers only the WAL source + its test |

Budgets (loop period, fsync p99) are set from E3-P1 measurements and written
into the record before E3-P2 runs; no arbitrary numbers are approved here.

## 6. Order of work

1. M2 node binary with authenticated peer transport and minimum health counters.
2. Script the fault-injection kit; dry-run every drill on two local processes.
3. Freeze a candidate root; run E*-L on all four platforms.
4. NUC+Pi process runs: E3-P1 first (it sets the budgets), then E1-P1..P3,
   E2-P1..P4, E3-P2..P5.
5. Operator-performed power cuts: E1-P4/P5, E2-P5.
6. Lost-voter repair implementation and E1-R before any live canary.

Pi runs are deferred until these final gates, not waived.
