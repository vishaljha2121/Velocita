#include "velocita/v2/bound_frame.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> from_hex(const std::string& hex) {
    std::vector<std::byte> result;
    require(hex.size() % 2 == 0, "odd hex fixture");
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return result;
}

VerifiedStaticIdentity identity() {
    // Independent static-identity golden from v2_static_identity_tests.cpp.
    auto bytes = from_hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0900090001000100010001000101030011000000010100000100010011000000"
        "a6326543a7df7e9c89692344318dadf2f97dc3f06b3dabfacdcd3875fb29334d");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
            "identity fixture refused");
    return result;
}

void reseal(std::vector<std::byte>& frame) {
    static constexpr char domain[] = "velocita.v2.bound-frame.v1";
    std::vector<std::byte> preimage;
    preimage.reserve(26 + frame.size() - 32);
    for (std::size_t i = 0; i < 26; ++i) preimage.push_back(static_cast<std::byte>(domain[i]));
    preimage.insert(preimage.end(), frame.begin(), frame.end() - 32);
    const auto digest = sha256_digest(preimage);
    std::copy(digest.bytes.begin(), digest.bytes.end(), frame.end() - 32);
}

std::vector<std::byte> encode(BoundFrameKind kind, const VerifiedStaticIdentity& id,
                              std::size_t payload_size, std::byte fill = std::byte{0x6d}) {
    std::vector<std::byte> payload(payload_size, fill);
    std::vector<std::byte> output(kBoundFrameOverheadBytes + payload_size);
    BoundFrameScratch scratch{};
    const auto result = encode_bound_frame(kind, id, payload, output, scratch);
    require(result.error == BoundFrameError::none && result.bytes_written == output.size(),
            "fixture encode failed");
    return output;
}

void expect_decode(BoundFrameError expected, std::span<const std::byte> bytes,
                   BoundFrameKind kind, const VerifiedStaticIdentity& id,
                   const std::string& context) {
    const std::array<std::byte, 1> sentinel{std::byte{0xa5}};
    BoundFrameView view{sentinel};
    BoundFrameScratch scratch{};
    require(decode_bound_frame(bytes, kind, id, view, scratch) == expected, context);
    if (expected != BoundFrameError::none)
        require(view.payload.data() == sentinel.data() && view.payload.size() == 1,
                context + " changed destination");
}

void test_independent_golden_and_borrowed_view() {
    const auto expected = from_hex(
        "56324246010004006800000010000000"
        "3ce5d6e3e9a886ab694852a129348bbe9413c2b55077a84804efa3cd645cc3e0"
        "00000000000000000102030405060708090a0b0c0d0e0f10"
        "8d7fd085ec7818f6d5ad19bb00e8e1c91647f398cd08fecf98aeb892c94cd9a6");
    const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}, std::byte{9},
        std::byte{10}, std::byte{11}, std::byte{12}, std::byte{13}, std::byte{14},
        std::byte{15}, std::byte{16}};
    std::array<std::byte, 109> output;
    output.fill(std::byte{0xa5});
    BoundFrameScratch scratch{};
    const auto result = encode_bound_frame(BoundFrameKind::peer, identity(), payload, output, scratch);
    require(result.error == BoundFrameError::none && result.bytes_written == expected.size(),
            "golden encode failed");
    require(std::equal(expected.begin(), expected.end(), output.begin()),
            "independent bound-frame golden differs");
    require(std::all_of(output.begin() + static_cast<std::ptrdiff_t>(expected.size()), output.end(),
                        [](std::byte b) { return b == std::byte{0xa5}; }),
            "encode changed output tail");

    BoundFrameView view{};
    require(decode_bound_frame(expected, BoundFrameKind::peer, identity(), view, scratch) ==
                BoundFrameError::none,
            "golden decode failed");
    require(view.payload.data() == expected.data() + kBoundFrameHeaderBytes &&
                view.payload.size() == payload.size() &&
                !std::equal(view.payload.begin(), view.payload.end(), scratch.bytes.begin()),
            "decoded view does not borrow input");
}

void test_lengths_header_identity_and_integrity() {
    const auto id = identity();
    const auto good = encode(BoundFrameKind::wal_record, id, 24);
    for (std::size_t n = 0; n < good.size(); ++n)
        expect_decode(n < kBoundFrameOverheadBytes ? BoundFrameError::truncated
                                                   : BoundFrameError::length_mismatch,
                      std::span{good}.first(n), BoundFrameKind::wal_record, id,
                      "truncation accepted");
    auto trailing = good; trailing.push_back(std::byte{});
    expect_decode(BoundFrameError::length_mismatch, trailing, BoundFrameKind::wal_record, id,
                  "trailing byte accepted");

    auto mutate = [&](std::size_t at, std::byte value, BoundFrameError error,
                      const char* context) {
        auto b = good; b[at] = value;
        expect_decode(error, b, BoundFrameKind::wal_record, id, context);
    };
    mutate(0, std::byte{'X'}, BoundFrameError::invalid_magic, "bad magic accepted");
    mutate(4, std::byte{2}, BoundFrameError::unsupported_schema, "bad schema accepted");
    mutate(6, std::byte{9}, BoundFrameError::unknown_kind, "unknown kind accepted");
    expect_decode(BoundFrameError::wrong_kind, good, BoundFrameKind::snapshot, id,
                  "wrong expected kind accepted");
    mutate(8, std::byte{0}, BoundFrameError::length_mismatch, "bad total accepted");
    mutate(12, std::byte{0}, BoundFrameError::invalid_payload, "bad payload length accepted");
    mutate(52, std::byte{1}, BoundFrameError::nonzero_reserved, "reserved accepted");
    mutate(16, std::byte{0}, BoundFrameError::digest_mismatch, "unsealed foreign manifest accepted");
    mutate(48, std::byte{18}, BoundFrameError::digest_mismatch, "unsealed wrong owner accepted");
    mutate(56, std::byte{0}, BoundFrameError::digest_mismatch, "corrupt payload accepted");
    mutate(good.size() - 1, std::byte{0}, BoundFrameError::digest_mismatch, "corrupt digest accepted");

    auto foreign = good; foreign[16] = std::byte{0}; reseal(foreign);
    expect_decode(BoundFrameError::foreign_manifest, foreign, BoundFrameKind::wal_record, id,
                  "resealed foreign manifest accepted");
    auto wrong_owner = good; wrong_owner[48] = std::byte{18}; reseal(wrong_owner);
    expect_decode(BoundFrameError::wrong_owner, wrong_owner, BoundFrameKind::wal_record, id,
                  "resealed wrong owner accepted");

    VerifiedStaticIdentity invalid;
    std::array<std::byte, 128> output{};
    BoundFrameScratch scratch{};
    const std::array payload{std::byte{1}};
    require(encode_bound_frame(BoundFrameKind::peer, invalid, payload, output, scratch).error ==
                BoundFrameError::invalid_identity,
            "default identity encoded");
    expect_decode(BoundFrameError::invalid_identity, encode(BoundFrameKind::peer, id, 16),
                  BoundFrameKind::peer, invalid, "default identity decoded");
}

void test_owners_bounds_and_output_preservation() {
    const auto id = identity();
    for (auto kind : {BoundFrameKind::metadata, BoundFrameKind::wal_record,
                      BoundFrameKind::snapshot, BoundFrameKind::peer}) {
        const std::size_t maximum = kind == BoundFrameKind::metadata ? kBoundMetadataBytes :
            kind == BoundFrameKind::wal_record ? kMaxBoundWalRecordBytes :
            kind == BoundFrameKind::snapshot ? kMaxBoundSnapshotBytes : kMaxBoundPeerBytes;
        const auto max_payload = maximum - kBoundFrameOverheadBytes;
        auto frame = encode(kind, id, max_payload);
        expect_decode(BoundFrameError::none, frame, kind, id, "maximum frame refused");

        std::vector<std::byte> too_large(max_payload + 1);
        std::vector<std::byte> output(maximum + 1, std::byte{0xa5});
        const auto before = output;
        BoundFrameScratch scratch{};
        require(encode_bound_frame(kind, id, too_large, output, scratch).error ==
                    BoundFrameError::capacity_exceeded && output == before,
                "oversize encode changed output or succeeded");
    }

    auto peer = encode(BoundFrameKind::peer, id, 16);
    peer[48] = std::byte{17};
    expect_decode(BoundFrameError::digest_mismatch, peer, BoundFrameKind::peer, id,
                  "unsealed owned peer accepted");
    reseal(peer);
    expect_decode(BoundFrameError::wrong_owner, peer, BoundFrameKind::peer, id,
                  "resealed owned peer accepted");
    auto local = encode(BoundFrameKind::metadata, id, 24);
    local[48] = std::byte{0};
    reseal(local);
    expect_decode(BoundFrameError::wrong_owner, local, BoundFrameKind::metadata, id,
                  "resealed ownerless local frame accepted");

    std::array<std::byte, 103> output;
    output.fill(std::byte{0xa5});
    const auto before = output;
    BoundFrameScratch scratch{};
    const std::array<std::byte, 16> payload{};
    require(encode_bound_frame(BoundFrameKind::peer, id, payload, output, scratch).error ==
                BoundFrameError::output_too_small && output == before,
            "small output changed");
    require(encode_bound_frame(static_cast<BoundFrameKind>(9), id, {}, output, scratch).error ==
                BoundFrameError::unknown_kind,
            "unknown encode kind accepted");

    for (auto kind : {BoundFrameKind::metadata, BoundFrameKind::wal_record,
                      BoundFrameKind::snapshot, BoundFrameKind::peer}) {
        const std::size_t minimum = kind == BoundFrameKind::metadata || kind == BoundFrameKind::wal_record ? 24 : 16;
        std::vector<std::byte> short_payload(minimum - 1);
        std::vector<std::byte> target(kBoundFrameOverheadBytes + minimum);
        require(encode_bound_frame(kind, id, short_payload, target, scratch).error ==
                    BoundFrameError::invalid_payload,
                "undersize payload encoded");
    }

    const auto expected = encode(BoundFrameKind::peer, id, 16, std::byte{0x33});
    std::vector<std::byte> overlap(expected.size() + 32, std::byte{0x33});
    auto overlapping_payload = std::span<const std::byte>{overlap}.subspan(20, 16);
    require(encode_bound_frame(BoundFrameKind::peer, id, overlapping_payload,
                               std::span<std::byte>{overlap}.first(expected.size()), scratch).error ==
                BoundFrameError::none && std::equal(expected.begin(), expected.end(), overlap.begin()),
            "overlapping payload/output was corrupted");
}

} // namespace

int main() {
    try {
        test_independent_golden_and_borrowed_view();
        test_lengths_header_identity_and_integrity();
        test_owners_bounds_and_output_preservation();
        std::cout << "v2 bound frame tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v2 bound frame tests failed: " << error.what() << '\n';
        return 1;
    }
}
