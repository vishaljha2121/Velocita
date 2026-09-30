# CP-038 archive catalog codec evidence

Scope is the bounded catalog codec only. This is a dependency of integrated E2;
it does not establish durable publication, compaction, restart recovery,
independent-copy durability, target behavior, or completion of E2/F5.

The implementation encodes the approved fixed 96-byte header, zero to 4,096
ordered 112-byte rows, and 32-byte SHA-256 footer, with a 458,880-byte exact
maximum. Hashing is incremental. Decode makes two bounded passes over immutable
input and uses caller-owned row storage; it does not place a 4,096-row array on
the stack. Refusal leaves all caller destinations unchanged.

The two-row golden footer
`5f54a2a8758e7f66e83e9c29374b346d1807e4392a5b9feb6276c3108ac57638`
was independently reproduced with Python `struct.pack` and `hashlib.sha256`
from the CP-038 table. Tests cover the golden layout/digest, round trip,
manifest/publisher/flags/body/footer corruption, destination atomicity, source
generation, gap/genesis rules, empty catalog, and the exact maximum catalog.

`catalog-red.log` is explicitly a component-stub RED rather than a claim about
production E2. `catalog-green.log` records isolated warning-clean and sanitizer
runs. No shared build directory was used.
