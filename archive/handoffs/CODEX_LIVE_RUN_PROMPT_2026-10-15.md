Continue the Velocita automated live run in `/Users/vishaljha/aeron-raft-pi` (branch `v2/phase-5-corrections`).

Read `docs/handoffs/CLAUDE_TO_CODEX_LIVE_RUN_2026-10-15.md` in full first. It supersedes earlier handoffs' next steps.

The goal is narrow: get the paper stochastic rule trading live, unattended, on the existing cluster `aae119b95ddcea5a206f03ab5ca12c15`. Do not add gates, features, ADRs or reviews that are not needed to trade safely. Work in this order and stop at the first refusal:

1. Native-qualify the current HEAD gateway (Raft-outage fix on top of the refused-route fix) on NUC1 and Pi with the committed runner in `docs/evidence/automated_launch_2026-10-15/focused-mvp/native/COMMANDS.md`, bundling `54a5fcb..HEAD`.
2. Retarget `docs/evidence/automated_launch_2026-10-15/route-fix/owner_upgrade_restart.py` (`COMMIT`, `ARCHIVE`) to that commit, commit it, and stage `source/` and `payload/` on NUC1 under `/home/vishal/.cache/velocita-build/route-fix-<short>/` with every SHA-256 verified.
3. Give the owner the read-only preflight command, then the `--run` command. The owner runs sudo; you never do.
4. Start the existing driver as `velocitas` (`systemctl --user start velocita-strategy-driver`; it is already at max_orders 240, sequence 0; never re-stage or reset it), expire Alertmanager silence `db074bd6-cefe-4003-b885-9e8e469c827c`, and watch every effect end to end. Report fills, fees, P&L and Raft-versus-wallet after the first round trip.
5. Warn the owner before the review window drains at 2026-10-16 13:41 UTC; renewal needs owner steps.

Current state: mode normal (next operator sequence 10), flat $25, signer running, gateway failed at 18:35:55 UTC (the bug the outage fix addresses), driver stopped on purpose.

Rules: sudo, Keychain and the TM session stay owner-run. No secrets anywhere. One cluster; archive, never delete; never retry a partial ceremony. Commit only explicit owned paths; never commit shared dirty files whole; no reset/clean/stash. No code comments. Mutation-test new logic and confirm each mutant recompiled. Commit footer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Explain results simply and keep moving.
