# Native integrated checkpoint qualification — October 13

**Complete:** `86f440806c6adc6f4c1dc9dc7a271e3d95293d76` qualified on NUC1
(x86_64) and Pi (aarch64), using full-suite results plus three repetitions of
each corrected native fixture. See [RESULT.md](RESULT.md) for exact test lineage,
failures, restoration, and limits; [results.json](results.json) contains hashes.

The production cluster was paused with owner authorization to release memory,
then restored without a reset or new deployment. Its strategy edge was already
inactive after a feed timeout and remains inactive.

- [Checkpoint review and source selection](../CHECKPOINT_REVIEW_2026-10-13.md)
- [Maintenance and restoration commands](MAINTENANCE_COMMANDS.md)
- [Restored voter health and image IDs](RESTORED_HEALTH.txt)
- [Compiler-warning inventory for next iteration](WARNINGS.md)
- Native evidence: [NUC1 status](nuc1/status.json), [Pi status](pi/status.json)

Both jobs used isolated clean committed source, pinned toolchain images,
read-only source mounts, non-root UIDs, private network namespaces, CPU 3 and
one CPU quota. Full tests used 2.75 GiB/no swap after capacity checks. No real
credentials were mounted. Original/corrected runners and failed attempts remain
in this folder; `native-fixture-final.py` is the final selective qualifier.

`checkpoint.json` intentionally records the original a890307 integration manifest.
Native/offline qualification is not a new release deployment or live approval.
