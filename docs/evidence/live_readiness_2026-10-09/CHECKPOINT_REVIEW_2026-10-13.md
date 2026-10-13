# Integrated swap checkpoint and native qualification

The owner authorized review, a relevant source checkpoint, and qualification of
that exact commit on NUC/Pi. This does not authorize deployment or live trading.

## Scope and review

The checkpoint includes the exact-input SOL swap reducer and codecs, gateway and
protected-signer composition, transaction inspection, durable preparation/signing/
submission/reconciliation, account-history startup checks, bounded storage recovery,
and the current-term Raft read barrier. The real-transaction admission function
remains compile-time false, before resource or credential opening.

Shared files are assembled selectively against parent `6349134`. The unrelated
resting-paper backend, mode-selector experiment, gateway-loop changes, strategy
research, monitoring edits and deployment changes remain in the working tree.
The selected CMake modules and gateway test fixtures drop their incidental
resting-paper dependencies. No shared file is staged wholesale from the dirty
tree. Private responses, account proposals, credentials and Python caches are
excluded. The account tools include their authentication-helper dependency;
checkpointing that helper does not execute it or authorize money movement.

Review focused on the changed integration boundaries: authenticated caller/role
checks; command digest binding; retained payload ownership; bounded journals and
ambiguous-send recovery; account identity and freshness; synthetic-only fixture
macros; and current-term marker admission, persistence, replay and compaction.
This is not a claim of a new independent line-by-line audit of every component.
Previous component tests and mutation evidence remain supporting evidence.

The marker is type 103 with 32 domain bytes, uses reserved control capacity, and
creates no economic result or signing authority. Reads wait for current-term
application; the result watermark is not weakened. Replication epoch 2 and
snapshot/command schema 12 require a coordinated compatible cluster identity;
mixed-version rolling deployment is not supported. No host migration occurs here.

## Qualification procedure

1. Build and check the isolated selected source locally before staging its exact
   patch. Verify the staging set and commit with the requested trailer.
2. Export the commit in a Git bundle; clone it into new host-specific build
   directories. Verify HEAD and a clean checkout, mount source read-only.
3. Use the existing immutable native TLS candidate toolchains:
   NUC1 `076701392104f2cebd7b312f2bd483330ddd518cddd678bab23ef6a33fd8e83f`;
   Pi `3f3cfb3403a2644b27bf21351e26683e62f6ad90fde1f1b1567c8d2279d4fb43`.
   They derive from the pinned development images with previously inventoried
   curl development dependencies. No packages or production images are changed.
4. Build Release with the optional swap runtime and local HTTPS/signer tests
   enabled. Pin compiler/test execution to CPU 3, one build job, 1 GiB memory;
   use a private network namespace with loopback only, no credentials, and
   durable scratch. Stop on insufficient disk rather than pruning images.
5. Run registered offline tests, account tests, independent schema goldens and
   strict validation of all eight process-drill artifacts. Retain commands,
   logs, source identity and binary hashes. Failures remain failures until a
   reviewed correction and rerun; compilation alone is not qualification.

The integrated gateway binary is qualified as a build artifact. Native local
fixtures do not establish deployed Aeron behavior, distinct production UIDs,
TM transaction economics or a release-container deployment. Existing services,
source mounts, paper ledger and image tags are unchanged.

## Remaining boundaries

Actual reviewed live funding/custody/session pins and fresh activation evidence;
remaining native/deployed failure drills and one-cluster paper release; external
heartbeat creation; deferred sponsor repayment and exact minimum net receipt;
and the exact separately approved live test with enforceable limits. Strategy
profitability and unattended rearming remain separate promotion questions.

Results and the final commit identity will be appended after qualification.
