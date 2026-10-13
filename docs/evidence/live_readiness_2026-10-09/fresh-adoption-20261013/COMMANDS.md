# Command record — completed dormant adoption

These describe the completed October13 operation. They are not a rerun script:
exclusive outputs, original manifest pins and staging files no longer match.
No credentials or sudo password belong in this record. Live units stayed stopped.

1. Mac: `tm-account-history-check.py --run`, using the session in the existing
   private TM directory and pinned account-bootstrap setupSHA
   fbf9602f339af07a4ad77e5002f688a8b2b57e604d2d788b77be94b6a46cd9c9.
   Outputs: fresh-adoption-history-20261013, later prevaluation and final-history.
   Every capture completed with20 reads; no retry of a network error.
2. Mac: `tm-mvp-quote-check.py --side buy --run`, with the exact fresh baseline
   hash and existing SOL catalog. One$1 unsigned quote. The production parser,
   read_accounts.py and verify_capture.py supplied account/route evidence.
3. NUC owner: `sh ~/.cache/velocita-build/fresh-adoption-20261013/owner-read.sh`.
   It ran the previously qualified authenticated_read_run.py under sudo for
   callers23/22, ports44414/44413, with the existing catalog/authority pins.
   See cluster-read.json for the full bounded transient read-unit arguments.
4. Scoped source commits5cdb5e1 andbb16daf. No shared C++ paths were staged.
   The dormant package copied the unchanged13cc98b native binary and used
   committed5cdb5e1 Python launcher/helper source. All27 transferred public
   files matched staging.json. Transfer was a tar stream over SSH to the owner
   cache directory; it did not replace the hosts' source mounts.
5. NUC owner: `sh ~/.cache/velocita-build/fresh-adoption-20261013/release/owner-install.sh`.
   Under sudo it invoked install_disabled.py with new bundleSHA
   bf70c6dd1e6f5a33e8ebaf511a92fa56e8595ea594c068ef610c0a7b53eacc96
   and previous bundleSHA
   f784a41cd6b53c26c5f741e0ca3f96c2a58b53b85887d61aab51b84397400a34,
   with `--apply`. Both units remained inactive/static.
6. Mac: `tm-mvp-quote-check.py --side sell --run` for exactly0.040764166 SOL
   from the prevaluation history. Final history capture followed. Then
   prepare_documents.py invoked the qualified prepare-swap-mvp opening builder
   within30seconds of the quote. No sell or funding command was sent.
7. Public account bundle transferred over SSH to the owner cache. Its adopter
   validation command used `--sha256 b6cfae8dcf7aa513b40decbea22a2e447c28c22775ac9f51cd2527ce7571dbc9`.
8. Mac: `tm-stage-swap-credential.py --role gateway|signer --send`, with the
   pinned profile and receiverSHA
   667ce3354e31c09a6709018875e5b6d44479a0ff79df9a9b7fe2784b8a9d2e09.
   Raw credentials traveled only in SSH stdin. First invocation hit a local
   argument typo before SSH; after the regression/fix commit one corrected
   transfer per role succeeded. Protected source paths are in the private reports.
9. NUC owner sudo: `release/receiver/adopt_account.py --bundle account --sha256
   b6cfae8dcf7aa513b40decbea22a2e447c28c22775ac9f51cd2527ce7571dbc9 --apply`.
   Then credential_handoff.py install, once per role, with each exact private
   owner staging path and profileSHA
   17370a3cbc1063fd7c8c8c2d2ebd57ea7d471b68e2d40b0e8e894cb4305912f0.
10. NUC owner sudo: verify_installed.py. Checked full hashes, metadata, actual
    opposite-user read denial, rendered units and empty recovery directories.
    Result retrieved into installed.json. Removed only the two hash-verified
    temporary owner staging copies; installed role credentials retained.
    `sudo -k` invalidated the cached administrative authorization, then SSH exited.

No `systemctl start`, enable, operator funding/intent, venue execution, voter
restart, monitoring restart or cluster reset was used.
