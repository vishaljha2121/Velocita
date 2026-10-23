# F5 snapshot install preflight focused evidence

Date: 2026-09-23

Scope: the pure, read-only local snapshot/suffix classifier in
`snapshot_install_preflight.hpp/.cpp` and its standalone test. This evidence
does not claim snapshot transfer, installation, persistent generation writes,
selector publication, result reconstruction, sender authentication, serving
authority, target hardware, power-loss behavior, or end-to-end F5 closure.

All direct compiles used the current required host ABI definition
`VELOCITA_TARGET_CACHE_LINE_SIZE=64`. The focused helper performs no I/O or
allocation and returns only a borrowed descriptive suffix range. It grants no
permission to install, publish, delete retained evidence, or release results.

## Original runtime stub RED

The first compile attempt omitted the required cache-line definition and failed
at compile time; it is not counted as RED evidence. After correcting that
invocation, the deliberate implementation stub compiled and the test exited 1
at the intended behavior assertion. `stub-red.txt` is a transcription of that
direct tool output; the stub was then replaced rather than retained as a mutant.

```sh
c++ -std=c++20 -Wall -Wextra -Werror \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_snapshot_install_preflight_tests.cpp \
  src/v2_snapshot_install_preflight.cpp \
  -o /private/tmp/v2_snapshot_install_preflight_red
/private/tmp/v2_snapshot_install_preflight_red
```

## Focused GREEN and sanitizer GREEN

`green.txt` and `sanitizer-green.txt` transcribe the exact successful outputs.

```sh
c++ -std=c++20 -Wall -Wextra -Werror \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 -Iinclude \
  tests/v2_snapshot_install_preflight_tests.cpp \
  src/v2_snapshot_install_preflight.cpp \
  -o /private/tmp/v2_snapshot_install_preflight_green
/private/tmp/v2_snapshot_install_preflight_green
```

```sh
c++ -std=c++20 -Wall -Wextra -Werror \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -fno-omit-frame-pointer -Iinclude \
  tests/v2_snapshot_install_preflight_tests.cpp \
  src/v2_snapshot_install_preflight.cpp \
  -o /private/tmp/v2_snapshot_install_preflight_san
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  /private/tmp/v2_snapshot_install_preflight_san
```

## Review-driven oracle corrections

Two permanent boundary cases were corrected during review before the final
GREEN, but neither has a separately retained failing execution and neither is
claimed as an observed RED:

- an incoming snapshot beyond the complete local tail is a valid no-splice
  description when local commit is not beyond that incoming index;
- the installed old snapshot prefix is committed history even when the mutable
  metadata commit checkpoint trails it, so a lower-term incoming prefix refuses.

The covered-prefix-only recovery layout and malformed incoming-boundary cases
were likewise added during review and passed in the final focused runs. Only the
deliberate original implementation stub above is labeled the component RED.

## Original integration RED retained

The unchanged `tests/v2_snapshot_catchup_red.cpp` was freshly compiled against
the supplied CP034 Release archives and run with a fresh, initially absent
fixture root. `original-lagger-red.txt` transcribes the exact output and exit.

```sh
c++ -std=c++20 -O1 -DNDEBUG \
  -DVELOCITA_TARGET_CACHE_LINE_SIZE=64 \
  -Iinclude -Itests -Ibuild/_deps/willemt_raft-src/include \
  tests/v2_snapshot_catchup_red.cpp \
  -L/private/tmp/velocita-full-audit.lYB4P5/release \
  -lvelocita_v2_replication -lvelocita_v2_kernel \
  -lvelocita_v2_evaluator -lvelocita_v2_contracts \
  -lvelocita_core -lwillemt_raft -pthread \
  -o /private/tmp/v2_snapshot_catchup_cp034_red
fixture=$(mktemp -d /private/tmp/v2-snapshot-catchup-cp034.XXXXXX)
rmdir "$fixture"
/private/tmp/v2_snapshot_catchup_cp034_red "$fixture"
```

This retained RED remains an integration gap: the pure helper is not wired to
network snapshot transfer or generation installation.
