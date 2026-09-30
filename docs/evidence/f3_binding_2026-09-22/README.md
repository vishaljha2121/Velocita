# CP-008: approved catalog installation, binding and recovery integration

2026-09-22; base d44a4a9, branch v2/phase-5-corrections, uncommitted remediation.
Host evidence only. Not a source freeze, target gate, venue verification or live
deployment authorization. See ADR 0012 and architecture section 10.18.14.

## What changed and why

- Added kind 20 with the approved 48-byte body (root32 and two u64 control
  expectations), five appended refusal codes, and one 32-byte replicated root.
  First install is halted/operator-only/before economics; replacement refuses.
  Same-root retry uses existing identity and control rules.
- Switched command/result/snapshot schemas together to 8. This prevents old WAL
  commands from acquiring new catalog-dependent outcomes in the same lineage.
  Old storage is preserved; no converter or root backfill exists.
- Made catalog context explicit in reducer/replay APIs; replicas own a const
  verified value. Missing required bytes are a local exception, not an economic
  result. Once installed, even control commands require the matching context.
- Added optional pinned Raft before_apply, before the applied-index increment.
  Refusal in applylog alone was too late. No exception crosses the C boundary;
  the node latches serving closed. Diagnostic reads and writer cleanup remain.
- Added exact row checks at staging/authorization, historical bound effect
  materialization and gateway admission. The returned metadata is copied, not a
  borrowed pointer into a compactable arena. OrderEffect bytes are unchanged.
- Added snapshot semantic prepublication checks; a valid checksum/root does not
  prove the retained profiles agree with that catalog. Missing or contradictory
  metadata refuses startup without changing existing evidence filenames/bytes.
- Preserved sweep-before-borrow and previous lifecycle logic. Fixture migration
  passes explicit contexts; the fixture apply helper never installs one behind
  the test's back. Replication fixtures install through committed commands.

## RED evidence and review corrections

These are tool-observed assertion excerpts, not fabricated full raw logs:

1. Optional Raft hook absent: exit 134, `before-apply refusal was not propagated`.
   Moving it after the index increment: exit 134,
   `before-apply refusal advanced last_applied_idx`. Direct refusal, periodic,
   begin-snapshot and NULL callback tests pin the boundary.
2. Rootless economics snapshot: `snapshot accepted economic state without an
   execution-catalog root`; structural validation now rejects it.
3. Snapshot row semantics, selected `v2-replication-tests catalog-snapshot-binding`:
   exit 1, `snapshot published metadata that contradicts its installed catalog`.
   Fixture builds an authorized state through public replicated commands, then
   deliberately corrupts the active maximum price while retaining a valid schema
   and checksum. This is a corruption test, not a claimed reachable reducer state.
   Startup now refuses and exact directory image is unchanged.
4. Rootless install admission: `receipt-capacity refusal incorrectly required
   candidate artifact`. Dependency lookup must respect deterministic capacity
   refusal before requiring candidate bytes. Installed-root gating is unchanged.
5. Initial installation RED used a marker root without an actual verified context;
   that fixture did not satisfy the final contract and is NOT sufficient proof.
   The corrected verified-context test kills an install-root-removal mutant at
   `valid installation did not persist catalog root` (exit 1).
6. Deleting standalone apply's dependency guard: exit 1,
   `missing artifact changed reducer state or produced a result`.
7. Restoring snapshot root-only validation, directly compiling mutated
   src/v2_replication.cpp: exit 1 at the same snapshot semantic assertion in (3).
8. Gateway bounds, deliberately ordered to avoid undefined behavior in RED:
   exit 1, `find_attempt accepted an oversized attempt count`. An earlier test
   ordering segfaulted on the old unchecked loop; that crash is not substituted
   for the intended assertion. Final tests cover capacity+1 and SIZE_MAX.
9. Deleting the rootless install admission preflight and directly compiling
   mutated src/v2_kernel.cpp against the final public-command capacity fixture:
   exit 1, `receipt-capacity refusal incorrectly required candidate artifact`.

The initial receipt-capacity draft hand-built rootless applied-fill records that
the reducer cannot create. Main review rejected that fixture. The final receipt
and caller saturation tests use public control/acknowledgement/source commands
and require canonical snapshot encoding. The uint64-max version case is a named
synthetic boundary, not a claim to have replayed 2^64 commands.

Mutant sources/binaries were isolated under
`/private/tmp/velocita-cp008-mutants.vV5bie` and
`/private/tmp/velocita-cp008-snapshot-mutant.HZLW6X`; production files were not
mutated for those runs. These are targeted kills, not exhaustive mutation proof.

The synthetic gateway/lifecycle fixture previously declared only place-limit
while exercising cancels. It now declares place-limit | cancel consistently.
This corrects synthetic facts; it makes no real venue capability claim. A
separate place-only catalog test proves cancel admission refuses. Historical
effect binding is independent of later active-policy changes; snapshot restore,
wrong roots, missing exact rows and dependency lifetime are tested separately.

The legacy replay/model golden is preserved by an explicit schema-7 projection:
remove only the appended root and restore old framing before comparing the old
answer. New schema-8 golden framing is independently derived by
`golden_schema8.py`, not copied from the C++ encoder output. This keeps prior
field/behavior regression evidence while independently checking the new format.

## Coverage and ownership

`v2-catalog-binding-tests` covers install/retry/conflict/control/occupancy guards,
missing context before state mutation, WAL and snapshot recovery, uncommitted
suffix behavior, peer-driven committed missing dependency, serving latch, owned
catalog lifetime and unchanged files on startup refusal. The dedicated hook
suite covers implicit Raft entry points. Codec/snapshot tests pin kind, codes,
canonical bytes and root retention. Policy/risk/gateway suites cover row binding.
The lifecycle enumerator now requires catalog validity for every canonically
encodable explored state; Phase-5 snapshot scenarios also validate the restored
catalog bindings. This extends positive coverage beyond a fresh pending submit.

The standalone materializer is pure reconstruction, not send permission. The
bare committed-outbox view is diagnostic and not a bound sender envelope.
F4's authority protocol is still open; local leadership is not a read-index
proof. A bound envelope is local typed data, not authenticated cross-process IPC.

## Verification

Final rebuilt Release **38/38** (2.75 s), Debug ASan+UBSan **38/38** (39.60 s),
with runtime UB halt and LeakSanitizer disabled. Offline tooling **40/40**,
passive Python model **109/109**. Actual CTest inventory matched the independent
gate list: **38 total / 35 V2**. Final CTest output is in `release-tests.txt` and
`sanitized-tests.txt`; `ctest-inventory.json` records host paths, not target ones.
Independent schema-8 hashes match the literals in the codec/snapshot suites.
Host cache-line-128 layout compile: TradingState 81,392, snapshot maximum 65,483,
verified context 8,488 bytes. No target layout/latency claim.

Interim Release 38/38 passed; an intermediate sanitizer run correctly
went RED when it picked up the new capacity regression before its fix. Do not
reuse either intermediate run as evidence for the final tree.

Recipes (distinct TMPDIR per concurrent test run avoids legacy fixture collisions):

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target velocita-all-tests -j2
TMPDIR=$(mktemp -d /private/tmp/velocita-cp008-release.XXXXXX) ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j2
TMPDIR=$(mktemp -d /private/tmp/velocita-cp008-sanitized.XXXXXX) ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
python3 docs/evidence/f3_binding_2026-09-22/golden_schema8.py
```

No TSan rerun, power-cut proof, image build, target measurement, source pin,
network/venue call, commit or tag change. LeakSanitizer is disabled; runtime UB
halt is not a claim about compile-time nonrecovering flags. Both unbound body
encoders remain quarantined. F3 real encoder/venue admission and F4/F5 contracts
remain open; F6/F7 prerequisites remain partial. Pi is deferred to final gates.
