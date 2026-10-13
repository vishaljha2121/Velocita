#include "velocita/v2/archive_catalog.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(std::byte(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
    return result;
}

VerifiedStaticIdentity identity() {
    const auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0c000c0001000100020001000101030011000000010100000100010011000000"
        "4a5cd70c7355448c05ae9d61053151ab8202198996d3317eecfc6a857d353719");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
            "identity fixture refused");
    return result;
}

Digest256 digest(std::uint8_t first) {
    Digest256 value;
    value.bytes[0] = std::byte{first};
    return value;
}

void reseal(std::span<std::byte> bytes) {
    constexpr std::string_view domain = "velocita.v2.archive-catalog.v1";
    Sha256 hash;
    hash.update(std::as_bytes(std::span{domain.data(), domain.size()}));
    hash.update(bytes.first(bytes.size() - kArchiveCatalogDigestBytes));
    const auto value = hash.finish();
    for (std::size_t i = 0; i < value.bytes.size(); ++i)
        bytes[bytes.size() - value.bytes.size() + i] = value.bytes[i];
}

ArchiveCatalogRow row(std::uint64_t first, std::uint64_t last,
                      std::int64_t predecessor, std::int64_t term) {
    return {first, last, predecessor, term, static_cast<std::uint32_t>(last - first + 1),
            256, 7, digest(static_cast<std::uint8_t>(first)),
            digest(static_cast<std::uint8_t>(last + 32))};
}

void test_golden_and_round_trip() {
    const auto id = identity();
    const ArchiveCatalogHeader header{3, 7, 5, 5, 3};
    const std::array rows{row(1, 2, 0, 2), row(3, 5, 2, 3)};
    std::array<std::byte, 352> bytes{};
    const auto encoded = encode_archive_catalog(header, rows, id, bytes);
    require(encoded.error == ArchiveCatalogError::none && encoded.bytes_written == 352,
            "golden encode failed");
    require(to_hex(encoded.catalog_digest) ==
        "f53dcc2c410d79228ab948625fedfcb24badd71767bbe827d20b360ffc233034",
        "independent catalog SHA golden mismatch");
    require(bytes[0] == std::byte{'V'} && bytes[3] == std::byte{'C'} &&
            bytes[6] == std::byte{96} && bytes[8] == std::byte{0x60} &&
            bytes[9] == std::byte{1} && bytes[12] == std::byte{2},
            "fixed header bytes differ");

    ArchiveCatalogHeader decoded{99, 99, 99, 99, 99};
    std::array<ArchiveCatalogRow, 2> decoded_rows{};
    std::size_t count = 77;
    require(decode_archive_catalog(bytes, id, decoded, decoded_rows, count) ==
                ArchiveCatalogError::none,
            "golden decode failed");
    require(decoded == header && decoded_rows == rows && count == rows.size(),
            "round trip changed values");
}

void test_refusals_leave_destinations_unchanged() {
    const auto id = identity();
    const ArchiveCatalogHeader header{1, 1, 1, 1, 1};
    auto only_row = row(1, 1, 0, 1);
    only_row.source_storage_generation = 1;
    const std::array rows{only_row};
    std::array<std::byte, 240> bytes{};
    require(encode_archive_catalog(header, rows, id, bytes).error == ArchiveCatalogError::none,
            "refusal fixture encode failed");
    const ArchiveCatalogHeader sentinel{9, 9, 9, 9, 9};
    const ArchiveCatalogRow row_sentinel = row(1, 1, 0, 1);
    for (std::size_t offset : {std::size_t{16}, std::size_t{48}, std::size_t{52},
                               std::size_t{96}, std::size_t{239}}) {
        auto bad = bytes;
        bad[offset] ^= std::byte{1};
        ArchiveCatalogHeader decoded = sentinel;
        std::array<ArchiveCatalogRow, 1> decoded_rows{row_sentinel};
        std::size_t count = 91;
        require(decode_archive_catalog(bad, id, decoded, decoded_rows, count) !=
                    ArchiveCatalogError::none,
                "corruption accepted");
        require(decoded == sentinel && decoded_rows[0] == row_sentinel && count == 91,
                "failed decode mutated destination");
    }
    std::array<ArchiveCatalogRow, 0> no_rows{};
    ArchiveCatalogHeader decoded = sentinel;
    std::size_t count = 91;
    require(decode_archive_catalog(bytes, id, decoded, no_rows, count) ==
                ArchiveCatalogError::capacity_exceeded && decoded == sentinel && count == 91,
            "short destination was not atomic");

    auto self_consistent_bad = bytes;
    self_consistent_bad[96] = std::byte{2};
    reseal(self_consistent_bad);
    decoded = sentinel;
    std::array<ArchiveCatalogRow, 1> decoded_rows{row_sentinel};
    count = 91;
    require(decode_archive_catalog(self_consistent_bad, id, decoded, decoded_rows, count) ==
                ArchiveCatalogError::invalid_coordinates && decoded == sentinel &&
            decoded_rows[0] == row_sentinel && count == 91,
            "self-consistent malformed coordinates mutated or decoded");
}

void test_validation_and_bounds() {
    const auto id = identity();
    std::array<std::byte, 240> output;
    output.fill(std::byte{0xa5});
    const auto before = output;
    auto invalid = row(2, 2, 0, 1);
    invalid.source_storage_generation = 1;
    require(encode_archive_catalog({1, 1, 2, 2, 1}, std::span{&invalid, 1}, id,
                                   output).error == ArchiveCatalogError::invalid_coordinates &&
            output == before, "gap/refusal changed output");
    invalid = row(1, 1, 0, 1);
    invalid.source_storage_generation = 2;
    require(encode_archive_catalog({1, 1, 1, 1, 1}, std::span{&invalid, 1}, id,
                                   output).error == ArchiveCatalogError::invalid_coordinates,
            "future row source generation accepted");
    std::array<std::byte, 128> empty{};
    require(encode_archive_catalog({1, 1, 0, 0, 0}, {}, id, empty).error ==
                ArchiveCatalogError::none,
            "empty catalog refused");
}

void test_exact_maximum() {
    const auto id = identity();
    std::vector<ArchiveCatalogRow> rows;
    rows.reserve(kMaxArchiveCatalogRows);
    for (std::size_t i = 0; i < kMaxArchiveCatalogRows; ++i) {
        auto value = row(i + 1, i + 1, i == 0 ? 0 : 1, 1);
        value.source_storage_generation = 1;
        rows.push_back(value);
    }
    std::vector<std::byte> bytes(kMaxArchiveCatalogBytes);
    const auto encoded = encode_archive_catalog(
        {1, 1, kMaxArchiveCatalogRows, kMaxArchiveCatalogRows, 1}, rows, id, bytes);
    require(encoded.error == ArchiveCatalogError::none &&
            encoded.bytes_written == kMaxArchiveCatalogBytes,
            "exact maximum catalog refused");
    std::vector<ArchiveCatalogRow> decoded_rows(kMaxArchiveCatalogRows);
    ArchiveCatalogHeader decoded;
    std::size_t count{};
    require(decode_archive_catalog(bytes, id, decoded, decoded_rows, count) ==
                ArchiveCatalogError::none && count == kMaxArchiveCatalogRows &&
            decoded_rows == rows, "exact maximum catalog did not round trip");
}

}

int main() {
    try {
        test_golden_and_round_trip();
        test_refusals_leave_destinations_unchanged();
        test_validation_and_bounds();
        test_exact_maximum();
        std::cout << "archive catalog tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
