# Pi container startup delay

During the authorized rolling upgrade, NUC1 and NUC2 recovered on `ba299ff` at index16.
The Pi pair was stopped, its configuration/data archived and hash-verified, and only the
image token in its two service units was replaced. The first new start did not become ready
within the90-second recovery deadline. The cleanup command also exceeded its45-second
wait while systemd completed its sequential service-stop timeouts. No automatic rollback
or repeated upgrade script was run.

The exporter log reported `creating an ID-mapped copy of layer` followed by
`signal: terminated`. The new container had not become ready. Both service MainPID and
ControlPID subsequently reached0, with failed state; the live configuration/data hashes
still matched the stopped backups. The existing backup is retained at
`/home/velocitas/.cache/velocita-build/strategy-grant-backup-ba299ff-node2/`.

One isolated image preparation was then started with `--userns=keep-id`, `--network=none`,
no volume mounts or credentials, and `/bin/sh` only to print UID and qualified binary hashes.
It uses a300-second bound with30 seconds for termination. This is container preparation,
not a second cluster, strategy start or trading attempt. The original packaging check
did not use `keep-id`, so it did not exercise this filesystem mapping cost.

Read-only process inspection identified `storage-chown-by-maps` working on the merged
image filesystem. Write I/O advanced from257269760 to397651968 to533942272 bytes;
metadata lists23 image layers totaling726884352 uncompressed bytes. This demonstrates
ongoing filesystem work rather than a completed voter start. Do not call the initial
failure a Raft replay bug or claim this check passed before its final result is available.

While the Pi was down, both upgraded NUCs elected NUC1 leader in term6 and converged at
commit/applied/durable17. Gateway and signer stayed active; the producer remained flat
at25000000 quote atoms, no base, no effects/pending/fees. Mode remained halted.
Driver and client-image replacement remain pending until the Pi is recovered and verified.

The first isolated check exited124 after301.626556seconds while mapping the same layer.
Its log and JSON remain on the Pi. After confirming both production services fully stopped
and original data/config hashes unchanged, one separate600-second isolated preparation
was started. It has no production mounts/network/credentials and uses a separate name/log.
Do not overwrite either attempt or rerun the initial upgrade script.

## Resolved

The600-second isolated attempt passed in419.109460932seconds. UID1004 and all three
qualified binary hashes matched. The recovery command verified the original data/config
against the stopped backups and the exact new unit images, then started the same Pi pair.
Pi returned up/ready, failed0, halted, term6/leader1, commit/applied/durable17. Both NUCs
and execution services remained healthy. Initial failure evidence remains preserved.
