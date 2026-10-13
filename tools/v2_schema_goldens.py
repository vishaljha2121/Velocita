#!/usr/bin/env python3
"""Independent generator for the identity-bound golden vectors in tests/.

Built from the approved byte layouts with struct and hashlib only; it never
calls the C++ codecs. Every vector is a function of the kernel command /
state snapshot schema, because the static cluster identity binds both.

    tools/v2_schema_goldens.py --check        # schema 8 must reproduce the
                                              # reviewed V8 literals exactly
    tools/v2_schema_goldens.py --schema 9     # print the V9 vectors
    tools/v2_schema_goldens.py --rewrite 8 9  # replace exact V8 literals in approved tests
    tools/v2_schema_goldens.py --rewrite 12 12 --old-replication-epoch 1 --replication-epoch 2
    tools/v2_schema_goldens.py --check-literals --schema 12 --replication-epoch 2

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


def identity(schema, incarnation, catalog_root, voters, local, mode=1, replication_epoch=1):
    if type(replication_epoch) is not int or not 1 <= replication_epoch <= 65535:
        raise ValueError('invalid replication epoch')
    body = bytearray()
    body += u16(1) + u16(0)
    body += incarnation + catalog_root
    body += u16(schema) + u16(schema)
    body += u16(1) + u16(1) + u16(replication_epoch) + u16(1)
    body += bytes([1, mode]) + u16(len(voters))
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


def fixture_identity(schema, local=17, replication_epoch=1):
    return identity(schema, bytes(range(1, 17)), bytes(range(0x20, 0x40)),
                    [17, 257, 65537], local, replication_epoch=replication_epoch)


def framed(magic, schema, prefix, payload):
    unsealed = magic + u32(len(payload)) + u16(schema) + prefix + payload
    return unsealed + sha(unsealed[8:])


def advance_kill_epoch_command(schema):
    payload = u64(1) + u64(1) + bytes([1]) + bytes(31) + u64(1)
    return framed(b"V2KC", schema, u16(6), payload)


def empty_state_snapshot(schema):
    return framed(b"V2ST", schema, b"", bytes(8) + bytes([5]) + bytes(86) + (u16(1) + bytes(1) if schema >= 12 else b""))


def bound_frame(kind, manifest_digest, owner, payload):
    header = (b"V2BF" + u16(1) + u16(kind) + u32(88 + len(payload)) + u32(len(payload)) +
              manifest_digest + i32(owner) + u32(0))
    return header + payload + sha(BOUND_FRAME_DOMAIN + header + payload)


def authority_marker(authority_domain):
    body = (u16(1) + bytes(range(0xa0, 0xb0)) + u64(3) + u64(5) + bytes(range(0x40, 0x60)) +
            u64(7) + u64(11) + bytes(range(0x80, 0xa0)) + authority_domain)
    frame = b"V2AB" + u32(len(body) - 2) + body
    return frame + sha(frame[8:])


def archive_segment(manifest_digest, predecessor_term, records):
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


def local_generation_selector(manifest_digest, local):
    head = (b"V2LG" + u16(1) + u16(128) + u32(160) + u32(0) + manifest_digest + i32(local) +
            u32(0) + u64(8) + u64(3) + u64(7) + bytes(range(160, 192)) + bytes(16))
    return head + sha(SELECTOR_DOMAIN + head)


def wal_block_crc(payload, generation=1, sequence=1):
    header = u32(0x32425756) + u16(2) + u16(0) + u64(generation) + u64(sequence) + u32(len(payload))
    return u32(zlib.crc32(header + u32(0) + payload))


STANDARD_CATALOG_ROOT = bytes.fromhex(
    "a17bf680a3bad9b9035efb898745f4d20df2bfd0a82f20cf397e923047c73c64")


def generation_base_seal(schema, replication_epoch=1):
    ident = identity(schema, bytes([0x55]) + bytes(15), STANDARD_CATALOG_ROOT, [1, 2], 1, replication_epoch=replication_epoch)
    snapshot = bound_frame(3, manifest(ident), 1,
                           u64(7) + u64(3) + empty_state_snapshot(schema))
    prefix = b"V2GB" + u16(1) + bytes([1, 0])
    digest = sha(GENERATION_BASE_DOMAIN + prefix + u64(8) + ident + u32(len(snapshot)) + snapshot)
    return prefix + digest


def vectors(schema, replication_epoch=1):
    ident = fixture_identity(schema, replication_epoch=replication_epoch)
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
        "local 257 checksum": sha(fixture_identity(schema, 257, replication_epoch)[:92]),
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
        "generation base seal": generation_base_seal(schema, replication_epoch),
    }


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


GOLDEN_TEST_PATHS = (
    "v2_static_identity_tests.cpp",
    "v2_archive_catalog_tests.cpp",
    "v2_archive_compaction_tests.cpp",
    "v2_archive_file_io_tests.cpp",
    "v2_archive_file_tests.cpp",
    "v2_archive_persistence_model_tests.cpp",
    "v2_archive_publication_fault_tests.cpp",
    "v2_archive_reclamation_red.cpp",
    "v2_archive_scrub_tests.cpp",
    "v2_archive_segment_adversarial_tests.cpp",
    "v2_archive_segment_tests.cpp",
    "v2_archive_store_tests.cpp",
    "v2_bound_frame_tests.cpp",
    "v2_bound_metadata_persistence_model_tests.cpp",
    "v2_bound_metadata_tests.cpp",
    "v2_local_generation_adversarial_tests.cpp",
    "v2_snapshot_store_bounds_tests.cpp",
    "v2_wal_arena_tests.cpp",
)


def rewrite_literals(text, mapping):
    def replace(match):
        pieces = re.findall(r'"([0-9a-f]+)"', match.group(1))
        joined = "".join(pieces)
        if joined not in mapping or mapping[joined] == joined:
            return match.group(0)
        replacement, at = mapping[joined], 0
        if len(replacement) != len(joined):
            raise ValueError('golden length change requires explicit layout review')
        def piece_replace(piece):
            nonlocal at
            size = len(piece.group(1))
            value = replacement[at:at+size]
            at += size
            return '"' + value + '"'
        return re.sub(r'"([0-9a-f]+)"', piece_replace, match.group(1))
    return LITERAL.sub(replace, text)


def rewrite(old_schema, new_schema, root, old_replication_epoch=1, replication_epoch=1):
    old = {name: value.hex() for name, value in vectors(old_schema, old_replication_epoch).items()}
    new = {name: value.hex() for name, value in vectors(new_schema, replication_epoch).items()}
    mapping = {old[name]: new[name] for name in old if old[name] != new[name]}
    changed = []
    for name in GOLDEN_TEST_PATHS:
        path = pathlib.Path(root, "tests", name)
        text = path.read_text()
        updated = rewrite_literals(text, mapping)
        if updated != text:
            path.write_text(updated)
            changed.append(path.name)
    print("rewrote:", ", ".join(changed) if changed else "nothing")
    return changed


def check_epoch_consistency():
    old, new = vectors(12, 1), vectors(12, 2)
    expected = {
        'manifest digest': '7b6a13e548fef5ff55053c1c513155ab40409fae8a165c5bd36e064d43a2cd7a',
        'authority domain': '2af7e8c9814b571e927bba39f81b642aa3b31577fbd12361b3a8c3a36cd3cb19',
        'local 257 checksum': '567fcb542acaf8d238c9ca9ef927f56a64a878a493d3cb719a34e8f43c6a02e3',
    }
    for name, value in expected.items():
        if new[name].hex() != value:
            raise ValueError('epoch-two independent oracle mismatch: '+name)
    if new['static identity'][68:70] != b'\x02\x00' or old['static identity'][68:70] != b'\x01\x00':
        raise ValueError('replication epoch byte layout changed')
    if old['AdvanceKillEpoch command'] != new['AdvanceKillEpoch command']:
        raise ValueError('replication epoch changed command encoding')
    for name in old.keys() - {'AdvanceKillEpoch command'}:
        if old[name] == new[name]:
            raise ValueError('dependent golden did not rebind: '+name)
    fixture = 'const auto value = "abcd"\n  "ef01";\nrequire(flag == 1, "unchanged");\n'
    rewritten = rewrite_literals(fixture, {'abcdef01':'11223344'})
    if rewritten != 'const auto value = "1122"\n  "3344";\nrequire(flag == 1, "unchanged");\n':
        raise ValueError('exact literal rewrite changed formatting or semantics')
    if rewrite_literals(rewritten, {'abcdef01':'11223344'}) != rewritten:
        raise ValueError('rewrite is not idempotent')
    print('epoch-two independent hashes, dependency rebinding, and exact rewrite checks passed')


def check_literals(schema, replication_epoch, root):
    old = vectors(schema, 1)
    current = vectors(schema, replication_epoch)
    obsolete = {value.hex(): name for name, value in old.items() if value != current[name]}
    stale = []
    for name in GOLDEN_TEST_PATHS:
        text = pathlib.Path(root, 'tests', name).read_text()
        for match in LITERAL.finditer(text):
            joined = ''.join(re.findall(r'"([0-9a-f]+)"', match.group(1)))
            if joined in obsolete:
                stale.append(name+': '+obsolete[joined])
    if stale:
        raise ValueError('stale dependent goldens: '+', '.join(stale))
    wal_test = pathlib.Path(root, 'tests', 'v2_wal_arena_tests.cpp').read_text()
    for offset, value in enumerate(current['WAL block crc'], 28):
        values = re.findall(r'block\['+str(offset)+r'\] == std::byte\{0x([0-9a-f]{2})\}', wal_test)
        if values != [format(value, '02x')]:
            raise ValueError('stale numeric WAL CRC oracle at byte '+str(offset))
    generation_test = pathlib.Path(root, 'tests', 'v2_generation_base_tests.cpp').read_text()
    seal = re.search(r'expected_seal\{(.*?)\};', generation_test, re.S)
    if seal is None or bytes(int(value, 16) for value in re.findall(
            r'std::byte\{0x([0-9a-f]{2})\}', seal.group(1))) != current['generation base seal']:
        raise ValueError('stale numeric generation-base seal oracle')
    print('no epoch-one dependent literals remain in '+str(len(GOLDEN_TEST_PATHS))+' approved test paths')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--check-literals", action="store_true")
    parser.add_argument("--replication-epoch", type=int, default=1)
    parser.add_argument("--old-replication-epoch", type=int, default=1)
    parser.add_argument("--schema", type=int)
    parser.add_argument("--rewrite", nargs=2, type=int, metavar=("OLD", "NEW"))
    args = parser.parse_args()
    if args.check:
        result = check()
        if result == 0:
            check_epoch_consistency()
        return result
    if args.check_literals:
        check_literals(args.schema or 12, args.replication_epoch, pathlib.Path(__file__).resolve().parent.parent)
        return 0
    if args.rewrite:
        if check() != 0:
            return 1
        check_epoch_consistency()
        rewrite(*args.rewrite, pathlib.Path(__file__).resolve().parent.parent, args.old_replication_epoch, args.replication_epoch)
        return 0
    for name, value in vectors(args.schema or 9, args.replication_epoch).items():
        print(f"{name}: {value.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
