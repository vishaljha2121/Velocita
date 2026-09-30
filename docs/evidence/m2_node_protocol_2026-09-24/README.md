# M2 ingress/response codec (decision D)

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted.
`include/velocita/v2/node_protocol.hpp`, `src/v2_node_protocol.cpp`. Exact bytes
are documented in `M2_NODE_PROCESS_REVIEW.md` "D encoding", derived from the
approved contents; the owner may still object before deployment.

RED first against a stub ("valid response refused", `red.txt`). Goldens from
Python struct (`python-oracle.txt`). Refusal reasons are pinned to
`ProposeStatus` 2..9 by a static_assert so a new status cannot silently fall
outside the layout. Five mutants killed by their intended assertions
(`mutants.txt`). Release/ASan+UBSan 103/103 run alone.
