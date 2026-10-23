# Deployment-tool qualification and owner staging

Python source checkpoint: `24ecbecbedc6333b66d37e8daa946533736f8685`.
Executable/model checkpoint remains `1f8d7b63c03c4ef1c0c916d833cdf8513f42a2dc`.
NUC1: bulbazar-NUC, unprivileged velocitas, Python 3.10.12. No sudo or venue access.

- 13 installer tests and 24 observer tests passed from the exact committed archive.
- Initial installer qualification failed because the existing build-cache ancestors were mode 0775. The production protected-path check correctly rejected this synthetic fixture. The initial logs/result are retained.
- Repeated qualification used a fresh private HOME under `/home/velocitas/.cache/velocita-qualification-home-24ecbec`, with umask 0077. No code or existing directory permissions changed. Tests still exercise real filesystem/locks, with host identity/systemd and account fixtures injected.
- All 60 archived source files verified against [the source manifest](SOURCE_ARCHIVE.json). The same archive was staged under the owner's source directory. Binary and Python payload pins passed the non-applying helper check. Observer and timer checksums passed; owner shell syntax passed.
- [Native result](qualification/nuc1/native-private-home-result.json) and [owner-stage result](OWNER_STAGE_RESULT.json) distinguish qualification/staging from installation.
- Post-staging unprivileged readback: gateway inactive/dead, signer failed/failed, both main/control PID 0; driver inactive/dead, PID 0. Driver state and client session absent. This is process evidence, not a fresh authenticated Raft/account read.

## Owner command

```zsh
ssh -t vishal@100.72.127.102 'sh "$HOME/.cache/velocita-build/focused-deploy-1f8d7b6/source/docs/evidence/automated_launch_2026-10-15/focused-deploy/owner-install.sh"'
```

This installs the pinned runtime under retained start guards, restores the two missing caller keys without deleting the originals, and updates the read-only observer. It does not start trading or the strategy driver. Share output before the startup/seal procedure. If it fails, preserve the partial state and do not rerun.

## Qualification commands

From the extracted committed source, with the private HOME and umask above:

```sh
python3 -B -m unittest discover -s docs/evidence/automated_launch_2026-10-15/focused-deploy -p test_upgrade_first_start.py -v
python3 -B -m unittest discover -s tests -p test_live_observer.py -v
```

The source archive was created using `git archive 24ecbec` with explicit helper/runtime/observer/test dependency paths, never the dirty working tree. Neither source archival nor the payload check executes installation. No C++ or economic policy changed in this checkpoint.
