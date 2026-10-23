# Qualification of ba299ff — October 15

Exact commit: `ba299ff6c89392177cfd4702fda11dabb7ee1719`.
The approved scoped test/evidence checkpoint is committed. Qualification completed successfully on both architectures; see [results](RESULTS.md). It started on NUC1 and Pi
at approximately 22:24 UTC. Both use the preserved cache, pinned images, core 3, one CPU, 2 GiB,
network disabled and no production credentials. The four transferred file hashes matched on both hosts.
No production state, services, journals, funding, credentials or driver sequences are changed.

Follow NUC1:

```sh
ssh velocitas@100.72.127.102 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-ba299ff6c89392177cfd4702fda11dabb7ee1719/driver.log"'
```

Follow Pi:

```sh
ssh velocitas@100.84.151.116 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-ba299ff6c89392177cfd4702fda11dabb7ee1719/driver.log"'
```

Use `build.log`, `tests.log` or `continuous-repeat-2.log` / `continuous-repeat-3.log` in the same
folder for details. `status.json` determines whole-run qualification. Both runners stop on the
first error; do not retry a failed run. Original `b490d68` evidence stays unchanged.

The transfer and launch procedure uses the committed `focused-mvp/native/{launch.py,native-focused.py,collect.py}`.
Source is a `54a5fcb..HEAD` Git bundle with exact HEAD pinned to the commit above. Only the dedicated
clean cache checkout is switched. Final allowlisted evidence must be collected and hash-verified.
