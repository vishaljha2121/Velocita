# V2 Phase 1 ARM64 Linux container candidate evidence

- Date: 2026-08-31
- Decision: compatibility candidate retained; Raspberry Pi acceptance pending
- Branch: `v2/phase-1-domain-contracts`
- Contract commit: `a6ee8df5870a`
- Gate-script commit: `fa938ff`
- Runtime: native ARM64 Colima VM using Apple Virtualization.Framework
- Container user space: Ubuntu 26.04 LTS, ARM64
- Compiler: GCC 15.2.0
- Resource envelope: 4 CPUs, 4 GiB memory, 256 MiB `/dev/shm`
- Contract source root:
  `6eafe3ffe4b78819682517e78846e8b997cc8e7adb4735cfd222397a99b52640`

## Decision and scope

The Phase 1 contracts now pass a second, materially different implementation
environment: Linux rather than macOS, GCC rather than Apple Clang, and the
Linux ARM64 ABI rather than the Darwin ARM64 ABI. This reduces the risk of
compiler assumptions, platform-only headers, undefined behavior, endian
mistakes, or accidental dependence on macOS semantics reaching the Pi.

This result is **not** Raspberry Pi hardware acceptance. A container cannot
reproduce the Pi 5 kernel, SD-card durability and latency, CPU governor,
thermal behavior, power supply, undervoltage flags, or physical device failure
modes. It is a compatibility and memory-safety gate between workstation tests
and target-hardware tests.

The retained local runs are `20260831T203424Z` and the post-`.dockerignore`
reproducibility run `20260831T223351Z`. Both passed; the second reduced the
image build context from about 239 MB to 838 KB.

## Reproducible environment

The outer runner is:

```sh
./deploy/run-v2-phase1-container-gate.sh --yes
```

It builds a pinned `linux/arm64` Ubuntu 26.04 image, verifies that both the
kernel machine and user space are ARM64, and runs with Rosetta disabled in the
Colima VM. Aeron and willemt/raft are fetched at the exact commits recorded in
`CMakeLists.txt`; CMake is then forced into disconnected dependency mode.

The test container is read-only except for three explicit locations:

- `/tmp`, a bounded temporary filesystem;
- `/work`, a named build-cache volume keyed by the Phase 1 content root; and
- `/evidence`, the timestamped evidence directory on the host.

All Linux capabilities are dropped and `no-new-privileges` is enabled. These
restrictions make hidden filesystem or privilege assumptions fail during the
gate instead of appearing later in deployment.

## Identity and dependency evidence

The gate recomputed the Phase 1 source root inside the container and matched:

```text
6eafe3ffe4b78819682517e78846e8b997cc8e7adb4735cfd222397a99b52640
```

Pinned dependency commits:

```text
Aeron:        5b62f21d917af027cdf5a3241aa5f355149b04fa
willemt/raft: e428eeb921a014192d1d703dd317f3f29f5916c5
```

The content root is more useful than a host Git commit for this gate because
deployment syncs may intentionally exclude `.git`. It binds the evidence to
the contract-bearing CMake, headers, sources, and tests rather than to an
unverifiable directory name.

## Test matrix

| Configuration | Scope | Result |
|---|---|---:|
| Release | 3 legacy suites + 3 V2 suites | 6/6 pass |
| Release with benchmark instrumentation | 3 V2 suites | 3/3 pass |
| Debug with ASan + UBSan | 3 V2 suites | 3/3 pass |

The instrumented build proves that the compile-time benchmark path does not
change contract behavior. AddressSanitizer checks invalid memory access and
object lifetime errors; UndefinedBehaviorSanitizer checks operations such as
invalid shifts, alignment, and signed arithmetic undefined behavior. Passing
sanitizers is strong negative evidence, not a proof that no defect exists.

## Codec measurement

One 100,000-iteration Release sample:

```text
policy_encode p50_ns=125 p99_ns=167
policy_decode p50_ns=83 p99_ns=126
policy_sha256 p50_ns=2418 p99_ns=3210
```

The encoded ceilings remained:

| Value | Maximum bytes |
|---|---:|
| policy payload | 516 |
| decision-frame payload | 381 |
| order-effect payload | 165 |
| venue-observation payload | 116 |
| generic maximum envelope | 65,620 |

These are codec micro-measurements inside a virtualized Linux environment.
They prove that the benchmark executes and preserve a regression reference;
they are not Raft throughput, OMS capacity, Pi latency, or a production SLO.

## Remaining Raspberry Pi gate

Phase 1 remains a candidate until the real Pi runs:

```sh
sudo -v
./deploy/run-v2-phase1-gate.sh --yes
```

Acceptance requires the same source root, canonical bytes, fixed-point
results, malformed-input behavior, and passing tests, plus clean Pi power and
thermal evidence. Container and Pi results must be retained separately so a
portable correctness result cannot be mistaken for a target capacity result.
