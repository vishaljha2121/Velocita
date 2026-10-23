# Ready-to-paste Claude takeover prompt

Start in `/Users/vishaljha/aeron-raft-pi`. Review and continue the Velocita V2 live-swap
checkpoint. First read local repository instructions and inspect Git status/diffs; preserve
all existing dirty/untracked work. Branch is `v2/phase-5-corrections`; expected committed
HEAD is `2e5642a30da3c9cea7e094afc1cbd6a20b2d6e33`. Reverify rather than assuming it stayed so.

Read in this order:
1. `docs/CURRENT_HANDOFF.md` (October14 entry, older entries are historical).
2. `docs/handoffs/LIVE_SWAP_REVIEW_2026-10-14.md` — detailed chronology, commits, operational
   commands/evidence, exact state, guarantees, deferrals and review targets.
3. `docs/adr/0025-bounded-swap-verification-and-policy-bound-recovery.md` — implemented record.
4. `docs/adr/0026-stable-execution-policy-and-per-attempt-route-evidence.md` — proposal only.
5. Latest section and current table in `docs/LIVE_PROMOTION_PLAN_2026-10-07.md`, plus linked
   `invariant/qualification` results and live-trading journey status addendum.

Current checkpoint: one three-voter cluster, incarnation `b79e65be1ea14e219016ec48ea02efef`,
healthy/halted at last check, voters on `1a46fd8`. Live gateway/signer are stopped and
startup-blocked. The installed live manifest is `fe54001` (Byreal); qualified `2e5642a`
is staged, NOT adopted. No cluster live buy has executed. Funding and protected journals
already exist: never reset or initialize them again. Historical owner CLI trades are
not cluster end-to-end proof. Refresh time-sensitive state before acting.

The immediate objective is ONE supervised SOL buy with $1 total USDC debit, conservative
maximum extra output deduction 2,039,280 lamports, then reconciliation and stop. Preserve
$0.50 realized-loss bound and original price cap; the optional amendment is not applied.
The owner accepted this bounded path to minimize development. The earlier $25 preference
does not authorize repeated buys, continuous strategy trading or relaxing the verifier.

Captured Invariant passes; subsequent fresh quotes selected unsupported Meteora38 and
Deriverse161. Both refused before signing. Do not repeat quotes until a route happens to
pass. Investigate the architecture: route/account/lookup/expiry evidence currently enters
persistent policy/runtime/journal identity, causing repeated adoption. Separate removal
of that identity coupling from the much harder claim that arbitrary DEX routes are safe.

First deliver a holistic C++/architecture review: concrete failure modes, code/evidence
pointers, what is proved versus assumed, minimal design, alternatives and acceptance tests.
Explain the problem, why it matters and solution intuition in plain developer language,
then cover ownership/lifetimes, bounded memory, integer economics, concurrency and recovery.
Review ADR0026 before implementing a changed trust boundary; this prompt does not approve
that proposal. Preserve exact payload/intent/policy binding and independent signing checks.
Explicitly revisit deferred deployed minimum-output rounding before broad aggregator trust.

Continue only the approved scope after review. No additional features unless required for
correctness. No new money movement authority is granted by this handoff. Non-moving POST
checks are allowed; preserve bounded protocols and stop on ambiguity. Never bypass failed
verification. Keep trackers updated and retain historical evidence.

Build/deploy committed source; commit only when asked, stage explicit owned paths and
never whole shared dirty files. One cluster only. Voter/exporter restart together; shipping
source requires monitoring mount refresh. No reset/clean/stash, secret logging or Safari
screen sharing. Private captures stay private. Use durable scratch, not /tmp. Work inline
unless I request agents; then at most two bounded agents, with shared core edits owned by
the main agent. The detailed handoff lists protected paths, service identities, preserved
journals, launch ordering and existing prepared-only scripts—read those before any action.
