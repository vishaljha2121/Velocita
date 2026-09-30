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
