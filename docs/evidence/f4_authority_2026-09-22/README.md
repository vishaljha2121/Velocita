# CP-009: F4 authority contract review and retained REDs

2026-09-22, branch v2/phase-5-corrections, HEAD d44a4a9 plus uncommitted CP-008
and earlier remediation. **F4 OPEN; design proposed, no production fix encoded.**
The owner asked to tackle F4, not to waive material-protocol review.

`authority_red.cpp` updates the audit's causal trace for schema 8 using the real
public replicated installation fixture and explicit verified catalog. It does
not modify the historical September 21 probe or pretend that probe runs unchanged
after the catalog prerequisite. It is intentionally outside passing CTest until
the protocol is approved; move/extend these assertions into the composed owning
suite during implementation. Do not weaken the expected refusal to make it green.

## Reproduction

Build first from repo root (cached dependencies already configured offline):

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release --target v2-replication-tests v2-gateway-tests -j2
f4_build=/private/tmp/velocita-full-audit.lYB4P5/release
f4_run=$(mktemp -d /private/tmp/velocita-f4-red.XXXXXX)
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Ibuild/_deps/willemt_raft-src/include \
  docs/evidence/f4_authority_2026-09-22/authority_red.cpp \
  "$f4_build/libvelocita_v2_replication.a" "$f4_build/libvelocita_v2_kernel.a" \
  "$f4_build/libvelocita_v2_evaluator.a" "$f4_build/libvelocita_v2_contracts.a" \
  "$f4_build/libvelocita_core.a" "$f4_build/libwillemt_raft.a" \
  -o "$f4_run/authority-red"
"$f4_run/authority-red" partition "$f4_run/partition"
"$f4_run/authority-red" delayed "$f4_run/delayed"
```

Run failures separately; exit 1 is expected. Exit 2 is argument/path misuse.
The fixture refuses an existing voter directory rather than deleting it.
Observed execution directory: `/private/tmp/velocita-f4-red.bgg61a`.

Actual assertion output, each exit 1:

```text
majority_kill=2 stale_kill=1 renewed=1 permitted=1
RED: stale leader renewed permission after majority kill

captured_at=1 received_at=10000 bound=50 renewed=1 permitted=1
RED: delayed old reply acquired fresh send permission
```

These assert permission from the composed public gateway admission API; there is
no live transport. Three voters model a minority old leader versus a majority
replacement, NOT the two-host deployment's availability. The second test uses
two voters, captures a currently healthy read and then partitions before its
delayed consumption. Ticks are synthetic and have no deployment milliseconds.

The test would not be meaningful if it omitted the catalog and failed earlier
for missing metadata. The explicit context keeps the RED about authority.

## Review outcome

Main reviewed ADR 0001, the F4 audit, plan Phase 8, architecture sections 9/11,
the gateway and replication boundaries and pinned Raft election/read support.
Two bounded read-only agent reviews challenged proof production and consumption.
Both favor a request-correlated proof; the replication review independently
favors a committed non-economic barrier for MVP over new ReadIndex machinery.
The transport review also identified the cancel-versus-fence exception and
unavoidable in-flight boundary; both are explicit proposed decisions, not hidden
in implementation. Main additionally records the clock-check-to-write pause gap.

Proposed ADR 0013 and `../../F4_AUTHORITY_PROTOCOL_REVIEW.md` contain the choices,
alternatives, C++ ownership, costs and staged acceptance tests. No material
bytes, schema constant, production behavior or accepted guarantee changed.
No target/venue calls, trading, credentials, commit, tag or protected-file edit.
Freshly rebuilt existing replication/gateway regression suites pass **2/2**
(1.24 s); diff check clean. No full-suite or sanitizer rerun is claimed for this
documentation/diagnostic increment. CP-008 remains the last full host checkpoint.
