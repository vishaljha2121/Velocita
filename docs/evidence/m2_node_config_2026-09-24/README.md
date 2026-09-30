# M2 step 3: node configuration (decision E)

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted.
`include/velocita/v2/node_config.hpp`, `src/v2_node_config.cpp`.

Clarification of the approved key set: `cluster.incarnation`, `catalog.root`
and `catalog.file` are required keys. Kernel bootstrap/recover need the full
expected StaticIdentity and a catalog verified against a pinned root; the
approved list omitted them. Local configuration, not a new byte contract.
Optional keys: callers, `archive.destination_identity` (required in
`two_host_pilot`), and owner-loop budgets with V1.1 defaults.

RED first against a stub ("valid config refused", `red.txt`). During GREEN the
test caught a real ordering defect: with `node_id` absent from the voters the
parser reported a misleading missing peer key; membership is now checked first.

Oracles: expected NodeConfig/StaticIdentity hand-built from the fixture text;
digest via the golden-pinned `sha256_digest`.

Ten mutants, all killed by the intended assertion (`mutants.txt`). The
inline-comment mutant first SURVIVED: its test used a numeric value that failed
parsing anyway (accidental coverage). The test now also uses a path value,
where a trailing comment would silently become part of the path.

Release 102/102, ASan+UBSan 102/102 run alone.
