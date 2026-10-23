# Local validation commands

All runs used an isolated `git archive HEAD` source tree plus the explicitly listed candidate
files in `candidate-files.json`, not the shared dirty CMake/gateway tree. No release was installed.
Dependencies were the existing pinned checkouts. Commands below are reference commands; preserve
existing output directories instead of rerunning a fixture over its prior evidence.

```zsh
S="$HOME/.cache/velocita-work/strategy-grant-20261015"
B="$HOME/.cache/velocita-build/strategy-grant-20261015"
cmake -S "$S" -B "$B" -DCMAKE_BUILD_TYPE=Release \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=128 \
  -DFETCHCONTENT_SOURCE_DIR_AERON="$HOME/aeron-raft-pi/build/_deps/aeron-src" \
  -DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT="$HOME/.cache/velocita-deps/willemt-raft-cp076" \
  -DVELOCITA_BUILD_SWAP_RUNTIME=ON -DVELOCITA_TEST_HTTPS=ON \
  -DVELOCITA_TEST_SWAP_HTTPS=ON -DVELOCITA_TEST_SWAP_GATEWAY_TLS=ON \
  -DVELOCITA_TEST_SWAP_SIGNER=ON
cmake --build "$B" -j 6 --target \
  v2-swap-integration-tests v2-swap-paper-tests v2-swap-continuous-drill \
  velocita-v2-client v2-model-replay-tests v2-state-snapshot-tests \
  v2-kernel-codec-tests v2-layout-tests v2-kernel-control-tests \
  v2-kernel-policy-tests v2-pinned-receipts-tests v2-executable-policy-kernel-tests
ctest --test-dir "$B" --output-on-failure -R \
  '^(v2-swap-integration-tests|v2-swap-paper-tests|v2-swap-continuous-drill|v2-model-replay-tests|v2-state-snapshot-tests|v2-kernel-codec-tests|v2-layout-tests|v2-kernel-control-tests|v2-kernel-policy-tests|v2-pinned-receipts-tests|v2-executable-policy-kernel-tests)$'
python3 -B tests/client_strategy_grant_tests.py --client "$B/velocita-v2-client" --output "$S/client-grant"
python3 -B docs/evidence/bounded_launch_review_2026-10-13/bison/client_amendment_tests.py \
  --client "$B/velocita-v2-client" --output "$S/client-legacy"
python3 -B tests/mutation_swap_strategy_grant.py --build "$B" --output "$S/mutations-qualified.json"
python3 -B tools/v2_schema_goldens.py --check
python3 -B tools/v2_schema_goldens.py --check-literals --schema 12 --replication-epoch 2
```

The process drill needs local Unix sockets; the initial sandbox run returned EPERM on bind.
It passed when that local IPC permission was available. Its HTTP implementation is the fake venue.
Mutation compilation is direct, in fresh directories, with object/binary hashes retained in JSON.
The legacy replay probe is `tests/v2_swap_grant_legacy_replay.cpp`, separately compiled against
old and candidate include/libraries using each CMake link command with the test object replaced.
The unchanged core source in the old cache was checked byte-for-byte against HEAD; outputs are in
`legacy-replay.json`. The expected old digest is frozen into the integration test for native runs.
