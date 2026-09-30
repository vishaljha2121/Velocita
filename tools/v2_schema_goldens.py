#!/usr/bin/env python3
"""Independent generator for the identity-bound golden vectors in tests/.

Built from the approved byte layouts with struct and hashlib only; it never
calls the C++ codecs. Every vector is a function of the kernel command /
state snapshot schema, because the static cluster identity binds both.

    tools/v2_schema_goldens.py --check        # schema 8 must reproduce the
                                              # reviewed V8 literals exactly
    tools/v2_schema_goldens.py --schema 9     # print the V9 vectors
    tools/v2_schema_goldens.py --rewrite 8 9  # replace V8 literals in tests/

--check is the generator's own oracle: it re-derives every reviewed schema-8
vector from its fields, so a wrong layout or domain cannot survive it.
"""

import argparse
import hashlib
import pathlib
import re
import struct
import sys
import zlib

sha = lambda data: hashlib.sha256(data).digest()
u16 = lambda v: struct.pack("<H", v)
u32 = lambda v: struct.pack("<I", v)
u64 = lambda v: struct.pack("<Q", v)
i32 = lambda v: struct.pack("<i", v)
i64 = lambda v: struct.pack("<q", v)

MANIFEST_DOMAIN = b"velocita.v2.cluster-manifest.v1"
AUTHORITY_DOMAIN = b"velocita.v2.authority-domain.v1"
BOUND_FRAME_DOMAIN = b"velocita.v2.bound-frame.v1"
SEGMENT_DOMAIN = b"velocita.v2.archive-segment.v1"
RECORD_DOMAIN = b"velocita.v2.archive-record.v1"
CATALOG_DOMAIN = b"velocita.v2.archive-catalog.v1"
SELECTOR_DOMAIN = b"velocita.v2.local-generation-selector.v1"
GENERATION_BASE_DOMAIN = b"velocita.v2.local-generation-base.v1"
AUTHORITY_BARRIER_ENTRY_TYPE = 101


# --- V2ID static identity -------------------------------------------------
def identity(schema, incarnation, catalog_root, voters, local, mode=1):
    body = bytearray()
    body += u16(1) + u16(0)                       # frame schema, reserved
    body += incarnation + catalog_root
    body += u16(schema) + u16(schema)             # state snapshot, command/result
    body += u16(1) + u16(1) + u16(1) + u16(1)     # marker, catalog, replication, storage
    body += bytes([1, mode]) + u16(len(voters))   # quorum mode, deployment, count
    for voter in voters:
        body += i32(voter)
    body += i32(local)
    frame = b"V2ID" + u32(4 + 4 + len(body) + 32) + bytes(body)
    return frame + sha(frame)


def manifest(identity_frame):
    count = struct.unpack_from("<H", identity_frame, 74)[0]
    return sha(MANIFEST_DOMAIN + identity_frame[12:76 + 4 * count])


def authority(manifest_digest):
    return sha(AUTHORITY_DOMAIN + manifest_digest)


def fixture_identity(schema, local=17):
    return identity(schema, bytes(range(1, 17)), bytes(range(0x20, 0x40)),
                    [17, 257, 65537], local)


# --- V2KC kernel command / V2ST snapshot --------------------------------
def framed(magic, schema, prefix, payload):
    # The length field counts the payload after the schema and any kind tag.
    unsealed = magic + u32(len(payload)) + u16(schema) + prefix + payload
    return unsealed + sha(unsealed[8:])


def advance_kill_epoch_command(schema):
    # kind 6; identity caller 1, sequence 1, digest 01 00..; new kill epoch 1
    payload = u64(1) + u64(1) + bytes([1]) + bytes(31) + u64(1)
    return framed(b"V2KC", schema, u16(6), payload)


def empty_state_snapshot(schema):
    return framed(b"V2ST", schema, b"", bytes(8) + bytes([5]) + bytes(86))


# --- V2BF bound frame ------------------------------------------------------
def bound_frame(kind, manifest_digest, owner, payload):
    header = (b"V2BF" + u16(1) + u16(kind) + u32(88 + len(payload)) + u32(len(payload)) +
              manifest_digest + i32(owner) + u32(0))
    return header + payload + sha(BOUND_FRAME_DOMAIN + header + payload)


# --- V2AB authority marker ------------------------------------------------
def authority_marker(authority_domain):
    body = (u16(1) + bytes(range(0xa0, 0xb0)) + u64(3) + u64(5) + bytes(range(0x40, 0x60)) +
            u64(7) + u64(11) + bytes(range(0x80, 0xa0)) + authority_domain)
    frame = b"V2AB" + u32(len(body) - 2) + body
    return frame + sha(frame[8:])


# --- V2AS archive segment -------------------------------------------------
def archive_segment(manifest_digest, predecessor_term, records):
    """records: (index, term, entry_id, entry_type, payload)"""
    body = bytearray()
    for index, term, entry_id, entry_type, payload in records:
        header = u64(index) + i64(term) + i32(entry_id) + i32(entry_type) + u32(len(payload)) + u32(0)
        body += header + payload + sha(RECORD_DOMAIN + manifest_digest + header + payload)
    total = 96 + len(body) + 32
    header = (b"V2AS" + u16(1) + u16(96) + u32(total) + u32(len(records)) +
              u64(records[0][0]) + u64(records[-1][0]) + i64(predecessor_term) +
              i64(records[0][1]) + i64(records[-1][1]) + manifest_digest + u32(0) + u32(0))
    unsealed = header + bytes(body)
    return unsealed + sha(SEGMENT_DOMAIN + unsealed)


# --- V2AC archive catalog -------------------------------------------------
def archive_catalog_digest(manifest_digest, local):
    def digest(first):
        return bytes([first]) + bytes(31)

    def row(first, last, predecessor, term):
        return (u64(first) + u64(last) + i64(predecessor) + i64(term) + u32(last - first + 1) +
                u32(256) + u64(7) + digest(first) + digest(last + 32))

    rows = row(1, 2, 0, 2) + row(3, 5, 2, 3)
    total = 96 + len(rows) + 32
    header = (b"V2AC" + u16(1) + u16(96) + u32(total) + u32(2) + manifest_digest + i32(local) +
              u32(0) + u64(3) + u64(7) + u64(5) + u64(5) + i64(3))
    return sha(CATALOG_DOMAIN + header + rows)


# --- V2LG local generation selector --------------------------------------
def local_generation_selector(manifest_digest, local):
    head = (b"V2LG" + u16(1) + u16(128) + u32(160) + u32(0) + manifest_digest + i32(local) +
            u32(0) + u64(8) + u64(3) + u64(7) + bytes(range(160, 192)) + bytes(16))
    return head + sha(SELECTOR_DOMAIN + head)


# --- WAL block v2 --------------------------------------------------------
def wal_block_crc(payload, generation=1, sequence=1):
    # CRC-32 over the header with its checksum field zeroed plus the payload.
    header = u32(0x32425756) + u16(2) + u16(0) + u64(generation) + u64(sequence) + u32(len(payload))
    return u32(zlib.crc32(header + u32(0) + payload))


# --- V2GB generation base seal --------------------------------------------
# Catalog root of catalog_test::standard_catalog(), taken from the reviewed
# F5 worker-golden evidence; the execution catalog codec is unchanged by V9.
STANDARD_CATALOG_ROOT = bytes.fromhex(
    "a17bf680a3bad9b9035efb898745f4d20df2bfd0a82f20cf397e923047c73c64")


def generation_base_seal(schema):
    ident = identity(schema, bytes([0x55]) + bytes(15), STANDARD_CATALOG_ROOT, [1, 2], 1)
    snapshot = bound_frame(3, manifest(ident), 1,
                           u64(7) + u64(3) + empty_state_snapshot(schema))
    prefix = b"V2GB" + u16(1) + bytes([1, 0])
    digest = sha(GENERATION_BASE_DOMAIN + prefix + u64(8) + ident + u32(len(snapshot)) + snapshot)
    return prefix + digest


# --- the vector set -------------------------------------------------------
def vectors(schema):
    ident = fixture_identity(schema)
    m = manifest(ident)
    command = advance_kill_epoch_command(schema)
    marker = authority_marker(authority(m))
    adversarial = archive_segment(m, 3, [
        (17, 4, -2**31, AUTHORITY_BARRIER_ENTRY_TYPE, marker),
        (18, 6, -7, AUTHORITY_BARRIER_ENTRY_TYPE, marker)])
    wal = bound_frame(2, m, 17, u64(7) + i64(3) + i32(7) + i32(0) + command)
    return {
        "static identity": ident,
        "manifest digest": m,
        "authority domain": authority(m),
        "local 257 checksum": sha(fixture_identity(schema, 257)[:92]),
        "AdvanceKillEpoch command": command,
        "archive segment": archive_segment(m, 2, [(7, 3, 7, 0, command)]),
        "adversarial archive segment": adversarial,
        "adversarial segment digest": adversarial[-32:],
        "WAL bound frame": wal,
        "WAL block crc": wal_block_crc(wal),
        "snapshot prefix bound frame": bound_frame(3, m, 17, u64(11) + i64(7)),
        "peer bound frame": bound_frame(4, m, 0, bytes(range(1, 17))),
        "metadata bound frame": bound_frame(1, m, 17, u64(7) + u64(17) + i64(3)),
        "archive catalog digest": archive_catalog_digest(m, 17),
        "local generation selector": local_generation_selector(m, 17),
        "generation base seal": generation_base_seal(schema),
    }


# Reviewed schema-8 literals, copied from the tests they were pinned in.
REVIEWED_V8 = {
    "manifest digest": "eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc323",
    "authority domain": "28c6283ffd06603e0e483836a876faf9f48c398f7fe6754f8b9eec0ac0fb6363",
    "local 257 checksum": "078511faa39fcd28f1b3d86d57330d51979e4e679a1f629ec01f07b227b70bd7",
    "static identity": "563249447c000000010000000102030405060708090a0b0c0d0e0f10202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0800080001000100010001000101030011000000010100000100010011000000b2dc3bacb3740557bf90ec225fac0e192e28d9014ca9f18ffe4a1491ff17bcbd",
    "AdvanceKillEpoch command": "56324b4338000000080006000100000000000000010000000000000001000000000000000000000000000000000000000000000000000000000000000100000000000000feed673fc6b9e3d92994ee3bd78e39b7dd9fcb22339301b3849a0a87fccb8ae9",
    "archive segment": "5632415301006000240100000100000007000000000000000700000000000000020000000000000003000000000000000300000000000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc3230000000000000000070000000000000003000000000000000700000000000000640000000000000056324b4338000000080006000100000000000000010000000000000001000000000000000000000000000000000000000000000000000000000000000100000000000000feed673fc6b9e3d92994ee3bd78e39b7dd9fcb22339301b3849a0a87fccb8ae90d87af8bf9e1a7d8c11d3b31e3147a7aa4d467750da6b166d6375f48d9960c362d7022f9e91f8333f6cbb07119c4782761a5f82d19eaecd0f5ba3c6c04d20036",
    "adversarial archive segment": "5632415301006000740200000200000011000000000000001200000000000000030000000000000004000000000000000600000000000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc3230000000000000000110000000000000004000000000000000000008065000000ba0000000000000056324142900000000100a0a1a2a3a4a5a6a7a8a9aaabacadaeaf03000000000000000500000000000000404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f07000000000000000b00000000000000808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f28c6283ffd06603e0e483836a876faf9f48c398f7fe6754f8b9eec0ac0fb63636eaa2e4c19a3c6d5e61f39fab4cc65f6bacd53afbed06335f2f72885151db181160fd11848cf5d47061a68736ea94b5bbca9d6cf73a21317045285d358e1a01212000000000000000600000000000000f9ffffff65000000ba0000000000000056324142900000000100a0a1a2a3a4a5a6a7a8a9aaabacadaeaf03000000000000000500000000000000404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f07000000000000000b00000000000000808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f28c6283ffd06603e0e483836a876faf9f48c398f7fe6754f8b9eec0ac0fb63636eaa2e4c19a3c6d5e61f39fab4cc65f6bacd53afbed06335f2f72885151db1817b6ccd8dc71dc4b588abb8171be7784ce2723bec7975738b9474de2301f35249d4f1956600b19c575528fe22a0887e1895e33d78d2eed0eb4d7b26c26cbf4fa1",
    "adversarial segment digest": "d4f1956600b19c575528fe22a0887e1895e33d78d2eed0eb4d7b26c26cbf4fa1",
    "WAL bound frame": "5632424601000200d40000007c000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc323110000000000000007000000000000000300000000000000070000000000000056324b4338000000080006000100000000000000010000000000000001000000000000000000000000000000000000000000000000000000000000000100000000000000feed673fc6b9e3d92994ee3bd78e39b7dd9fcb22339301b3849a0a87fccb8ae984c3963dd0936b4f94fa50516c7597dbefbcf49cd433f14139a4ee86aa856f93",
    "snapshot prefix bound frame": "56324246010003006800000010000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc32311000000000000000b00000000000000070000000000000011a871af03709fda752d36394b51ee3793a71b32b52ab96dbd005b5b47df428f",
    "peer bound frame": "56324246010004006800000010000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc32300000000000000000102030405060708090a0b0c0d0e0f1057f6a72cca5a41c9c2d62340da351afe1f725ba8de790d94659c1c5a89aa8abc",
    "metadata bound frame": "56324246010001007000000018000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc323110000000000000007000000000000001100000000000000030000000000000031c73d8ea42fdee2db8c8b3c4519ef2ca29b47b6e4555f61518893c934ba8ab9",
    "archive catalog digest": "5f54a2a8758e7f66e83e9c29374b346d1807e4392a5b9feb6276c3108ac57638",
    "local generation selector": "56324c4701008000a000000000000000eef0ad41482478f93cf9ac45c22d104c60c6d233638eef41429a4e6050ccc3231100000000000000080000000000000003000000000000000700000000000000a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf0000000000000000000000000000000017d467dd970d21c01cd80118aad34f8d3d0fb10224ee5e5934f8cef403576e50",
    "WAL block crc": "aedc6e0f",
    "generation base seal": "5632474201000100ebf0d9195e6eae14a7c958e87799251a68f4b8fe0d209c11ac1a49e69de0bc9a",
}


def check():
    produced = {name: value.hex() for name, value in vectors(8).items()}
    wrong = [name for name, value in REVIEWED_V8.items() if produced[name] != value]
    missing = sorted(set(produced) - set(REVIEWED_V8))
    for name in wrong:
        print(f"MISMATCH {name}\n  reviewed {REVIEWED_V8[name]}\n  produced {produced[name]}")
    if missing:
        print("unreviewed vectors:", missing)
    if wrong or missing:
        return 1
    print(f"all {len(REVIEWED_V8)} reviewed schema-8 vectors reproduced")
    return 0


LITERAL = re.compile(r'((?:"[0-9a-f]+"\s*)+)')


def rewrite(old_schema, new_schema, root):
    old = {name: value.hex() for name, value in vectors(old_schema).items()}
    new = {name: value.hex() for name, value in vectors(new_schema).items()}
    mapping = {old[name]: new[name] for name in old}
    changed = []
    for path in sorted(pathlib.Path(root, "tests").glob("*.cpp")):
        text = path.read_text()

        def replace(match):
            pieces = re.findall(r'"([0-9a-f]+)"', match.group(1))
            joined = "".join(pieces)
            if joined not in mapping:
                return match.group(0)
            replacement, out, at = mapping[joined], match.group(1), 0
            for piece in pieces:
                out = out.replace(f'"{piece}"', f'"\0{replacement[at:at + len(piece)]}"', 1)
                at += len(piece)
            return out.replace("\0", "")

        updated = LITERAL.sub(replace, text)
        if updated != text:
            path.write_text(updated)
            changed.append(path.name)
    print("rewrote:", ", ".join(changed) if changed else "nothing")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--schema", type=int)
    parser.add_argument("--rewrite", nargs=2, type=int, metavar=("OLD", "NEW"))
    args = parser.parse_args()
    if args.check:
        return check()
    if args.rewrite:
        if check() != 0:
            return 1
        rewrite(*args.rewrite, pathlib.Path(__file__).resolve().parent.parent)
        return 0
    for name, value in vectors(args.schema or 9).items():
        print(f"{name}: {value.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
