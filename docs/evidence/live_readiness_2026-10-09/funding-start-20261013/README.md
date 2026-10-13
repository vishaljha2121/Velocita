# Funding/start preparation status

October13, around22:02 UTC: owner authorized the funding/start ceremony. The
ceremony has **not run**. Both production services remain stopped; no replicated
opening, role change, journal initialization, signing or transaction submission.

Fresh authenticated reads under both service users returned term5, leader2,
applied index4/state_version3 and the same halted/unfunded state digest
663d7599b83597d327a2991e11c4f13e9f6184d4400d1daae85309c5a32dc47c.
Each host has exactly one existing stochastic-named voter container. This is a
container name, not a claim that the old strategy is trading. No hosts restarted.

One fresh20-request history capture found40.006989USDC,0.040764166SOL,
four finalized transactions and zero blocking orders. One keyless finalized
six-account snapshot passed all account/lookup checks at slot456598383.
Existing limitations remain: snapshot facts do not guarantee execution-time
state, and provider archival completeness/closed historical accounts are not
proved. No fresh buy quote was requested.

The previous account review expired at21:52:03.602UTC. Its evidence and installed
files are preserved. The final unsigned valuation/final-history cut is deliberately
not collected yet, to avoid expiring before the installation steps are ready.

Prepared review_transition.py handles only exact pinned dormant review refresh,
explicit first-use flags and later removal of the initialization flag. It cannot
start services or create trades. It preserves prior files, checks current review,
identity, credential bindings and exact installed bytes, and refuses nonempty
journals for refresh/first start.31 focused tests passed(9transition,6adoption,
16launcher);3targeted mutations failed named assertions with zero test errors.
The opening builder now accepts an explicit route-evidence directory; its default
and canonical C++ encoder are unchanged. No C++ or deployed binary changed.

The owner approved the narrow ceremony checkpoint under the committed-deployment-
source rule. The next steps are: checkpoint only the ceremony helper/test/protocol
support, prepare fresh final mark/history,
review the canonical opening and exact bundle hashes, then perform the authorized
role/funding/start sequence. No normal-mode command or Intent is included.

Read-only commands used:
- Existing authenticated_read_run.py with the pinned helper/catalog/incarnation/
  authority arguments and new funding-pre-read output (see cluster-read.json).
- podman ps over SSH on100.72.127.102,100.84.151.116,100.85.36.22.
- tools/tm-account-history-check.py using the retained setup pin and new private
  funding-start-prehistory-20261013 output.
- read_route.py: one getMultipleAccounts, using the fresh history minimum slot,
  same six retained account identities and a new exclusive capture directory.
- owner-role.zsh --dry-run only; it sent no command and accessed no caller key.

Raw account responses remain private. No credentials are included here.
