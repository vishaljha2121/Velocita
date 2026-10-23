#!/bin/zsh
# Owner Mac, steps 10, 11 and 13: keep one vishal session open for the tmpfs staging, retire the old swap install, install
# the dormant release, adopt the documents, stage and install both credentials, verify. Starts nothing. No retry.
set -euo pipefail
REPO=$HOME/aeron-raft-pi
TM=$HOME/.velocita-credentials/truemarkets
A=$TM/automated-launch-aae119b9/documents/adoption
NUC=vishal@100.72.127.102
B=/home/vishal/.cache/velocita-build/automated-launch-aae119b9
RECV=$B/receiver/deploy/v2/swap-runtime
PROFILE_SHA=b9734e53cfb37ee38344a5cf9fcdcb4905e248818dbc151c5e18a9f01d0208b2
s(){ shasum -a 256 "$1" | cut -d' ' -f1; }
[[ "$(s $A/runtime.profile)" == $PROFILE_SHA && "$(s $A/adoption.json)" == a5e9c4fea7b4b9c8658d6011ea9ebacd9b907d6ad72d634bf427316f7a5274a5 ]]
ssh -f -o BatchMode=yes $NUC 'sleep 1800'
print -- '--- steps 10, 11, 13a: retire, install dormant release, adopt documents (sudo)'
ssh -t $NUC "sudo python3 -B $B/scripts/retire_installation.py 20261015 && \
  sudo python3 -B $RECV/install_disabled.py --bundle $B/install-bundle --bundle-sha256 383c79a9178a2a65095bc2d2bca914711d391152286c1c1ae06a025fe1410932 --apply && \
  sudo python3 -B $RECV/adopt_account.py --bundle $B/adoption --sha256 a5e9c4fea7b4b9c8658d6011ea9ebacd9b907d6ad72d634bf427316f7a5274a5 --apply; sudo -k"
print -- '--- step 13b: stage both credentials over SSH stdin'
stage(){ python3 -B $REPO/tools/tm-stage-swap-credential.py --role $1 --private-source $TM/api-key.json --profile $A/runtime.profile \
  --profile-sha256 $PROFILE_SHA --receiver $RECV/credential_handoff.py \
  --receiver-sha256 667ce3354e31c09a6709018875e5b6d44479a0ff79df9a9b7fe2784b8a9d2e09 --send; }
G=$(stage gateway); S=$(stage signer)
GP=$(print -r -- $G | python3 -c 'import json,sys;print(json.load(sys.stdin)["staged_path"])')
SP=$(print -r -- $S | python3 -c 'import json,sys;print(json.load(sys.stdin)["staged_path"])')
SSHA=$(print -r -- $S | python3 -c 'import json,sys;print(json.load(sys.stdin)["sha256"])')
print -- "staged gateway $GP"; print -- "staged signer $SP"
print -- '--- step 13c: install both credentials and verify (sudo)'
ssh -t $NUC "sudo python3 -B $RECV/credential_handoff.py install --role gateway --source $GP --profile-sha256 $PROFILE_SHA && \
  sudo python3 -B $RECV/credential_handoff.py install --role signer --source $SP --profile-sha256 $PROFILE_SHA && \
  sudo python3 -B $B/scripts/verify_installed.py --receiver $RECV --profile-sha256 $PROFILE_SHA \
    --manifest-sha256 73bec28444375f0641d6406aed5e7d0d0a5881c418a1dadb1a6ffa47358adcc3 --signer-sha256 $SSHA; sudo -k"
