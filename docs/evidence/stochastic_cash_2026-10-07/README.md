# Ongoing stochastic paper trading

Owner request: keep the same paper strategy running while its remaining virtual
balance can fund the next order, instead of ending after the 30-minute pilot.
This folder supersedes the timed-run requirement; the old pilot evidence remains
under ../stochastic_1s_2026-10-07/.

- Same cluster and account; start continuation from $24.869170 with four previous
  fills. No new $25 funding and no history reset.
- One-second stochastic/EMA signals, 0.12 SOL clips, 20 bp per side fee-only model.
- No scheduled end, pilot frame cap, pilot fill cap or lifetime reconnect cap.
- Normal cash stop only while flat and unable to afford the clip including fees.
  The existing $10 drawdown, 240-order, reconciliation and storage guards remain.
- New session metrics are separate from lifetime account metrics. Old balances,
  fees and confirmed fills remain intact. Policy/schema 129/139.
- Checked final ledger copies keep stopped-session metrics readable with the
  observer's filesystem access still read-only.

See PROTOCOL.md for the exact contract, VALIDATION.md and MUTATIONS.json for
checks, and activate.py for the ordered deployment recipe. Runtime results and
exact image/command records are recorded in DEPLOYED.md after verification.
No live venue access or real-money order is part of this work.
