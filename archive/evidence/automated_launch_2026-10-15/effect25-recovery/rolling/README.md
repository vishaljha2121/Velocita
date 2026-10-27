# Rolling voter upgrade, 2026-10-16

Each voter was stopped, its config and data copied and hash-verified, its units switched to the
`qualified-64e6f9f` image, and restarted, one at a time: Pi, nuc2, then NUC1 (the leader) last. The cluster
was halted, so no entries were committed while a voter was away. Script:
[`rolling_voter_upgrade.py`](../rolling_voter_upgrade.py).

| Voter | Stop to back in step | Rejoined at | Note |
|---|---|---|---|
| Pi (node 2) | 7 s | commit = applied = durable = 139 | |
| nuc2 (node 3) | 4 s | 139 | |
| NUC1 (node 1) | 4 s | 139 | was the leader; leadership moved to the Pi (term 14 → 15) |

Times are from the files each run wrote on its host: `preflight.json` before the stop and `result.json` after
the voter reported up, ready and fully applied (`stat` modification times, one-second resolution).
`node<N>/` holds both files.
