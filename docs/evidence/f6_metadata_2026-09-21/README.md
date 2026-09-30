# F6 metadata process-crash evidence, 2026-09-21

Base d44a4a9 plus uncommitted remediation; NOT target acceptance. Permanent test:
`test_hard_state_atomic_replace_cut_matrix`, selector `hard-state-cuts`, in
`tests/v2_replication_tests.cpp`. Build before invoking the suite.

The six rows terminate the child at before-write, half-write, full-write,
file-sync, rename and directory-sync checkpoints. Old metadata is independently
specified as `(term=7,voted_for=2,commit_index=19)`, new as `(8,3,23)`.
Before rename the named file must retain the old tuple; after rename it must
contain the new tuple. Temporary lengths 0/18/36 and disappearance after rename
check that the fault actually reached its intended boundary. A final completed
save observes its callback; that is not a syscall oracle.

This adds missing coverage to an existing atomic-replacement implementation.
No production defect was reproduced here, so there is no invented pre-fix RED.

## Mutation observations

Agent compiled replacement storage translation units directly into copies of
the core archive and linked the replication test object. Main inspected the
two fsync-only source diffs and independently reran all three binaries below:

```text
/private/tmp/f6-hard-state-mutant/v2-replication-tests hard-state-cuts
V2 replication test failed: process crash exposed the wrong hard-state generation
exit 1 (rename removed)

/private/tmp/f6-hard-state-syscall-mutants/v2-replication-tests-no-directory-fsync hard-state-cuts
hard-state-cuts passed
exit 0

/private/tmp/f6-hard-state-syscall-mutants/v2-replication-tests-no-file-fsync hard-state-cuts
hard-state-cuts passed
exit 0
```

Each fsync-only mutant changes its selected `if (::fsync(fd) != 0)` to
`if (false)` and retains checkpoint calls. Both SURVIVE. Production storage.cpp
SHA-256 at that check:
`8dab43d64414d96ac62703028c9cc07507d642f102a2fac1dd330c1604645f47`.
Temporary binaries are local reproduction artifacts, not durable build inputs.
An earlier directory-sync-plus-checkpoint-removal kill is explicitly excluded:
it proved missing callback detection, not fsync enforcement.

## Meaning and remaining work

`_exit` removes a process, not the OS page cache. These tests establish atomic
named-file visibility for process crashes, not power-loss durability. No real
storage device or power cut was exercised. G-047/F6 remain OPEN, as does the
synchronous hard-state persistence on the consensus owner thread. New asynchronous
fencing needs its own design and tests; merely removing fsync is unsafe.
