# Focused MVP correction batch — 2026-10-15

Status: **committed and qualified on NUC1/x86_64 and Pi/aarch64 at `1f8d7b6`**. Not installed or enabled live. Each architecture passed 59 CTest targets, 383 Python/CLI checks, eight process-drill cases and nine six-effect continuous cases (54 fixture trades). See [native results](native/RESULTS.md). Local results below are retained historical evidence.

This supersedes the five implementation findings in [CODEX_REVIEW.md](../CODEX_REVIEW.md), not the recorded deployment state. See [PROTOCOL.md](PROTOCOL.md) for the fixed scope and qualification limits.

## What changed and why

| Finding | Failure | Correction |
|---|---|---|
| F1 | A wider order allowance still encountered one-effect process latches and unresolved signer journals. | Gateway submission, gateway signing-request and protected signer journals advance only after matching authenticated predecessor settlement. Every next effect receives new signing authority. Create/execute limits are per effect; recovery is never a second execution POST. |
| F2 | Retirement removed valid Raft caller keys; verification missed their absence. | Retirement preserves caller keys; verification checks metadata, exact size and role access. A separate owner-only restoration helper creates absent active files from retained tmpfs sources without overwriting or deleting originals. |
| F3 | An old candle was labeled with the current time and could trigger a trade. | Preserve candle-close time, reject age over two seconds or future timestamps, and recheck freshness before submission. The signing deadline remains source plus 20 seconds under the current CLI contract. |
| F4 | A final buy could consume the last allowed effect, preventing an exit. | Reserve one effect for an exit; never issue effects beyond the six/240 configured ceiling. Unknown submissions stop durably with their inflight identity preserved. |
| F5 | Review expiry stopped services without ensuring a position was closed. | Inhibit buys and request eligible exits 420 seconds before expiry; stop starting exits 210 seconds before expiry. Persist explicit unknown/pending/exposure stops when draining cannot finish. No automatic renewal or guaranteed flatness. |

Historical funding remains pinned to its reviewed opening/history. Restart validates that history separately from the fresh wallet comparison against current settled Raft balances plus declared outside holdings. Neither comparison substitutes for the other.

## C++ design and recovery

No Raft command schema, kernel reducer, replicated-state layout, journal format, wallet scope or economic-verifier cap changes. The gateway remains a single-threaded coordinator; network work stays outside apply. The next-effect transition uses bounded scans of retained settlements. Journal records are copied before validation; no snapshot pointer is retained across a new authenticated read. Owned sessions are destroyed before another acquires the link.

The ordered durable writes are: resolve execution submission; resolve gateway signing request; prepare the next effect. Independently, the signer verifies the previous settlement with its own authenticated read, resolves its old fence, obtains a fresh authority barrier and persists the next fence before signing. A crash may leave the stores at different stages; no store is reset to make them agree.

Automatic restart is qualified at completed-settlement transitions. A crash during an unfinished current prepare/create can still require supervised recovery. Unknown execution is reconciled in recovery-only mode; that process exits before a normal continuation. A missing/corrupt proof, expired review, mismatching balance or ambiguous driver submission stops activity.

## Results

[Machine-readable qualification and source hashes](QUALIFICATION.json); [C++ output](cpp-tests.log).

| Check | Result |
|---|---|
| Python driver, staging and credential-tooling tests | 104 passed |
| C++ regression targets, including production gateway build | Gateway built; 12 CTest targets passed |
| Combined fake-venue baseline | Six consecutive buy/sell effects; six signatures and execute POSTs |
| Crashes at journal transition | SIGKILL after settlement 2 before request retirement, and after settlement 4 following retirement; six effects completed |
| Crash after acceptance | Effect 3 retained its reservation, reconciled on recovery-only restart without re-execution, then completed effects 4–6 on normal restart |
| Mutation checks | 33 named assertion kills; compilation errors and stale-build/test-oracle mistakes excluded |

Every combined case uses three actual Raft node implementations and independent gateway/signer
processes. All execution responses are lost deliberately. The fake endpoint verifies the P256
stamp against the exact retained payload. All replicas finish flat with six settlements, no holds,
$25 cash and $0.012 recorded fees. Scripted price movement offsets those fees; this says nothing
about strategy profitability. The three cases cover 18 executed fixture effects in total.

Final transcripts: [baseline](case-0/events.log), [journal-transition crashes](case-1/events.log),
[accepted-then-crashed recovery](case-2/events.log). Earlier incorrectly labeled crash boundaries
were superseded by the final run, which checks receipt ordering. A premature asynchronous test
assertion and stale-object mutation attempt were also corrected; neither counted as a pass.

## Deployment boundary

Nothing in these tests accesses TM, real credentials, sudo, host services or the production cluster. Synthetic keys and fixture bearers stay in local test scratch. The fake-venue drill uses real production codecs, transaction verification and receipt admission; its HTTPS transport leaf is substituted at link time. It does not qualify external TLS, production service users, the Aeron network transport or live authorization refresh.

Next: review installation of the qualified checkpoint and owner caller-key restoration, followed by fresh authorization checks and the supervised six-effect run. Preserve the single cluster, funding and all journals. Recheck authorization expiry before any start; the current recorded window ends 2026-10-16 13:48:34 UTC.
