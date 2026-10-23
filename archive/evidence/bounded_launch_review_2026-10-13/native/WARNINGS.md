# Warnings retained for the requested cleanup iteration

Both native runs emitted46 unique warning lines (same inventory on both hosts):
misleading indentation in existing compact project statements, a gateway-test
missing member initializer, GCC15 string-operation diagnostics in test-fixture
vector construction, and unused parameters in upstream Aeron.

The string-operation call traces point to the existing inspection fixture and
`ByrealBuyFixture::envelope` (vector initialized with one byte, resized to129,
then extended using vector insert). These are test serialization helpers, not
production transaction buffers. The resize/insert operations were reviewed;
local AddressSanitizer/UBSan verifier checks passed. Preserve these diagnostics
for the cleanup pass; do not suppress them or claim a warning-free build.

Source statement warnings in the review parser predate the route change; line
locations are retained verbatim in each architecture's `warnings.json`. The new
Byreal fixture also contributes optimizer diagnostics, so not every warning is
pre-existing. No source was altered during exact-commit qualification.

This is an incremental-build inventory, not a full clean-build warning audit.
Full compiler call traces remain in the collected `build.log` for each host.
