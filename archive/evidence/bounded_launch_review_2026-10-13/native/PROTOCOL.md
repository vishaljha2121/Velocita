# Approved exact-commit NUC/Pi qualification

The owner approved a scoped checkpoint and offline native qualification. No
production tree, unit, credentials, journals, funding or services may be changed.
Use the existing clean qualification cache on each host, pinned architecture-specific
TLS image, one CPU (CPU3), 2GiB, one compiler worker, no container network.
Require >2.5GB free before starting and stop below1.8GB or on timeout/test failure.
Preserve failed artifacts; diagnose before any explicit corrective continuation.

Build every swap/venue/operational target plus the Jupiter decoder. Run at least
54 selected CTests (including Jupiter IDL/decoder), the existing Python suites plus
the locked apply tests, TLS fixtures, schema goldens and all eight process-drill
cases. Header changes must rebuild every affected consumer. Record exact source,
toolchain and binary hashes, all warnings and any failed stage. Collect completed
logs with the bounded collector and verify its per-file hashes locally.

`launch.py`, `native-focused.py`, `collect.py` are retained in this checkpoint.
Ship a Git bundle whose HEAD equals the approved checkpoint; never the dirty tree.
The launcher may switch only the clean isolated qualification checkout. It must
refuse concurrent use, a dirty checkout, wrong bundle/toolchain or low disk space.
Production activation and the separately reviewed bounded live trade are excluded.
