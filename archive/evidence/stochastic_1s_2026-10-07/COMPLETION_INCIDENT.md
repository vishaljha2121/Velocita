# Bounded pilot completion and shutdown incident — 2026-10-07

The stochastic one-second pilot reached its fixed end at 20:39:58 UTC (16:39:58 EDT).
The edge logged `ONE_SECOND_STOP reason=market data ended decisions=1568` at 16:39:59
and exited successfully. No extension or new run was started.

## Verified account and cluster

- Incarnation: c8a6a0a78fbf49a099b222124a5d557f.
- Starting paper cash: $25. Final cash: $24.869170. Position: zero SOL.
- Four fills / two completed round trips; volume $55.814400.
- Fees $0.111630; gross P&L -$0.019200; net P&L -$0.130830.
- Venue report: failed=false, pending_reports=0, committed_through=2721.
- All four journal fill IDs, sides, lots, notionals and fees match the venue report.
- SQLite integrity_check returned ok. Original ledger and balances were preserved.
- All three voter exporters report velocita_v2_up=1; nuc1 is the sole leader.
- Gateway and observer remain running. The expired edge is inactive/dead.

## Failure mechanism

1. `podman run --rm` removed the container after the successful edge exit.
2. Systemd then ran `podman stop -t 15 NAME`, which returned 125 because NAME was
   already absent. That cleanup failure marked the unit failed.
3. Restart=on-failure repeatedly relaunched the edge. Its expired-epoch check
   returned immediately without starting another trading run. The loop reached
   NRestarts=97 before being explicitly stopped during this investigation.
4. After the writer closed SQLite, ledger.db-wal and ledger.db-shm disappeared.
   The observer's read-only mount could not satisfy SQLite's WAL metadata needs:
   an actual SELECT inside the observer failed with `unable to open database file`.
   This is an observer access problem, not evidence that the ledger was deleted
   or corrupt. Integrity was checked with immutable=1 only after verifying the
   writer was stopped and both sidecars absent; this is not a live-reader fix.
5. Monitoring lacks a completed-pilot state, so expected submitter/feed inactivity
   and the buy deadline appear alongside a real ledger-readability alert.

## Applied correction

On nuc1, stopped only the edge; installed this systemd drop-in at
~/.config/systemd/user/velocita-paper-stochastic-edge.service.d/completed-cleanup.conf:

```ini
[Service]
ExecStop=
ExecStop=/usr/bin/podman stop --ignore -t 15 velocita-paper-stochastic-edge
```

Verified this installed Podman supports `stop --ignore`: it ignores only missing
containers rather than suppressing every cleanup error. Executed:

```sh
systemctl --user stop velocita-paper-stochastic-edge.service
# Write the exact drop-in above, preserving the base unit.
systemctl --user daemon-reload
podman stop --ignore -t 15 velocita-paper-stochastic-edge
systemctl --user reset-failed velocita-paper-stochastic-edge.service
systemctl --user show velocita-paper-stochastic-edge.service -p ActiveState -p SubState -p Result -p ExecStop
```

The cleanup command returned zero; unit is inactive/dead with Result=success.
No voter/exporter, gateway, observer or host was restarted. No image was rebuilt,
no credentials were pushed, and no paper state was reset.
The matching prepare.py generator correction and deployment regression assertion
are uncommitted in both working trees; four deployment tests passed.

## Still outstanding

The dashboard/observer has NOT been repaired by the cleanup correction. It needs
an explicit completed-run state and a durable final ledger snapshot that remains
readable without observer write access. Do not solve this by granting the observer
writes, by using immutable=1 on an actively written ledger, by inventing fresh
market-data timestamps, or by resetting/restarting the account. A flat finalized
account should display cash as equity without requiring a fresh market price;
non-flat finalization must remain visibly unresolved.

Current additional firing alerts include market-data recorder database size and
alert-delivery failure. They are separate operational issues, not evidence of a
Raft failure or a missing paper balance; they were not remediated in this diagnosis.

This engineering pilot is too short to establish strategy profitability.
