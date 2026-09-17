#!/usr/bin/env python3
"""Exchange the True Markets API key for a short-lived token. ADR 0010.

Run by a PERSON. Nothing in this repository holds key material, and nothing
automated may call this - a component that can re-mint its own token is a
component that holds the key.

Reads the key JSON on stdin or from a path, writes ONLY the token to the
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
import sys
import tempfile
import time

BASE = os.environ.get("TM_BASE", "https://api.truemarkets.co")
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


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    key_path, token_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    material = json.loads(key_path.read_text())
    key_id = material["key_id"]

    # The PEM exists only for as long as openssl needs it, with 0600, in a
    # private temporary directory.
    with tempfile.TemporaryDirectory() as scratch:
        pem = pathlib.Path(scratch) / "k.pem"
        pem.write_bytes(jwk_to_pem(material["private_key"]))
        pem.chmod(0o600)
        timestamp = int(time.time())
        der = subprocess.run(["openssl", "dgst", "-sha256", "-sign", str(pem)],
                             input=f"{key_id}.{timestamp}".encode(),
                             capture_output=True, check=True).stdout
    signature = base64.urlsafe_b64encode(der_signature_to_raw(der)).decode().rstrip("=")

    # Verified envelope (D-13): key_id, an INTEGER timestamp, and the raw
    # signature. A string timestamp is rejected as "invalid request body".
    body = json.dumps({"key_id": key_id, "timestamp": timestamp,
                       "signature": signature}).encode()
    result = subprocess.run(
        ["curl", "-sS", "-w", "\n%{http_code}", "-X", "POST",
         "-H", f"user-agent: {UA}", "-H", "content-type: application/json",
         "--data-binary", "@-", f"{BASE}/v1/auth/api-key/token"],
        input=body, capture_output=True, check=True)
    raw = result.stdout.decode()
    payload, _, status = raw.rpartition("\n")
    if status.strip() != "200":
        print(f"mint failed http={status.strip()}: {payload[:300]}", file=sys.stderr)
        return 1

    token = json.loads(payload).get("access_token")
    if not token:
        print(f"no access_token in response; fields were {sorted(json.loads(payload))}",
              file=sys.stderr)
        return 1

    token_path.parent.mkdir(parents=True, exist_ok=True)
    # Create with 0600 BEFORE writing, so the token is never briefly world-readable.
    fd = os.open(token_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as handle:
        handle.write(token)

    claims = json.loads(base64.urlsafe_b64decode(
        token.split(".")[1] + "=" * (-len(token.split(".")[1]) % 4)))
    expiry = claims.get("exp")
    print(f"token written to {token_path}")
    if expiry:
        print(f"expires in {int(expiry - time.time()) // 60} minutes "
              f"({time.strftime('%H:%M:%S', time.localtime(expiry))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
