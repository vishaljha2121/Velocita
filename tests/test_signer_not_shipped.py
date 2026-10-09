"""The signer accepts only a test key, so no release image, unit or other binary may carry it."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
failures = []

for path in sorted((root / "deploy").rglob("*")):
    if not path.is_file() or path.suffix in {".pyc", ".tar", ".gz", ".bin", ".png"}:
        continue
    try:
        text = path.read_text()
    except UnicodeDecodeError:
        continue
    if "velocita-v2-signer" in text or "velocita_v2_signer" in text:
        failures.append(f"{path.relative_to(root)} mentions the signer")

cmake = (root / "CMakeLists.txt").read_text()
links = re.findall(r"target_link_libraries\(\s*([^\s)]+)([^)]*)\)", cmake)
allowed = {"velocita-v2-signer", "v2-signer-tests"}
for target, libraries in links:
    if "velocita_v2_signer" in libraries.split() and target not in allowed:
        failures.append(f"CMake target {target} links the signer library")
if not any(target == "velocita-v2-signer" for target, _ in links):
    failures.append("the signer binary is no longer defined; this check would pass vacuously")

source = (root / "src" / "v2_signer_main.cpp").read_text()
if "--test-key-file" not in source or re.search(r'"--(key-file|live-key|credential)', source):
    failures.append("the signer accepts a key option other than --test-key-file")

for failure in failures:
    print("FAIL", failure)
print("OK" if not failures else f"{len(failures)} failure(s)")
sys.exit(1 if failures else 0)
