# Approved checkpoint and offline qualification

Owner approved the scoped checkpoint and offline NUC/Pi qualification on October 14.
Scope: Bison177 transaction-layout support, the one-time pre-trade price amendment,
its canonical codec and snapshot extension, operator CLI/offline builder, tests,
and evidence. The original funding record and all journals remain unchanged.

Stage explicit paths only. In cmake/LiveReadinessTests.cmake stage only the two
additional swap-balance-test source dependencies. Exclude shared gateway, top-level
CMake, research, monitoring and unrelated tracker edits.

Create a Git bundle from the previous qualified commit through this checkpoint.
Use the committed native launcher/runner on both hosts, pinned architecture images,
existing incremental caches, one compiler worker on CPU 3, 2 GiB memory, and no
container network. Require >2.5 GB initial free space and stop below 1.8 GB.
Run at least 58 selected CTests including the eight process drills, Python service,
account and renewal suites, native client amendment checks and schema goldens.
Collect and checksum evidence, binary hashes and warnings independently per host.
A failure is not qualification; preserve its evidence before any corrective run.

This approval does not authorize production installation, application of the price
amendment, service activation, signing or fund movement. All state readers must
receive the qualified build before amendment; old releases cannot replay kind 9.
