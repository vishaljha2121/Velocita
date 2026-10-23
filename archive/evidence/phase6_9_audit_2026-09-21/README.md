# Reproducing the audit REDs

These are audit-only tests, not production fixes and not included in CTest.
Each mode below exits **1** for its named violated property on d44a4a9.
Exit 2 means fixture/setup misuse; a compiler error is never a RED.
Sources reuse existing suite fixtures but assert independent architecture
properties. The authority probe initializes nonzero controls through committed
commands and requires materializable bytes before partitioning.

Run from the repository root. Build first. Use fresh temporary directories;
the replication/WAL probes refuse existing targets and never erase voter data.

## Build commands used

The sanitizer library build already existed; it was rebuilt before CTest:

```sh
cmake --build /private/tmp/aeron-phase67-audit-build -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

To reproduce from scratch with local dependencies, configure an isolated build
with Debug, BUILD_TESTING=ON, cache line 64, C/C++ ASan+UBSan as appropriate,
`-fno-sanitize-recover=undefined`, and dependency source directories pointing to
the reviewed patched `build/_deps` trees. Do not download a different dependency
and call it the same candidate. The earlier Phase 6/7 report retains its exact
configure command and flags; the full audit records what was actually run.

Probe compilation (library order is intentional):

```sh
audit_dir=$(mktemp -d /private/tmp/velocita-audit-repro.XXXXXX)
audit_build=/private/tmp/aeron-phase67-audit-build
audit_sources=docs/evidence/phase6_9_audit_2026-09-21
c++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Ibuild/_deps/willemt_raft-src/include \
  "$audit_sources/replication_probes.cpp" \
  "$audit_build/libvelocita_v2_replication.a" \
  "$audit_build/libvelocita_v2_kernel.a" \
  "$audit_build/libvelocita_v2_evaluator.a" \
  "$audit_build/libvelocita_v2_contracts.a" \
  "$audit_build/libvelocita_core.a" "$audit_build/libwillemt_raft.a" \
  -o "$audit_dir/replication-probes"

c++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude "$audit_sources/gateway_probe.cpp" \
  "$audit_build/libvelocita_v2_kernel.a" \
  "$audit_build/libvelocita_v2_evaluator.a" \
  "$audit_build/libvelocita_v2_contracts.a" -o "$audit_dir/gateway-probe"

c++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude "$audit_sources/venue_probes.cpp" src/v2_venue_wire.cpp \
  -o "$audit_dir/venue-probes"

c++ -std=c++20 -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=undefined -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude "$audit_sources/wal_probe.cpp" \
  "$audit_build/libvelocita_v2_replication.a" \
  "$audit_build/libvelocita_v2_kernel.a" \
  "$audit_build/libvelocita_v2_evaluator.a" \
  "$audit_build/libvelocita_v2_contracts.a" \
  "$audit_build/libvelocita_core.a" "$audit_build/libwillemt_raft.a" \
  -o "$audit_dir/wal-probe"
```

Run separately; expected failures should not short-circuit the remaining probes:

```sh
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1
"$audit_dir/replication-probes" authority "$audit_dir/authority"
"$audit_dir/replication-probes" replacement "$audit_dir/replacement"
"$audit_dir/replication-probes" membership "$audit_dir/membership"
"$audit_dir/replication-probes" poison "$audit_dir/poison"
"$audit_dir/replication-probes" zero-controls "$audit_dir/zero-controls"
"$audit_dir/replication-probes" queues "$audit_dir/queues"
"$audit_dir/gateway-probe"
"$audit_dir/venue-probes"
"$audit_dir/wal-probe" "$audit_dir/wal"
python3 "$audit_sources/token_probe.py"
```

## Observed output, 2026-09-21

```text
authority:
majority_kill=2 stale_kill=1 old_authoritative=1 renewed=1 permitted=1 refusal=0
RED: isolated old leader renews new-risk authority after majority kill

replacement:
compacted=9 replacement_applied=0
RED: replacement cannot fetch compacted history

membership:
restarted_with_single_member_leader=1
RED: unchanged durable directory accepts a different quorum configuration

poison:
malformed_admitted=1
apply_error=raft_periodic failed: -4
restart_error=raft_periodic failed: -4
RED: malformed normal-lane proposal poisons durable restart

zero-controls:
authorized_effects=1 materializable=0
RED: public-command bootstrap authorizes an unreconstructable effect

queues:
admitted=2000 queued_results=2000 retained_log_entries=2000
RED: stalled consumer receives no bounded-queue backpressure in 2000 proposals

gateway:
cancel_guard_permitted=1 planned_sends=0
RED: planner drops reserved emergency quota although guard permits cancel

venue:
post_only_limit_to_market=1 body={"base_asset":"BTC","quote_asset":"USDC","qty":"0.003","side":"buy","qty_unit":"base","type":"market"}
both_specs_agree=1 price_a=2.5 price_b=25
wrong_instrument_encoded=1
RED: wire helpers do not enforce unchanged committed economics

wal:
sealed_before=2
flush_error=WAL completion order violation
stored_index=1
stored_index=1
RED: multi-block synchronous flush repeats the front descriptor

token:
mint_result=0 existing_token_mode=0o644
RED: existing token file retains group/world permissions
```

Stdout/stderr interleaving varied; values above preserve the observed content.
The queue probe is a growth witness, not a contractual choice of maximum 1999.
The metadata product equality probe shows ambiguity, not faulty multiplication.
The token probe mocks ALL subprocess calls and uses dummy material only.

## Fresh Release regression command

```sh
cmake -S . -B /private/tmp/velocita-full-audit.lYB4P5/release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -DFETCHCONTENT_SOURCE_DIR_AERON=/Users/vishaljha/aeron-raft-pi/build/_deps/aeron-src \
  -DFETCHCONTENT_SOURCE_DIR_WILLEMT_RAFT=/Users/vishaljha/aeron-raft-pi/build/_deps/willemt_raft-src \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release \
  --target velocita-all-tests -j2
ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
```

Result: 36/36. A future reproduction should use its own fresh directory and
retain compiler, flags and dependency identities alongside the output.
