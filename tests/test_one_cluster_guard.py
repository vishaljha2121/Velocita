"""The start guard that keeps a second cluster's voter from starting on a host."""
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "deploy/v2/one-cluster-guard.sh"


class OneClusterGuard(unittest.TestCase):
    def run_guard(self, name, running):
        with tempfile.TemporaryDirectory() as folder:
            podman = pathlib.Path(folder) / "podman"
            podman.write_text("#!/bin/sh\ncat <<'END'\n" + "".join(n + "\n" for n in running) + "END\n")
            podman.chmod(podman.stat().st_mode | stat.S_IXUSR)
            env = {"PATH": f"{folder}:{os.environ['PATH']}"}
            return subprocess.run(["/bin/sh", str(SCRIPT), name], env=env, capture_output=True, text=True)

    def test_starts_when_no_other_voter_runs(self):
        for running in ([], ["velocita-v2-grafana", "velocita-paper-stochastic-exporter", "velocita-v2-node-exporter"]):
            self.assertEqual(self.run_guard("velocita-paper-stochastic-node", running).returncode, 0, running)

    def test_its_own_container_name_is_not_another_cluster(self):
        self.assertEqual(self.run_guard("velocita-v2-node-1", ["velocita-v2-node-1"]).returncode, 0)

    def test_refuses_with_64_when_another_clusters_voter_runs(self):
        cases = [("velocita-v2-node-1", "velocita-paper-stochastic-node"),
                 ("velocita-paper-stochastic-node", "velocita-v2-node-1"),
                 ("velocita-paper-sma-node", "velocita-paper-stochastic-node")]
        for starting, running in cases:
            result = self.run_guard(starting, ["velocita-v2-prometheus", running])
            self.assertEqual(result.returncode, 64, (starting, running))
            self.assertIn(running, result.stderr)

    def test_wrong_arguments_are_a_usage_error(self):
        self.assertEqual(subprocess.run(["/bin/sh", str(SCRIPT)], capture_output=True).returncode, 64)


if __name__ == "__main__":
    unittest.main()
