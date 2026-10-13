# Live account and execution dashboard

**Deployed October13, around22:23UTC, from checkpoint19f67d2.** Grafana unified
storage contains the new27-panel Live trading dashboard and all six navigation
links on every dashboard. Prometheus reports observer_up=1 and
node_textfile_scrape_error=0. The timer succeeds every15seconds; no Grafana,
voter, exporter or trading service was restarted. Existing panel counts remain
22/10/80/37/19 for Overview/Hosts/Paper/Raft/Strategy, including their banners.

Prepared after reviewed funding/start. No trade was requested or sent.
The supervised gateway stopped at22:17:27UTC because its review expired; signer
stopped normally at22:17:29UTC. Both journals remain idle/effect0/index6.

The new Live trading dashboard has27panels/rows and joins the common navigation.
It reports observer freshness, gateway/signer state, review expiry, cluster mode,
last reviewed SOL/USDC balances and evidence age, durable preparation/signing/
submission stages, effect IDs, committed index, restart counts and configured
maximum output deduction. Incarnation selection prevents blending separate runs.

Balance values are the latest reviewed account-history snapshot, not a recurring
wallet poll. Polling the observer does not refresh their evidence timestamp.
A quote, signature, submission or journal result is not labeled an economic fill.
Actual settlement amounts, fill-price markers and P&L require confirmed settlement
telemetry; these are not fabricated from quoted output or paper data. This gap is
an observability enhancement, not a new gate for the already-bounded owner test.

The collector reads only public review documents and checksummed local journals.
It has no credential access, no IP network access and no write access to trading
state. A separate root oneshot/timer reads the two service-owned journal trees;
systemd makes those paths read-only and hides the credential directories. It
writes only a scalar Prometheus textfile every15seconds, through an atomic rename.
No private request bodies, handles, wallet/user IDs or credentials become labels.
On read/schema/hash errors it publishes observer_up=0 and omits account/order
samples. Dashboard queries also expire when the observer is older than45seconds.

Validation:7collector tests,19existing dashboard/layout tests and3named mutation
kills. All26dashboard PromQL queries passed the running Prometheus parser. A
read-only run against actual protected files decoded the idle VSSJ/VPRP journals,
reported the exact reviewed balances, and correctly reported expired/stopped
services. No production native rebuild, voter restart or trade restart is needed.

Deployment must preserve the actual mounted directory:
/home/velocitas/read-b79e65be-86f4408/observability.
The deployed five dashboards each have one additional operational banner compared
with generated source. Preserve their panel JSON and add only the Live trading
link. Add the new generated live dashboard. Grafana polls provisioned files every
30seconds; verify its loaded database and Prometheus samples from the terminal.
Do not replace the source tree or restart voters/exporters.

## Strategy consequence

The20bp explicit fee is not the entire buy cost. The prior owner buy observed
550,840lamports deducted from gross SOL output. The bounded MVP conservatively
allows up to2,039,280lamports, with no sponsor credit assumed. The allowance is not
an expected fee estimate, and one settlement does not establish a universal
route/size cost law.

At an illustrative$100/SOL, the observed deduction is$0.055084: on a$1buy,
adding the$0.002explicit fee costs about5.71%before other price effects. The same
fixed deduction is0.22%of a$25purchase. Its size dependence matters. Re-test the
existing strategy using separate explicit fees and native-output deductions,
including conservative stress costs and realistic receipt/slippage assumptions.
Do not claim the old fee-only paper P&L survives, and do not alter indicator
thresholds merely to fit the new cost observation. Test turnover/edge/size changes
under a registered experiment before strategy promotion.

With flat gross trading P&L,20bp on$50,000one-way executed volume alone costs
about$100, above the$20loss objective; extra deductions add to that. Profitable
price moves would need to offset the difference. Faster execution does not
remove this cost requirement.

## Next trading steps

1. Refresh the account review against the existing committed opening and journals;
   never initialize a second ledger. Renew the bearer separately if necessary.
2. Review a fresh route and positive conservative net SOL receipt for exactly
   one$1total-debit buy; the deferred minimum-output/rounding check concerns
   executable receipt, not the established$1minimum order size.
3. Obtain the specific buy authorization, execute once, and reconcile TM status,
   finalized chain receipt and balances before any retry or strategy promotion.
4. Re-evaluate the strategy economics with that measured receipt/cost evidence.


## Deployment and verification record

- Committed the scoped collector, generator, six JSON dashboards, tests and unit
  files as19f67d2. Unrelated dirty alert/kernel/gateway files stayed unstaged.
- Staged committed source to the owner cache live-observability-19f67d2-20261013;
  all three collector/unit hashes matched observer.sha256. systemd-analyze verify
  passed, with unrelated existing netplan permission/snapd RestartMode warnings.
  Tar emitted harmless macOS provenance-header warnings; exact bytes were hashed.
- Owner sudo ran install-observer.sh once. Root-owned source0444 at
  /opt/velocita/live-observer/live_observer.py; enabled only the observer timer.
  The root service has the velocitas supplemental group to atomically write the
  existing group-writable textfile directory; protected journals remain read-only.
- On the velocitas account, checked all five prior dashboard SHA256s before
  copying each to ~/.cache/velocita-live-dashboard-19f67d2/before. Added the link
  while asserting panels unchanged, then created the new dashboard in the
  actual mounted directory. No source swap or monitoring restart.
- Read Prometheus over host-local HTTP; real balances, review expiry, idle phase,
  observer health and zero textfile errors were present.
- Verified Grafana with a read-only SQLite query of its unified `resource` table,
  group dashboard.grafana.app. The legacy `dashboard` table is empty on this
  deployment; its empty result was not treated as a provisioning failure.
- Live URL: http://100.72.127.102:3000/d/velocita-v2-live
- No Safari, signing, new intent, venue execution or balance reset.

## Entry/exit chart requested by owner

The Live tab now includes a green-entry/red-exit point chart, and an explicit
NOT CONNECTED indicator. This is dashboard preparation, not completed fill
instrumentation. Current production gateway journals retain lifecycle state and
checksums, but do not export economic settlement events. The chart deliberately
has no fabricated data, no paper markers and no quote-as-fill fallback.

Remaining connection: export finalized, Raft-committed successful settlements from
the gateway's authenticated state, keyed by incarnation and effect. Publish total
USDC debit/net SOL credit for buys and net USDC credit/SOL debit for sells, plus
quantity and confirmation time. A repeated poll/restart must not create another
marker; failed/unknown outcomes and deposits must not become fills. Preserve
recorded event time across restarts rather than using file mtime. Show any price
reference as a separately labeled proxy, with freshness checks. This requires
production gateway instrumentation and native qualification; the read-only
collector alone cannot infer it from result digests. No kernel or Raft rule change
is needed. This is required for the requested chart, not an added trade gate.

The cluster-mode selector was corrected in c05dd8f: live queries use the actual
incarnation, not the old stochastic paper label. All 26 original queries returned
samples and Grafana's loaded resource was checked from the host terminal.
