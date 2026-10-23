# 0016. The strategy and the operator are separate callers; paper mode is level-triggered

Date: 2026-09-29
Status: Accepted. Approved by the owner as part of Phase 2 ("start with P2"; operator-adjustable
parameters; follow the book). Implemented in `~/.cache/velocita-work/edge` (118/119 ctest; the one
failure is fixed by the kernel copy; 223 model tests pass). Pending merge.

No byte contract change. It relies on the result detail from ADR 0015.

## Context

The shadow submitter held caller key 11, which maps to CallerId 9, inside the operator band 1..15.
It needed that key because its startup ran the setup: publish, validate and activate the policy,
stage and activate the executable bundle, then `SetStrategyMode normal`. After setup, the same
process and the same key sent every tick.

Two problems follow.

- **One worker proposed and approved.** Book ch49: "Do not let one worker both propose and approve
  a material limit change". Once limits became operator-only (ADR 0015), a strategy holding the
  operator key could still change them.
- **A restart re-armed a halted strategy.** Setup ran on every start and ended with
  `SetStrategyMode normal`. An operator's halt therefore lasted only until the next crash or
  restart of the edge.

Paper mode adds a third problem: the strategy has to bind each decision frame to the current
`risk_version` and `position_version`. Fills, which the gateway submits, advance the position
version, and the submitter never sees them.

## Decision

1. **Two callers.**
   - The tick loop runs as CallerId **21** (key 12), outside the operator band. It refuses an
     operator CallerId with exit 64, a configuration error that systemd must not restart.
   - Setup is a separate one-shot, `velocita-v2-shadow-submitter --setup-only`, run as the
     operator (CallerId 9, key 11) from its own unit `velocita-v2-shadow-setup.service`.
   - The gateway gets CallerId **22** (key 13; ADR 0017).
   - Each unit mounts only its own key file.
2. **Operator verbs in `velocita-v2-client`.**
   - `strategy-mode MODE KILL GEN` on the control lane.
   - `risk-limits key=value …`: every field is required once, and `reason_code` must be non-zero.
     It always prints a human summary (lots, USD, percent) before sending. Its payload digest is
     over canonical field order, so the same limits typed in another order are the same command.
3. **Paper mode (`--mode paper`) is level-triggered.** Every tick is evaluated authoritatively.
   When the committed decision is an `action` with a proposed resolution, the submitter sends
   `AuthorizeDecision`. The kernel is the risk authority, so it refuses, typed, whatever must not
   happen:
   - `position_limit_exceeded` when the account is already long;
   - `order_slot_occupied` while an order is open;
   - `control_halted` when halted.

   Outcomes are counted by code (`velocita_paper_authorizations_total{code}`).
4. **Re-sync from the refusal.** On `stale_risk_state` carrying the `risk_state` detail, the
   submitter adopts the carried versions for its next frame. The stale decision itself is never
   retried.

## Why level-triggered, not edge-triggered

- **Edge-triggered** (authorize only when the rule's action changes) needs the submitter to mirror
  the kernel's position. It fails badly when an order is killed: the signal no longer changes, so
  the trade is never retried.
- **Level-triggered** keeps one authority. The kernel's typed refusal is the right audit record
  ("why didn't it buy?" is answerable from the log). The cost is one refused authorization per
  tick while a signal persists, bounded by the 30 s throttle. Those receipts are reclaimed like any
  other.

## Why re-sync through the refusal, not a read API

- The submitter talks to the cluster only through committed commands. A read query would be a new
  protocol surface with its own consistency question: which replica, and at what index.
- A refusal is committed, so its versions are the kernel's state at that index. Costs:
  - one lost tick after each fill;
  - an exact retry returns the retained, older versions, so the submitter always sends a fresh
    request.

## Consequences

- **Operator key.** It still lives on nuc1 but is mounted only by the setup unit. Moving it off the
  host (for example, running setup from the Mac) is a later hardening step.
- **Rotating a key** now needs a restart of that unit.
- **Old cluster.** Policy 21 v1 is active with a 10-lot clip. The 25-lot clip needs
  `--policy-version 2` or the fresh cluster that ADR 0015 already requires.
- **Placeholder at merge.** The edge copy has a placeholder `kRiskStateDetailKind`. Replace it with
  `ApplyResultDetailKind::risk_state` (value 7, which matches).

## Amendment 2026-10-07: two practices introduced by the stochastic paper run

**Status: the first is recorded and bounded; the second is decided (below).** Found by the independent review
(`docs/evidence/dual_indicator_paper_2026-10-07/INDEPENDENT_REVIEW.md`, M3 and M4).

### 1. The edge reads the gateway's venue report

This ADR chose "the submitter talks to the cluster only through committed commands". The
indicator edge (`model/src/velocita_model/shadow/indicator_paper.py`) additionally reads
`venue.json`, a file the gateway writes twice a second, and uses it to:
- send an exit only when the venue holds a position, and an entry only when it is flat;
- stop buying on drawdown, order capacity or insufficient cash;
- keep its fill ledger.

**Why it was done.** The kernel holds lots but no cash, fees or fill prices (ADR 0021 is not built),
so the drawdown stop has nowhere else to read them.

**How it is bounded.** The report is accepted only if it names this incarnation, account and
venue, is at most 5 seconds old, has no pending reports, and its cash, lots, fees and volume
equal the sum of its own order list. Anything else is "unreconciled", and the edge then holds: it
neither buys nor sells. The kernel's risk limits still bound every order.

**What it costs.** Position now has two sources of truth for the edge (the kernel through
refusals, the venue through this file), and a stale or missing report freezes an open position
until it clears. `VelocitaPaperAccountUnreconciled` alerts on that.

**It ends** when ADR 0021 puts cash and P&L in the kernel; the edge then reads committed state only.

### 2. Operator commands were issued by a script on a voter host

`deploy/v2/indicator-paper/control.py` (action `ceremony`) sent the kill epoch, executor
generation, catalog install and risk limits as the operator (CallerId 7), using the copy of
`caller-9.key` that every voter holds, from nuc1.

Until then operator commands came from the owner's Mac, with the key read from the Keychain.
The kernel cannot tell the two apart: the keys are symmetric, and every voter must hold every
caller key to verify it. So ADR 0015's "operator-only" has always meant "anyone who can read a
voter's secrets directory"; this run is the first time that was used.

**Owner decision, 2026-10-07: operator commands stay on the Mac for now.**
- `control.py` no longer sends them. Its `ceremony` action prints the four operator commands for
  the owner to run from the Mac with `velocita-v2-client --key-stdin`, and exits 64.
- The `setup` action is unchanged: it runs as the set-up caller (key 11), which this ADR already
  assigns to a one-shot unit on nuc1.
- This is a rule the tooling follows, not one the kernel enforces: every voter still holds the
  operator key, because verifying a symmetric key needs the same bytes as signing with it.



## Amendment 2026-10-15: explicitly granted direct spot-swap intents

Status: owner approved implementation and testing; implemented locally, deployment and activation
pending committed native qualification and review. This applies to the ADR 0025 swap path, not
the legacy DecisionFrame rule program.

The live driver uses strategy caller 21/key 12, but the outer kernel allowed `swap::Intent` only
from operators. Its first two requests were committed refusals at indices 13/14. The old
continuous drill used an operator outer identity, hiding that mismatch. Granting every strategy
caller permission through a code-only whitelist change would change those historical replay
results. Giving the driver an operator key would violate this ADR.

The operator can instead commit one `swap::GrantStrategy` while the initialized account is halted
and has no effect history or pending exposure. It names an already assigned strategy caller,
minimum request sequence, original opening digest, review-evidence digest, kill epoch and executor
generation. Absence of a grant preserves the old permission checks and encoded snapshot bytes.
Only that caller at or above the sequence floor, with the current grant epochs, gains permission
to propose direct swap intents. For the stopped live driver the proposed floor is 3; requests 1/2
must not be reissued. The operator must verify the persisted driver sequence before choosing the
floor because unauthorized refusals do not advance the caller receipt cursor.

Funding, price amendments, controls, catalog changes and gateway evidence remain unavailable to
the strategy. Existing swap amount, balance, source-age, deadline, mode and loss checks still run;
the signer independently verifies each transaction. The Python driver decides the signal: this
permission does not prove its stochastic calculation or replicate that calculation in the kernel.
Existing gateway authority proofs authorize execution stages; they do not grant strategy intent
admission and are unchanged by this amendment.

A kill-epoch or executor-generation advance invalidates the grant. This first-launch grant is
one-time; no renewal, replacement or cross-account authority is introduced. A later restart that
requires new epochs must remain stopped until an explicitly designed renewal exists. Ordinary
halt/normal mode changes do not replace the grant. Recovery can still settle an already admitted
order under the existing reconciliation rules.

The swap command adds kind 10 and the snapshot adds ordered optional extension tag 2 (73 bytes).
Old records and states with no grant retain their bytes. Old readers cannot consume a granted
state: upgrade all three voters and the gateway/signer/client before committing it, while halted
and preserving WAL, funding and journals. Do not roll back to an old reader after activation.
`swap::State` grows from 4088 to 4160 bytes; its local assertion is adjusted accordingly. Total
`TradingState` is 78512 bytes on the qualified-local Mac layout, within the unchanged 81920-byte
budget. The reducer remains fixed-capacity; target-role validation scans at most eight callers.

[Implementation, evidence and next steps](../../archive/evidence/automated_launch_2026-10-15/strategy-grant/README.md).
