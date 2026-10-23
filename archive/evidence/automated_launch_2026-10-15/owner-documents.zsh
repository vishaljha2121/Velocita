#!/bin/zsh
# Owner Mac, steps 9 and 12: refresh the TM session, capture the account snapshot, build the new profile, opening and adoption bundle.
# Reads only; changes nothing on any host. No retry: a refusal stops here and leaves its output.
set -euo pipefail
REPO=$HOME/aeron-raft-pi
HERE=$REPO/docs/evidence/automated_launch_2026-10-15
TM=$HOME/.velocita-credentials/truemarkets
OLD=$TM/rerun-launch-6c54f3f-20261014
D=$TM/automated-launch-aae119b9
MAC=$HOME/.cache/velocita-build/automated-launch-54a5fcb/mac
s(){ shasum -a 256 "$1" | cut -d' ' -f1; }
[[ "$(s $HERE/dormant-manifest.json)" == 252bef136ad33220ee1ebaef459fdc53ff7d22c0b7f5dc8b38ee7e7f6471c42f ]]
[[ "$(s $OLD/window-1/bundle/after/runtime.profile)" == db1388aec6e6bd9c8c7741390477e0bc82d3eb6013f8f48ec2618f964a0b44e6 ]]
[[ "$(s $MAC/prepare-swap-mvp)" == 3343f2e994a1e247758ef73589a65ba2cb1903e4731fd17f4f76ebb79da9c80f ]]
[[ ! -e $D ]]
mkdir -m 700 $D
python3 -B $REPO/tools/tm-account-session.py --session $TM/session.json --refresh > $D/session-refresh.log
python3 -B $REPO/tools/tm-account-history-check.py --session $TM/session.json \
  --setup-pin $TM/funding-start-valuation-20261013/identity.private.json \
  --setup-sha256 fbf9602f339af07a4ad77e5002f688a8b2b57e604d2d788b77be94b6a46cd9c9 --output $D/history --run > $D/history.log
python3 -B $HERE/prepare_documents.py --history $D/history --previous-profile $OLD/window-1/bundle/after/runtime.profile \
  --transaction-review $OLD/intent-bundle/transaction.review --dormant-manifest $HERE/dormant-manifest.json \
  --builder $MAC/prepare-swap-mvp --output $D/documents \
  --incarnation aae119b95ddcea5a206f03ab5ca12c15 \
  --authority-domain ce15af7abc612bcc8f7ba2358867afe709d26816c2da915a84b6b0b8c2c09703
