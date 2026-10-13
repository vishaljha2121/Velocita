#!/bin/sh
set -eu
B="$HOME/.cache/velocita-build/swap-cutover-20261013-read-r3"
O="$HOME/.cache/velocita-build/fresh-adoption-20261013/authenticated-read"
sudo -v
sudo python3 -B "$B/authenticated_read_run.py" \
 --helper-sha256 e8b8fa9888d393cfdcab5a8beda3b2891c8ec8d9aa74e7025a5edee5857e0a58 \
 --catalog-sha256 8a18bbee0363a626a158aeaebf5c23087ee2268ee67b23cc4987c70f2758ad3f \
 --incarnation b79e65be1ea14e219016ec48ea02efef \
 --catalog-root 3b8b7f798e31c6ce39ef6a88ba44fd2644ec8c6e4792ed7beb227fd14a532dfe \
 --authority-domain 29b772a949627cd25c657a527ef2ea79a7b470454de9be9aa842462982b9d95e \
 --output "$O" --run
sudo python3 -B - "$O" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);d=json.loads((p/'result.json').read_bytes())
print(json.dumps({k:d[k] for k in ('authenticated_voter_reads','results','live_admitted')},indent=2))
PY
