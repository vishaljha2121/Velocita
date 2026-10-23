# Executed installation commands

All commands ran from the Mac against NUC1. Password entry occurred only at the
interactive sudo prompt and is not retained here. SCP transferred public committed
source, binaries, catalog and manifests into fresh `.cache/velocita-build` staging
folders; it did not transfer credentials. No source-tree swap or cluster stop ran.

Initial dormant install:

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && cd "$HOME/.cache/velocita-build/swap-services-20261013-r5" && sudo -n python3 -I -B install_disabled.py --bundle "$PWD" --bundle-sha256 be7e435bf322b9e5d2b24a2df161188be6ed475c3fcbd472e8e058d1d5b08028 --apply > install-result.json'
```

Corrected dormant release, exact previous bundle required:

```zsh
ssh -t vishal@100.72.127.102 'sudo -v && cd "$HOME/.cache/velocita-build/swap-services-20261013-r5-fd3" && sudo -n python3 -I -B install_disabled.py --bundle "$PWD" --bundle-sha256 a67abe6eca61c74ada95e4fcef0a0f1e9f4978bd6119224eedff1f34c44d43b1 --previous-bundle "$HOME/.cache/velocita-build/swap-services-20261013-r5" --previous-bundle-sha256 be7e435bf322b9e5d2b24a2df161188be6ed475c3fcbd472e8e058d1d5b08028 --apply > install-result.json'
```

Do not rerun either install: fresh-path/exact-old-state checks deliberately refuse.
Installer calls `systemd-analyze verify --man=no` on both installed unit files,
then `systemctl daemon-reload`; it never calls start/enable/restart.

Read-only status commands:

```zsh
ssh vishal@100.72.127.102 'systemctl show velocita-swap-gateway.service velocita-swap-signer.service -p LoadState -p ActiveState -p SubState -p UnitFileState -p ExecStart'
ssh velocitas@100.72.127.102 'systemctl --user list-units --all --plain --no-legend "velocita-paper-stochastic-*"'
```

`build_probe.py` and `build-command.json` contain exact bounded container compiler
arguments. `native-smoke.json` contains the network-disabled native loader smoke.
No production authenticated-read command was executed against incompatible voters.
