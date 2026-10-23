# Retained Bison transaction: offline production verifier and identity parity

The current production verifier accepts the retained unsigned Bison envelope
under the newly prepared schema4 account/lookup policy and its trusted program
contract. It verifies total USDC debit1,000,000 atoms, explicit fee2,000 atoms,
gross guard8,817,231 lamports, and net floor6,777,951 lamports after subtracting
full2,039,280 repayment with zero sponsor credit. The exact floor passes;
requiring one additional lamport refuses (`minimum_shortfall`,11).

The prior schema3 policy refuses this same envelope with `invalid_accounts`4 and
lookup `table_mismatch`6. Its diagnostic review had not expired, so the refusal
is a route/account mismatch. Its zero receipt/debit diagnostic fields mean
unverified, not zero cost.

Native and Python implementations agree on signing, protected-journal and runtime
identity digests for the exact candidate document bytes. The historical diagnostic
clock is1791944722956ms, derived from the retained envelope timestamp plus1ms.
The intent is synthetic. These are not current account checks, real Raft authority,
startup authorization, settlement proof, or admission to live trading. Nothing was
signed, simulated or broadcast; these checks make no network request.

`check_actual.py` recompiles all14 project source dependencies and both diagnostic
programs directly from current source, with no cached project libraries. Dead code
is discarded by the linker; this is a diagnostic executable, not qualification of
the complete gateway. Input source and all V2 public header hashes were identical
before/after compilation. This avoids mixing an older `swap::State` library ABI
with current headers. Initial preparation builds exposed missing OpenSSL include
and `swap_account_user_id_valid` linkage; supplying the existing dependency paths
and fresh `v2_swap_account_setup.cpp` resolved them. The recorded final build
completed successfully.

Evidence: `actual-capture-verification.json`, `actual-capture-build-commands.json`,
and `CAPTURE_REVIEW.json`. Raw envelope, account snapshots, lookup addresses and
candidate documents remain private. Full build logs are under
`~/.cache/velocita-build/bison-offline-parity/final/`.

Reproduce from the repository, after preparing the pinned private candidate:

```sh
python3 -B docs/evidence/bounded_launch_review_2026-10-13/bison/check_actual.py \
  --candidate "$HOME/.velocita-credentials/truemarkets/bison-offline-review-20261014" \
  --old-policy "$HOME/.velocita-credentials/truemarkets/obsidian-prelaunch-offline-refusal-8dde28e" \
  --output "$HOME/.cache/velocita-build/bison-offline-parity/recheck"
```
