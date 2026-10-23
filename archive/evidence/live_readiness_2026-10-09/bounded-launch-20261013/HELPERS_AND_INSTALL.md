# Offline preparation and dormant installation

The qualified runtime is commit `84c353a7f17581546c84db414b0d759a2ab0e695`.
The helpers below do not change that executable and cannot sign or submit a
transaction. They prepare evidence and canonical records for later review.

`tools/prepare-swap-mvp.cpp` uses the production codecs, reducer and identity
checks to create opening/account documents or one effect-1 buy intent. It enforces
the $1 total debit, $0.002 fee, at most $0.50 realized-loss allowance and full
2,039,280-lamport repayment treatment with zero sponsor credit. All-inventory
balances must match the history cut; the full SOL position needs a fresh mark.
Outputs are create-only, private and synced. Four test groups pass and five
isolated mutations fail their intended assertions (`builder-mutations.json`).

Build and test with `validate_builder.py --build PATH_TO_LOCAL_CMAKE_BUILD
--output NEW_DURABLE_DIRECTORY`. The script reuses production libraries and emits
`prepare-swap-mvp`. Its two modes are `opening` and `intent`; invoke with explicit
`--parameters` and fresh `--output`, plus the profile/history/transaction-review
inputs for opening, or `--bundle` for intent. The parameter schemas are exercised
in `tests/test_prepare_swap_mvp.cpp`. Fixture values are not live parameters.

The helper validates arithmetic and provenance hashes; it does not fetch or
independently derive supplied quote, account or valuation observations. Those
values must be extracted and reviewed from fresh captured evidence. A canonical
record is not an operator commitment or authorization to submit it.

`tools/tm-mvp-quote-check.py` permits one explicit unsigned $1 buy quote or one
unsigned sell quote for the exact entire SOL balance in a pinned fresh history
cut. Each invocation permits at most one refresh, one account-identity GET and one
create-order POST, with no retry. It checks owner identity before the quote and
persists an attempt record before network access. Raw responses stay private.
It does not certify net receipt or adopt an opening valuation. Seventeen tests
and eleven named mutations pass; the reused legacy route probe has eleven tests.
No real quote was requested during this helper preparation.

## Prepared owner installation

Only public files were staged at
`~/.cache/velocita-build/bounded-dormant-84c353a-20261013` on NUC1 as `vishal`.
All 24 staging-file hashes and the new bundle validator passed remotely. Both
production units were still inactive/dead/static after staging.

From the Mac:

```sh
ssh -t vishal@100.72.127.102 'sh "$HOME/.cache/velocita-build/bounded-dormant-84c353a-20261013/owner-install.sh"'
```

The script needs an interactive sudo session because the deployment account has
no sudo authority. It installs the immutable release and dormant units, preserves
existing valid Raft caller keys, refuses any staged venue credentials, verifies
the exact previous installed bytes and performs `systemctl daemon-reload`.
It does not start/enable services, alter voters, fund a ledger or move money.
On failure preserve partial state and inspect; do not repeat automatically.
The old immutable release and its exact public bundle are retained.

Bundle hashes and executable provenance are in `dormant-install.json`; exact
commands are in `owner-install.sh`. Installation is now confirmed by the owner output and an independent SSH check
of the executable SHA-256 and both loaded units. Both services remain
inactive/dead/static. See `dormant-installed.json` for the exact verification
scope; this check did not inspect private credential contents.

## Remaining launch sequence

After dormant installation, finish protected credential/public-document adoption,
then capture fresh account history and transaction/account facts. Review the
opening mark and fresh conservative receipt floor; submit only their canonical
records with current authenticated Raft controls. Keep the strategy edge stopped.

The current reconciliation makes a bounded finality read. If settlement is not
yet proven, retain exposure and journals. Recovery must use initialization=false
and reconcile the existing operation without another quote, signature or execute
request. Review credential/profile expiry before that restart; a refreshed
identity must not silently replace the journal's pinned identity. This recovery
procedure still needs completion before the one-buy launch.
