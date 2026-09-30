#!/usr/bin/env python3
"""Exchange the True Markets API key for a short-lived token. ADR 0010.

Run by a PERSON. Nothing in this repository holds key material, and nothing
automated may call this - a component that can re-mint its own token is a
component that holds the key.

Reads the key JSON from a path, writes ONLY the token to the
destination file with 0600, and prints nothing sensitive.

    ./tools/mint-token.py ~/secure/tm-key.json /run/velocita/tm.token

The key JSON is the venue's own download:
    {"key_id": "...", "private_key": {"kty":"EC","crv":"P-256","x":..,"y":..,"d":..},
     "algorithm": "ES256"}
"""
from __future__ import annotations

import base64
import json
import os
import pathlib
import subprocess
import stat
import secrets
import re
import sys
import tempfile
import time

BASE = os.environ.get("TM_BASE", "https://api.truemarkets.co")
CANONICAL_ORIGIN = "https://api.truemarkets.co"
# Cloudflare rejects clients that announce a non-browser user-agent with
# `403 error code: 1010`, before the request reaches the API. See D-10: the
# block is on the user-agent, not the TLS fingerprint.
UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/140.0 Safari/537.36")


def b64url_decode(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def der_len(n: int) -> bytes:
    if n < 0x80:
        return bytes([n])
    body = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(body)]) + body


def tlv(tag: int, body: bytes) -> bytes:
    return bytes([tag]) + der_len(len(body)) + body


def jwk_to_pem(jwk: dict) -> bytes:
    """SEC1 EC private key, so no third-party crypto library is needed."""
    d, x, y = (b64url_decode(jwk[k]).rjust(32, b"\0") for k in ("d", "x", "y"))
    body = (tlv(0x02, b"\x01") + tlv(0x04, d)
            + tlv(0xA0, bytes.fromhex("06082A8648CE3D030107"))
            + tlv(0xA1, tlv(0x03, b"\x00\x04" + x + y)))
    blob = base64.encodebytes(tlv(0x30, body)).decode().replace("\n", "")
    lines = "\n".join(blob[i:i + 64] for i in range(0, len(blob), 64))
    return f"-----BEGIN EC PRIVATE KEY-----\n{lines}\n-----END EC PRIVATE KEY-----\n".encode()


def der_signature_to_raw(der: bytes) -> bytes:
    """ES256 wants raw r||s. `openssl dgst -sign` emits DER, and sending DER is
    rejected - this conversion is not optional."""
    index = 2 if der[1] < 0x80 else 2 + (der[1] & 0x7F)
    parts = []
    for _ in range(2):
        length = der[index + 1]
        parts.append(der[index + 2:index + 2 + length].lstrip(b"\0").rjust(32, b"\0"))
        index += 2 + length
    return parts[0] + parts[1]


def validate_auth_origin() -> int:
    return 0 if os.environ.get("TM_BASE", BASE) in (
        CANONICAL_ORIGIN, CANONICAL_ORIGIN + "/") else 1


def _open_private_parent(path: pathlib.Path) -> tuple[int, pathlib.Path]:
    if ".." in path.parts:
        raise RuntimeError("token path must not contain parent traversal")
    path = pathlib.Path(os.path.abspath(path))
    text = str(path)
    if sys.platform == "darwin":
        for alias in ("/var", "/tmp"):
            if text.startswith(alias + "/"):
                info = os.lstat(alias)
                if (info.st_uid == 0 and stat.S_ISLNK(info.st_mode) and
                        os.readlink(alias) in ("private" + alias, "/private" + alias)):
                    path = pathlib.Path("/private" + text)
    parts = [part for part in path.parent.parts if part not in ("/", "")]
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in parts:
            try:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW |
                                os.O_CLOEXEC, dir_fd=fd)
            except FileNotFoundError:
                os.mkdir(part, 0o700, dir_fd=fd)
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW |
                                os.O_CLOEXEC, dir_fd=fd)
            os.close(fd)
            fd = child
            child_info = os.fstat(fd)
            if child_info.st_uid not in (0, os.getuid()):
                raise RuntimeError("token parent has unexpected ownership")
            if child_info.st_mode & 0o022 and not (child_info.st_uid == 0 and child_info.st_mode & stat.S_ISVTX):
                raise RuntimeError("token parent is writable by another user")
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise RuntimeError("token parent is not private to the current user")
        try:
            destination = os.stat(path.name, dir_fd=fd, follow_symlinks=False)
            if stat.S_ISLNK(destination.st_mode) or not stat.S_ISREG(destination.st_mode):
                raise RuntimeError("token destination is not a regular file")
        except FileNotFoundError:
            pass
        return fd, path
    except Exception:
        os.close(fd)
        raise


def _atomic_write_token(token_path: pathlib.Path, token: str) -> None:
    parent_fd, normalized = _open_private_parent(token_path)
    name = normalized.name
    try:
        try:
            info = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
                raise RuntimeError("token destination is not a regular file")
        except FileNotFoundError:
            pass
        temp_name = f".token-{os.getpid()}-{secrets.token_hex(12)}"
        fd = os.open(temp_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                     os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=parent_fd)
    except Exception:
        os.close(parent_fd)
        raise
    try:
        os.fchmod(fd, 0o600)
        handle = os.fdopen(fd, "w")
        fd = -1
        with handle:
            handle.write(token)
            handle.flush()
            os.fsync(handle.fileno())
        os.rename(temp_name, name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
    except Exception:
        try:
            os.unlink(temp_name, dir_fd=parent_fd)
        except FileNotFoundError:
            pass
        raise
    finally:
        if fd >= 0:
            os.close(fd)
        os.close(parent_fd)


def _validate_token(token: str) -> None:
    parts = token.split(".")
    if len(parts) != 3 or any(not part or any(ch not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-" for ch in part) for part in parts):
        raise ValueError("unsafe token")


def mint_token(key_name: str, token_name: str) -> int:
    if validate_auth_origin() != 0:
        print("refusing non-canonical HTTPS auth origin", file=sys.stderr)
        return 2
    key_path, token_path = pathlib.Path(key_name), pathlib.Path(token_name)
    try:
        parent_fd, _ = _open_private_parent(token_path)
        os.close(parent_fd)
    except (OSError, RuntimeError) as exc:
        print(f"refusing token destination: {exc}", file=sys.stderr)
        return 2
    material = json.loads(key_path.read_text())
    key_id = material["key_id"]
    # The PEM exists only for as long as openssl needs it, with 0600, in a
    # private temporary directory.
    with tempfile.TemporaryDirectory() as scratch:
        pem = pathlib.Path(scratch) / "k.pem"
        pem.write_bytes(jwk_to_pem(material["private_key"]))
        pem.chmod(0o600)
        timestamp = int(time.time())
        try:
            der = subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(pem)],
                                 input=f"{key_id}.{timestamp}".encode(),
                                 capture_output=True, check=True).stdout
        except (OSError, subprocess.SubprocessError):
            print("mint failed during signing", file=sys.stderr)
            return 1
    signature = base64.urlsafe_b64encode(der_signature_to_raw(der)).decode().rstrip("=")

    # Verified envelope (D-13): key_id, an INTEGER timestamp, and the raw
    # signature. A string timestamp is rejected as "invalid request body".
    body = json.dumps({"key_id": key_id, "timestamp": timestamp,
                       "signature": signature}).encode()
    try:
        result = subprocess.run(
            ["curl", "--disable", "-sS", "-m", "20", "--max-redirs", "0", "-w", "\n%{http_code}", "-X", "POST",
             "-H", f"user-agent: {UA}", "-H", "content-type: application/json",
             "--data-binary", "@-", f"{CANONICAL_ORIGIN}/v1/auth/api-key/token"],
            input=body, capture_output=True, check=True)
    except (OSError, subprocess.SubprocessError):
        print("mint failed during HTTP request", file=sys.stderr)
        return 1
    try:
        raw = result.stdout.decode()
    except UnicodeError:
        print("mint failed: invalid HTTP response", file=sys.stderr)
        return 1
    payload, _, status = raw.rpartition("\n")
    status = status.strip()
    if not re.fullmatch(r"[0-9]{3}", status):
        print("mint failed: invalid HTTP response", file=sys.stderr)
        return 1
    if status != "200":
        print(f"mint failed http={status}", file=sys.stderr)
        return 1

    try:
        response = json.loads(payload)
        if not isinstance(response, dict):
            raise ValueError("response is not an object")
        token = response.get("access_token")
    except (TypeError, ValueError):
        print("mint failed: invalid JSON response", file=sys.stderr)
        return 1
    if not isinstance(token, str) or not token:
        print("mint failed: no access_token in response", file=sys.stderr)
        return 1
    try:
        _validate_token(token)
        claims = json.loads(base64.urlsafe_b64decode(
            token.split(".")[1] + "=" * (-len(token.split(".")[1]) % 4)))
        if not isinstance(claims, dict):
            raise ValueError("claims must be an object")
        expiry_line = None
        if "exp" in claims:
            expiry = claims["exp"]
            if isinstance(expiry, bool) or not isinstance(expiry, (int, float)):
                raise ValueError("invalid expiry")
            expiry_line = (f"expires in {int(expiry - time.time()) // 60} minutes "
                           f"({time.strftime('%H:%M:%S', time.localtime(expiry))})")
    except (ValueError, TypeError, OverflowError, OSError):
        print("mint failed: unsafe access_token", file=sys.stderr)
        return 1

    try:
        _atomic_write_token(token_path, token)
    except (OSError, RuntimeError) as exc:
        print(f"mint failed writing token: {exc}", file=sys.stderr)
        return 1

    print(f"token written to {token_path}")
    if expiry_line is not None:
        print(expiry_line)
    return 0


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    return mint_token(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    raise SystemExit(main())
