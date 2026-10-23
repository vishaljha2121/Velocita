# Bounded MVP native qualification

Exact source: `84c353a7f17581546c84db414b0d759a2ab0e695`.
Both host runs finished on October 13, 2026. Neither installed or activated the
production live services. This qualifies the selected production capability and
fixture failure drills; it is not evidence of a real venue settlement.

| Check | NUC1 x86_64 | Pi aarch64 |
|---|---:|---:|
| Selected C++ tests | 52 passed | 52 passed |
| Account Python tests | 50 passed | 50 passed |
| Launcher/profile/runtime installer/dormant installer/provision tests | 60 passed | 60 passed |
| Actual executable early refusal | Passed | Passed |
| Signer isolation audit and schema golden checks | Passed | Passed |
| Validated fixture process failure cases | 8 passed | 8 passed |

See each host's `status.json`, `tests.log`, Python logs and retained process
results. `artifact-manifest.json` pins every collected byte; all hashes were
verified on collection. Collection excludes credentials and real venue payloads.

The initial `73e753d` run also passed all 52 C++ tests but failed a Python fixture:
`/usr/bin/true` is a symlink in the image. The correction resolves the fixture
executable while retaining an explicit symlink-refusal test. Original logs remain
under `initial-failure/`. The correction changed no production C++; the exact
checkpoint rebuild reused its objects and took about two seconds per host.

Warnings remain recorded in `warning-inventory.json`, including misleading
indentation in project sources, Aeron unused parameters and GCC diagnostics in
test fixture vector construction. The incremental correction emitted no new
warnings; this is **not** a warning-free clean-build claim. Cleanup remains the
owner-requested later iteration; no diagnostic was hidden or disabled.

The NUC gateway executable SHA-256 is
`001e5526a87a641c1e0f7334853973de73acddb0e22fe9c0d8de02b12933d21e`.
Its public dormant installation bundle is staged in the owner's NUC1 cache.
Fresh account documents, protected credential adoption, authenticated opening
and the reviewed one-buy launch remain outstanding.
