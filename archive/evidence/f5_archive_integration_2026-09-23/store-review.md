# Archive store worker component

Historical first component report. CP-039 subsequently implemented and tested
the greedy four-segment transaction; the one-segment limitation below describes
this earlier report, not the current source. See README.md and current tests.

Focused native filesystem coverage passed for fresh dependency publication,
one immutable CP-028 object, atomic catalog replacement, restart verification,
one-shot capability consumption and refusal of an unlisted orphan. The store
holds a nonblocking exclusive archive-directory lock; validates owner, mode,
single-link files, exact canonical dependencies, catalog, every named segment,
terminal digest, bounds and closed namespace; and never cleans up an orphan or
temporary automatically.

The public API cannot publish arbitrary provenance. Publication and recovery
capability minting are private to `ReplicatedKernel` (plus the named test access
friend). Local capability consumption refuses non-laboratory deployment because
independent-copy receipts are not approved or implemented.

Limit: this focused component presently publishes one CP-028 segment per catalog
transaction. It must not be integrated as the complete <=8,192-record archive
publisher until the approved greedy <=4-segment single-catalog transaction is
implemented and tested. No E1/E2/E3 closure or durability-fault coverage is
claimed by this log.
