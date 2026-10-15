# Supervised six-effect attempt — stopped before any order

Owner authorized the supervised run after successful startup/seal. Owner mode-normal sequence 7 committed at index 8. The existing driver started exactly once at 13:34:03 EDT, then was stopped after repeated feed bootstrap failures before any intent; [activation](ACTIVATION.json) records the initial start only. Owner halt sequence 8 subsequently committed at index 9. See [current readback](HALTED_HANDOFF_STATUS.json), [public diagnostic](FEED_DIAGNOSTIC.json), and [full continuation](../../../../handoffs/CODEX_TO_CLAUDE_MVP_2026-10-15.md). The existing committed `operator.zsh` remains unchanged. Sudo/Keychain/operator actions remain owner-run under the handoff.

## Pre-activation evidence (historical)

- Existing driver: six attempted effects maximum, $25 managed opening, 0.12 SOL buy target, configured $10 realized-loss stop. Six is a ceiling, not a guarantee of three completed round trips. The 240-effect extension is not part of this run.
- All 76 deployed model files match the qualified `1f8d7b6` archive. Client image and opening record match their fixed pins. Driver static/inactive, client/state directories empty.
- Public data: three bootstrap pages yielded 3000 consecutive warmup candles; a separate bounded WebSocket check decoded three consecutive closed one-second bars aged 181/171/175 ms. No submitter was constructed during those checks.
- Producer observation is fresh at funding index 7: $25 managed USDC, no position/effect/pending trade. Review expires October 16, 2026 at 13:48:34.459 UTC.
- Current-incarnation NUC1 WAL: all block CRCs, manifest/owner bindings, bound-frame hashes and command hashes verified. Seven entries contain operator sequences 1,2,2,3,4,5,6. Sequence 2 is byte-identical in both entries. No strategy caller request exists. Next operator sequence is 7; next strategy sequence is 1. This is read-only durable-history evidence corroborated by native producer/voter index 7, not a fresh remote authenticated read proof. Raw WAL stays in private scratch.

## Failure and continuation

The WebSocket opens before three REST history pages are fetched. Bootstrap took 4.413633s;
the first queued consecutive bar was 3615ms old. The strict2s guard correctly refused it,
but re-bootstrap repeated the backlog. Later diagnostic gap flags are relative to an unchanged
bootstrap endpoint: the recorded row timestamps themselves are consecutive.

Driver/client state directories are empty. Current producer state at index 9 is $25 managed USDC,
zero SOL/effects/pending/fees/settlements. This attempt did not test live settlement. Gateway and
signer remain active and sealed; all voters are healthy/halted. No fresh venue wallet read was
performed for the handoff.

Two catch-up tests are written but implementation is absent: [RED output](CATCHUP_RED.log).
Finish non-trading catch-up while retaining freshness, source timestamps, continuity and expiry
checks. Test main-loop behavior and rehearse public data without constructing a submitter.
Then request a scoped checkpoint, qualify committed Python on NUC1 and update only the existing
stopped model source, preserving all state. Never re-stage a fresh driver or repeat first startup.

Operator sequences7 and8 are consumed. Next is 9 only after verifying no intervening command;
no activation command is provided here to accidentally replay. The review expires
2026-10-16 13:48:34.459 UTC. Six attempted effects remain the ceiling; 240 requires separate approval.
