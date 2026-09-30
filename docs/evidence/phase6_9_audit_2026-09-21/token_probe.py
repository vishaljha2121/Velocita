"""Offline token-output permission reproduction; all secrets and transport are fake."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import stat
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

module = runpy.run_path(str(Path(__file__).resolve().parents[3] / "tools/mint-token.py"))
namespace = module["main"].__globals__
namespace["jwk_to_pem"] = lambda _: b"DUMMY-NOT-A-KEY"
namespace["der_signature_to_raw"] = lambda _: bytes(64)
with tempfile.TemporaryDirectory(prefix="velocita-token-permission-audit-") as temp:
    root = Path(temp)
    key, token = root / "dummy.json", root / "token"
    # All files are disposable synthetic fixtures inside this private directory.
    with key.open("w") as output:
        json.dump({"key_id": "dummy", "private_key": {}}, output)
    token.touch(mode=0o644)
    token.chmod(0o644)
    with patch("sys.argv", ["mint-token.py", str(key), str(token)]), \
         patch("subprocess.run", return_value=SimpleNamespace(
             stdout=b'{"access_token":"dummy.e30.dummy"}\n200')), \
         contextlib.redirect_stdout(io.StringIO()):
        result = module["main"]()
    actual = stat.S_IMODE(token.stat().st_mode)
    print(f"mint_result={result} existing_token_mode={oct(actual)}")
    if actual != 0o600:
        raise SystemExit("RED: existing token file retains group/world permissions")
