# F3 catalog-v1 codec evidence — 2026-09-21

Base d44a4a9 plus uncommitted remediation. Owner approved
`docs/F3_CATALOG_V1_BYTE_REVIEW.md` before implementation. This evidence covers
the standalone codec/context prerequisite, not root installation or live sending.

## Initial RED

Main compiled the permanent positive test against compileable fail-closed codec
stubs. The independent adversarial test function was temporarily supplied as an
empty stdin translation unit because that second test file was still being built.
The positive test failed before reaching it:

```sh
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_execution_catalog_tests.cpp src/v2_execution_catalog.cpp \
  -x c++ - -o /private/tmp/v2-catalog-initial-red <<'CPP'
void run_execution_catalog_adversarial_tests() {}
CPP
/private/tmp/v2-catalog-initial-red
```

Observed exit 1: `valid catalog did not encode`. This is a missing-codec RED,
not a reproduced production economic loss or a compile-error substitute.

## Independent canonical oracle

The reviewer derived this fixture from the approved field table; the main agent
independently reconstructed it using Python's standard struct/hashlib libraries.
No production encoder output supplied the expected bytes or digest.

```python
import struct, hashlib
row = (struct.pack('<Q', 11)
       + struct.pack('<HQQQqqqqq', 1, 22, 33, 44, 10, 1, 100000, 1, 1000)
       + struct.pack('<HQQBBB', 1, 22, 55, 3, 3, 1)
       + struct.pack('<qqBHH', 10000, 100000, 8, 7, 9))
for value in (b'net-A', b'BASE', b'QUOTE', b'BASE/QUOTE'):
    row += struct.pack('<B', len(value)) + value
wire = struct.pack('<4sHHI', b'V2EC', 1, 1, 12 + len(row)) + row
print(len(wire), hashlib.sha256(b'velocita.execution-catalog.v1\0' + wire).hexdigest())
alternate = bytearray(wire)
alternate[107:123] = struct.pack('<qq', 100000, 10000)
print(hashlib.sha256(b'velocita.execution-catalog.v1\0' + alternate).hexdigest())
```

Observed: 156 bytes, root
`a299122f237ec96dca84da1912cfaddaacf83cd506bcf60bbe6d04dfcbc3fbf0`.
Alternate factors preserve the notional product but change the root to
`49fd4478812b43e262ec92e21b25d9a8fba772f808f1a5e44e32ac8470d213ff`.
The C++ suite must compare actual encoded bytes/digests to these constants, not
merely compare the two constants with each other.

## Scope boundary

The catalog has its own version 1. Kernel command/result and state-snapshot
versions remain 7 until installation, mandatory row binding and dependency
preflight can switch coherently to 8. Neither OrderEffect bytes nor TradingState
are changed by this standalone prerequisite. Send APIs remain quarantined.
Host tests cannot establish venue facts, authenticated IPC, quorum freshness,
target durability or complete F3 acceptance.

## Review corrections and permanent coverage

The first implementation draft counted fixed row fields as 96 rather than 116
bytes, before the four text-length bytes. A direct isolated reproduction of that
draft failed the independent fixture with `golden encoded length changed`
(exit 1). The final fixed part is 120 bytes and the maximum is 12+16*504=8076.
This was a review-stage draft defect, not evidence of a previously deployed bug.
Its reproduction was `/private/tmp/velocita-catalog-count-red.OFqkJg/probe`.

Main added explicit writer bounds/sticky failure and exact final-position checks.
A span's operator[] does not enforce its logical bound; ample backing storage
can hide this defect from sanitizers. Encoding now stages into bounded scratch
before copying, including when output aliases input rows. Every undersized
output span refuses with zero bytes/root and preserves the entire backing array.

Main also removed a draft refusal on nonempty decode destinations: it prevented
the malformed-input tests from reaching parsing. Failed decode now demonstrably
preserves the complete retained row, root and count, while successful decode
replaces by value. Callers must publish a const lineage context; replacement is
not safe while borrowers retain row pointers. The output owns rows, not pointers
into artifact bytes, pinned by destroying the caller's input after decode.

An unjustified blanket ban on the existing logical Replace capability bit was
removed; the approved masks did not ban it. This does not enable native amend or
overlapping replacement. The root helper was named `root_digest` to avoid member
name hiding. Both are implementation corrections, not new byte decisions.

An initial alternate-factor test compared two constants, which proved nothing
about production. It now encodes the alternate factors, compares the resulting
root to the independently derived constant, and refuses decode under the original
root. Other cases cover the 8076-byte maximum, the 17th row, invalid values/text,
duplicate/unsorted keys, repeated-version conflicts and account denominations.
Unused builder-text tails do not contribute to canonical identity.

Main's additional parser sweep tests every truncated prefix twice: once with the
original length/root and once with length repaired and digest recomputed. The
latter reaches structural parsing instead of merely failing header/hash checks.
Correctly hashed malformed fields, trailing bytes, oversized input and zero
expected root all refuse without changing the existing destination. This is a
bounded fixture sweep, not exhaustive enumeration of every possible artifact.

## Direct mutation verification

Production workspace sources were not mutated. Each isolated source copy was
compiled directly with the permanent tests, avoiding stale CMake object reuse:

```sh
c++ -std=c++20 -O1 -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_execution_catalog_tests.cpp tests/v2_execution_catalog_adversarial.cpp \
  /private/tmp/velocita-catalog-mutants.eWE6ru/fraction.cpp src/v2_digest.cpp \
  -o /private/tmp/velocita-catalog-mutants.eWE6ru/fraction
/private/tmp/velocita-catalog-mutants.eWE6ru/fraction
```

Use the corresponding mutant name below in place of `fraction`; copies differ
only at the named guard/domain, apart from later production constant/comment
cleanup. Main rebuilt and reran all six against the final permanent tests.

| Mutant | Change | Observed intended exit-1 assertion |
|---|---|---|
| root_check | Ignore expected-root mismatch | alternate factors decoded under original expected root |
| cross_row | Skip repeated instrument-version facts | cross-account same-version conflicting identity accepted |
| denomination | Skip same-account denomination consistency | one account with conflicting quote identity accepted |
| fraction | Remove product remainder check | fractional factor product was accepted |
| domain | Replace digest-domain separator zero with one | catalog domain root changed |
| lookup_account | Ignore account in lookup | lookup ignored the account binding |

The `fraction` mutant initially **SURVIVED** (exit 0). The old test changed both
the quotient and remainder, so quotient comparison alone killed that input.
Permanent `test_factor_requires_exact_divisibility` now uses tick=250,
lot=100001, decimals=6: truncated quotient is the expected 25, remainder is 250.
Main independently confirmed the new test kills the mutant. Recording this
survival is essential: the initial green suite did not prove exact divisibility.
Six killed targeted mutants do not prove all guards or all fault combinations.

## Final host GREEN and layout

Both all-test targets were rebuilt after the final test/source edits:

```sh
cmake --build /private/tmp/velocita-full-audit.lYB4P5/release \
  --target velocita-all-tests v2-layout-report -j2
audit_tmp=$(mktemp -d /private/tmp/velocita-cp007-release.XXXXXX)
TMPDIR="$audit_tmp" ctest --test-dir /private/tmp/velocita-full-audit.lYB4P5/release --output-on-failure
cmake --build /private/tmp/aeron-phase67-audit-build --target velocita-all-tests -j2
audit_tmp=$(mktemp -d /private/tmp/velocita-cp007-sanitized.XXXXXX)
TMPDIR="$audit_tmp" ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /private/tmp/aeron-phase67-audit-build --output-on-failure
```

Observed Release **37/37**, 6.98 s; Debug ASan+UBSan **37/37**, 43.93 s.
UB halt is runtime-configured; do not infer `-fno-sanitize-recover` from it.
Leak detection is explicitly disabled. The focused catalog suite also passed a
fresh direct compile with `VELOCITA_TARGET_CACHE_LINE_SIZE=128` and both test
translation units. Neither host cache-line configuration is a target-hardware gate.

Layout at settings 64/128: context **8488 bytes**, alignment **8**, separate
ceiling **16384**; maximum wire **8076**, ceiling **8192**. TradingState remains
**81360**, headroom **560** under 81920; max snapshot remains **65451**. The
context-size figure excludes temporary codec stack scratch. Validation compares
at most 120 row pairs; lookup at most 16 rows. No latency benchmark is claimed.

Main validated the rebuilt Release CTest JSON against the independent gate list:
**37 total / 34 V2**. Offline helper tests **40/40** (16 manifest, 9 read-only,
15 token), passive model **109/109**, all rerun with bytecode writes disabled.
No fresh TSan, image build, NUC/Pi/ARM64 gate, source-root freeze, venue contact,
credential use, commit, tag movement or live order. Protected tracked paths stay
clean and model remains untracked. F3 is still open at the integration boundary.
