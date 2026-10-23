# Production services and actual account preparation — October 13

**Latest: permanent users/runtime installed and native communication passed.**
Gateway UID29001 and signer UID29002 passed native Aeron and protected signer IPC
in bounded transient services, using synthetic state/key. All 64 checks pass on
Mac and NUC1. Production units, real credentials and live activation remain open.
See [revision 4 outcome and commands](setup-r4/README.md). Earlier results below
are retained as historical evidence.

**Current implementation:** the TCP probe now requires controlled endpoint
evidence bracketed by successful control services, and the production launcher
mapping is implemented. Twenty-six synthetic checks pass on Mac and NUC1, with
five named mutation kills. The owner-run corrected privileged probe passed (run
`1791885226758329135`), with phase records and source hashes checked; actual
Aeron, production-user qualification and service installation/custody remain open. Use the
[revision 3 status and owner command](setup-r3/README.md), which supersedes the
older two-service test command. The post-live explainer is noted in the tracker.

**Subsequent update:** the owner approved voter-address-only signer connectivity;
ADR 0019 and the profiles are amended. The corrected bounded native network test has now passed on NUC1;
the original failed result remains retained. See [approval, tests and owner command](NETWORK_APPROVAL.md).
The text below preserves the initial review and account evidence.

The owner requested service setup and account-startup work in parallel. This
batch prepares deployment contracts and refreshes actual account evidence. It
does not install/start services, initialize a ledger, load a production signing
key, submit orders, or enable live trading. The qualified C++ source remains
`86f440806c6adc6f4c1dc9dc7a271e3d95293d76`; the new deployment helpers are not yet
part of that committed/native-qualified release.

## Account evidence and proposed opening

At 08:56:49 UTC, an authenticated setup/balance read verified the same user and
Solana main-wallet identity as the retained account bootstrap, DeFi enabled,
and zero holds. The session refresh succeeded once. Raw identity and session
material remain private. The subsequent bounded history capture completed
18 read requests between 08:57:48.433 and 08:57:52.055 UTC. It found one current
USDC account, two finalized transactions and zero listed TM orders. The existing
offline proposal builder revalidated every response hash and replayed the
request sequence before producing the new proposal.

| Item | Observed / proposed amount |
|---|---:|
| Actual USDC, proposed managed cash | 1.006989 USDC |
| Actual SOL, proposed outside strategy | 0.031896934 SOL |
| Proposed managed SOL / acquisition cost | 0 / 0 |
| Requested cumulative purchase cap including fees | 5 USDC |
| Requested loss stop | 0.50 USDC |

The five-dollar cap is not five dollars of available cash. Keeping the existing
SOL outside the strategy avoids inventing its acquisition cost. Rent is excluded.
The proposal requires owner adoption; it is not an executable funding command.
It uses actual TM funds, never the virtual $25 paper balance.

Private evidence, all beneath `~/.velocita-credentials/truemarkets/`:

- `account-profile-review-20261013/`: setup and balances.
- `account-profile-history-20261013/`: bounded history and exact baseline cut.
- `account-profile-funding-20261013/`: offline funding proposal.

The public [summary](account-summary.json) retains hashes and amounts only.
History-cut SHA256:
`bd803a22fd05eb6c7bc43acb17b5836eb41f9aa56a06733bfd1a7f812a569281`.
Private proposal SHA256:
`a76f67a3eb31e59364e8fa6d289883a51ff04dd3b60c7e3c820b03f7164584ce`.

Current-account pagination and stable brackets do not establish archival
completeness, cover closed historical token accounts, or prevent later outside
wallet activity. An empty TM order list does not erase the historical sale.
Activation needs a fresh cut: runtime capture/review age is capped at 15 minutes.
Do not silently extend these timestamps or treat the evidence as a permanent
permit. Authenticated account identity does not establish P-256 signing-key
registration, custody authority, sponsor identity or owner funding approval.

## Service boundary and architecture issue

The two roles need distinct Linux users, separate private state and credential
files, and a shared Unix-socket group. Separate C++ objects under the same UID
would not protect the signing key from the gateway. The proposed arrangement
uses the same qualified executable in two roles, not a second Raft cluster.

ADR 0019 specifies `PrivateNetwork=yes` for the signer. The integrated signer
independently reads and records authority through Raft/Aeron; its current
implementation therefore needs network access to the voters. Blindly applying
that old setting would prevent it from obtaining authority and serving requests.

The review proposal permits only the three fixed voter IPv4 addresses using
systemd cgroup filtering, with all other IP addresses denied. This is a material
isolation amendment, not proof that the existing ADR is satisfied. Address rules
permit all ports/protocols on those hosts and cannot exclude an intermediary
relay there. The alternative is an authenticated local Raft proxy, adding another
process/protocol and failure path. Prefer the bounded direct connection if its
tradeoff is accepted and target-host enforcement is demonstrated. Do not weaken
the restriction when enforcement is unavailable.

NUC1 read-only inspection confirmed systemd 249, unified cgroup v2, and kernel
`CONFIG_BPF_SYSCALL=y` / `CONFIG_CGROUP_BPF=y`. These are prerequisites, not an
enforcement test. No new users, group memberships, firewall rules, credentials or
units were installed. Existing voter, exporter and paper gateway user services
were active; the previously stopped strategy edge remained inactive.

The executable accepts an inherited secret descriptor, not a session JSON path.
The C++ loader checks read-only access, a regular singly linked file, exact 0600
permissions and the role UID; its owning FD wrapper closes on errors and after
loading. Borrowed credential views must remain within the owning runtime object's
lifetime. These startup mechanisms are outside the kernel's decision hot path.
Missing identity, expired evidence or credential mismatch must fail closed.

## Remaining closure of the same two items

Implementation: [profile and descriptor helpers](../../../../deploy/v2/swap-runtime/README.md).
The profile generator emits two disabled review units and public launch contracts.
The FD helper CLI refuses unconditionally; its tested primitives are not yet a
complete production supervisor. It is not sufficient to replace `/usr/bin/false`
with this helper and claim deployability.

Verification commands run from the repository:

```zsh
python3 -B tests/test_swap_service_profile.py -v
python3 -B -m unittest discover -s tests -p 'test_tm_account*.py' -v
```

All 8 service/descriptor tests and 44 account tests passed. Tests cover rejected
root/shared identities, malformed metadata, unsafe credential paths/modes/links,
binary digest mismatch, refusal before credential access, and a harmless child
receiving synthetic FD3 while an unrelated inherited FD and ambient environment
are excluded. Five profile mutations caused named assertion failures, not test
errors; see [mutation evidence](mutation-results.json) and [verification pins](verification.json).
No Linux enforcement or real-key custody conclusion follows from these Mac tests.
An initial Apple system-Python child emitted a temporary-directory warning; the
test now uses the current interpreter and an explicit durable scratch directory,
and the final run is clean. This change does not alter production binaries.

1. Review the signer networking amendment, provision distinct principals and
   qualify their actual filesystem/socket/network isolation with native Aeron.
2. Bind the approved account partition to real release/custody/session/signer
   pins and a fresh history cut. Use canonical C++ opening encoding for its
   digest, not JSON or struct-memory hashing. No key registration or sponsor
   identity is inferred from a balance match.

The deferred 550,840-lamport sponsor-repayment discrepancy and exact on-chain
minimum net receipt/rounding remain unresolved. They are separate from the
established $1 order minimum. The production economic admission gate stays
closed; this batch neither resolves nor bypasses it.
