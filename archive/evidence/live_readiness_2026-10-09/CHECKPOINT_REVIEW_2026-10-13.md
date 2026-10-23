# Integrated swap checkpoint and native qualification

The owner authorized review, a relevant source checkpoint, and qualification of
that exact commit on NUC/Pi. This does not authorize deployment or live trading.

## Scope and review

The checkpoint includes the exact-input SOL swap reducer and codecs, gateway and
protected-signer composition, transaction inspection, durable preparation/signing/
submission/reconciliation, account-history startup checks, bounded storage recovery,
and the current-term Raft read barrier. The real-transaction admission function
remains compile-time false, before resource or credential opening.

Shared files are assembled selectively against parent `6349134`. The unrelated
resting-paper backend, mode-selector experiment, gateway-loop changes, strategy
research, monitoring edits and deployment changes remain in the working tree.
The selected CMake modules and gateway test fixtures drop their incidental
resting-paper dependencies. No shared file is staged wholesale from the dirty
tree. Private responses, account proposals, credentials and Python caches are
excluded. The account tools include their authentication-helper dependency;
checkpointing that helper does not execute it or authorize money movement.

Review focused on the changed integration boundaries: authenticated caller/role
checks; command digest binding; retained payload ownership; bounded journals and
ambiguous-send recovery; account identity and freshness; synthetic-only fixture
macros; and current-term marker admission, persistence, replay and compaction.
This is not a claim of a new independent line-by-line audit of every component.
Previous component tests and mutation evidence remain supporting evidence.

The marker is type 103 with 32 domain bytes, uses reserved control capacity, and
creates no economic result or signing authority. Reads wait for current-term
application; the result watermark is not weakened. Replication epoch 2 and
snapshot/command schema 12 require a coordinated compatible cluster identity;
mixed-version rolling deployment is not supported. No host migration occurs here.

## Qualification procedure

1. Build and check the isolated selected source locally before staging its exact
   patch. Verify the staging set and commit with the requested trailer.
2. Export the commit in a Git bundle; clone it into new host-specific build
   directories. Verify HEAD and a clean checkout, mount source read-only.
3. Use the existing immutable native TLS candidate toolchains:
   NUC1 `076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f`;
   Pi `3f3cfb3403a2644b27bf21351e26683e62f6ad90fde1f1b1567c8d2279d4fb43`.
   They derive from the pinned development images with previously inventoried
   curl development dependencies. No packages or production images are changed.
4. Build Release with the optional swap runtime and local HTTPS/signer tests
   enabled. Pin compiler/test execution to CPU 3, one build job, 1 GiB memory;
   use a private network namespace with loopback only, no credentials, and
   durable scratch. Stop on insufficient disk rather than pruning images.
5. Run registered offline tests, account tests, independent schema goldens and
   strict validation of all eight process-drill artifacts. Retain commands,
   logs, source identity and binary hashes. Failures remain failures until a
   reviewed correction and rerun; compilation alone is not qualification.

The integrated gateway binary is qualified as a build artifact. Native local
fixtures do not establish deployed Aeron behavior, distinct production UIDs,
TM transaction economics or a release-container deployment. Existing services,
source mounts, paper ledger and image tags are unchanged.

## Remaining boundaries

Actual reviewed live funding/custody/session pins and fresh activation evidence;
remaining native/deployed failure drills and one-cluster paper release; external
heartbeat creation; deferred sponsor repayment and exact minimum net receipt;
and the exact separately approved live test with enforceable limits. Strategy
profitability and unattended rearming remain separate promotion questions.

Results and the final commit identity will be appended after qualification.

## Source checkpoint and local assembly checks

Committed `a890307b1d8e357cd8e27819b8f589fb48d64459`: 259 explicitly selected
paths. The original shared working files were not overwritten. Source staging
was checked against the assembled-source SHA-256 manifest before committing.
Three local integrated suites passed (Raft, runtime production, full gateway;
192.77 seconds), all eight process scenarios and independent JSON oracles pass,
44 account tests and 20 build-boundary regression tests pass. The source guard
and epoch-2 literal oracle pass. A first sandboxed process run could not listen
on its Unix socket; the permitted fixture run passed. That refusal is not a
code regression or a test pass.

Both hosts received the same source bundle, SHA-256
`b6265b67b5561bc82af9c11e214ef851ab135bd65840b258f960e62b8acf0b15`,
and verified clean HEAD before starting. NUC1 configured successfully. Pi's
first container refused because the delegated cgroup has no cpuset controller;
its retry retains CPU 3 through taskset plus an in-container affinity assertion,
and the same memory and CPU-quota limits. The original error log is preserved.
No service, credential or production image was modified.

Host output root on both machines:
`$HOME/.cache/velocita-build/integrated-a890307-20261013/qualification/`.
`driver.log` records stages, `build.log` compilation, `tests.log` CTest, and
`status.json` remains `qualified:false` until every required check passes.
Source is cloned separately in `../source`, and the isolated compiler cache is
`../ccache` (512 MiB cap). Build scratch is durable and the wrapper stops below
2.5 GB free disk. Native compilation uses CPU quota 1, taskset CPU 3, one job,
1 GiB memory and no swap. No native result is claimed while these jobs run.

Compiler diagnostics are retained rather than suppressed. Observed categories
include GNU `__int128` pedantic warnings, a legacy variant initialization warning,
and compact-statement indentation warnings in swap code. The new response enum
produces a command-client switch warning, but `decode_response()` rejects its
zero response-size classification before entering that switch. No warning-free
build claim is made; warnings are not automatically classified as defects.


## Native compilation correction — 062c75a

Pi stopped at the runtime configuration test because `std::strlen` lacked its
explicit `<cstring>` declaration. The previous local compiler accepted a
transitive include; this was a portability defect in the test, not a failed
assertion. Commit `062c75a17eeb6c2da4b46fdd49dca0b0fd6a58ca` adds only that
header. No production behavior or assertion changed.

The NUC qualification container was stopped before updating its isolated source;
Pi had already exited. Both clean build clones were advanced to the correction
from a verified bundle (SHA-256
`17da5168031d93e2e709b79f120ef2f9b6b0ec12a8e238e39bb74c71aadc447c`).
Original logs are retained under `qualification/attempt-a890307/`. Existing
objects, pinned dependencies and compiler caches are reused. Both corrected
jobs started at approximately 07:15 UTC October 13, with the same offline,
CPU-3, single-job, 1-GiB limits. The resume driver does not recopy dependencies;
it reconfigures and verifies the clean corrected source before compiling.
The original failed/interrupted attempt is not a qualification pass.

Future build-speed work: preserve incremental caches across source checkpoints;
share identical production compilations between test targets while keeping
fixture-macro variants separate; run focused checks during development before
full release qualification. Extra build parallelism requires CPU/memory planning
around the running paper cluster. No such build-graph changes enter this run.


A second native compile failure exposed the same missing declaration in the
process drill (`std::memcpy`). An audit of all committed C++ string-API uses also
found `std::strlen` in the production-runtime test. Commit
`02728cafdc96047a80f6febbf8edcc4e9e347249` adds `<cstring>` to those two test
translation units; no assertions or production code change. Both hosts resumed
incrementally on that commit. The preceding logs are preserved in
`attempt-062c75a/`; corrected bundle SHA-256 is
`2ec852684a4332734e84b617c6682c8413699456e75bda458d78eca516379c61`.
The original 1,889 build steps comprise 797 C++ dependency scan/metadata tasks,
873 compilations and 219 link steps, not 1,889 installed packages.


## Full native suite findings and correction — 2dc439a

Both architectures compiled `02728ca`. NUC's full 201-test CTest attempt reported
11 failures, so compilation is explicitly not a qualification pass:

- Three registered offline tests lacked committed reader/helper files. The
  corrected checkpoint includes those existing helper dependencies and tests;
  33 focused Python checks pass locally without external network requests.
- The generation-base numeric seal still bound epoch 1. The independent Python
  oracle reproduces the original seal and computes epoch 2; the corrected C++
  literal and the numeric-literal audit agree. Corrupting one literal byte is
  rejected by the named oracle. No encoder-derived expected value was used.
- The complete crash-image oracle rejected newly supported byte-preserving
  quarantine. Its revised comparison permits only the two fixed temporary-file
  renames; it rejects missing/corrupt evidence and every unrelated mutation.
  All original crash cuts remain exercised. This suite and generation-base suite
  pass locally (3.51 seconds total).
- Four signer/runtime fixture failures came from container UID 0. Rootless
  Podman still presents root inside its default namespace. Production correctly
  refuses root. The corrected runner uses `--userns=keep-id --user UID:GID`,
  with nonzero UID asserted; it does not change or bypass production policy.
- The synthetic TLS redirect lacked explicit zero-body framing. The fixture now
  emits `Content-Length: 0`; exact 302 classification and no-follow assertions
  remain. Native verification is pending.
- Native node-process test 189 could not elect a ready leader. This remains an
  unresolved qualification failure, not assumed fixed by the runner change.

Commit `2dc439a40deafc6b054a16b52cd6405e3acbd7bf` contains fixture corrections
and missing offline dependencies, no production changes. Bundle SHA-256:
`610f323f2c6cac87e8f961e93178ce62bf39e9edf96971de3610a61be0f4ba92`.
`native-nonroot.py` runs the 11 failed checks first, then the full suite if those
pass. It uses fresh synthetic HOME/TMPDIR paths, retains the original build
objects, and preserves preceding logs under `attempt-02728ca/`.


## Runner resources and Pi request timing — cbb893c

After private-directory setup, NUC's focused preflight passed 11/12 checks; only
the native node-process suite failed. Cgroup evidence reports `memory.peak =
1073741824` and `oom_kill = 1`. A fixture voter was killed by the one-GiB test
container cap; retained health pages showed one stopped voter and the surviving
voter unable to elect. This is not evidence of a Raft election defect.

The runner had omitted the deployed `AERON_TERM_BUFFER_LENGTH=1048576` setting;
the pinned Aeron default is 16 MiB. Corrected runs match the deployed 1 MiB UDP
term buffers and retain the same 1 GiB cap, CPU quota and CPU-3 affinity. No
production service settings change. Qualification root mode is 0700 with umask
0077, so synthetic account-session tests obey the real parent-directory checks.
The earlier 0775 mounted work directory was correctly refused.

Pi's first full suite had the same 11 failures plus the process drill. Its signer
returned refusal before the intended crash points. The fixture used
`serve_once(session,100)`, but this is a deadline for the entire authenticated
request and replicated durable authorization, not merely the idle accept poll.
Commit `cbb893caec2d62def4e6b1474637afba086b8d5b` sets a finite two-second
request budget, as used by the signer service fixtures, below the ten-second
client and overall worker bounds. Crash points, economic checks and exact
signature/send counts are unchanged. Native verification is still required.
Bundle SHA-256: `3c3f0107ce6af0f1bc0b5379ab036fdedb50dbdec3b2b5e435b2b8f9359896aa`.

`native-final.py` adds the deployment-matched Aeron environment and per-stage
cgroup memory diagnostics. It validates all eight result artifacts from both the
focused preflight and final full suite. Pi's initial keep-id launch required a
one-time Podman storage ownership remap; it was not another source rebuild.
Its superseded `2dc439a` preflight was stopped after that remap, logs preserved,
then resumed incrementally on `cbb893c`.


## Multi-process capacity and closed-child bridge — 1d2c820

Matching the deployed Aeron buffer size did not by itself fit the multi-process
node test inside 1 GiB. NUC's next preflight again passed 11/12 tests and recorded
`oom_kill = 1` at the cap. The current qualification allowance is 2 GiB/no swap,
still one CPU and one compiler job. Before launch, MemAvailable was 5,389,536 KiB
on NUC and 2,608,796 KiB on Pi; both passed the 2.25-GiB admission check. The owner
authorized pausing the production cluster if capacity requires it; it has not
been paused because current capacity permits these bounded isolated tests.

Pi's two-second signer budget fixed the earlier signing refusals: six of eight
process scenarios passed. Cases 5 and 7 then exposed a parent bridge ordering
race at intentional child SIGKILL: the Unix stream may reset before waitpid
reports exit. Commit `1d2c820a18066b4616af6bf8cba65fccc32f6005` lets only the
parent bridge observe EOF/ECONNRESET/EPIPE, retain complete already-read frames,
and discard undeliverable replies. Worker failures still throw; bounded waitpid,
actual SIGKILL, exact signature/send counts and economic-state checks remain.
A closed-peer socket oracle tests the new parent behavior and strict worker
failure. Local cases 5/7 and their independent evidence validators pass.
Bundle SHA-256: `4fb4c3d53f1dd87ff95fce9fdb7c1951773e0fd64fa24ac7282f0a2f64f6baf1`.

Both native jobs now run this exact clean commit with `native-qualified.py`.
Prior attempts are preserved under `attempt-cbb893c/`. Full qualification remains
open until every focused and full-suite check, oracle and artifact validator passes.


## October 13 maintenance capacity correction

Both `1d2c820` 2-GiB preflights passed 11/12 checks, including all gateway/signer
process scenarios. The multi-voter test failed after Linux killed test children:
NUC memory peak 2,147,483,648 bytes and two OOM kills; Pi the same peak and one
OOM kill. The timeouts are not accepted as correctness passes. Earlier October 9
qualification already documented a 2.75-GiB requirement for this test; the initial
runner budget did not carry that requirement forward.

The owner explicitly authorized freeing memory and pausing services. On October 13
around 08:05 UTC, stopped NUC1's stochastic edge, paper gateway and observer first,
then stopped each stochastic voter/exporter pair on NUC1, Pi and NUC2. The edge was
already not running. No ledger, WAL, configuration, image or credentials were reset.
Monitoring, alerts and market-data recording remain active; down-voter alerts are
expected during this maintenance interval. Host pre-stop states are recorded under
`~/.cache/velocita-build/integrated-a890307-20261013/maintenance-20261013/`.

Post-pause available memory: NUC1 6,201 MiB; Pi 3,248 MiB. Both now run the same
committed binaries in a 2.75-GiB/no-swap container, CPU 3, one CPU quota, with a
3,100,000-KiB host memory admission check. The failed node test runs first, followed
by all 201 tests and independent validators. No code rebuild is required (Ninja
checks the existing build). Previous attempt logs are preserved under
`attempt-1d2c820-2g/`; current runner is `native-maintenance.py`. Qualification and
restoration remain pending until verified.

At 08:07 UTC the focused multi-voter test passed on both hosts in about 65 seconds.
NUC peak was 2,648,178,688 bytes; Pi 2,666,024,960 bytes. Both recorded zero OOM
events/kills and no cgroup-limit hits. Full 201-test suites then started in parallel.
This directly validates the capacity correction; no deadline or assertion changed.

## Supplementary account fixture — 04c991d

NUC completed all 201 CTest cases (524.01 seconds), both independent schema checks,
and zero OOM events/kills. Its supplementary 44 Python account checks then found
four setup errors: the older session test created scratch directories inside the
read-only `/source` mount. They did not reach their assertions.

Commit `04c991dbbab225ca564ea18eeb03627f83a6d2b6` changes only
`tests/test_tm_account_session.py`: use the same private durable HOME-cache scratch
convention as the other account tests. All 44 checks pass locally; no production
code, CMake input, C++ test or assertion changes. Bundle SHA-256:
`934a97b286d427966fc99baa06c3e0a66dc0b3216002706e8f896a8bdddb253d`.

`native-finalize.py` verifies that this one Python test is the entire tree delta
from `1d2c820`, requires the completed 201/201 CTest XML and zero OOMs, runs Ninja
incrementally and compares every top-level executable hash before/after, then
runs the corrected account tests and independent schema/process-artifact checks.
Native CTest provenance remains explicitly `1d2c820`; the final source checkpoint
is `04c991d`, with identical executable hashes. This avoids a redundant complete
C++ rebuild or full-suite rerun for a supplementary Python scratch-only change.

## Pi full-suite follow-up — 86f4408

Pi completed the full suite in 1,170.90 seconds with 199/201 passes and zero OOM
events/kills (peak 2,744,483,840 bytes). The native multi-voter process test passed.
Two failures remain in that retained run: process-drill case 2 failed during its
unknown-state recovery stage, and the node busy-frame test read a cumulative
counter from an earlier event as if it described the currently parked gate.

Review found the process fixture stopped advancing all three in-parent voters
while waiting up to two seconds for the separate signer to exit. This creates an
extra all-voter stall during a nominal gateway-only crash. The correction services
voter turns and the signer bridge throughout that bounded wait, and logs wait
duration/turn counts. The busy-frame check now waits for an increment after the
persistence worker parks, retaining the exact held-frame and reducer assertions.
No production code or request deadlines changed.

Local node tests and all eight process scenarios plus independent artifact checks
pass. In 26 signer shutdown waits, the maximum was 2,001 ms with 80,044 total
voter turns, demonstrating continued replica progress. The initial local process
run could not bind its Unix socket inside the sandbox and is retained as a failed
environment run; the allowed local-socket run is the passing result.

Committed `86f440806c6adc6f4c1dc9dc7a271e3d95293d76`. Its entire delta from the native
full-suite baseline is three test files (the two C++ fixtures and Python scratch
setup). `native-fixture-final.py` retains the 199 unaffected checks, verifies no
other executable hash changes, and runs the two affected checks three times each
on both architectures, followed by 44 account tests and independent oracles.
Earlier NUC completion on `04c991d` remains valid for that checkpoint; current
combined qualification remains pending until these repetitions pass.

## Final result and restoration

Both hosts passed final selective qualification at `86f4408`: the 199 unchanged
registered checks retain their full-suite passes; each of the two corrected
native fixtures passes three repetitions. Each host also passed 44 account
checks, 24 independent process-artifact validations and both schema oracles.
Only those two test executable hashes changed among 193 checked. See
`native-integrated-a890307-20261013/RESULT.md` for exact provenance and limits.

The owner-authorized pause ended around 08:41 UTC. Restored the original three
voter/exporter pairs, then NUC1 gateway/observer after confirming Raft convergence.
All three voters report ready=1, failed=0, term 17, leader 2, current/commit/applied/
durable 17822. Gateway: paper, kernel_lots=venue_lots=0, new-risk-stopped=0.
One cluster, unchanged images/incarnation/state. The edge was already inactive
before maintenance (October 12 market-data timeout exit) and remains inactive.
No new release or live mode was deployed.
