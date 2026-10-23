# Proposed single-cluster cutover to qualify authenticated reads

**Prepared for owner approval; not executed.** This changes persisted cluster
identity/state, so the current permission to install dormant units and read state
does not cover it. The owner's standing instruction requires confirmation before
destructive/irreversible changes; retirement is state-changing even though old
bytes will be preserved for rollback.

## Concrete scope

Current: exactly one paper cluster on NUC1 `100.72.127.102`, Pi
`100.84.151.116`, NUC2 `100.85.36.22`, image `roles-884816d`, incarnation
`348e0b2cd4f639ff404df4eeadee0ee5`. Deployment root on each host:
`/home/velocitas/velocita-indicator-paper/stochastic`. Production swap units are
inactive and remain so. This plan creates **one replacement cluster**, never a
second concurrent cluster, and performs no venue operations.

1. Pin compatible voter/exporter/gateway artifacts to the qualified production
   C++ source86f4408 (unchanged through45db6a6). Reuse verified native outputs where
   possible. Record NUC/Pi image and binary hashes before stopping anything.
2. Capture old unit/config/health identities and current paper pending orders and
   ledger. Stop edge/gateway, then all three voter/exporter pairs together.
   Archive old configurations, Raft data, paper venue/book, gateway sessions and
   monitoring run identity. Verify archive hashes; do not delete/reset old state
   or include secret contents in repository evidence.
3. Create fresh directories and one new incarnation, retaining the same SOL catalog.
   The old paper balance/history stays in the archive. **Do not initialize live
   money, copy paper cash into live funding, or start either strategy edge.**
   This is initially a stopped-strategy read-qualification cluster; a new paper
   funding/run decision remains explicit rather than silently resetting to$25.
4. Register gateway caller22/key13 on44413 and independent signer caller23/key14
   on44414 in all voter configs, including `signer.caller`. Verify the two actual
   authentication key sources and use the approved tmpfs custody workflow.
   Derive and compare all three new static-identity authority domains using the
   new release's parser/encoder; do not reuse old domain or substitute config SHA.
5. Start only the three new voter/exporter pairs after proving old voters stopped.
   Perform the minimum operator bootstrap/catalog setup from the Mac. No live
   mode, funding, strategy activation, venue signature or order submission.
6. Hand off exclusive caller endpoints/sessions while gateway trading processes
   remain stopped. Explicitly prepare fresh durable session epochs; place only
   required Raft caller keys into role tmpfs, no TM bearer/signing key. Run each
   read helper under productionUID29001/29002 with the qualified sandbox/network
   policy, outer60-second service bound and75-second controller bound. Each
   helper permits one correlated read with an internal12-second deadline; no
   outer retries, commands, authority acquisition or venue connection.
7. Require correct role/authentication, new domain/incarnation, full snapshot
   decoding/integrity, current catalog and leader/term/applied-index evidence.
   Verify zero bad-auth/fenced/correlation counters, no extra running cluster,
   and matching exporter/dashboard identities. Preserve failed evidence, stop
   and investigate rather than resizing scope or retrying blindly.

If source shipping changes mounted paths, restart only affected monitoring
services to refresh mounts; voters and exporters always restart together.

## Failure/rollback

Stop the replacement processes before considering restoration. Restore the
archived old units/configuration and matching old images/data as a complete set;
never feed schema12 state to schema11 binaries or run mixed incarnations. Preserve
new run evidence too. Fresh-account funding and live production activation remain
separate. No migration code is proposed for this paper-state cutover.

Approval requested covers retiring/archiving the existing paper incarnation,
starting this one compatible replacement and the bounded authenticated reads.
It does **not** authorize money movement or a fresh live account opening.
