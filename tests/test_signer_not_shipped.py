"""The signer accepts only a test key and the venue transport reaches only a local mock, so no release image, unit or other binary may carry either."""
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
    if "truemarkets_transport" in text:
        failures.append(f"{path.relative_to(root)} mentions the venue transport")

cmake = (root / "CMakeLists.txt").read_text()
links = re.findall(r"target_link_libraries\(\s*([^\s)]+)([^)]*)\)", cmake)
allowed = {"velocita-v2-signer", "v2-signer-tests", "v2-truemarkets-transport-tests"}
for target, libraries in links:
    if "velocita_v2_signer" in libraries.split() and target not in allowed:
        failures.append(f"CMake target {target} links the signer library")
transport_allowed = {"velocita_v2_truemarkets_transport", "v2-truemarkets-transport-tests"}
for target, libraries in links:
    if "velocita_v2_truemarkets_transport" in libraries.split() and target not in transport_allowed:
        failures.append(f"CMake target {target} links the venue transport")
transport = (root / "src" / "v2_truemarkets_transport.cpp").read_text()
if "INADDR_LOOPBACK" not in transport or re.search(r"getaddrinfo|gethostbyname|inet_pton|inet_addr|SSL_|TLS", transport):
    failures.append("the venue transport can name an address other than loopback")
if not re.search(r"kLiveVenueTransportCompiled\s*=\s*false", "".join(
        path.read_text() for path in (root / "include" / "velocita" / "v2").glob("*.hpp"))):
    failures.append("the compiled live-venue switch is no longer false")
if not any(target == "velocita-v2-signer" for target, _ in links):
    failures.append("the signer binary is no longer defined; this check would pass vacuously")

source = (root / "src" / "v2_signer_main.cpp").read_text()
if "--test-key-file" not in source or re.search(r'"--(key-file|live-key|credential)', source):
    failures.append("the signer accepts a key option other than --test-key-file")

for failure in failures:
    print("FAIL", failure)
print("OK" if not failures else f"{len(failures)} failure(s)")
sys.exit(1 if failures else 0)
