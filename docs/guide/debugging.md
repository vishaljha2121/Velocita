# Debugging and operations tools

Everything here is read-only unless it says otherwise. `$NUC1`, `$PI` and `$NUC2` are the three voter hosts.

## The two-minute health check

```sh
# 1. All three voters agree: ready 1, failed 0, same term, leader and commit index
for h in $NUC1 $PI $NUC2; do
  curl -s -m4 http://$h:11410/metrics |
    grep -E '^velocita_v2_(ready|failed|term|leader_id|strategy_mode)\{|log_index\{.*commit' | tr '\n' ' '; echo
done
# 2. Services: gateway and signer (system units), strategy driver (user unit)
ssh velocitas@$NUC1 'systemctl is-active velocita-swap-gateway velocita-swap-signer;
                    systemctl --user is-active velocita-strategy-driver'
# 3. The account as the gateway last validated it (published every 5 s)
ssh velocitas@$NUC1 'cat ~/velocita-v2/textfile/strategy-settlements.json'
```

| Field | Healthy | Worry when |
|---|---|---|
| `ready` / `failed` | 1 / 0 on all three | any voter failed, or two not ready (no quorum) |
| `term`, `leader_id`, `commit` | identical on all three | term rising every few minutes: an election storm |
| `strategy_mode` | 1 while trading, 5 halted | an unexpected change |
| `pending_effect` | 0, or briefly the newest effect | non-zero for minutes |
| `observed_at_ms` | within 5–10 s of now | older than 12 s: the driver stops deciding |
| `quote + cost − funding` | equals `realized_pnl_atoms` | any difference is an accounting bug |

`velocita-v2-status` prints one voter's health page directly from its shared memory, without the exporter.

## Where each answer lives

| Question | Source |
|---|---|
| What did the strategy decide each second? | driver log: `quiet`, `held`, `stale` (no fresh account), `catchup`, or an intent line |
| Which intents committed, at which index and term? | `COMMITTED sequence=… index=… term=… latency_ms=…` lines in the driver log |
| Why was an effect released? | gateway journal: `WARNING TM route refused code=8` or `route lookup unavailable` |
| Why did the gateway stop? | gateway journal: `settlement unresolved stage=… code=…`, and its exit status |
| Why did the signer refuse? | signer journal: the verification code (for example `unsupported_route`) |
| Did an alert fire? | Alertmanager on NUC1; alerts are pushed to a phone through self-hosted ntfy |
| What happened over time? | Grafana: price with entry and exit markers, gateway phases, settlement feed, terms |

Gateway and signer logs carry codes and digests, never secrets. Read them filtered, not dumped whole.

## Symptom → cause → action

| Symptom | Likely cause | Do |
|---|---|---|
| Many effects released, few settle | the venue routes through DEXes outside the allowlist, or a route needs more than one lookup table | nothing urgent: each release is a committed, safe no-op; watch the 240-effect limit |
| `pending_effect` stuck at `reserved` | waiting for `sign_by` (≤ 20 s), then a release; longer means the gateway stopped | check the gateway |
| Stuck at `prepared` / `attempted` | signer refused, or the gateway stopped mid-step | read the signer and gateway journals; a restart resumes from journals and never resends an execute |
| Stuck at `unknown` | reconciling against venue status and finalized chain data | it retries within the settlement window, then stops with `settlement_unproven`; investigate before restarting |
| Driver stopped with `submission_unconfirmed` | the client gave no clean `COMMITTED` line, or the intent wasn't seen within 20 s | if `last_effect` reached the in-flight effect, it committed; never lower the sequence |
| Term keeps rising | Wi-Fi drops, a slow `fsync` on the Pi, compaction pauses | check term history in Grafana and the Pi's disk; fix the network or disk, not the timeouts |
| A voter not ready after restart | a new image's first start on the Pi can take minutes; disk space; a refused recovery | the other two keep quorum; a refused recovery keeps its files and names the condition |
| Account conflicted | a settlement disagreed with its intent or history | halt, preserve everything, compare the stored conflict evidence with the chain |

## Forensic tools

| Tool | Use |
|---|---|
| Log replay ([`replay_log.cpp`](../../archive/evidence/gateway_stop_2026-10-09/replay_log.cpp)) | replays a voter's archived log through the kernel of the deployed commit; found the one refused report among 8,107 commands in the 2026-10-09 gateway stop |
| [`tools/read-owner-settlement.py`](../../tools/read-owner-settlement.py) | one finalized transaction read: the owner wallet's exact balance changes, to compare with a settlement |
| [`tools/tm-account-history-check.py`](../../tools/tm-account-history-check.py) | bounded, read-only capture of the venue account's history |
| [`tools/roundtrip-cost.py`](../../tools/roundtrip-cost.py), [`measure-markup.py`](../../tools/measure-markup.py) | what a swap round trip costs at the venue |
| `v2-layout-report` | prints every object size and budget from the build ([memory.md](memory.md)) |
| `raft-dump` | prints a V1 log |

## Probes (read-only, one request each)

| Tool | Checks |
|---|---|
| `v2-https-read-probe`, `v2-swap-https-probe`, `v2-swap-order-https-probe`, `v2-swap-runtime-https-probe` | the HTTPS paths to the venue and Solana RPC, through the production client |
| [`tm-route-a-probe.py`](../../tools/tm-route-a-probe.py), [`tm-unsigned-contract-probe.py`](../../tools/tm-unsigned-contract-probe.py) | what the venue's unsigned swap actually contains, before any signing exists |
| [`read-jupiter-idl.py`](../../tools/read-jupiter-idl.py) | the route program's interface, to build the verifier's byte oracles |
| [`probe-market-data.py`](../../tools/probe-market-data.py), [`ws-observe.py`](../../tools/ws-observe.py) | which market-data paths exist and what the WebSocket sends |
| [`tm-account-session.py`](../../tools/tm-account-session.py) | authenticate and read balances; no trading endpoints |

## Drills and stress

| Tool | Proves |
|---|---|
| `v2-swap-process-drill` | real gateway and signer processes against three voters, killed and restarted at each step |
| `v2-swap-continuous-drill` | many effects in a row, with restarts, settle exactly once |
| power-cut and torn-write tests (`v2-replication-tests`, the persistence model tests) | a log cut at every structurally distinct offset recovers or refuses, never guesses |
| `tests/mutation_*.py` | each safety check is load-bearing: the mutant is rebuilt (proven by hash) and must fail a test |
| [`tools/velocita-soak.sh`](../../tools/velocita-soak.sh) | long runs under load |

## Operator controls

The operator client takes a `--dry-run` that prints the exact command without sending it. Halting the strategy is
one committed command (`strategy-mode halted`); every owner helper that changes a host has a read-only preflight
mode and refuses to continue from an unexpected state.
