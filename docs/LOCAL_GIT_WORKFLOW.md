# Local Git release and recovery workflow

This repository uses Git locally and has no network remote. Git records history
inside `.git/`; GitHub is optional hosting, not part of version control itself.

## Release model

- `main` contains the latest locally accepted state.
- Short-lived branches isolate work that is not yet accepted.
- Small commits describe one coherent change and its tests.
- Annotated tags identify target-hardware-validated releases.
- `CHANGELOG.md` records product and operational meaning; `git log` records code
  history. Neither replaces the other.

Current baseline:

```text
main
└── tag v1.0.0: accepted V1 code, operations, observability, and evidence
```

V2 begins from the retained V1.1 storage-design checkpoint on a separate
architecture branch:

```text
v1.1/stage-3-owner-loop-priority
└── v2/architecture-contracts: ADR, contracts, and TDD plan only
```

This does not make V1.1 accepted or move `main`. V1/V1.1 branches and tags are
rollback/reference points and must not be deleted or force-moved while V2 is
developed.

The planned V1.1 work should use one branch per stage:

```bash
git switch main
git switch -c v1.1/stage-1-leader-routing
```

After implementation and verification:

```bash
git status --short
git diff --check
git diff
git add <only-the-files-reviewed-for-this-stage>
git commit -m "Implement V1.1 stage 1 leader-aware routing"
```

Merge only a tested stage into `main`:

```bash
git switch main
git merge --no-ff v1.1/stage-1-leader-routing
```

Repeat for stages 2 through 4. After the V1.1 acceptance gates pass on the Pi,
finish the changelog, commit the evidence summary, and create an annotated tag:

```bash
git tag -a v1.1.0 -m "Velocita Raft V1.1 accepted on Raspberry Pi 5"
```

An annotated tag is a signed release marker in local history: it records a tag
name, message, creator, timestamp, and exact commit. It does not move when new
commits are created.

V2 uses the same test-first pattern with one branch per accepted phase. Create
the next phase from the last reviewed V2 integration checkpoint, for example:

```bash
git switch v2/architecture-contracts
git switch -c v2/phase-1-domain-contracts
```

Keep experiments separate:

```bash
git switch -c experiment/v2-wal-block-prototype
```

An experiment can retain valuable negative evidence without becoming release
code. Merge it only after its contract, tests, data migration, and rollback
plan satisfy the relevant V2 phase gate. V2 uses new wire/storage versions;
never test a V2 storage writer against the only copy of a V1/V1.1 WAL volume.

## Daily inspection

Show the current branch and uncommitted files:

```bash
git status --short --branch
```

Review history and release markers:

```bash
git log --oneline --decorate --graph --all
git tag --list --sort=version:refname
```

Review changes before committing:

```bash
git diff
git diff --staged
git diff --check
```

Never commit private keys, API secrets, passwords, `.env` files, WAL volumes,
benchmark artifacts, or production data. `.gitignore` is a guardrail, not a
security boundary: a file explicitly added with `git add -f` can still enter
history.

## Safe recovery choices

### Preserve unfinished work before investigating

```bash
git stash push --include-untracked -m "WIP before recovery"
git stash list
```

This is safer than deleting an uncertain working tree. Restore it later with:

```bash
git stash apply
```

Use `apply` before `drop` so the saved copy remains until restoration is
verified.

### Inspect an old release without moving `main`

```bash
git switch --detach v1.0.0
```

Return afterward:

```bash
git switch main
```

Detached mode is useful for reading, building, and testing an old version. Do
not perform normal feature work there because new commits would not belong to a
named branch.

### Create a recoverable branch from an old release

```bash
git switch -c recovery/v1.0.0 v1.0.0
```

This is the preferred way to redeploy or investigate V1 while preserving the
newer `main` history.

### Undo a bad committed change on `main`

```bash
git revert <bad-commit-id>
```

`revert` creates a new commit containing the inverse change. It preserves the
audit trail and is safer than rewriting history.

### Restore one uncommitted file

First inspect it:

```bash
git diff -- path/to/file
```

Only when certain that the uncommitted edits can be discarded:

```bash
git restore -- path/to/file
```

`git restore` is destructive for uncommitted changes. Stash first when unsure.

Avoid `git reset --hard`, deleting `.git`, or force-moving release tags during
ordinary recovery. Those operations can erase the exact evidence local version
control was introduced to preserve.

## Local backup without GitHub

Local Git protects against code mistakes, but not loss of the Mac's internal
disk. Periodically write a complete Git bundle to an encrypted external drive:

```bash
git bundle create /Volumes/<ENCRYPTED_DRIVE>/velocita-raft-$(date +%Y%m%d).bundle --all
git bundle verify /Volumes/<ENCRYPTED_DRIVE>/velocita-raft-YYYYMMDD.bundle
```

Test restoration into a temporary directory before trusting the backup:

```bash
git clone /Volumes/<ENCRYPTED_DRIVE>/velocita-raft-YYYYMMDD.bundle /tmp/velocita-restore-test
```

The bundle contains committed Git history and tags. It does not contain
uncommitted files, ignored evidence, Podman volumes, or Pi WAL data; those need
separate backup policies.

## Deployment provenance

Keep the Mac repository as the source of truth. Source synchronization to the
Pi should exclude `.git/`; otherwise the deployment host receives a second,
easy-to-diverge copy of the repository database.

Before copying a release to the Pi, record the source commit:

```bash
git rev-parse --short=12 HEAD
git describe --tags --always --dirty
```

Build logs and benchmark metadata should retain that identity. A binary called
`v1` is not sufficient provenance if its source commit is unknown.
