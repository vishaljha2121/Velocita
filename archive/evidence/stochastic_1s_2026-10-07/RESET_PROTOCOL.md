# Owner-requested fresh paper run — registered before new trading

Owner requested a ledger reset to avoid mixing the old and new strategies, then
selected a fresh $25 virtual balance. This supersedes the continuation/account
preservation clause in PROTOCOL.md. All old data is archived, never deleted.
The initial continuation epoch was prepared but forwarded zero frames and
executed zero orders because its edge startup failed before submitter launch.
It will not be resumed. The startup fix is committed at 0680b08.

Create exactly one replacement cluster with incarnation `c8a6a0a78fbf49a099b222124a5d557f`. Stop all old
voters/exporters before any replacement voter starts. Preserve complete old
stochastic directories and service/config copies under each host's retired/
directory. Keep the existing voter/exporter m3-cp081 binaries and gateway
paper-7297bd5; only edge/observer use the new committed shadow image. No wire or
kernel implementation changes. No host reboots or new credentials.

New paper account: $25 USDC, zero SOL lots, zero orders/fees/volume. New ledger,
source/session state, gateway state, Raft history and run identity. Existing
risk ceilings remain $25 notional/exposure, 12 lots, long-only, maximum fee
30 bp; new simulated fills cost 20 bp per side. Buy fee remains assumed from
sell evidence; no all-in execution or profit claim.

Strategy rules and engineering bounds remain PROTOCOL.md's: one-second data,
30-minute entry window and five-minute exit window beginning at actual fresh
activation after operator setup; 2400 attempts and 40 new fills plus exit
reserve. No live orders. The $25 is virtual paper funding, not a live allocation.

The four operator commands remain owner-run from the Mac per ADR 0016. Prepare
the cluster and scripts first; do not send operator commands from a host or
silently run the owner ceremony. No edge trading before those commands are
verified committed and policy/setup plus paper initialization succeed.

Metrics for the replacement receive a distinct paper_run label. Dashboard
selectors and default start time select this run, so old Prometheus samples
remain archived without appearing in the new strategy view.
