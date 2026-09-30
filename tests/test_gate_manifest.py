import os
import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
HELPER = REPO_ROOT / "deploy" / "source-manifest.sh"
INVENTORY_HELPER = REPO_ROOT / "deploy" / "validate-ctest-inventory.py"


class SourceManifestTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tempdir.name)
        files = {
            "CMakeLists.txt": "project(fixture)\n",
            "include/velocita/shared.hpp": "shared header\n",
            "include/velocita/v2/kernel.hpp": "v2 header\n",
            "src/storage.cpp": "shared storage\n",
            "src/v2_kernel.cpp": "v2 source\n",
            "tests/v2_kernel_tests.cpp": "test source\n",
            "cmake/toolchain.cmake": "toolchain input\n",
            "deploy/source-manifest.sh": "fixture helper\n",
            "deploy/run-v2-phase7-container-gate.sh": "gate input\n",
            "deploy/pi-container/Containerfile": "FROM scratch\n",
            "deploy/pi-container/run-phase2-kernel-gate-inside.sh": "inner gate\n",
            "deploy/patches/0001.patch": "dependency patch\n",
            "deploy/quadlet/node.container": "runtime config\n",
            "deploy/systemd-user/node.service": "service config\n",
            "deploy/observability/prometheus.yml": "monitor config\n",
            "model/private-research.txt": "unrelated model\n",
            "docs/research/private.md": "unrelated research\n",
        }
        for relative, contents in files.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

    def tearDown(self):
        self.tempdir.cleanup()

    def manifest(self, locale="C"):
        manifest = self.root / "manifest.txt"
        env = os.environ.copy()
        env["LC_ALL"] = locale
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(manifest)],
            check=True,
            text=True,
            capture_output=True,
            env=env,
        )
        return result.stdout.strip(), manifest.read_text(encoding="utf-8")

    def test_manifest_is_deterministic_and_dependency_complete(self):
        root_before, manifest = self.manifest("C")
        self.assertIn("  src/storage.cpp\n", manifest)
        self.assertIn("  deploy/run-v2-phase7-container-gate.sh\n", manifest)
        self.assertIn("  deploy/pi-container/Containerfile\n", manifest)
        self.assertIn("  deploy/patches/0001.patch\n", manifest)
        self.assertIn("  deploy/quadlet/node.container\n", manifest)
        self.assertIn("  cmake/toolchain.cmake\n", manifest)
        self.assertNotIn("model/private-research.txt", manifest)
        self.assertNotIn("docs/research/private.md", manifest)

        (self.root / "src/storage.cpp").write_text("changed storage\n", encoding="utf-8")
        storage_root, _ = self.manifest("C")
        self.assertNotEqual(root_before, storage_root)

        (self.root / "src/storage.cpp").write_text("shared storage\n", encoding="utf-8")
        (self.root / "deploy/run-v2-phase7-container-gate.sh").write_text(
            "changed gate\n", encoding="utf-8"
        )
        gate_root, _ = self.manifest("C")
        self.assertNotEqual(root_before, gate_root)

    def test_manifest_ignores_ambient_locale(self):
        c_root, c_manifest = self.manifest("C")
        locale = next(
            (candidate for candidate in ("en_US.UTF-8", "C.UTF-8", "UTF-8")
             if self._locale_available(candidate)),
            None,
        )
        if locale is None:
            self.skipTest("no UTF-8 locale is installed")
        other_root, other_manifest = self.manifest(locale)
        self.assertEqual(c_root, other_root)
        self.assertEqual(c_manifest, other_manifest)

    def _locale_available(self, locale):
        return subprocess.run(
            ["locale", "charmap"],
            env={**os.environ, "LC_ALL": locale},
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode == 0

    def test_unrelated_protected_trees_do_not_change_root(self):
        root_before, _ = self.manifest()
        (self.root / "model/private-research.txt").write_text("changed\n", encoding="utf-8")
        (self.root / "docs/research/private.md").write_text("changed\n", encoding="utf-8")
        root_after, _ = self.manifest()
        self.assertEqual(root_before, root_after)

    def test_selected_symlink_is_rejected(self):
        os.symlink("storage.cpp", self.root / "src/storage-link.cpp")
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(self.root / "out")],
            text=True,
            capture_output=True,
        )
        self.assertEqual(65, result.returncode)
        self.assertIn("Symlinks are not allowed", result.stderr)

    def test_top_level_cmake_symlink_is_rejected(self):
        cmake = self.root / "CMakeLists.txt"
        cmake.unlink()
        os.symlink("src/storage.cpp", cmake)
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(self.root / "out")],
            text=True,
            capture_output=True,
        )
        self.assertEqual(65, result.returncode)
        self.assertIn("Symlinks are not allowed", result.stderr)

    def test_output_symlink_does_not_overwrite_its_source_target(self):
        target = self.root / "outside-target"
        target.write_text("preserve me\n", encoding="utf-8")
        output = self.root / "manifest.txt"
        os.symlink(target, output)
        self.manifest()
        self.assertEqual("preserve me\n", target.read_text(encoding="utf-8"))
        self.assertFalse(output.is_symlink())

    def test_output_hardlink_does_not_overwrite_its_source_target(self):
        target = self.root / "outside-hardlink-target"
        target.write_text("preserve me too\n", encoding="utf-8")
        output = self.root / "manifest.txt"
        os.link(target, output)
        self.manifest()
        self.assertEqual("preserve me too\n", target.read_text(encoding="utf-8"))
        self.assertNotEqual(target.stat().st_ino, output.stat().st_ino)

    def test_failed_file_hash_cannot_produce_a_root(self):
        fake_bin = self.root / "fake-bin"
        fake_bin.mkdir()
        fake_hasher = fake_bin / "sha256sum"
        fake_hasher.write_text(
            "#!/bin/sh\n"
            "if [ \"$#\" -gt 0 ]; then exit 9; fi\n"
            "printf '%064d  -\\n' 0\n",
            encoding="utf-8",
        )
        fake_hasher.chmod(0o755)
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(self.root / "out")],
            text=True,
            capture_output=True,
            env={**os.environ, "PATH": f"{fake_bin}:{os.environ['PATH']}"},
        )
        self.assertNotEqual(0, result.returncode)
        self.assertEqual("", result.stdout)

    def test_manifest_output_cannot_be_inside_selected_tree(self):
        selected_output = self.root / "deploy" / "generated-manifest.txt"
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(selected_output)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(65, result.returncode)
        self.assertIn("outside selected source trees", result.stderr)
        self.assertFalse(selected_output.exists())

    def test_manifest_output_cannot_be_a_directory(self):
        output = self.root / "output-directory"
        output.mkdir()
        result = subprocess.run(
            [str(HELPER), "--root", str(self.root), "--manifest", str(output)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(65, result.returncode)
        self.assertIn("must not be a directory", result.stderr)

    def test_phase7_wrappers_require_external_expected_root(self):
        for relative in (
            "deploy/run-v2-phase7-container-gate.sh",
            "deploy/run-v2-phase7-nuc-gate.sh",
        ):
            text = (REPO_ROOT / relative).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"VELOCITA_EXPECTED_SOURCE_ROOT=[0-9a-f]{64}")
            self.assertIn("VELOCITA_EXPECTED_SOURCE_ROOT:?", text)

    def test_inner_gate_rejects_an_empty_test_inventory(self):
        text = (REPO_ROOT / "deploy/pi-container/run-phase2-kernel-gate-inside.sh").read_text(
            encoding="utf-8"
        )
        self.assertEqual(3, text.count("--no-tests=error"))
        self.assertIn("required-targets.txt", text)
        self.assertIn("failure_step=evidence_inventory", text)

    def test_ctest_inventory_rejects_nonempty_but_incomplete_suite(self):
        inventory = self.root / "ctest.json"
        inventory.write_text(
            '{"kind":"ctestInfo","version":{"major":1,"minor":0},'
            '"tests":[{"name":"v2-contract-tests"}]}',
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                "python3",
                str(INVENTORY_HELPER),
                "--inventory",
                str(inventory),
                "--expected",
                "v2-contract-tests",
                "v2-replication-tests",
            ],
            text=True,
            capture_output=True,
        )
        self.assertEqual(1, result.returncode)
        self.assertIn("missing required CTest suites: v2-replication-tests", result.stderr)

    def run_inventory(self, tests, expected):
        inventory = self.root / "inventory.json"
        import json
        inventory.write_text(json.dumps({"tests": tests}), encoding="utf-8")
        return subprocess.run(
            ["python3", str(INVENTORY_HELPER), "--inventory", str(inventory),
             "--expected", *expected],
            text=True,
            capture_output=True,
        )

    def test_ctest_inventory_accepts_exact_runnable_suites(self):
        result = self.run_inventory(
            [{"name": "v2-contract-tests"}, {"name": "v2-replication-tests"}],
            ["v2-contract-tests", "v2-replication-tests"],
        )
        self.assertEqual(0, result.returncode, result.stderr)

    def test_ctest_inventory_rejects_unexpected_and_duplicate_suites(self):
        unexpected = self.run_inventory(
            [{"name": "required"}, {"name": "surprise"}], ["required"]
        )
        self.assertIn("unexpected CTest suites: surprise", unexpected.stderr)
        duplicate = self.run_inventory(
            [{"name": "required"}, {"name": "required"}], ["required"]
        )
        self.assertIn("duplicate suite names", duplicate.stderr)

    def test_ctest_inventory_rejects_disabled_or_skippable_required_suite(self):
        for property_name, value in (
            ("DISABLED", True),
            ("SKIP_RETURN_CODE", 77),
            ("SKIP_REGULAR_EXPRESSION", ["unsupported"]),
        ):
            with self.subTest(property_name=property_name):
                result = self.run_inventory(
                    [{"name": "required", "properties": [
                        {"name": property_name, "value": value}
                    ]}],
                    ["required"],
                )
                self.assertEqual(1, result.returncode)
                self.assertIn(f"required suite is not unconditionally runnable: required ({property_name})",
                              result.stderr)


if __name__ == "__main__":
    unittest.main()
