# Locked renewal completed — October 13

At 23:38:39UTC, host verification confirmed the locked review renewal, protected
bearer handoff and qualified telemetry installation. Both trading services are
inactive, PID 0, with `RefuseManualStart=yes` and negative sentinel conditions.
No service activation, signing, order submission, funding reset or fund movement.

| Item | Verified result |
|---|---|
| Gateway executable | Qualified 69679c081ce2d0b8939a89c3c846948c460d8d3e |
| Renewal helper | Committed f6d2567; 30 renewal/application and 19 credential tests passed on NUC1 |
| Authentication | Same-account token refreshed through protected stdin/tmpfs handoff; installed hash/lifetime verified |
| Fresh account capture | 20 read requests; no unresolved TM orders; 0.040764166 SOL and 40.006989 USDC unchanged |
| Authenticated Raft reads | Both protected service users returned original state digest at index 6 |
| Gateway journals | All four remain byte-identical, ordinal 1 |
| Protected signer journal | Idle/zero effect retained; ordinal 1 → 2; only policy domain/checksum changed alongside ordinal |
| Archive | All before/after pins, ready/complete markers and installed bytes verified |
| Monitoring | Observer healthy; 39 Grafana panels/rows; scrape-error 0 on all 3 nodes |

Review validity ends **2026-10-13 23:50:24.625UTC (19:50:24.625EDT)**.
This is a time-bounded review, not permanent live readiness. Once expired, a new
same-account capture and locked renewal are required before a separately reviewed
start. The one-$1 scope and accepted 2039280-lamport maximum buy-output deduction
are unchanged. No continuous strategy authorization was added.

## Commands and recovery references

Source was extracted with `git archive f6d2567 deploy/v2/swap-runtime tests tools`,
staged by SCP and tested under `velocitas` with a private test HOME and umask077.
Tests used standard-library unittest discovery with patterns
`test_idle_policy*.py` and `test*credential*.py`. No C++ rebuild was needed.

On NUC1, the verified helper was invoked as root:

```sh
python3 -B /home/vishal/.cache/velocita-build/locked-renewal-f6d2567/deploy/v2/swap-runtime/idle_policy_apply.py \
  --bundle /home/vishal/.cache/velocita-build/locked-renewal-f6d2567/renewal-bundle \
  --sha256 8cf0ba91156aa85c01f6c647d727de32e0d360beef2a6cf01d32f9410a13633c \
  --apply
```

Do not rerun this completed apply. The completed private archive is:
`/etc/velocita-swap/public/idle-renewal-archive-8cf0ba91156aa85c01f6c647d727de32e0d360beef2a6cf01d32f9410a13633c`.
Protected read results are in the adjacent `idle-renewal-read-<pin>-<nonce>`
directory. The durable public sentinel is `/etc/velocita-idle-renewal-active`.
The prior telemetry upgrade also retained `90-telemetry-upgrade-inhibit.conf`
on both units; a reviewed launch must account for both layers of inhibition.

The first apply stopped before document/journal publication because `systemctl
stop` did not clear the gateway's previous expiry failure. Saved
ActiveState/SubState/MainPID/Result/ExecMainStatus/RefuseManualStart in
`/var/lib/velocita-telemetry-upgrade-69679c0/renewal-preflight-failed-state.json`,
verified PID 0 and loaded guards, then explicitly ran:

```sh
systemctl reset-failed velocita-swap-gateway.service
```

This clears only systemd's historical failure flag; no journal or service restart.
After diagnosing that refusal and verifying inactive state, the same pinned apply
was explicitly resumed and completed. There was no automated retry loop.

`tm-stage-swap-credential.py --role gateway --send` staged the matching token
via SSH stdin. Root then called the committed `credential_handoff.install` with
the exact new profile hash and previous bearer hash. Only the gateway bearer
was atomically replaced. Credential bytes were never printed or archived. The
separate handoff result is
`/var/lib/velocita-telemetry-upgrade-69679c0/gateway-bearer-renewal.json`.
The staged source remains in private owner tmpfs under the handoff's existing
retention contract. The signer credential and caller credentials were unchanged.

## Qualification and limits

Local 30 renewal/application,19credential and 18 transition tests passed. Four new
session mutations were killed by assertion failures, zero errors; see
`session-mutations.json`. NUC 30+19 logs and sanitized `host-verification.json`
record the final host checks. Earlier preflight refusals (transaction review
size and expired bearer) changed no account documents or journals.

The live entry/exit chart is installed, but no settlement event has been generated
by this stopped deployment. A real end-to-end fill remains a separate bounded
launch step requiring a fresh executable receipt floor and current account review.
