import importlib.util
import json
import os
import pathlib
import stat
import subprocess
import tempfile
import unittest
import base64
import contextlib
import io
from unittest import mock


ROOT = pathlib.Path(__file__).parents[1]


def load_tool(name):
    path = ROOT / "tools" / name
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MINT = load_tool("mint-token.py")
PROBE = load_tool("probe-market-data.py")
TOKEN = "aaa." + base64.urlsafe_b64encode(b'{"exp":9999999999}').decode().rstrip("=") + ".ccc"


class TokenToolTests(unittest.TestCase):
    def test_mint_replaces_existing_file_with_private_mode(self):
        with tempfile.TemporaryDirectory() as d:
            key = pathlib.Path(d) / "key.json"
            out = pathlib.Path(d) / "token"
            key.write_text(json.dumps({"key_id": "kid", "private_key": {"d": "AA", "x": "AA", "y": "AA"}}))
            out.write_text("old")
            out.chmod(0o644)
            response = mock.Mock(stdout=(json.dumps({"access_token": TOKEN}) + "\n200").encode(), returncode=0)
            with mock.patch.object(MINT, "jwk_to_pem", return_value=b"pem"), mock.patch.object(MINT, "der_signature_to_raw", return_value=b"s"), mock.patch.object(MINT.subprocess, "run", return_value=response):
                with mock.patch.object(MINT.sys, "argv", ["mint-token.py", str(key), str(out)]):
                    self.assertEqual(MINT.main(), 0)
            self.assertEqual(out.read_text(), TOKEN)
            self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o600)

    def test_mint_refuses_symlink_destination_and_parent(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            key = base / "key.json"
            key.write_text("{}")
            real = base / "real"
            real.mkdir(mode=0o700)
            target = real / "token"
            target.write_text("old")
            link = base / "link"
            link.symlink_to(target)
            with self.assertRaises((OSError, RuntimeError)):
                MINT._atomic_write_token(link, TOKEN)
            parent = base / "parent-link"
            parent.symlink_to(real, target_is_directory=True)
            with self.assertRaises((OSError, RuntimeError)):
                MINT._atomic_write_token(parent / "token", TOKEN)
            self.assertEqual(target.read_text(), "old")

    def test_mint_failure_preserves_old_token_and_redacts_body(self):
        with tempfile.TemporaryDirectory() as d:
            key = pathlib.Path(d) / "key.json"
            out = pathlib.Path(d) / "token"
            key.write_text("{}")
            out.write_text("old")
            result = mock.Mock(stdout=b'{"access_token":"SECRET"}\n500', returncode=0)
            key.write_text(json.dumps({"key_id": "kid", "private_key": {"d": "AA", "x": "AA", "y": "AA"}}))
            with mock.patch.object(MINT, "der_signature_to_raw", return_value=b"s"), mock.patch.object(MINT.subprocess, "run", return_value=result), mock.patch.object(MINT.sys, "stderr", new_callable=mock.Mock) as err:
                self.assertNotEqual(MINT.mint_token(str(key), str(out)), 0)
                self.assertEqual(out.read_text(), "old")
                self.assertNotIn("SECRET", str(err.write.call_args_list))

    def test_mint_rejects_noncanonical_origin(self):
        with mock.patch.dict(MINT.os.environ, {"TM_BASE": "http://evil.example"}):
            self.assertNotEqual(MINT.validate_auth_origin(), 0)

    def test_mint_rejects_malformed_claims_preserving_old(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            key, out = base / "key.json", base / "token"
            key.write_text(json.dumps({"key_id": "kid", "private_key": {"d": "AA", "x": "AA", "y": "AA"}}))
            out.write_text("old")
            bad = "aaa." + base64.urlsafe_b64encode(b'[]').decode().rstrip("=") + ".ccc"
            response = mock.Mock(stdout=(json.dumps({"access_token": bad}) + "\n200").encode(), returncode=0)
            with mock.patch.object(MINT, "jwk_to_pem", return_value=b"pem"), mock.patch.object(MINT, "der_signature_to_raw", return_value=b"s"), mock.patch.object(MINT.subprocess, "run", return_value=response):
                self.assertNotEqual(MINT.mint_token(str(key), str(out)), 0)
            self.assertEqual(out.read_text(), "old")

    def test_mint_rejects_trailing_newline_token(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            key, out = base / "key.json", base / "token"
            key.write_text(json.dumps({"key_id": "kid", "private_key": {"d": "AA", "x": "AA", "y": "AA"}}))
            out.write_text("old")
            response = mock.Mock(stdout=(json.dumps({"access_token": TOKEN + "\n"}) + "\n200").encode(), returncode=0)
            with mock.patch.object(MINT, "jwk_to_pem", return_value=b"pem"), mock.patch.object(MINT, "der_signature_to_raw", return_value=b"s"), mock.patch.object(MINT.subprocess, "run", return_value=response):
                self.assertNotEqual(MINT.mint_token(str(key), str(out)), 0)
            self.assertEqual(out.read_text(), "old")

    def test_mint_rejects_non_string_access_token(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            key, out = base / "key.json", base / "token"
            key.write_text(json.dumps({"key_id": "kid", "private_key": {"d": "AA", "x": "AA", "y": "AA"}}))
            out.write_text("old")
            response = mock.Mock(stdout=b'{"access_token": {"bad": true}}\n200', returncode=0)
            with mock.patch.object(MINT, "jwk_to_pem", return_value=b"pem"), mock.patch.object(MINT, "der_signature_to_raw", return_value=b"s"), mock.patch.object(MINT.subprocess, "run", return_value=response):
                self.assertNotEqual(MINT.mint_token(str(key), str(out)), 0)
            self.assertEqual(out.read_text(), "old")

    def test_probe_does_not_put_bearer_in_argv_and_validates_token(self):
        seen = {}
        def fake_run(args, **kwargs):
            seen["args"] = args
            seen["input"] = kwargs.get("input", b"")
            return mock.Mock(stdout=b'{}\n200', returncode=0)
        with mock.patch.object(PROBE.subprocess, "run", side_effect=fake_run):
            PROBE.get("/v1/prices", "aaa.bbb.ccc")
        self.assertNotIn("aaa.bbb.ccc", " ".join(seen["args"]))
        self.assertIn(b"aaa.bbb.ccc", seen["input"])
        self.assertIn("--disable", seen["args"])

    def test_probe_rejects_header_injection_before_subprocess(self):
        with mock.patch.object(PROBE.subprocess, "run", return_value=mock.Mock(stdout=b'{}\n200')) as run:
            for token in (TOKEN + "\n", TOKEN + "\r", 'aaa.bbb.ccc"\nurl="https://bad"'):
                with self.subTest(token=repr(token)), self.assertRaises(SystemExit):
                    PROBE.get("/v1/prices", token)
            for path in ("/v1/orders", "//bad.example/", "/v1/prices?redirect=bad"):
                with self.subTest(path=path), self.assertRaises(SystemExit):
                    PROBE.get(path, TOKEN)
            run.assert_not_called()

    def test_invalid_response_never_replaces_old_token_or_leaks_body(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            key, out = base / "key.json", base / "token"
            key.write_text('{"key_id":"dummy","private_key":{}}')
            bad_claims = (b'[]', b'{"exp":NaN}', b'{"exp":1e999}',
                          b'{"exp":true}', b'{"exp":99999999999999999999999999}')
            bodies = [b'[]\n200', b'null\n200', b'{broken\n200', b'\xff\n200',
                      b'SECRET_ECHO', b'{"access_token":2}\n200']
            for claims in bad_claims:
                token = "aaa." + base64.urlsafe_b64encode(claims).decode().rstrip("=") + ".ccc"
                bodies.append((json.dumps({"access_token": token}) + "\n200").encode())
            for body in bodies:
                out.write_text("old")
                captured = io.StringIO()
                with self.subTest(body=body), \
                     mock.patch.object(MINT, "jwk_to_pem", return_value=b"dummy"), \
                     mock.patch.object(MINT, "der_signature_to_raw", return_value=b"dummy"), \
                     mock.patch.object(MINT.subprocess, "run", return_value=mock.Mock(stdout=body)), \
                     contextlib.redirect_stderr(captured):
                    self.assertNotEqual(MINT.mint_token(str(key), str(out)), 0)
                self.assertEqual(out.read_text(), "old")
                self.assertNotIn("SECRET_ECHO", captured.getvalue())

    def test_write_failures_preserve_old_token_and_cleanup(self):
        with tempfile.TemporaryDirectory() as d:
            out = pathlib.Path(d) / "token"
            out.write_text("old")
            for operation in ("fchmod", "fdopen", "fsync", "rename"):
                with self.subTest(operation=operation), \
                     mock.patch.object(MINT.os, operation, side_effect=OSError("injected")), \
                     self.assertRaises(OSError):
                    MINT._atomic_write_token(out, TOKEN)
                self.assertEqual(out.read_text(), "old")
                self.assertEqual(list(out.parent.glob(".token-*")), [])

    def test_private_parent_and_relative_path_policy(self):
        with tempfile.TemporaryDirectory() as d:
            base = pathlib.Path(d)
            public = base / "public"
            public.mkdir(mode=0o755)
            with self.assertRaises(RuntimeError):
                MINT._atomic_write_token(public / "token", TOKEN)
            previous = os.getcwd()
            try:
                os.chdir(base)
                MINT._atomic_write_token(pathlib.Path("token"), TOKEN)
                self.assertEqual((base / "token").read_text(), TOKEN)
                with self.assertRaises(RuntimeError):
                    MINT._atomic_write_token(pathlib.Path("public/../token"), TOKEN)
            finally:
                os.chdir(previous)

    def test_partial_write_failure_keeps_old_token_and_private_temp(self):
        with tempfile.TemporaryDirectory() as d:
            out = pathlib.Path(d) / "token"
            out.write_text("old")
            real_fdopen = os.fdopen
            observed_modes = []

            def partial(fd, mode):
                observed_modes.append(stat.S_IMODE(os.fstat(fd).st_mode))
                handle = real_fdopen(fd, mode)
                class PartialWriter:
                    def __enter__(self):
                        return self
                    def __exit__(self, *exc):
                        handle.close()
                    def write(self, data):
                        handle.write(data[:3])
                        handle.flush()
                        raise OSError("injected partial write")
                return PartialWriter()

            with mock.patch.object(MINT.os, "fdopen", side_effect=partial), \
                 self.assertRaises(OSError):
                MINT._atomic_write_token(out, TOKEN)
            self.assertEqual(observed_modes, [0o600])
            self.assertEqual(out.read_text(), "old")
            self.assertEqual(list(out.parent.glob(".token-*")), [])

    def test_auth_origin_is_checked_before_key_or_transport_access(self):
        for origin in ("http://api.truemarkets.co", "https://evil.example",
                       "https://api.truemarkets.co@evil.example", "https://[",
                       "https://api.truemarkets.co/path"):
            with self.subTest(origin=origin), \
                 mock.patch.dict(MINT.os.environ, {"TM_BASE": origin}), \
                 mock.patch.object(MINT.pathlib.Path, "read_text") as read, \
                 mock.patch.object(MINT.subprocess, "run") as run:
                self.assertNotEqual(MINT.mint_token("missing", "missing"), 0)
                read.assert_not_called()
                run.assert_not_called()

    def test_auth_transport_ignores_config_and_keeps_secrets_off_argv(self):
        with tempfile.TemporaryDirectory() as d:
            key, out = pathlib.Path(d) / "key", pathlib.Path(d) / "token"
            key.write_text('{"key_id":"DUMMY_KEY_ID","private_key":{}}')
            response = mock.Mock(stdout=(json.dumps({"access_token": TOKEN}) + "\n200").encode())
            with mock.patch.object(MINT, "jwk_to_pem", return_value=b"DUMMY_PRIVATE_KEY"), \
                 mock.patch.object(MINT, "der_signature_to_raw", return_value=b"DUMMY_SIGNATURE"), \
                 mock.patch.object(MINT.subprocess, "run", return_value=response) as run:
                self.assertEqual(MINT.mint_token(str(key), str(out)), 0)
            args = run.call_args.args[0]
            self.assertEqual(args[:2], ["curl", "--disable"])
            self.assertNotIn("--location", args)
            self.assertNotIn("-L", args)
            self.assertNotIn("DUMMY_KEY_ID", " ".join(args))
            self.assertIn(b"DUMMY_KEY_ID", run.call_args.kwargs["input"])
            self.assertNotIn(TOKEN, " ".join(args))


if __name__ == "__main__":
    unittest.main()
