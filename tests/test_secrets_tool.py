import importlib.machinery
import importlib.util
import io
import os
import pathlib
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).parents[1]


def load_tool():
    path = pathlib.Path(os.environ.get("VELOCITA_SECRETS_TOOL", ROOT / "tools" / "velocita-secrets"))
    loader = importlib.machinery.SourceFileLoader("velocita_secrets", str(path))
    spec = importlib.util.spec_from_loader("velocita_secrets", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


TOOL = load_tool()


class FakeKeychain:
    def __init__(self):
        self.items = {}

    def read(self, account):
        return self.items.get(account)

    def write(self, account, raw):
        assert len(raw) == 32
        self.items[account] = raw

    def delete(self, account):
        del self.items[account]


class FakeRemote:
    def __init__(self):
        self.files = {}
        self.reloads = []

    def write(self, host, path, raw):
        self.files[(host, path)] = raw

    def remove(self, host, path):
        self.files.pop((host, path), None)

    def reload(self, host, command):
        self.reloads.append((host, command))


class RecordingRun:
    def __init__(self, stdout="", returncode=0):
        self.calls = []
        self.stdout = stdout
        self.returncode = returncode

    def __call__(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        return mock.Mock(returncode=self.returncode, stdout=self.stdout, stderr=b"")


class TtyOut:
    def __init__(self):
        self.buffer = io.BytesIO()

    def isatty(self):
        return True


class PipeOut:
    def __init__(self):
        self.buffer = io.BytesIO()

    def isatty(self):
        return False


class KeychainTransportTests(unittest.TestCase):
    def test_write_sends_value_on_stdin_never_in_argv(self):
        run = RecordingRun()
        TOOL.Keychain(run).write("link-1-2", bytes(range(32)))
        argv, kwargs = run.calls[0]
        self.assertEqual(argv, ["security", "-i"])
        self.assertIn(bytes(range(32)).hex(), kwargs["input"])
        self.assertNotIn(bytes(range(32)).hex(), " ".join(argv))

    def test_read_rejects_wrong_size_and_non_hex(self):
        with self.assertRaises(TOOL.ToolError):
            TOOL.Keychain(RecordingRun(stdout="00" * 31)).read("x")
        with self.assertRaises(TOOL.ToolError):
            TOOL.Keychain(RecordingRun(stdout="zz" * 32)).read("x")
        self.assertIsNone(TOOL.Keychain(RecordingRun(returncode=44)).read("x"))


class RemoteTransportTests(unittest.TestCase):
    def test_push_sends_bytes_on_stdin_and_checks_tmpfs(self):
        run = RecordingRun()
        TOOL.Remote(run).write("velocita@nuc", "/run/velocita/secrets/link.key", bytes(range(32)))
        argv, kwargs = run.calls[0]
        self.assertEqual(kwargs["input"], bytes(range(32)))
        joined = " ".join(argv)
        self.assertNotIn(bytes(range(32)).hex(), joined)
        self.assertIn("tmpfs", joined)
        self.assertIn("mode 700", joined)
        self.assertIn("mv -f", joined)
        self.assertIn("umask 077", joined)

    def test_remote_refusal_is_an_error(self):
        with self.assertRaises(TOOL.ToolError):
            TOOL.Remote(RecordingRun(returncode=3)).write("h", "/run/x", bytes(32))


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.keychain = FakeKeychain()
        self.remote = FakeRemote()

    def run_tool(self, *argv, out=None):
        return TOOL.main(list(argv), keychain=self.keychain, remote=self.remote, out=out or PipeOut())

    def test_init_refuses_to_overwrite(self):
        self.assertEqual(self.run_tool("init", "link-1-2"), 0)
        first = self.keychain.items["link-1-2"]
        self.assertEqual(self.run_tool("init", "link-1-2"), 1)
        self.assertEqual(self.keychain.items["link-1-2"], first)

    def test_full_rotation_cycle_keeps_hosts_in_sync(self):
        self.run_tool("init", "link")
        old = self.keychain.items["link"]
        path = "/run/velocita/secrets/link.key"

        self.assertEqual(self.run_tool("rotate-begin", "link"), 0)
        new = self.keychain.items["link.next"]
        self.assertNotEqual(new, old)
        self.run_tool("push", "link", "nuc", path)
        self.assertEqual(self.remote.files[("nuc", path)], old)
        self.assertEqual(self.remote.files[("nuc", path + ".next")], new)

        self.assertEqual(self.run_tool("rotate-promote", "link"), 0)
        self.assertEqual(self.keychain.items["link"], new)
        self.assertEqual(self.keychain.items["link.previous"], old)
        self.assertNotIn("link.next", self.keychain.items)
        self.run_tool("push", "link", "nuc", path)
        self.assertEqual(self.remote.files[("nuc", path)], new)
        self.assertEqual(self.remote.files[("nuc", path + ".previous")], old)
        self.assertNotIn(("nuc", path + ".next"), self.remote.files, "stale .next left on the host")

        self.assertEqual(self.run_tool("rotate-finish", "link"), 0)
        self.run_tool("push", "link", "nuc", path)
        self.assertEqual(self.remote.files, {("nuc", path): new})

    def test_rotation_steps_refuse_out_of_order(self):
        self.run_tool("init", "link")
        self.assertEqual(self.run_tool("rotate-promote", "link"), 1)
        self.assertEqual(self.run_tool("rotate-finish", "link"), 1)
        self.run_tool("rotate-begin", "link")
        self.assertEqual(self.run_tool("rotate-begin", "link"), 1)
        self.assertEqual(self.run_tool("rotate-finish", "link"), 1)
        self.run_tool("rotate-promote", "link")
        self.assertEqual(self.run_tool("rotate-begin", "link"), 1, "began a rotation before finishing the last")

    def test_client_key_refuses_terminal_and_writes_raw_bytes(self):
        self.run_tool("init", "caller-9")
        terminal = TtyOut()
        self.assertEqual(self.run_tool("client-key", "caller-9", out=terminal), 1)
        self.assertEqual(terminal.buffer.getvalue(), b"", "raw secret bytes written to a terminal")
        out = PipeOut()
        self.assertEqual(self.run_tool("client-key", "caller-9", out=out), 0)
        self.assertEqual(out.buffer.getvalue(), self.keychain.items["caller-9"])
        self.run_tool("rotate-begin", "caller-9")
        self.run_tool("rotate-promote", "caller-9")
        out = PipeOut()
        self.run_tool("client-key", "caller-9", out=out)
        self.assertEqual(out.buffer.getvalue(),
                         self.keychain.items["caller-9"] + self.keychain.items["caller-9.previous"])

    def test_no_secret_value_is_printed(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch("sys.stdout", stdout), mock.patch("sys.stderr", stderr):
            self.run_tool("init", "link")
            self.run_tool("rotate-begin", "link")
            self.run_tool("push", "link", "nuc", "/run/x")
        printed = stdout.getvalue() + stderr.getvalue()
        for value in self.keychain.items.values():
            self.assertNotIn(value.hex(), printed)


if __name__ == "__main__":
    unittest.main()
