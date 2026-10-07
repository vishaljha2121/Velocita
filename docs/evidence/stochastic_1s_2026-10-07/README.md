# One-second stochastic paper deployment

Current status: fresh replacement cluster ready; owner Mac ceremony and paper
activation pending. Only one cluster runs. Old state is archived on all hosts.
The owner selected a new $25 virtual balance with no old trades or fees.

Read [RESET_COMMANDS.md](RESET_COMMANDS.md) for the exact commands executed,
verification results, archive locations and the next owner command.
[RESET_PROTOCOL.md](RESET_PROTOCOL.md) supersedes the original continuous-account
plan. [DEPLOYED.md](DEPLOYED.md) records the initial startup failure and repair.

Production image: paper-1s-0680b08, built on nuc1 from committed source.
140 C++ tests, 369 local model tests and 11 in-image strategy tests passed.
The Mac runs Python validation and builds the owner operator client only.

Strategy: one-second completed SOLUSDT candles, 900s stochastic range, SMA10 K,
SMA30 D, EMA300/1200 trend filter, three-second confirmation, 60s cooldown, and
one completed second of execution delay. Policy/schema 128/138. New fills cost
20 bp per side, fee-only; buy symmetry is assumed from sell evidence.

Bounded paper engineering run: 30-minute entry window plus five-minute exit
window after actual activation, 2400 attempts, 40 new fills plus exit reserve.
No profitability claim, no indefinite run, no live orders.

ACTIVATE_RECORDED.py is the historical continuation attempt. REPAIR_RECORDED.py
was prepared but not executed; the owner chose a fresh reset instead. Do not run
those historical scripts. Fresh-run scripts are one-time records, not reusable
reset commands; never rerun against an already initialized account.
