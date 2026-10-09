# 0020. Commands are authorized by caller role, not only authenticated

Date: 2026-10-01
Status: Accepted and built 2026-10-08 (owner: "continue with the plan", live promotion plan item 6).
Not deployed: it changes the command and snapshot formats (schema 11), so it needs a new incarnation.
Origin: architecture review, `docs/ARCHITECTURE_REVIEW_2026-10-01.md` finding A1.
**Blocking for live trading.**

## Context

A command reaches the kernel through two checks:

1. **Authentication**, at the node (`src/v2_node.cpp`, `receive_ingress`).
   - The envelope must verify under the sending key.
   - The command's `caller_id` must equal the CallerId configured for that key.
2. **Authorization**, in the reducer (`src/v2_kernel.cpp`, `apply`). Only five control commands
   are restricted to operator callers (≤ 15): `AdvanceKillEpoch`, `SetExecutorGeneration`,
   `SetStrategyMode`, `InstallExecutionCatalog` and `ConfigureRiskLimits`.

Every other command kind is accepted from **any** authenticated caller. That includes:

- `ObserveFill` and `ObserveOrderEvent`. This is venue evidence, and it moves the kernel's position
  and releases reservations.
- `PublishPolicy`, `StageExecutablePolicy` and `ActivateExecutablePolicy`. These replace the
  trading program.
- `RequestCancel`, `RequestReplace`, `RequestMassCancel` and `AcknowledgeGatewayResults`.

Three callers exist today (ADR 0016, 0017):

| Caller | Who | Runs where | Should send |
|---|---|---|---|
| 9 | operator (setup, `velocita-v2-client`) | operator shell, setup unit | everything |
| 21 | strategy tick loop | edge container on nuc1, next to Python that parses internet JSON | `RecordSourceEvent`, `EvaluateDecisionFrame`, `AuthorizeDecision` |
| 22 | gateway | gateway container on nuc1 | `ObserveFill`, `ObserveOrderEvent`, `AcknowledgeGatewayResults`, cancels it must issue |

Key 12 (caller 21) is mounted into the edge container, so any code running there can read it.
A bug or a compromise in the edge could therefore commit a **fabricated fill**. The kernel would
then believe a position that does not exist, or that a real position is flat. Its risk limits,
the only replicated safety, would be computed from false evidence. ADR 0016 separated the callers
so that the strategy could not change limits, but the separation stops at those five commands.

## Decision

1. **Roles live in replicated state.** Add a small role table to `TradingState`: `CallerId` →
   role, with roles `operator`, `strategy`, `gateway`.
   - Operator callers (≤ 15) are implicitly `operator`.
   - Other callers get a role only through a new operator-only command, `AssignCallerRole`, and
     a caller can hold only one role.
   - The table is bounded (`kMaxCallerCursors` entries; 8 today).
2. **A static allowlist per command kind**, checked in `apply` next to the existing
   operator-only check, before any receipt is written (so a refusal leaves no trace, as today):

   | Command | operator | strategy | gateway |
   |---|---|---|---|
   | control (the five above), `AssignCallerRole` | ✔ | | |
   | `PublishPolicy`, `ValidatePolicy`, `ActivatePolicy`, `SupersedePolicy`, `StageExecutablePolicy`, `ActivateExecutablePolicy` | ✔ | | |
   | `RecordSourceEvent`, `EvaluateDecisionFrame`, `AuthorizeDecision` | ✔ | ✔ | |
   | `ObserveFill`, `ObserveOrderEvent`, `AcknowledgeGatewayResults` | | | ✔ |
   | `RequestCancel`, `RequestReplace`, `RequestMassCancel` | ✔ | | ✔ |

   A refusal returns `ApplyCode::unauthorized_command` (a new code). The operator is deliberately
   **not** allowed to forge venue evidence either: an operator correction goes through
   reconciliation, not through a hand-typed fill.
3. **The dispatch becomes exhaustive.** Replace the `std::get_if` chain in `apply_new_command`
   with `std::visit` over an overload set, and make the allowlist a `constexpr` function over the
   variant index. A new command kind then fails to compile until someone decides its roles
   (review finding C4).
4. **The node keeps checking authentication only.** Authorization stays in the reducer so that it
   is replicated, deterministic, visible in the log, and covered by replay tests.

## Consequences

- **Byte change.** The codec, snapshot and identity schemas each bump: the role table, a new
  command and a new ApplyCode. Deploy is a fresh cluster or a stop-all restart (as for CP-076).
  The snapshot grows by about 8 × 9 bytes; see ADR 0022 for the budget.
- **Setup gains one step.** It assigns roles 21 → strategy and 22 → gateway before activating the
  strategy.
- **Blast radius after this change.** The strategy key can still propose and authorize trades
  within the kernel's risk limits. That is its job, and the limits bound it. It can no longer
  rewrite the facts that the limits are computed from.
- **Tests.** Write a table-driven test over every (role × command kind) pair, plus mutation checks
  that each ✔/blank in the table is load-bearing.

## Alternatives considered

- **Allowlist at the node, from config.** Rejected. It is not replicated, a single voter with a
  bad config would diverge in what it admits, and it is invisible to replay.
- **Separate Aeron channels per role.** Rejected. Transport topology is not an authorization
  model, and it still leaves the reducer trusting whoever arrives.

## As built (2026-10-08)

- **Where the role lives.** There is no separate table. Each caller already has one entry in the
  caller cursor table (8 entries), and the role is one more byte on that entry. A caller with a
  role therefore always has a cursor, and the bound is the same 8. The snapshot grows by 8 bytes.
- **Assigning a role.** `AssignCallerRole{caller, role}` from an operator caller. The same role
  again answers `already_applied`; a different role answers `caller_role_conflict`; an operator
  caller, caller 0 or an unknown role is `invalid_command`; no free entry is
  `receipt_capacity_exceeded`. A role cannot be removed or changed inside an incarnation.
- **The check.** `apply` computes the roles allowed for the command and the role of the sender and
  refuses with `unauthorized_command` when they do not meet. The refusal writes no receipt and
  creates no cursor. The five control commands from a non-operator keep their older code,
  `unauthorized_control`.
- **Exhaustiveness.** The allowlist is a `std::visit` with a `static_assert` in its last branch, so
  a new command kind does not compile until its roles are chosen. The dispatch in
  `apply_new_command` is still a `std::get_if` chain; part 3 of the decision is done only for the
  allowlist.
- **Client.** `velocita-v2-client ... assign-role CALLER_ID strategy|gateway`.
- **Ceremony.** After `install-catalog`: `assign-role 21 strategy`, then `assign-role 22 gateway`.
  Until both are committed the strategy's bars and the gateway's reports are refused.
- **Tests.** `tests/v2_kernel_control_tests.cpp` holds the table (22 commands × operator, strategy,
  gateway, a known caller with no role, an unknown caller) and the assignment rules. Codec and
  snapshot tests cover the bytes. 34 mutants: 31 killed, 3 equivalent (one unreachable branch, two
  decode checks that a later validation repeats).
- **Consequence for tests and tools.** Evidence can no longer be sent with an operator key. Every
  fixture that did so now uses a caller holding the gateway role.
