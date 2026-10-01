# Market-data archive runbook (nuc1 stream.db -> the Mac)

`velocita-md-stream` on nuc1 records about 1.1 GB a day into
`~/velocita-md/stream.db`. Every night each complete UTC day is archived, copied
to the owner's Mac, verified there, and only then deleted from nuc1.

## Where the data lives

| Place | What |
|---|---|
| nuc1 `~/velocita-md/stream.db` | live database: today plus the last 3 complete days (4-5 days, about 5.5 GB) |
| nuc1 `~/velocita-md/archive/stream-YYYY-MM-DD.sqlite.gz` | one UTC day, kept on nuc1 for 7 days once acknowledged and pruned |
| `... .sqlite.gz.sha256` | sha256sum-format sidecar; written last, so its presence means the archive is complete |
| `... .manifest.json` | rows, id range and sha256 (the prune reads the id range) |
| nuc1 `~/velocita-md/archive/acked/stream-YYYY-MM-DD.ack` | written by the Mac after verifying its copy; contains that sha256 |
| nuc1 `~/velocita-md/archive/pruned/stream-YYYY-MM-DD.pruned` | the day's rows are gone from stream.db |
| Mac `~/velocita-backups/md/` | the long-term copy: archives and their sidecars |

An archive is itself a SQLite database with the live schema: that day's
`market_message` rows (every column, storage classes intact), the
`collector_session` rows they reference, and the `data_quality_interval` rows
overlapping the day. gzip keeps it compact and readable with stock tools.

Timeline for day D (UTC): archived at D+1 00:30 UTC (`velocita-md-archive.timer`);
pulled and acknowledged at 21:30 Mac local time (`com.velocita.md-backup`);
deleted from stream.db at the first nightly run with D older than 3 days; the
nuc1 archive is removed once it is older than 7 days.

## The deletion gate

A day's rows leave stream.db only if all of these hold, checked every night:

- (a) the archive file hashes to its sidecar's sha256 (and the manifest agrees);
- (b) `acked/stream-D.ack` exists and names that same sha256;
- (c) D is more than `--retention-days` (3) days before today (UTC).

Deletes run in transactions of at most 5000 rows and only touch rows with an id
no larger than the archive's highest id. A day that gained rows after it was
archived (a clock stepped back) is left alone and reported as `late_rows`.

## Disk space

Deleted rows become free pages inside stream.db, and SQLite reuses them for new
rows, so the file stops growing at its high-water mark without any vacuum.
Returning space to the filesystem is a separate step, because a plain `VACUUM`
holds the write lock for minutes (md-stream's inserts would time out and it
would crash), and `VACUUM INTO` plus a swap would lose rows written meanwhile
and leave md-stream writing to the unlinked file. So:

- `md-archive --compact` runs only with md-stream **stopped**. It refuses if a
  row arrived in the last 120 s, if another process has the database open, or
  if free disk is under twice the used size. It VACUUMs and switches the
  database to `auto_vacuum=INCREMENTAL`.
- After that one-time compaction, every nightly prune also returns freed pages
  live with `PRAGMA incremental_vacuum` in bounded steps, which is safe beside
  the running recorder.

```sh
# on nuc1, as velocitas; md-stream is down for the duration (a recorded gap)
systemctl --user stop velocita-md-stream
podman run --rm --network none --userns=keep-id --read-only --cap-drop all \
    -v ~/src/aeron-raft-pi/model:/model:ro -v ~/velocita-md:/data \
    -e PYTHONPATH=/model/src -e PYTHONDONTWRITEBYTECODE=1 -e SQLITE_TMPDIR=/data/archive/tmp \
    docker.io/library/python:3.12-slim \
    python -m velocita_model.cli md-archive --db /data/stream.db --archive-dir /data/archive --compact
systemctl --user start velocita-md-stream
```

## Install on nuc1

Ship the source the usual way (git archive + tar over ssh; never rsync from the
Mac, see the deployment notes), so `~/src/aeron-raft-pi/model` has
`storage/md_archive.py`. Then, as `velocitas`:

```sh
cp ~/src/aeron-raft-pi/deploy/v2/systemd/velocita-md-archive.{service,timer} ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user start velocita-md-archive.service      # first run by hand: archives every complete day
journalctl --user -u velocita-md-archive -n 30
ls -l ~/velocita-md/archive
systemctl --user enable --now velocita-md-archive.timer
systemctl --user list-timers velocita-md-archive.timer
systemctl --user restart velocita-v2-prometheus          # loads the VelocitaMd* alerts from the shipped rules
```

The first run archives every complete day already in stream.db (up to 10 per
run). Nothing is deleted until the Mac has acknowledged.

## Install on the Mac

The job runs `tools/md-backup-pull` from the main checkout with
`/usr/bin/python3`. ssh to nuc1 must work without a prompt:

```sh
ssh -o BatchMode=yes velocitas@nuc1 true && echo ok
cd ~/aeron-raft-pi
python3 tools/md-backup-pull                              # first pull by hand
mkdir -p ~/Library/LaunchAgents
sed -e "s|__REPO__|$HOME/aeron-raft-pi|g" -e "s|__HOME__|$HOME|g" \
    deploy/mac/com.velocita.md-backup.plist > ~/Library/LaunchAgents/com.velocita.md-backup.plist
plutil -lint ~/Library/LaunchAgents/com.velocita.md-backup.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.velocita.md-backup.plist
launchctl kickstart -k gui/$(id -u)/com.velocita.md-backup   # run once now
tail ~/Library/Logs/velocita-md-backup.log
```

Remove with `launchctl bootout gui/$(id -u)/com.velocita.md-backup`. A Mac
asleep at 21:30 runs the job on wake; one that was off or offline catches up on
the next run (exit 2 = nuc1 unreachable; nothing was changed). Exit 1 names the
day that failed: it was neither kept nor acknowledged.

## Checking health

- Grafana/Prometheus: `velocita_md_archive_*` and `velocita_md_stream_db_bytes`
  (node-exporter textfile `velocita-md-archive.prom`). Alerts:
  `VelocitaMdBackupNotAcked` (an archived day unacknowledged for over 2 days),
  `VelocitaMdStreamDbLarge` (over 8 GB), `VelocitaMdArchiveFailing` (last run
  failed, none in 26 h, or never ran), `VelocitaMdArchiveBlocked`.
- nuc1: `journalctl --user -u velocita-md-archive -n 50` prints one line per
  day (`archived`, `prune D: pruned|retention|awaiting_ack|...`).
- Mac: `tail ~/Library/Logs/velocita-md-backup.log`;
  `cd ~/velocita-backups/md && shasum -a 256 -c *.sha256`.

## Restore a day for research (on the Mac)

```sh
cd ~/velocita-backups/md
shasum -a 256 -c stream-2026-09-28.sqlite.gz.sha256
gunzip -c stream-2026-09-28.sqlite.gz > ~/research/md-2026-09-28.db     # it is a SQLite database
sqlite3 ~/research/md-2026-09-28.db 'SELECT type, count(*) FROM market_message GROUP BY type'

# several days merged into one database (Python 3.11+; verifies each sidecar first; rerunning is harmless)
cd ~/aeron-raft-pi
PYTHONPATH=model/src python3 -m velocita_model.cli md-restore --into ~/research/md.db \
    ~/velocita-backups/md/stream-2026-09-2*.sqlite.gz
```

## When a day is blocked

`md-archive` exits 3 and `velocita_md_archive_blocked_days{reason}` is non-zero.
Nothing is deleted for that day until it is resolved.

- `archive_unverified`: the nuc1 archive does not match its sidecar. If the
  rows are still in stream.db (no `pruned/` record), remove that day's
  `.sqlite.gz`, `.sha256` and `.manifest.json` on nuc1 and start
  `velocita-md-archive.service`; the day is archived again. If the Mac then
  reports `differs`, keep its old copy aside (`mv X X.old`) and pull again.
- `ack_mismatch`: the ack names other bytes than the archive on nuc1. Compare
  the Mac copy's `shasum` with nuc1's sidecar before changing anything; remove
  the stale `acked/` file only once the Mac holds a copy matching the sidecar.
- `late_rows`: rows stamped inside an archived day arrived after it was
  archived (a clock step). Inspect them:
  `sqlite3 ~/velocita-md/stream.db "SELECT id, receive_time_ns FROM market_message WHERE id > <max_id from the manifest> AND receive_time_ns >= <start_ns> AND receive_time_ns < <end_ns>"`,
  then either archive the day again as above (removing its ack too) or delete
  those rows by hand if they are worthless.
