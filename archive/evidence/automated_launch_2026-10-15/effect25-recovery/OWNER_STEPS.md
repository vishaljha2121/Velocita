# Effect 25 recovery: run order

Executable commit `64e6f9f4bd499127c7584bea02f20d6fc129597b` (36de930 plus a 300 s ctest limit for the gateway runtime
drills; the Pi needed 143.9 s). Qualified on NUC1 and Pi: [qualification-64e6f9f.json](qualification-64e6f9f.json).
Voter images: [packaged-images.json](packaged-images.json). NUC1/nuc2 binaries are byte-identical to 36de930.

| Host | Image id |
|---|---|
| NUC1 | `ba3c801425036ac1945e7022269884c0932bbb9ed81e35e8fe4ee02ca1a9a6b9` |
| nuc2 | `0179f14c3b79032ddaccb32f959c548fdcab44930573ce1464d7f737cefd0ad9` |
| Pi | `3820e4099626a05d9a361b79767a7216cf90d62310f03b122488321a62fd1f92` |

Gateway binary (NUC1): `907656eae7cb8508ef7c3a61448d649465fa0fd9f308155119fd182dcc153e29`.
Staging on NUC1: `/home/vishal/.cache/velocita-build/effect25-64e6f9f/` (`payload/`, `source/`, `stage-manifest.json`).

Every voter must run this build before an `expired_unexecuted` settlement is proposed: an older voter rejects the new
outcome. The driver stays stopped throughout. Any refusal means stop and inspect; nothing here is retried automatically.

## 1. Owner, Mac: halt

```sh
zsh docs/evidence/automated_launch_2026-10-15/operator.zsh aae119b95ddcea5a206f03ab5ca12c15 mode halted 13 --run
```

Then all three voters must report `strategy_mode` 5 at the same commit index.

## 2. velocitas, each voter host: rolling image change

Order Pi, nuc2, then NUC1 (leader) last. Each run waits for the voter to rejoin halted before the next host starts.

```sh
python3 -B rolling_voter_upgrade.py <that host's image id>
```

Backups: `~/.cache/velocita-build/effect25-backup-64e6f9f-node<N>/` on each host.

## 3. Owner, Mac: fresh TM session and bearer staging

```sh
S=~/.velocita-credentials/truemarkets/session.json
B=/home/vishal/.cache/velocita-build/effect25-64e6f9f
python3 -B tools/tm-account-session.py --session $S --refresh
python3 -B docs/evidence/automated_launch_2026-10-15/effect25-recovery/owner_recovery_token.py --session $S \
  --receiver $B/source/deploy/v2/swap-runtime/credential_handoff.py \
  --receiver-sha256 667ce3354e31c09a6709018875e5b6d44479a0ff79df9a9b7fe2784b8a9d2e09 --send
```

The first command prints only the expiry and balances. The second prints `bearer_sha256`, `expires_at_ms` and the staged
path; no token is printed. The bearer must stay valid for at least ten minutes when step 4 runs.

## 4. Owner, NUC1: preflight, then run

```sh
ssh -t vishal@100.72.127.102 '
B="$HOME/.cache/velocita-build/effect25-64e6f9f"
sudo python3 -B "$B/source/docs/evidence/automated_launch_2026-10-15/effect25-recovery/owner_recover.py" \
  --bundle "$B/payload" --binary-sha256 907656eae7cb8508ef7c3a61448d649465fa0fd9f308155119fd182dcc153e29 \
  --bearer-sha256 <bearer_sha256> --expires-at-ms <expires_at_ms> --staged <staged path>
result=$?; sudo -k; exit "$result"'
```

The preflight is read-only and must report the pending effect 25 shape, all voters halted and the NUC1 voter on
`qualified-64e6f9f`. The same command with `--run` installs the recovery-only release, starts only the gateway, waits for it
to exit and requires `pending_effect` 0, `last_effect` 25, base 0 and quote 24,047,860. The signer is never started.

## 5. After recovery

Renewal of the review (accounting for the owner's manual trades), a non-recovery manifest, the driver image on
`qualified-64e6f9f`, then normal mode and the retained driver. None of that is part of this run.
