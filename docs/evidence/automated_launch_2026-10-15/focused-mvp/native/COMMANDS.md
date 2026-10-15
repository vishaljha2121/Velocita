# Offline native qualification commands — October 15

Qualified source: `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`.
These commands operate on dedicated build caches, not deployed source or services.
The exact executed container argv and build/test argv are retained in each host's evidence.

## Prepare on the Mac

```sh
rev=1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc
out="$HOME/.cache/velocita-build/focused-native-$rev"
mkdir -m 700 "$out"
git bundle create "$out/source.bundle" 54a5fcb..HEAD
for script in launch.py native-focused.py collect.py; do
  git show "$rev:docs/evidence/automated_launch_2026-10-15/focused-mvp/native/$script" > "$out/$script"
done
```

The actual preparation used Python `subprocess` argument lists for these operations. It refused
an existing output directory. The launcher verifies the bundle's HEAD equals the exact supplied
revision before checking out a clean dedicated cache. Never bundle an uncommitted overlay.

## Copy and launch independently on NUC1 and Pi

Use `velocitas@100.72.127.102` for NUC1 and `velocitas@100.84.151.116` for Pi.
For each host (the two hosts ran in parallel):

```sh
host=velocitas@100.72.127.102
ssh "$host" "mkdir -m 700 .cache/velocita-build/focused-native-$rev"
scp "$out/source.bundle" "$out/launch.py" "$out/native-focused.py" "$out/collect.py" \
  "$host:.cache/velocita-build/focused-native-$rev/"
ssh "$host" "python3 -B .cache/velocita-build/focused-native-$rev/launch.py --commit $rev --bundle .cache/velocita-build/focused-native-$rev/source.bundle"
```

The launcher starts a detached, network-disabled Podman container with pinned image, one CPU,
core-3 affinity, 2 GiB RAM, no swap, unprivileged user and no production credentials. It refuses
existing evidence/container names, competing cache users, dirty source and inadequate disk.
The runner stops on any failure; it never restarts production services or retries a failed suite.

## Follow progress

```sh
ssh velocitas@100.72.127.102 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc/driver.log"'
ssh velocitas@100.84.151.116 'tail -F "$HOME/.cache/velocita-build/bounded-native-20261013-1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc/driver.log"'
```

Use `build.log` or `tests.log` in the same directory for detailed compiler/CTest output.
`status.json` is authoritative for whether the whole runner qualified, not a successful compiler
exit or individual drill alone.

## Collect

Run the committed `collect.py --commit <exact revision>` on each host. Its stdout is a gzip tar
stream of allowlisted logs and synthetic case results, with `artifact-manifest.json`. Extract
using Python's `tarfile` data filter and verify every SHA-256 before recording success. Do not
copy fixture key/bearer files, real credentials or the entire scratch directory.

## Earlier attempts

The same launch/collection scheme was used for `c19a675`, `887d6f5` and `26b1e51`; retain their failures.
Pi capacity archival used the adjacent `archive-pi-cache.py` and its recorded JSON result.
The private archive and manifest remain on Pi. No production WAL, recovery journals, images,
funding, credential files or services were modified.
