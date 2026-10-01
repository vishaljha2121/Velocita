"""tools/md-backup-pull: the Mac copies nuc1's market-data archives, verifies
each against its sidecar, and only then writes the acknowledgement that lets
nuc1 delete the rows.

The fake ssh runs each remote command with /bin/sh in a scratch "home", so the
exact shell text the tool sends is exercised. Archive contents are random
bytes; every expected sha256 is computed here with hashlib.
"""
import contextlib
import hashlib
import importlib.machinery
import importlib.util
import io
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_tool():
    # Mutation runs point this at a scratch copy; normal runs use the real tool.
    path = pathlib.Path(os.environ.get("VELOCITA_MD_BACKUP_TOOL", ROOT / "tools" / "md-backup-pull"))
    loader = importlib.machinery.SourceFileLoader("md_backup_pull", str(path))
    spec = importlib.util.spec_from_loader("md_backup_pull", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


TOOL = load_tool()
DAYS = ("2026-09-28", "2026-09-29", "2026-09-30")


def name(day):
    return f"stream-{day}.sqlite.gz"


class FakeSsh:
    """subprocess.run stand-in for `ssh HOST COMMAND`."""

    def __init__(self, home):
        self.home = home
        self.calls = []
        self.offline = False
        self.corrupt_in_transit = set()
        self.fail_ack = False

    def __call__(self, argv, input=None, stdout=None, stderr=None, **_):
        self.calls.append(list(argv))
        assert argv[0] == "ssh", argv
        command = argv[-1]
        if self.offline:
            return subprocess.CompletedProcess(argv, 255, b"", b"ssh: connect to host nuc1 port 22: timed out")
        if self.fail_ack and "acked/" in command and " mv " in command:
            return subprocess.CompletedProcess(argv, 1, b"", b"No space left on device")
        proc = subprocess.run(["/bin/sh", "-c", command], cwd=self.home, input=input,
                              env={"HOME": str(self.home), "PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out = proc.stdout
        if command.startswith("cat ") and any(n in command for n in self.corrupt_in_transit):
            out = out[:100] + bytes([out[100] ^ 0x40]) + out[101:]
        if stdout is not None and hasattr(stdout, "write"):
            stdout.write(out)
            out = None
        return subprocess.CompletedProcess(argv, proc.returncode, out, proc.stderr)

    def commands(self):
        return [argv[-1] for argv in self.calls]


class Fixture(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        root = pathlib.Path(self._dir.name)
        self.home, self.local = root / "nuc1-home", root / "mac-backups"
        self.archive = self.home / "velocita-md" / "archive"
        (self.archive / "acked").mkdir(parents=True)
        self.content, self.sha = {}, {}
        for i, day in enumerate(DAYS):
            data = os.urandom(5000 + i)
            self.content[day], self.sha[day] = data, hashlib.sha256(data).hexdigest()
            (self.archive / name(day)).write_bytes(data)
            (self.archive / f"{name(day)}.sha256").write_text(f"{self.sha[day]}  {name(day)}\n")
        self.ssh = FakeSsh(self.home)

    def tearDown(self):
        self._dir.cleanup()

    def run_tool(self, *extra):
        out = io.StringIO()
        code = TOOL.main(["--host", "velocitas@nuc1", "--local-dir", str(self.local), "--attempts", "1", *extra],
                         run=self.ssh, sleep=lambda _s: None, out=out)
        return code, out.getvalue()

    def remote_ack(self, day):
        path = self.archive / "acked" / f"stream-{day}.ack"
        return path.read_text() if path.exists() else None

    def expected_ack(self, day):
        return f"{self.sha[day]}  {name(day)}\n"


class Pull(Fixture):
    def test_pulls_verifies_stores_and_acks_every_new_archive(self):
        code, out = self.run_tool()
        self.assertEqual(code, 0, out)
        for day in DAYS:
            self.assertEqual((self.local / name(day)).read_bytes(), self.content[day])
            self.assertEqual((self.local / f"{name(day)}.sha256").read_text(), self.expected_ack(day))
            self.assertEqual(self.remote_ack(day), self.expected_ack(day))
        for argv in self.ssh.calls:
            self.assertEqual(argv[0], "ssh")
            self.assertIn("BatchMode=yes", argv)
            self.assertEqual(argv[-2], "velocitas@nuc1")
            self.assertNotIn("rsync", " ".join(argv))

    def test_ack_write_leaves_no_temporary_file(self):
        self.run_tool()
        self.assertEqual(sorted(p.name for p in (self.archive / "acked").iterdir()),
                         [f"stream-{d}.ack" for d in DAYS])
        self.assertEqual(sorted(p.name for p in self.local.iterdir() if not p.name.startswith(".lock")),
                         sorted([name(d) for d in DAYS] + [f"{name(d)}.sha256" for d in DAYS]))

    def test_second_run_downloads_nothing(self):
        self.run_tool()
        self.ssh.calls.clear()
        code, _ = self.run_tool()
        self.assertEqual(code, 0)
        self.assertEqual([c for c in self.ssh.commands() if c.startswith("cat ")], [])
        self.assertEqual(len(self.ssh.calls), 1, "only the listing")


class NeverAckUnverified(Fixture):
    def test_corrupted_in_transit_is_never_acked_or_kept(self):
        self.ssh.corrupt_in_transit.add(name("2026-09-29"))
        code, out = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIsNone(self.remote_ack("2026-09-29"))
        self.assertFalse((self.local / name("2026-09-29")).exists())
        self.assertFalse((self.local / f"{name('2026-09-29')}.sha256").exists())
        self.assertEqual([p.name for p in self.local.glob(".part*")], [])
        self.assertIn("2026-09-29", out)
        self.assertEqual(self.remote_ack("2026-09-28"), self.expected_ack("2026-09-28"))
        self.assertEqual(self.remote_ack("2026-09-30"), self.expected_ack("2026-09-30"))

    def test_archive_corrupted_on_nuc1_is_never_acked(self):
        data = bytearray(self.content["2026-09-28"])
        data[7] ^= 0x01
        (self.archive / name("2026-09-28")).write_bytes(bytes(data))
        code, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIsNone(self.remote_ack("2026-09-28"))
        self.assertFalse((self.local / name("2026-09-28")).exists())

    def test_conflicting_local_copy_is_neither_overwritten_nor_acked(self):
        self.local.mkdir()
        (self.local / name("2026-09-30")).write_bytes(b"an older, different archive")
        code, out = self.run_tool()
        self.assertEqual(code, 1)
        self.assertEqual((self.local / name("2026-09-30")).read_bytes(), b"an older, different archive")
        self.assertIsNone(self.remote_ack("2026-09-30"))
        self.assertIn("differs", out)

    def test_failed_ack_is_reported_and_retried_next_run(self):
        self.ssh.fail_ack = True
        code, _ = self.run_tool()
        self.assertEqual(code, 1)
        self.assertIsNone(self.remote_ack("2026-09-28"))
        self.assertEqual((self.local / name("2026-09-28")).read_bytes(), self.content["2026-09-28"])
        self.ssh.fail_ack = False
        self.ssh.calls.clear()
        self.assertEqual(self.run_tool()[0], 0)
        self.assertEqual(self.remote_ack("2026-09-28"), self.expected_ack("2026-09-28"))
        self.assertEqual([c for c in self.ssh.commands() if c.startswith("cat ")], [], "verified copies are reused")

    def test_listing_ignores_names_it_did_not_expect(self):
        evil = self.archive / "stream-2026-10-01.sqlite.gz.sha256"
        evil.write_text(f"{'a' * 64}  ../../.ssh/id_ed25519\n")
        (self.archive / "stream-x;touch PWNED.sqlite.gz.sha256").write_text(f"{'b' * 64}  x\n")
        code, _ = self.run_tool()
        self.assertEqual(code, 0)
        joined = "\n".join(self.ssh.commands())
        self.assertNotIn("id_ed25519", joined)
        self.assertNotIn("PWNED", joined)
        self.assertFalse((self.home / "PWNED").exists())
        self.assertIsNone(self.remote_ack("2026-10-01"))


class CatchUp(Fixture):
    def test_offline_run_acks_nothing_and_the_next_run_catches_up(self):
        self.ssh.offline = True
        code, out = self.run_tool()
        self.assertEqual(code, 2)
        self.assertIn("timed out", out)
        self.assertEqual([p.name for p in self.local.glob("stream-*")] if self.local.exists() else [], [])
        self.ssh.offline = False
        self.assertEqual(self.run_tool()[0], 0)
        for day in DAYS:
            self.assertEqual(self.remote_ack(day), self.expected_ack(day))

    def test_listing_is_retried_before_giving_up(self):
        self.ssh.offline = True
        waits = []
        code = TOOL.main(["--local-dir", str(self.local), "--attempts", "3", "--retry-wait", "30"],
                         run=self.ssh, sleep=waits.append, out=io.StringIO())
        self.assertEqual(code, 2)
        self.assertEqual((len(self.ssh.calls), waits), (3, [30, 30]))

    def test_a_lost_local_copy_is_fetched_again(self):
        self.run_tool()
        (self.local / name("2026-09-28")).unlink()
        self.assertEqual(self.run_tool()[0], 0)
        self.assertEqual((self.local / name("2026-09-28")).read_bytes(), self.content["2026-09-28"])

    def test_an_existing_matching_local_copy_is_acked_without_download(self):
        self.local.mkdir()
        (self.local / name("2026-09-29")).write_bytes(self.content["2026-09-29"])
        self.run_tool()
        self.assertNotIn(f"cat velocita-md/archive/{name('2026-09-29')}", self.ssh.commands())
        self.assertEqual(self.remote_ack("2026-09-29"), self.expected_ack("2026-09-29"))

    def test_an_archive_already_removed_from_nuc1_is_left_alone(self):
        self.run_tool()
        for suffix in ("", ".sha256"):
            (self.archive / f"{name('2026-09-28')}{suffix}").unlink()
        self.ssh.calls.clear()
        self.assertEqual(self.run_tool()[0], 0)
        self.assertTrue((self.local / name("2026-09-28")).exists())
        self.assertEqual(len(self.ssh.calls), 1)


class RemoteDirectory(unittest.TestCase):
    def test_remote_dir_must_be_a_plain_relative_path(self):
        for bad in ("velocita-md/archive; rm -rf ~", "/abs/path", "../up", "a b"):
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                TOOL.main(["--remote-dir", bad, "--local-dir", "/nonexistent"], run=None, sleep=None,
                          out=io.StringIO())


if __name__ == "__main__":
    unittest.main()
