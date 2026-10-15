# Automated stochastic swap run on a new incarnation

Owner decisions (2026-10-15): run the paper stochastic one-second rule unattended at 0.12 SOL clips,
$25 managed cash, $10 realized-loss stop, 240 lifetime orders, new incarnation, TM API key on NUC1
(signer only, ADR 0029), the SOL already in the wallet stays unsold outside the managed account.
Release `54a5fcb` (pieces 1–5) plus the uncommitted launch tooling listed at the end.

Who runs each step: **me** = Claude over SSH as `velocitas`; **owner `!`** = a Mac command that
touches the Keychain or the TM session, typed with `!`; **owner sudo** = a NUC1 root step.
Stop at the first refusal. Never retry a partial ceremony; reconcile instead.

## A. Release (no cluster or account change)

1. **me** — native qualification of `54a5fcb` on NUC1 (x86_64) and the Pi (aarch64). **Done 2026-10-15 12:57 UTC**: both qualified, 58 native tests and 8 process drills each (`qualification-54a5fcb.json`).
2. **me** — `package.py` builds `localhost/velocita-v2:qualified-54a5fcb` on all three hosts from the qualified
   binaries (NUC1 binaries copied to nuc2), each pinned by hash.
3. **me** — build the Mac client and `prepare-swap-mvp` at `54a5fcb`.
4. **me** — `release_bundle.py` on NUC1 builds the dormant install bundle (new incarnation, `launch_authorized=false`,
   empty account pins; passes the reviewed installer's payload checks). Installed in step 11a.

## B. Retire `b79e65be`, bootstrap the new incarnation

5. **me** — record the closing state of `b79e65be`: halted, no pending effect, both services
   stopped, Raft settlements equal to the wallet.
6. **me** — generate the new incarnation id (16 random bytes).
7. **owner go, then me** — per host: stop voter and exporter, move `data` and `config` to
   `~/velocita-indicator-paper/retired/stochastic-b79e65be-20261015/`, write the new config
   (new incarnation, `startup = bootstrap`), point the units at `qualified-54a5fcb` (`voters.py stage`), start, check
   ready with commit 0, then `voters.py recover`. Nothing is deleted. `tools/cluster-authority-domain` computes the
   new authority domain from the three configs (checked against the live cluster's recorded domain).
   **Done 2026-10-15 13:30 UTC**: incarnation `aae119b95ddcea5a206f03ab5ca12c15`, authority domain `ce15af7a…`
   (`authority-domain-aae119b9.txt`); all three voters ready at term 1, commit 0, halted; startup set to recover.
   The Pi took about seven minutes to create its containers under load.
8. **owner `!`** — `operator.zsh NEW ceremony --run` from the Mac as operator caller 7/key 9, new session directory:
   seq 1 `kill-epoch 1`, seq 2 `executor-generation 1`, seq 3 `install-catalog <root> 1 1`,
   seq 4 `assign-role 22 gateway`, seq 5 `assign-role 21 strategy`.

## C. Account adoption and funding

9. **owner `!`** — mint a Mac TM session; run `tm-account-history-check.py --run` for a fresh
   all-inventory history cut.
10. **owner sudo** — `retire_installation.py 20261015`: moves the stopped `b79e65be` swap installation (public
    documents, units and guards, journals, observer cache) into `/var/lib/velocita-retired-b79e65be-20261015`; the
    credential tmpfs is renamed inside `/run`. Nothing is deleted.
11. **owner sudo** — `install_disabled.py` (fresh) with the bundle from step 4.
12. **owner `!`** — `prepare_documents.py` for the new incarnation: profile (gateway credential is the API
    key id, `expires_at_ms` = review end), the unchanged transaction review, and `prepare-swap-mvp live-opening`
    with `managed_quote_atoms=25000000`, `max_buy_price_per_sol=200000000`,
    `min_sell_price_per_sol=20000000`. Everything else in the wallet becomes outside balance.
    The review ends at most 24 hours after the history cut.
13. **owner sudo** — `adopt_account.py --apply`; **owner `!`** stage both credentials
    (`tm-stage-swap-credential.py`: gateway gets only the key id, signer the PEM); **owner sudo**
    install both, then `verify_installed.py`.
14. **owner `!`** — `operator.zsh NEW funding opening.record SHA --run` (seq 6, the cash-only Initialize).
15. **owner sudo** — `review_transition.py` mode `arm` (launch authorized, first-use journals), start signer then
    gateway with trading halted. The gateway mints its first TM token through the signer here; that is the
    first live check of ADR 0029. Confirm admission and the four gateway journals, then `review_transition.py`
    mode `seal`, then `restore-guards.py`.

## D. Observer and driver

16. **owner sudo** — `upgrade-observer.sh` (pinned files; publishes `strategy-settlements.json`, timer 5 s). Its old
    settlement cache was archived in step 10.
17. **me** — `stage_driver.py --max-orders 6` with the opening record and the git archive of
    `model/src/velocita_model` at the release commit. The unit has no `[Install]` section.

## E. Supervised drill: three round trips

18. **owner go, owner `!`** — `operator.zsh NEW mode normal 7 --run`.
19. **me** — `systemctl --user start velocita-strategy-driver`; watch every effect from intent to
    settlement and compare Raft with the wallet after each settlement.
20. The driver stops itself flat after six effects (`capacity`). **owner `!`** `operator.zsh NEW mode halted 8 --run`.
21. Review the six effects. If the owner approves the unattended run: `live_swap_driver --config driver.json
    --extend 240` (keeps the driver's sequence numbers, which Raft tracks per caller, and clears only the
    `capacity` stop), `operator.zsh NEW mode normal 9 --run`, start the driver. The run ends at the $10 loss,
    240 orders, insufficient cash or the review window end.

## Uncommitted launch tooling

- This folder: `voters.py`, `package.py`, `release_bundle.py`, `retire_installation.py`, `prepare_documents.py`,
  `operator.zsh`, `upgrade-observer.sh` with pins, and a test file for each Python script.
- `tools/cluster-authority-domain.cpp` (production config parser and identity encoder).
- `deploy/v2/swap-runtime/adopt_account.py`: a schema 6 transaction review relies on the account review expiry.
- `tools/prepare-swap-mvp.cpp` `live-opening` mode and its tests.
- `deploy/v2/observability/live_observer.py` publishes the validated gateway snapshot; timer 5 s.
- `deploy/v2/strategy/stage_driver.py` and `tests/test_stage_driver.py`.
- Driver: snapshots up to 12 s old, 20 s signing window, optional `max_orders`, `--extend`.
