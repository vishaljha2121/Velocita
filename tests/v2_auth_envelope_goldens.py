#!/usr/bin/env python3
"""Independent oracle for tests/v2_auth_envelope_tests.cpp.

Uses only Python's hmac/hashlib/struct, never the C++ code under test. Rerun to
regenerate the golden hex embedded in the C++ test; any drift is a failure.
"""
import hashlib
import hmac
import struct

SECRET = bytes(range(0x40, 0x60))
INCARNATION = bytes([0xAB, 0x5A] + [0] * 14)
DOMAIN = b"velocita.auth.v1"


def derive(role, sender, recipient, secret=SECRET, incarnation=INCARNATION):
    info = DOMAIN + struct.pack("<Hii", role, sender, recipient) + incarnation
    return hmac.new(secret, info, hashlib.sha256).digest()


def envelope(role, sender, recipient, epoch, sequence, body, secret=SECRET):
    key = derive(role, sender, recipient, secret)
    header = b"VLA1" + struct.pack("<HHiiQQI", 1, role, sender, recipient,
                                   epoch, sequence, len(body))
    return header + body + hmac.new(key, header + body, hashlib.sha256).digest()


if __name__ == "__main__":
    print("peer_key_1_to_2", derive(1, 1, 2).hex())
    print("peer_key_2_to_1", derive(1, 2, 1).hex())
    print("ingress_key_9_to_1", derive(2, 9, 1).hex())
    print("peer_envelope", envelope(1, 1, 2, 7, 42, b"bound-peer-frame").hex())
    print("empty_response", envelope(3, 1, 9, 1, 1, b"").hex())
