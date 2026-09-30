#!/usr/bin/env python3
"""Independent oracle for tests/v2_session_fence_tests.cpp: session-epoch file
bytes are "VLS1" | u64 LE counter | CRC-32 (zlib) LE of the first 12 bytes."""
import struct
import zlib


def epoch_file(counter):
    body = b"VLS1" + struct.pack("<Q", counter)
    return body + struct.pack("<I", zlib.crc32(body))


if __name__ == "__main__":
    for counter in (1, 2, 4, 2**64 - 1):
        print(counter, epoch_file(counter).hex())
