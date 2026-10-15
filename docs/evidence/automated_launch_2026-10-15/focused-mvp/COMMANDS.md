# Commands and build method used

All work was local and offline. No SSH, sudo, live service, credential restoration or venue commands were executed in this batch. Local socket tests required sandbox permission; that permission did not start production services.

## Isolated source

The source snapshot was created with `git archive HEAD` at `12d0be6` into
`$HOME/.cache/velocita-work/focused-mvp-20261015`, then only the batch-owned C++ files/tests were overlaid.
The gateway main file came from HEAD plus [the one-line patch](gateway-main-scoped.patch).
Unrelated shared CMake, kernel/gateway, test and documentation changes were not part of the build.
The exact file and executable hashes are in [QUALIFICATION.json](QUALIFICATION.json).

Configuration used Unix Makefiles, Release, four compiler jobs, cached pinned dependencies,
`VELOCITA_BUILD_SWAP_RUNTIME=ON`, `VELOCITA_TEST_SWAP_SIGNER=ON`,
`VELOCITA_TEST_SWAP_GATEWAY_TLS=ON`, `VELOCITA_TEST_SWAP_HTTPS=ON`, and
`OPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3`. An initial Ninja configure failed because Ninja
was unavailable; no result from that attempt was counted.

## Re-run the qualified local binaries

```zsh
cd "$HOME/aeron-raft-pi"
B="$HOME/.cache/velocita-build/focused-mvp-20261015/cpp-make"
ctest --test-dir "$B" -R '^v2-swap-(continuous-drill|signer-session-tests|signer-production-gate|signer-service-tests|signer-verified-tests|runtime-config-tests|signing-connection-tests|submission-resolution-tests|submission-session-tests|prepare-session-tests|gateway-runtime-tests|runtime-production-tests)$' --output-on-failure
```

`v2-swap-continuous-drill` creates a fresh fixture directory per invocation and refuses to reuse an existing one. It uses generated test keys and a fake HTTPS implementation; it does not contact TM or Solana. Its test transport and same-UID Unix IPC do not qualify Aeron networking, TLS or production UID isolation.

Python checks used:

```zsh
cd "$HOME/aeron-raft-pi/model"
PYTHONPATH=src python3 -B -m unittest discover -s tests -p 'test_live_swap*.py'
cd "$HOME/aeron-raft-pi"
python3 -B -m unittest discover -s tests -p 'test_stage_driver.py'
python3 -B -m unittest discover -s docs/evidence/automated_launch_2026-10-15 -p 'test_*installation.py'
python3 -B -m unittest discover -s docs/evidence/automated_launch_2026-10-15 -p 'test_verify_installed.py'
python3 -B -m unittest discover -s docs/evidence/automated_launch_2026-10-15 -p 'test_restore_caller_keys.py'
```

## Future checkpoint boundary

No commit or staging occurred. A future scoped commit must include the owned paths in the qualification manifest plus this evidence and the narrow tracker/ADR/changelog amendments. Never stage `src/v2_gateway_main.cpp`, `CHANGELOG.md` or the shared trackers whole: use only this batch's hunks. Other sessions' dirty `CMakeLists.txt`, gateway loop and gateway-node tests remain untouched.

NUC/Pi qualification must use that committed source. Only afterwards review host installation and the owner-run caller-key restoration. Do not rerun arm, funding, retirement, a fresh driver staging over existing state, or a live launch from this command note.
