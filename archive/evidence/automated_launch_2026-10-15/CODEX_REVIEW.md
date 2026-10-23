# Post-handoff review — 2026-10-15

**Verdict: retain the halted deployment; do not follow the start-to-six-order drill yet.**
The credential problem is real, but fixing it alone leaves a one-effect production execution
path behind a multi-effect strategy driver. This is a focused correctness review of commits
`997742b` through `12d0be6`, against the prior `2e5642a` handoff. It is not a claim that every
line of all 122 changed files received a full security audit.

## Findings, ordered by launch impact

### F1 — P1: production execution still terminates after one settlement

`src/v2_swap_gateway_runtime.cpp:59–63` completes/stops after successful resolution;
`:100–104` also stops when the submission journal is resolved. In committed
`src/v2_gateway_main.cpp:622–658`, stopped means the process exits. The service profile uses
`Restart=no`. There is no production transition back to awaiting the next strategy effect.

This is more than an exit flag: `SwapProductionPorts` retains process-wide `created_` and
`executed_` latches (`src/v2_swap_runtime_production.cpp:165,179`); the signing connection
requires an idle signing-request journal for a new sign (`src/v2_swap_signing_connection.cpp:56–59`);
`SwapSignerSession::start` requires idle status (`src/v2_swap_signer_session.cpp:165`).
No reviewed terminal transition rearms all of these for the next effect. Starting the old
journals again selects recovery, not a fresh sell. Simply deleting a journal, changing
Restart or removing a guard is incorrect.

**Consequence:** a successful buy can leave managed SOL held, with no automatic execution
path for the strategy's exit. The six-order drill and unattended run are not operationally
implemented despite the broader envelope and new driver.

**Minimal fix:** explicit per-effect lifecycle advancement only after finalized settlement
is committed and reconciled, with monotonic IDs/sequences, retained replay evidence and
independent signer authorization. Make per-effect latches per-effect without permitting
another send for an ambiguous old effect. Recovery must finish the old effect first.

**Required proof:** production-composition fake venue executes buy/sell/buy/sell/buy/sell
without journal reset, with exactly six sends and flat inventory; restart at transition
boundaries cannot replay a send or forget a reservation. Existing runtime tests explicitly
assert `complete` after one effect, and the eight process cases assert one send/settlement.
They do not cover the new operational goal.

### F2 — P1: retirement removes required caller keys; verification misses them

`retire_installation.py:52` moves the entire credentials directory. The new credential
handoff restores the TM credential leaves, not the Raft `caller.key` leaves.
`verify_installed.py:38–59` checks those TM leaves but never the caller keys, although
`deploy/v2/swap-runtime/service_launcher.py:186` opens each required caller key first.

Owner readback now confirms both active caller keys missing, retired keys still present
with UIDs/GIDs 29001/29002, mode0600, one link, 32 bytes. No recovery files were printed.
The signer log reports the generic protected-resource validation refusal and exit64.
This confirms a startup blocker, not that no later startup failure can exist.

**Minimal fix:** owner restores exactly the retained keys after checking destinations are
absent; preserve their metadata and tmpfs location. Change retirement to preserve caller
keys and verify both keys, directories, role accessibility and structural constraints.
Use the existing private-file checks and key parsing rules; never log key contents.
Add tests where a caller key is missing, wrong-role/wrong-mode, linked or malformed.
Do not rerun arm, funding or the retirement procedure. Keep launch stopped during F1/F3 work.

### F3 — P1: old market data is relabeled as fresh decision data

`live_swap_driver.py:60–81` checks candle continuity and observation freshness but never
candle age against wall time. `live_swap.py:35–46` sets the intent's source time to `now_ms`,
not the actual candle source time. The kernel therefore cannot detect a stale feed from
that timestamp. A socket backlog or slow consumer can remain contiguous while stale.

**Reproduced on committed code:** a candle 300,000ms old, accompanied by a current account
observation and an entry flag, produced a buy; its intent source age was 0ms.
See [reproduction](codex-review/stale-candle-reproduction.json). No network or trade was used.

**Minimal fix:** enforce bounded closed-candle age/future skew before sending, preserve
actual source timestamp in the intent, and re-bootstrap rather than trade a backlog.
Keep decision/deadline timestamps separate where required. Add delayed contiguous-feed
and future-candle tests, not only missing-bar and stale-account tests.

### F4 — P1 before unattended extension: the order ceiling can prevent an exit

`LiveRule` allows a buy while `orders == 239` and then forces a sell once `orders >= 240`
(`live_swap.py:77–94`). The driver uses `last_effect`, which includes released/failed attempts,
not successful buy/sell parity. `swap_live_scope` rejects `state.last_effect > 240`
(`src/v2_swap_transaction_review.cpp:30`).

**Reproduced on committed driver:** flat at effect239 -> buy240 -> held at240 -> sell241.
The last request cannot pass signer scope once committed. One expired/released earlier
effect is sufficient to make this parity possible. The six-order drill similarly can
ask for a seventh effect to flatten, contradicting a strict six-order description.
See [reproduction](codex-review/order-cap-reproduction.json).

**Minimal fix:** reserve exit capacity before opening a position and define whether the
reviewed ceiling counts attempted effects or finalized orders. Keep the current authority
bounds; do not silently authorize order241. If exit attempts fail, halt with explicit
remaining exposure rather than promise guaranteed flatness. Test odd counts, released
attempts, failed settlements, final-slot buys and final-slot exits.

### F5 — P2, required before unattended operation: expiry is a cutoff, not a drain

ADR0029 says services close at the window end with no effect in flight. Committed
`src/v2_gateway_main.cpp:623` throws on review expiry regardless of pending phase, while
`:570` stops the signer. The driver has no review-end field or pre-expiry stop policy.
A decision shortly before expiry can therefore leave a reservation, uncertain submission
or inventory. Recovery token minting also ends with the review window.

The existing fail-closed behavior preserves the need for recovery; it does not establish
flat inventory or completed settlement. This is not proof that funds are lost.

**Minimal fix:** inhibit new entries before expiry with a reviewed time margin; provide
an explicit supervised exit/reconciliation deadline and preserve recovery authority only
within its reviewed contract. Do not widen signing validity implicitly. For the supervised
six-order run, an owner-controlled early stop with pending/position checks can be the MVP
procedure. Before unattended operation, automate and test that procedure or narrow the
run guarantee. Include pending and held-position expiry cases.

## Work that materially advanced the project

| Commits | What changed | Review interpretation |
|---|---|---|
| `997742b` | Stable policy, per-attempt lookup evidence, role-based transaction verification | Removes repeated captured-pool adoption. ADR0026 explicitly accepts gateway-fetched lookup evidence and five compiled program contracts; this is a material trust change, not unchanged independent provenance. Rounded-down arithmetic is conservative under the assumed program contract, not verification of deployed bytecode. |
| `6c54f3f`, `1b03add` | Fresh balance/authority projection, durable unsent release, bounded finalized-read retries | Addresses real one-buy failures without blindly resending an ambiguous operation. Read retries differ from executing an order again. |
| `b6128d2` | Sixteen-entry settlement window and lifetime counter | Bounded memory; older totals no longer derivable from state alone, explicitly documented. New incarnation required and used. |
| `d7d4b9d` | Sell transaction inspection and accounting | Necessary for round trips; isolated sell support is not multi-effect gateway lifecycle support. |
| `8903fb5`, `538215e` | Wider owner envelope and stochastic driver | New limits and strategy path exist, with deterministic sizing and persisted request sequence. F1/F3/F4 prevent calling it ready for unattended execution. |
| `54a5fcb` | Signer-mediated TM token authentication, day-long review window | Keeps private signing material out of gateway. Authentication request shape is constrained; live token-mint path has not yet passed this new startup. F5 limits the expiry claim. |
| `828b65b` through `12d0be6` | Native release packaging, new-cluster ceremony, funding, adoption and startup tooling | Significant deployment work with preserved archives. Caller-key verification gap caused current refusal; fix it rather than repeat the ceremony. |

Owner reports the earlier bounded buy succeeded. The new handoff/managed-opening balances
are consistent with that report. This review did not fetch a fresh on-chain receipt or
independently re-reconcile that historical trade. Its success is valuable one-effect
integration evidence; it does not demonstrate repeated strategy round trips.

## Checks performed and their limits

- Compared committed changes from `2e5642a` to `12d0be6`; inspected relevant ADRs,
  verifier/signer, gateway/recovery, driver and launch code. Existing shared dirty gateway
  and CMake edits are outside the reviewed committed release and remain untouched.
- Ran 24 live-rule/driver tests, four driver-staging tests and six retirement tests from
  an isolated `git archive 12d0be6`: **34 pass**. Passing tests do not cover the findings.
- Reproduced F3/F4 offline against that archive. Retained native record reports 58 selected
  CTests and eight process cases on each architecture at `54a5fcb`; not rerun this review.
- Read host services: signer failed64, gateway inactive, both manual starts currently
  permitted because arm was already applied. Read all three voter exporters: up/ready,
  nuc2 leader. Exporter status is not an authenticated authority/sequence proof.
- Owner supplied protected key metadata and journal-directory check. No sudo, private key
  reads, venue requests, signing, funding, key moves or service mutations by this review.
- [Verification record](codex-review/verification.json). No production code changed or committed.

## Corrected next-step plan

1. Leave mode halted and driver stopped. Preserve new incarnation, funding, operator sequence
   history and archives. Do not rerun ceremony/arm or delete state. Treat sequence7 as recorded
   next sequence, subject to fresh authenticated verification before any operator command.
2. Fix F1–F4 as the smallest correctness batch; include F5 cutoff behavior needed for the
   intended run. Restore the two caller keys under the owner's sudo boundary, and test the
   installer check. Restoring keys is not permission to start the strategy.
3. Commit only explicitly owned changes when requested; qualify that build and exercise
   real production composition across multiple effects and recovery boundaries. Preserve
   original one-effect and fault tests. No new cluster/reset is indicated by these findings.
4. Refresh reviewed account/documents if needed; current window ends 2026-10-16 13:48:34 UTC.
   Install the qualified fix while halted. First-start checks must account for already-armed
   state and existing journals; never assume a virgin start. Verify minting, service health,
   seal and observer, then stage the driver once with the approved supervised limit.
5. Only after those checks, use the owner's normal-mode authorization and supervise each
   settlement/wallet comparison. Stop flat when possible, halt and review. `--extend 240`
   remains a separate approval. Preserve driver sequences when extending or restarting.

This review changes the order of Claude's continuation plan because startup repair alone
cannot satisfy the requested multi-trade MVP. It adds no strategy, venue or optional feature.
