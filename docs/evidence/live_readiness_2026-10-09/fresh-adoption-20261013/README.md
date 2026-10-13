# Fresh verification and dormant account adoption

No transaction was signed or submitted. Both live units remain inactive/dead/static;
the installed dormant manifest remains pinned to
c4a1fe1ba9ffb2055a11a533efa8280425eca03b88cadc93ccaa57dc9a49d543.

The initial account refresh used 20 requests and saw the same four finalized
transactions, one USDC token account, no TM orders and no blocking orders. Stable
balances: 40.006989 USDC and 0.040764166 SOL. Closed historical token accounts and
independent provider archival completeness remain outside the collector's claim.

The one unsigned buy returned HTTP201: $1 input, $0.002 quoted explicit fee and
0.009325186 quoted SOL output. Independent finalized six-account lookup passed:
source USDC belongs to the reviewed owner, initialized/unfrozen/no delegate;
fee recipient unchanged; WSOL destination absent; active warmed lookup resolves
20 distinct message accounts. Historical instruction/account positions match;
only the quoted route output, blockhash and envelope timestamp changed.

The same production C++ verifier source as 13cc98b accepted the captured payload
in an offline diagnostic with a synthetic pending intent and capture-time clock:

    ceil(9,325,186 * 9500 / 10000) = 8,858,927 lamports gross floor
    8,858,927 - 2,039,280 + 0 sponsor credit = 6,819,647 net floor
    998,000 principal + 2,000 fee = 1,000,000 USDC atoms debit

This remains conditional on the reviewed Jupiter/Kipseli program contract; account
snapshots are not a promise of future account state. No current Raft authorization
was manufactured by the synthetic diagnostic. The expired quote is never executed.

Fresh authenticated reads under both UID29001/caller22/key13 and UID29002/caller23/
key14 passed: term5, leader2, applied index4, state version3 and the same reviewed
halted/catalog-only state digest. Existing recovery directories contain no files.
The owner explicitly authorized use of interactive sudo; no password was written
to a file, argv, environment or command output.

## Correctness findings before credential transfer

1. The exporter incorrectly compared the API-key UUID with profile signer_id.
   C++ interprets signer_id as the envelope's Solana signWith wallet. Export now
   validates that wallet against owner_wallet; API-key format and exact P-256
   private/public-key validation remain enforced. The source private key is not
   converted into a Solana key and is never a wallet key.
2. The Python launcher compared raw 16-byte cluster ID with the swap profile's
   32-byte incarnation digest. C++ requires SHA256(raw incarnation bytes). Python
   now follows that same mapping. Synthetic launcher fixtures previously repeated
   the incorrect assumption. A regression covers both acceptance and rejection.
3. Initial account adoption needs a bounded installer. adopt_account.py validates
   all four document hashes, principal bindings and freshness; accepts only a
   dormant manifest whose account pins were null; preserves prior manifest/units;
   creates root-owned documents, replaces disabled unit pins and reloads systemd.
   It cannot activate, fund, initialize journals or install credentials. Partial
   failure is retained and cannot be silently retried/overwritten.

42 service tests, six adoption tests and seven credential-export tests pass on Mac and NUC1; 11 credential-handoff tests also pass locally.
Five targeted mutations fail named assertions, without test/compilation errors.
The offline capture reader initially used a wrong route-byte offset; preparation
refused before networking. Correct offset25/length36 was checked against the
production decoder before the single metadata read. No network retry occurred.

Current work: prepare the final account cut and full-position unsigned sell mark
late, after the corrected launcher is checkpointed and installed dormant. The
short account freshness window is not extended to accommodate installation.
Credential adoption remains separate from activation and actual signing.

The earlier owner checkpoint/install authorization covers this required scoped fix.
A redundant new approval question was withdrawn in chat before proceeding.
