"""Keep the test signer, mock venue transport and experimental HTTPS reader out of release binaries and deployment recipes."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
failures = []
completion_targets = {
    'settlement_connection': {'v2-swap-settlement-connection-tests'},
    'signing_connection': {'v2-swap-signing-connection-tests'},
    'prepare_https': {'v2-swap-prepare-https-tests'},
    'prepare_store': {'v2-swap-settlement-connection-tests', 'v2-swap-signing-connection-tests', 'v2-swap-prepare-https-tests', 'v2-swap-prepare-store-tests', 'v2-swap-prepare-session-tests', 'v2-swap-order-status-tests', 'v2-swap-order-https-probe', 'v2-swap-submission-gateway-drill'},
    'prepare_session': {'v2-swap-prepare-https-tests', 'v2-swap-prepare-session-tests', 'v2-swap-submission-gateway-drill'},
    'signer_session': {'v2-swap-signing-connection-tests', 'v2-swap-signer-session-tests', 'v2-swap-signer-production-gate', 'v2-swap-signer-service-tests', 'v2-swap-submission-gateway-drill', 'v2-swap-process-drill'},
    'signer_protocol': {'v2-swap-signing-connection-tests', 'v2-swap-signer-protocol-tests', 'v2-swap-signer-service-tests'},
    'signer_service': {'v2-swap-signing-connection-tests', 'v2-swap-signer-service-tests'},
    'settlement_evidence': {'v2-swap-settlement-connection-tests', 'v2-swap-settlement-evidence-tests'},
    'order_status': {'v2-swap-order-status-tests', 'v2-swap-order-https-probe', 'v2-swap-submission-gateway-drill'},
    'order_https': {'v2-swap-order-https-probe'},
}


runtime_target = 'velocita_v2_swap_runtime'
runtime_tests = {'v2-swap-gateway-runtime-tests', 'v2-swap-runtime-config-tests'}
completion_targets.update({
    'runtime_config': {'v2-swap-runtime-config-tests', runtime_target},
    'runtime_https': {runtime_target},
    'runtime_production': {runtime_target},
    'account_review': {runtime_target},
    'account_history': {runtime_target},
    'gateway_runtime': {'v2-swap-gateway-runtime-tests', runtime_target},
    'settlement_admission': {runtime_target},
    'settlement_lifecycle': {runtime_target},
})
for component in completion_targets:
    completion_targets[component].add(runtime_target)
for component in ('signing_connection', 'prepare_store', 'prepare_session', 'signer_session', 'signer_protocol', 'signer_service'):
    completion_targets[component].add('v2-swap-gateway-runtime-tests')
for component in ('signing_connection', 'prepare_store', 'prepare_session', 'signer_session', 'signer_protocol', 'signer_service'):
    completion_targets[component].add('v2-swap-process-drill')
completion_targets['gateway_runtime'].add('v2-swap-process-drill')
for component in ('signer_session', 'signer_protocol', 'signer_service'):
    completion_targets[component].add('v2-swap-runtime-config-tests')
completion_targets['signer_session'].add('v2-swap-signer-verified-tests')
# Exact test target only: real verifier/receipt admission with a fake HTTPS leaf.
completion_targets['signer_session'].add('v2-swap-continuous-drill')

for path in sorted((root / "deploy").rglob("*")):
    if not path.is_file() or path.suffix in {".pyc", ".tar", ".gz", ".bin", ".png"}:
        continue
    try:
        text = path.read_text()
    except UnicodeDecodeError:
        continue
    if "v2-swap-continuous-drill" in text or "v2_swap_continuous_drill" in text:
        failures.append(f"{path.relative_to(root)} mentions the test-only continuous drill")
    for component in completion_targets:
        if f'swap_{component}' in text or f'v2-swap-{component.replace("_", "-")}' in text:
            failures.append(f'{path.relative_to(root)} mentions unqualified {component}')
    if "velocita-v2-signer" in text or "velocita_v2_signer" in text:
        failures.append(f"{path.relative_to(root)} mentions the signer")
    if "truemarkets_transport" in text:
        failures.append(f"{path.relative_to(root)} mentions the venue transport")
    if "https_read" in text or "v2-https-read-probe" in text:
        failures.append(f"{path.relative_to(root)} mentions the HTTPS reader or probe")
    if "swap_https" in text or "v2-swap-https-probe" in text:
        failures.append(f"{path.relative_to(root)} mentions the swap HTTPS transport or probe")
    if "swap_reconcile_session" in text or "v2-swap-reconcile-session" in text or "v2-swap-gateway-tls-drill" in text:
        failures.append(f"{path.relative_to(root)} mentions the swap TLS drill or reconciliation session")
    if any(name in text for name in ("swap_reconcile_store", "swap_transaction_inspection", "v2-swap-reconcile-store", "v2-swap-transaction-inspection")):
        failures.append(f"{path.relative_to(root)} mentions experimental recovery or inspection")
    if "swap_paper" in text or "v2-swap-paper" in text:
        failures.append(f"{path.relative_to(root)} mentions the local swap paper harness")
    if "swap_submission_session" in text or "v2-swap-submission-session" in text or "v2-swap-submission-gateway-drill" in text:
        failures.append(f"{path.relative_to(root)} mentions the unqualified submission session")
    if "swap_submission_resolution" in text or "v2-swap-submission-resolution" in text:
        failures.append(f"{path.relative_to(root)} mentions the unqualified submission resolution")

cmake_files = [root / "CMakeLists.txt", *sorted((root / "cmake").rglob("*.cmake")),
               *sorted((root / "tests" / "live_readiness").rglob("CMakeLists.txt"))]
cmake = '\n'.join(path.read_text() for path in cmake_files)
swap_tls_tests = {"v2-swap-reconcile-session-tests", "v2-swap-gateway-tls-drill", "v2-swap-reconcile-store-tests", "${swap_tls_target}"}
links = re.findall(r"target_link_libraries\(\s*([^\s)]+)([^)]*)\)", cmake)
allowed = {"velocita-v2-signer", "v2-signer-tests", "v2-truemarkets-transport-tests"}
for target, libraries in links:
    if "v2-swap-continuous-drill" in libraries.split():
        failures.append(f"CMake target {target} links the test-only continuous drill")
    if re.search(r"swap_paper|v2-swap-paper", libraries) and not (
            target in {"velocita-v2-gateway", "v2-swap-integration-tests", "v2-gateway-node-tests", "v2-gateway-startup-tests", "v2-swap-submission-gateway-drill", "v2-swap-gateway-runtime-tests", "v2-swap-process-drill", "v2-swap-continuous-drill"}.union(swap_tls_tests) and
            "velocita_v2_swap_paper_gateway" in libraries.split() and
            not re.search(r"v2-swap-paper|velocita_v2_swap_paper_gateway\S", libraries)):
        failures.append(f"CMake target {target} links the local swap paper harness")
    if "velocita_v2_signer" in libraries.split() and target not in allowed:
        failures.append(f"CMake target {target} links the signer library")
transport_allowed = {"velocita_v2_truemarkets_transport", "v2-truemarkets-transport-tests"}
for target, libraries in links:
    if "velocita_v2_truemarkets_transport" in libraries.split() and target not in transport_allowed:
        failures.append(f"CMake target {target} links the venue transport")
    if re.search(r"CURL::|\bcurl\b|CURL_LIBRAR|https_read|v2-https-read-probe|swap_https|v2-swap-https-probe", libraries) and target not in {"v2-https-read-probe", "v2-swap-https-probe", "v2-swap-order-https-probe", "v2-swap-prepare-https-tests", "v2-swap-settlement-admission-tests", "v2-swap-runtime-https-probe", runtime_target}.union(swap_tls_tests):
        failures.append(f"CMake target {target} links HTTPS dependencies")
    if 'OpenSSL::Crypto' in libraries and target not in completion_targets['signer_session'].union({'v2-swap-settlement-admission-tests', 'v2-swap-runtime-https-probe'}):
        failures.append(f'CMake target {target} links unqualified signer crypto')
for target, libraries in links:
    if runtime_target in libraries.split() and target not in {'velocita-v2-gateway', 'v2-swap-runtime-production-tests', 'v2-swap-settlement-lifecycle-tests', 'v2-swap-account-setup-tests', 'v2-swap-failed-settlement-tests', 'v2-swap-continuous-drill'}:
        failures.append(f'CMake target {target} links swap runtime outside gateway')
for definitions in re.findall(r'add_compile_definitions\(([^)]*)\)', cmake):
    if 'VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE' in definitions or 'VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE' in definitions:
        failures.append('global synthetic definition can contaminate production')
for target, definitions in re.findall(r'target_compile_definitions\(\s*([^\s)]+)([^)]*)\)', cmake):
    if 'VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE' in definitions and target not in {'v2-swap-gateway-runtime-tests', 'v2-swap-process-drill', 'v2-swap-settlement-admission-tests'}:
        failures.append(f'CMake target {target} enables synthetic settlement outside its fixture')
    if 'VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE' in definitions and target not in {'v2-swap-signing-connection-tests', 'v2-swap-signer-session-tests', 'v2-swap-signer-service-tests', 'v2-swap-submission-gateway-drill', 'v2-swap-process-drill'}:
        failures.append(f'CMake target {target} enables synthetic signing outside its fixture')
for target, sources in re.findall(r"(?:add_executable|add_library|target_sources)\(\s*([^\s)]+)([^)]*)\)", cmake):
    if "v2_swap_continuous_drill.cpp" in sources and target != "v2-swap-continuous-drill":
        failures.append(f"CMake target {target} includes the test-only continuous drill")
    for component, targets in completion_targets.items():
        if f'v2_swap_{component}.cpp' in sources and target not in targets:
            failures.append(f'CMake target {target} includes unqualified {component}')
    if "v2_swap_submission_resolution.cpp" in sources and target not in {"v2-swap-submission-resolution-tests", "v2-swap-submission-gateway-drill", "v2-swap-gateway-runtime-tests", "v2-swap-process-drill", runtime_target}:
        failures.append(f"CMake target {target} includes the unqualified submission resolution")
    if "v2_swap_submission_session.cpp" in sources and target not in {"v2-swap-submission-session-tests", "v2-swap-submission-gateway-drill", "v2-swap-gateway-runtime-tests", "v2-swap-process-drill", runtime_target}:
        failures.append(f"CMake target {target} includes the unqualified submission session")
    if "v2_swap_paper" in sources and target not in {"${swap_target}", "v2-swap-paper-tests", "v2-swap-paper-drill", "velocita_v2_swap_paper_gateway"}:
        failures.append(f"CMake target {target} includes the local swap paper harness")
    if target == "velocita_v2_swap_paper_gateway" and any(
            token for token in sources.split() if "v2_swap_paper" in token and
            token not in {"src/v2_swap_paper.cpp", "src/v2_swap_paper_gateway.cpp"}):
        failures.append(f"CMake target {target} includes the local swap paper harness")
    if "v2_swap_https.cpp" in sources and target not in {"v2-swap-https-probe", "v2-swap-order-https-probe", "v2-swap-prepare-https-tests", "v2-swap-settlement-admission-tests", "v2-swap-runtime-https-probe", runtime_target}.union(swap_tls_tests):
        failures.append(f"CMake target {target} includes the swap HTTPS transport")
    if "v2_swap_reconcile_session.cpp" in sources and target not in swap_tls_tests:
        failures.append(f"CMake target {target} includes the experimental reconciliation session")
    if ("v2_swap_reconcile_store.cpp" in sources and target not in swap_tls_tests.union({"velocita_v2_swap_recovery"})) or ("v2_swap_transaction_inspection.cpp" in sources and target != "v2-swap-transaction-inspection-tests"):
        failures.append(f"CMake target {target} includes experimental recovery or inspection")
    if target == "velocita_v2_swap_recovery" and any(token not in {
            "src/v2_swap_result_watermark.cpp", "src/v2_swap_reconcile_store.cpp"} for token in sources.split()):
        failures.append("swap recovery library includes unreviewed sources")
    if "v2_https_read.cpp" in sources and target != "v2-https-read-probe":
        failures.append(f"CMake target {target} includes the HTTPS reader")
swap_loops = re.findall(r"foreach\(swap_target\s+IN\s+ITEMS\s+([^)]*)\)", cmake)
if any(set(value.split()) != {"v2-swap-paper-tests", "v2-swap-paper-drill"} for value in swap_loops):
    failures.append("swap paper target loop includes a non-test target")
tls_loops = re.findall(r"foreach\(swap_tls_target\s+IN\s+ITEMS\s+([^)]*)\)", cmake)
if not tls_loops or any(set(value.split()) != swap_tls_tests - {"${swap_tls_target}"} for value in tls_loops):
    failures.append("swap TLS target loop includes a non-test target")
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
