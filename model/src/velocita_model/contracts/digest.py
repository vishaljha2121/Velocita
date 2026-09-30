"""SHA-256 identity, matching `src/v2_digest.cpp`.

A digest here means the same thing it means in the cluster: an immutable name
for an exact byte sequence. It is never a checksum of a paraphrase.
"""

from __future__ import annotations

import hashlib
from typing import Final

from .enums import DIGEST_BYTES

ZERO_DIGEST: Final = bytes(DIGEST_BYTES)


def sha256_digest(payload: bytes) -> bytes:
    return hashlib.sha256(payload).digest()


def to_hex(digest: bytes) -> str:
    return digest.hex()


def from_hex(text: str) -> bytes:
    digest = bytes.fromhex(text)
    if len(digest) != DIGEST_BYTES:
        raise ValueError(f"expected {DIGEST_BYTES}-byte digest, got {len(digest)}")
    return digest
