import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReadinessBuildBoundaryTests(unittest.TestCase):
    def setUp(self):
        scratch = pathlib.Path.home() / '.cache/velocita-build/test-scratch'
        scratch.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        for relative in ['CMakeLists.txt', 'src/v2_truemarkets_transport.cpp', 'src/v2_signer_main.cpp']:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        for relative in ['cmake', 'include/velocita/v2', 'tests/live_readiness']:
            if (ROOT / relative).exists():
                shutil.copytree(ROOT / relative, self.root / relative)
        (self.root / 'deploy').mkdir()

    def check_guard(self):
        return subprocess.run([sys.executable, '-B', str(ROOT / 'tests/test_signer_not_shipped.py'),
                               str(self.root)], capture_output=True, text=True, check=False)

    def append(self, relative, value):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('a') as stream:
            stream.write('\n' + value + '\n')

    def test_current_tree_is_allowed(self):
        result = self.check_guard()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_continuous_drill_cannot_be_shipped(self):
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-continuous-drill /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('mentions the test-only continuous drill', result.stdout)

    def test_continuous_drill_source_cannot_join_production(self):
        self.append('CMakeLists.txt', 'target_sources(velocita_v2_swap_runtime PRIVATE tests/v2_swap_continuous_drill.cpp)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the test-only continuous drill', result.stdout)

    def test_continuous_drill_cannot_link_into_production(self):
        self.append('CMakeLists.txt', 'target_link_libraries(velocita-v2-gateway PRIVATE v2-swap-continuous-drill)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('links the test-only continuous drill', result.stdout)

    def test_continuous_drill_must_keep_real_verification(self):
        self.append('CMakeLists.txt', 'target_compile_definitions(v2-swap-continuous-drill PRIVATE VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1 VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('enables synthetic settlement outside its fixture', result.stdout)
        self.assertIn('enables synthetic signing outside its fixture', result.stdout)

    def test_new_completion_components_cannot_ship_or_link(self):
        for component in ('settlement_connection', 'signing_connection', 'prepare_https', 'prepare_store', 'prepare_session', 'signer_session', 'signer_protocol', 'signer_service', 'settlement_evidence', 'order_status', 'order_https'):
            self.append('CMakeLists.txt', f'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_{component}.cpp)')
            self.append('deploy/Containerfile.fixture', f'COPY v2-swap-{component.replace("_", "-")}-tests /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        for component in ('settlement_connection', 'signing_connection', 'prepare_https', 'prepare_store', 'prepare_session', 'signer_session', 'signer_protocol', 'signer_service', 'settlement_evidence', 'order_status', 'order_https'):
            self.assertIn(f'includes unqualified {component}', result.stdout)
            self.assertIn(f'mentions unqualified {component}', result.stdout)

    def test_synthetic_signer_macro_cannot_enable_production(self):
        self.append('CMakeLists.txt', 'target_compile_definitions(velocita-v2-gateway PRIVATE VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('enables synthetic signing outside its fixture', result.stdout)

    def test_runtime_never_links_into_voter(self):
        self.append('CMakeLists.txt', 'target_link_libraries(velocita-v2-node PRIVATE velocita_v2_swap_runtime)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('links swap runtime outside gateway', result.stdout)

    def test_synthetic_settlement_cannot_enable_production(self):
        self.append('CMakeLists.txt', 'target_compile_definitions(velocita_v2_swap_runtime PRIVATE VELOCITA_SWAP_SETTLEMENT_SYNTHETIC_FIXTURE=1)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('enables synthetic settlement outside its fixture', result.stdout)

    def test_global_fixture_macro_refused(self):
        self.append('CMakeLists.txt', 'add_compile_definitions(VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE=1)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('global synthetic definition', result.stdout)

    def test_submission_resolution_cannot_ship_or_link(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_submission_resolution.cpp)')
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-submission-resolution-tests /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the unqualified submission resolution', result.stdout)
        self.assertIn('mentions the unqualified submission resolution', result.stdout)

    def test_gateway_cannot_link_curl(self):
        self.append('CMakeLists.txt', 'target_link_libraries(velocita-v2-gateway PRIVATE CURL::libcurl)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1, 'a gateway TLS link must be caught')
        self.assertIn('velocita-v2-gateway links HTTPS dependencies', result.stdout)

    def test_included_module_cannot_link_curl(self):
        self.append('cmake/LiveReadinessTests.cmake', 'target_link_libraries(velocita-v2-gateway PRIVATE CURL::libcurl)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1, 'included CMake files must also be checked')
        self.assertIn('velocita-v2-gateway links HTTPS dependencies', result.stdout)

    def test_https_source_cannot_be_added_to_production(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_https_read.cpp)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1, 'direct source inclusion must be caught')
        self.assertIn('velocita-v2-gateway includes the HTTPS reader', result.stdout)

    def test_https_probe_cannot_be_shipped(self):
        self.append('deploy/Containerfile.fixture', 'COPY v2-https-read-probe /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1, 'release recipe must not copy the HTTPS probe')
        self.assertIn('mentions the HTTPS reader or probe', result.stdout)

    def test_swap_https_cannot_ship_or_link(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_https.cpp)')
        self.append('CMakeLists.txt', 'target_link_libraries(velocita-v2-gateway PRIVATE v2-swap-https-probe)')
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-https-probe /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the swap HTTPS transport', result.stdout)
        self.assertIn('links HTTPS dependencies', result.stdout)
        self.assertIn('mentions the swap HTTPS transport or probe', result.stdout)

    def test_swap_reducer_cannot_be_added_to_live_gateway(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_paper.cpp)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the local swap paper harness', result.stdout)

    def test_swap_gateway_library_cannot_include_the_drill(self):
        self.append("CMakeLists.txt", "target_sources(velocita_v2_swap_paper_gateway PRIVATE tests/v2_swap_paper_drill.cpp)")
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn("includes the local swap paper harness", result.stdout)

    def test_swap_drill_cannot_be_shipped(self):
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-paper-drill /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('mentions the local swap paper harness', result.stdout)


    def test_tls_drill_loop_cannot_include_production(self):
        self.append('cmake/LiveReadinessTests.cmake', 'foreach(swap_tls_target IN ITEMS velocita-v2-gateway)\nendforeach()')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('swap TLS target loop includes a non-test target', result.stdout)

    def test_reconcile_session_cannot_ship_or_join_production(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_reconcile_session.cpp)')
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-gateway-tls-drill /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the experimental reconciliation session', result.stdout)
        self.assertIn('mentions the swap TLS drill or reconciliation session', result.stdout)

    def test_recovery_library_cannot_hide_network_or_signing(self):
        self.append('CMakeLists.txt', 'target_sources(velocita_v2_swap_recovery PRIVATE src/v2_swap_https.cpp)')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('swap recovery library includes unreviewed sources', result.stdout)

    def test_submission_session_cannot_ship_or_join_production(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_submission_session.cpp)')
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-submission-session-tests /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes the unqualified submission session', result.stdout)
        self.assertIn('mentions the unqualified submission session', result.stdout)

    def test_recovery_and_inspection_cannot_join_production(self):
        self.append('CMakeLists.txt', 'target_sources(velocita-v2-gateway PRIVATE src/v2_swap_reconcile_store.cpp src/v2_swap_transaction_inspection.cpp)')
        self.append('deploy/Containerfile.fixture', 'COPY v2-swap-reconcile-store-tests /usr/local/bin/')
        result = self.check_guard()
        self.assertEqual(result.returncode, 1)
        self.assertIn('includes experimental recovery or inspection', result.stdout)
        self.assertIn('mentions experimental recovery or inspection', result.stdout)


if __name__ == '__main__':
    unittest.main()
