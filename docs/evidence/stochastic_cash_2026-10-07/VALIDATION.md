# Validation record

- Model suite: 383 unittest cases passed on the Mac.
- Repository Python tooling: 131 unittest cases passed, including 15 dashboard
  layout/generation cases and four deployment cases.
- Focused Linux container Python checks: 25 cases passed using committed model
  source and the deployed Python runtime, with networking disabled. The initial
  test-only mount `/source` was too shallow for an existing research ROOT lookup;
  `/opt/check/model/src` corrected the harness. Production image paths were not
  affected and no application change was needed for that harness issue.
- Five mutations killed by their named assertions: see MUTATIONS.json.
- Actual read-only mount drill passed: finalized SQLite journal_mode=DELETE,
  integrity_check=ok, two expected fixture fills and $25.071760 fixture cash;
  no WAL or SHM sidecar required. Observer remains read-only.
- Full Prometheus rule test suite returned SUCCESS (the test process finished
  before the attempted stop of its unrelated multi-day simulations). Focused
  paper lifecycle cases also returned SUCCESS. Full syntax check: 47 rules.
- First native image build (f854001) was withheld: 139/140 CTest targets passed.
  The failing dashboard target caught hand-edited JSON drifting from its generator
  plus historical query assertions needing explicit handling of the new lifecycle
  wrapper. Commit 9a71762 regenerated the dashboards and separately pins the exact
  completed-state query and original readiness fallback. All 15 dashboard tests
  pass locally. The corrected 9a71762 native rebuild passed all 140 CTest targets
  before activation. Its packaged image also passed all 25 focused Python tests
  without a source overlay.

No failed-build image was deployed. Observed deployment and runtime results are
recorded in DEPLOYED.md and runtime/.
