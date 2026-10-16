# Live dashboard fill and signing display fix — 2026-10-15

Prepared and validated, not yet committed or deployed. Owner asked to fix the dashboard
without stopping/interfering with trading. Standing committed-deployment-source and explicit
commit rules require a scoped checkpoint before installation. No trading code changed.

Only two production panels change; after.json preserves all other deployed panels,
variables, selectors, layout and dashboard identity from before.json.

- Price overlay uses one instant table row per confirmed effect and the observer's persisted
  first-confirmed timestamp. Transformations scoped to B/C remove scrape Time, rename the
  event timestamp, convert it to numeric/time, and sort; price/EMA frames remain untouched.
  This replaces the repeated30-second gauge/max_over_time markers. Price decimals are3.
- Protected signing shows TRADE SETTLED only when current settlement telemetry contains the
  same run/effect as its retained signing record. Without a match, phase2 is labeled SIGNING
  RECORD RETAINED; raw journal phases remain in the lifecycle graph. No journal writes.

Validation: existing21 dashboard-layout tests pass. Five synthetic PromQL scenarios pass
in the installed promtool: matching effect, different effect, stale, invalid and missing
settlement evidence. Two deliberately broken query variants fail the expected cases:
removing effect matching and allowing stale/invalid settlement status. JSON fixtures retained.
Exact live queries returned one entry(effect1), one exit(effect3), and signing status4 at
2026-10-16 00:04 UTC. See live-query-results.json. Browser rendering remains owner-verified;
no Safari screen sharing. No C++ rebuild or service restart is needed for this change.

Install plan: archive current mounted live-dashboard file, verify before SHA,
atomically replace only that file with committed after.json, and let Grafana's file provider
reload it. Verify file hash inside container and read Grafana's stored dashboard using the
read-only database; compare gateway/signer/driver PIDs and service start timestamps before
and after. No credential access, sudo, dashboard API key or trading mutation required.

Expected before SHA256:8ecee72af60d94617c39014b77cd7cb0f2e59a07cbf7b3726566e917df1d78c9
Expected after SHA256:054bc02e411f0035da9f047e32ea09e4254ee035f008ee96ff853ca5a8c9db86
Target:/home/velocitas/read-b79e65be-86f4408/observability/velocita-v2-live.json
Initial gateway/signer/driver PIDs:511436/511419/516074. Preserve runtime and all journals.
