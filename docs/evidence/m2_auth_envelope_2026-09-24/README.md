# M2 step 1: authenticated envelope (decision C)

2026-09-24, `v2/phase-5-corrections` / `d44a4a9`, uncommitted. Owner-approved bytes
(`M2_NODE_PROCESS_REVIEW.md` §3C); `include/velocita/v2/auth_envelope.hpp`,
`src/v2_auth_envelope.cpp`, library `velocita_v2_node`.

RED first: `tests/v2_auth_envelope_tests.cpp` was written against a stub that
refuses everything; it failed at "RFC 4231 case 1" (`red.txt`). GREEN after the
real implementation.

Independent oracles: RFC 4231 vectors 1, 2 and 6 (long key) for HMAC-SHA256;
`tests/v2_auth_envelope_goldens.py` (Python hmac/hashlib/struct only) for the
derived keys and complete envelopes (`python-oracle.txt`).

Covered: every single-bit flip and every truncation of a golden envelope
refuses; trailing byte is a length mismatch; reflected and cross-role keys
refuse by route; wrong secret or cluster incarnation refuse by tag; each
structural refusal names its reason and leaves the destination unchanged;
encode refusals leave output unchanged; maximum 32,768-byte body round-trips
at exactly 32,836 bytes. Derived keys and secrets are move-only and wiped.

Six guard mutants, each killed by the named assertion (`mutants.txt`,
`mutants.diff`); `tag_prefix_only` is caught by the bit-flip sweep and
`tag_excludes_body` (verify-only) by the golden check.

Not covered: timing side channels are not measured (the compare is written to
be constant-time; no timing test); HMAC cost on the Pi not yet measured.
Release 99/99 at this step (`release-tests.txt`).
