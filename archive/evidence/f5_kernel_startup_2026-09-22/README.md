# CP-016 — verified startup is the kernel's only construction boundary

2026-09-22. Base HEAD d44a4a9, branch v2/phase-5-corrections. All remediation
remains uncommitted. This implements the already-approved token integration;
no additional durable, economic, peer or marker bytes were introduced.

## Failure, fix and scope

Before the change, the kernel accepted a raw node ID, directory, membership,
catalog and arbitrary authority domain. The independently verified startup owner
was optional: callers could bypass it. `api-red.txt` records a freshly compiled
constructor-availability test failing on that API. `membership-red.txt` reruns
the retained CP-015 pre-integration diagnostic binary (not a fresh compilation
this turn): compacted three-voter history became a one-voter leader. Its original
current-source compilation is recorded in CP-015. These are distinct RED sources.

The raw/default constructor is removed. `ReplicatedKernel` takes a move-only
`VerifiedIdentityStartup` by value, revalidates ownership, then stores it as its
FIRST member. Local ID, canonical voters, catalog and authority domain come only
from this token. Members initialize in declaration order, not textual initializer
order; destruction reverses declaration order, so the directory lock outlives
every store and writer, including constructor-failure unwinding. The kernel's
catalog is a const reference into its owned token, not into the short-lived
constructor parameter or caller. This avoids a second resident catalog copy.

Revalidation rejects a moved-from descriptor, already-replaced directory name,
or changed identity before any storage constructor runs. Identity input remains
bounded to 125 bytes. Member copying is bounded by three voters. These checks,
hashes and filesystem operations occur at cold startup, not per economic apply.
The diagnostic identity accessor is not a quorum proof or send permission.

Availability cost: invalid or mismatched identity, missing catalog and competing
ownership refuse startup; none is repaired by adopting history, dropping voters,
or inventing a fresh authority domain. A two-voter deployment still requires both
voters for quorum-backed new risk. User data is not deleted or rewritten to make
startup succeed.

## Permanent tests and migration review

- New `v2-kernel-startup-tests`: raw API absence, lock retention while kernel and
  writer live, persisted election-term restart, configured three-voter self-quorum
  refusal, moved-token rejection, replaced-path refusal, changed-file refusal and
  corrupt-metadata constructor unwind with lock reacquisition. The metadata case
  must actually reach the metadata reader; identity refusal cannot count as PASS.
- Regular replication CTest now includes `identity-membership`: commit and compact
  three-voter history, require typed `identity_mismatch` on one-voter recovery,
  assert unchanged names/bytes, and recover original applied index/state digest.
  The rebuilt standalone diagnostic also passes and shows no self-election.
- Fixtures explicitly bootstrap fresh directories and recover existing ones.
  Corrupt/unsupported history fixtures publish identity BEFORE planting damage.
  No helper detects existing files and silently chooses adoption/bootstrap.
- Missing catalog is now a typed pre-storage startup refusal; the old late
  missing-artifact construction test was removed because that state is no longer
  constructible. Bootstrap/recovery tests assert unchanged storage. Real
  before-apply coverage uses a valid alternate-catalog startup and a committed
  foreign-root install, so startup validation does not mask the guard under test.
- Main's first integration run caught a fixture calling bootstrap on copied
  power-cut history. It failed for unrelated unknown-content refusal; corrected
  explicit recovery preserves the missing-committed-history oracle. Review also
  moved wrong-lineage recovery after releasing the original directory owner,
  avoiding an unrelated lock-contention refusal.

Two bounded workers migrated disjoint test suites; main implemented/reviewed
production integration, added ownership tests and ran final regression. One worker
independently checked member/reference lifetime and performed the catalog mutant.

## Mutation evidence, not merely failure labels

All mutants are isolated source copies, compiled directly against current static
archives. Shared production files were never mutated. Exact patches/logs retained:

1. Remove ownership revalidation: `stale-path` fails because writable storage was
   created in the replacement directory, before checking the refusal enum.
2. Replace token voters with `{node_id_}`: `three-voters` fails because an isolated
   node actually becomes leader; the oracle is not the exposed identity getter.
3. Remove only before-apply catalog readiness: the migrated catalog suite fails
   because apply/state advance despite the unavailable dependency. Worker compiled
   this mutant; main reran it and independently inspected the isolated diff.

Later ownership cases and these mutants are retrospective coverage of the fix,
not falsely claimed as original production RED-first tests. Raw API RED and the
pre-existing A04 reproduction preceded production integration.

Direct compilation uses C++20, `-O1`, target cache line 128, include/tests and the
pinned Raft headers, then the replication/kernel/evaluator/contracts/core/Raft
archives. For a mutant, compile its isolated replication translation unit before
the archives. No shared CMake mutation/rebuild loop was used.

## Verification and remaining obligations

Built before CTest. Host Release **47/47**, Debug ASan+UBSan **47/47**; runtime
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`, existing C++ sanitizer
configuration, no new Raft-C instrumentation claim. Tools **40/40**, passive model
**109/109**, bytecode writing disabled. Independent required inventory **47 total /
44 V2**, including the new suite. Logs retained here. No target acceptance implied.

Release build: `/private/tmp/velocita-full-audit.lYB4P5/release`.
Sanitized build: `/private/tmp/aeron-phase67-audit-build` with runtime TMPDIR
`/private/tmp/f4-sanitized-runtime.IpVp53`. Configure used existing disconnected
dependencies; final builds used `-j4`. Working evidence/mutants:
`/private/tmp/f5-kernel-token.hV7xxL`.

`replacement-still-red.txt` and `archive-still-red.txt` are freshly compiled
current-source diagnostics, outside passing CTest: a fresh voter still cannot
fetch the compacted prefix, and compaction still removes the last exact command
without verified archive coverage. They remain failures, not accepted behavior.

The lock is advisory and storage still reopens by pathname. Revalidation detects
an ALREADY replaced directory; it does not atomically prevent replacement between
the check and a later store open. Trusted cooperative owners and stable ancestor/
deployment paths remain explicit assumptions, not hostile same-UID protection.
The sidecar does not bind independently substituted WAL/metadata/snapshot bytes,
authenticate peer frames, fence old executables, or prove publication after real
power loss. Further reviewed lineage envelopes, bounded queues, archive/catch-up,
async fences and target evidence remain F5/F6 obligations. Pi is deferred, not
waived. No venue, credentials, target, source freeze, commit or tag action occurred.
