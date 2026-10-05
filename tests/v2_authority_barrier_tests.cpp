#include "velocita/v2/authority_barrier.hpp"
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

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> bytes_from_hex(const std::string& hex) {
    require((hex.size() % 2U) == 0U, "odd test hex length");
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t offset = 0; offset < hex.size(); offset += 2U) {
        bytes.push_back(static_cast<std::byte>(
            std::stoul(hex.substr(offset, 2), nullptr, 16)));
    }
    return bytes;
}

template <std::size_t Size>
std::array<std::byte, Size> increasing(const unsigned start) {
    std::array<std::byte, Size> bytes{};
    for (std::size_t index = 0; index < Size; ++index) {
        bytes[index] = static_cast<std::byte>(start + index);
    }
    return bytes;
}

AuthorityBarrier fixture() {
    AuthorityBarrier value;
    value.incarnation = increasing<16>(0x01);
    value.request_sequence = 0x1817161514131211ULL;
    value.effect_id = EffectId{0x2827262524232221ULL};
    value.expected_effect_payload_digest.bytes = increasing<32>(0x40);
    value.expected_kill_epoch = KillEpoch{0x3837363534333231ULL};
    value.expected_executor_generation =
        ExecutorGeneration{0x4847464544434241ULL};
    value.catalog_root.bytes = increasing<32>(0x80);
    value.authority_domain.bytes = increasing<32>(0xC0);
    return value;
}

const std::vector<std::byte>& golden() {
    static const auto bytes = bytes_from_hex(
        "563241429000000001000102030405060708090a0b0c0d0e0f10"
        "11121314151617182122232425262728404142434445464748494a4b4c4d4e4f"
        "505152535455565758595a5b5c5d5e5f31323334353637384142434445464748"
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
        "c0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
        "8397e4a19616b927f171ac32aa55e2e1273537737a0885d807631aa31c92d8a6");
    return bytes;
}

void test_golden_round_trip_and_type() {
    require(golden().size() == kAuthorityBarrierBytes, "golden size changed");
    std::array<std::byte, kAuthorityBarrierBytes> output{};
    const auto encoded =
        encode_authority_barrier(kAuthorityBarrierEntryType, fixture(), output);
    require(encoded.error == AuthorityBarrierCodecError::none, "golden encode refused");
    require(encoded.bytes_written == output.size(), "encoded byte count changed");
    require(std::equal(output.begin(), output.end(), golden().begin()),
            "canonical bytes differ from independent golden");
    require(to_hex(encoded.frame_digest) ==
                "8397e4a19616b927f171ac32aa55e2e1273537737a0885d807631aa31c92d8a6",
            "body digest changed");
    require(to_hex(sha256_digest(output)) ==
                "08d9ab385b267bc1eac2b45c2edc34c13bfa8a0fb42d6357658ae66c8c52f163",
            "independent full-frame hash changed");

    AuthorityBarrier decoded;
    const auto result = decode_authority_barrier(kAuthorityBarrierEntryType,
                                                  golden(), decoded);
    require(result.error == AuthorityBarrierCodecError::none, "golden decode refused");
    require(decoded == fixture(), "decoded barrier differs");

    for (const auto wrong : std::array<std::int32_t, 4>{-1, 0, 100, 102}) {
        auto destination = fixture();
        destination.request_sequence++;
        const auto sentinel = destination;
        require(decode_authority_barrier(wrong, golden(), destination).error ==
                    AuthorityBarrierCodecError::wrong_entry_type,
                "wrong entry type accepted");
        require(destination == sentinel, "wrong type changed destination");
    }
}

void test_every_truncation_and_trailing_byte() {
    for (std::size_t size = 0; size < golden().size(); ++size) {
        auto destination = fixture();
        destination.request_sequence++;
        const auto sentinel = destination;
        require(decode_authority_barrier(kAuthorityBarrierEntryType,
                                         std::span{golden()}.first(size),
                                         destination).error ==
                    AuthorityBarrierCodecError::truncated,
                "truncation was not classified as truncated");
        require(destination == sentinel, "truncation changed destination");
    }
    auto trailing = golden();
    trailing.push_back(std::byte{0xAA});
    auto destination = fixture();
    destination.request_sequence++;
    const auto sentinel = destination;
    require(decode_authority_barrier(kAuthorityBarrierEntryType, trailing,
                                     destination).error ==
                AuthorityBarrierCodecError::length_mismatch,
            "trailing byte accepted");
    require(destination == sentinel, "trailing byte changed destination");
}

void test_header_digest_and_field_mutations() {
    struct Mutation {
        std::size_t offset;
        AuthorityBarrierCodecError expected;
    };
    constexpr std::array mutations{
        Mutation{0, AuthorityBarrierCodecError::invalid_magic},
        Mutation{4, AuthorityBarrierCodecError::length_mismatch},
        Mutation{8, AuthorityBarrierCodecError::unsupported_schema},
        Mutation{10, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{26, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{34, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{42, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{74, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{82, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{90, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{122, AuthorityBarrierCodecError::digest_mismatch},
        Mutation{154, AuthorityBarrierCodecError::digest_mismatch},
    };
    for (const auto mutation : mutations) {
        auto bytes = golden();
        bytes[mutation.offset] ^= std::byte{0x01};
        auto destination = fixture();
        destination.request_sequence++;
        const auto sentinel = destination;
        require(decode_authority_barrier(kAuthorityBarrierEntryType, bytes,
                                         destination).error == mutation.expected,
                "mutated field received wrong error");
        require(destination == sentinel, "mutation changed destination");
    }
}

void test_zero_fields_and_encode_preservation() {
    const auto assert_invalid = [](const AuthorityBarrier& invalid) {
        std::array<std::byte, kAuthorityBarrierBytes + 7> output;
        output.fill(std::byte{0xA5});
        const auto sentinel = output;
        require(encode_authority_barrier(kAuthorityBarrierEntryType, invalid,
                                         output).error ==
                    AuthorityBarrierCodecError::invalid_value,
                "zero field encoded");
        require(output == sentinel, "failed encode changed output");
    };
    {
        auto value = fixture(); value.incarnation = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.request_sequence = 0; assert_invalid(value);
    }
    {
        auto value = fixture(); value.effect_id = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.expected_effect_payload_digest = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.expected_kill_epoch = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.expected_executor_generation = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.catalog_root = {}; assert_invalid(value);
    }
    {
        auto value = fixture(); value.authority_domain = {}; assert_invalid(value);
    }

    for (const auto wrong : std::array<std::int32_t, 3>{-1, 0, 102}) {
        std::array<std::byte, kAuthorityBarrierBytes> output;
        output.fill(std::byte{0x5A});
        const auto sentinel = output;
        require(encode_authority_barrier(wrong, fixture(), output).error ==
                    AuthorityBarrierCodecError::wrong_entry_type,
                "encode accepted wrong entry type");
        require(output == sentinel, "wrong-type encode changed output");
    }
    std::array<std::byte, kAuthorityBarrierBytes> short_output;
    for (std::size_t size = 0; size < kAuthorityBarrierBytes; ++size) {
        short_output.fill(std::byte{0x3C});
        const auto sentinel = short_output;
        require(encode_authority_barrier(kAuthorityBarrierEntryType, fixture(),
                                         std::span{short_output}.first(size)).error ==
                    AuthorityBarrierCodecError::output_too_small,
                "short output accepted");
        require(short_output == sentinel, "short encode changed output");
    }

    std::array<std::byte, kAuthorityBarrierBytes + 17> oversized;
    oversized.fill(std::byte{0x6D});
    const auto encoded =
        encode_authority_barrier(kAuthorityBarrierEntryType, fixture(), oversized);
    require(encoded.error == AuthorityBarrierCodecError::none,
            "oversized destination refused");
    require(std::equal(oversized.begin(),
                       oversized.begin() +
                           static_cast<std::ptrdiff_t>(kAuthorityBarrierBytes),
                       golden().begin()),
            "oversized destination prefix differs from golden");
    require(std::all_of(oversized.begin() +
                            static_cast<std::ptrdiff_t>(kAuthorityBarrierBytes),
                        oversized.end(),
                        [](const std::byte byte) { return byte == std::byte{0x6D}; }),
            "successful encode changed destination tail");
}

void test_zero_field_decode_with_recomputed_digest() {
    constexpr std::array<std::pair<std::size_t, std::size_t>, 8> fields{{
        {10, 16}, {26, 8}, {34, 8}, {42, 32},
        {74, 8}, {82, 8}, {90, 32}, {122, 32},
    }};
    for (const auto [offset, length] : fields) {
        auto bytes = golden();
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), length,
                    std::byte{0});
        const auto digest = sha256_digest(std::span{bytes}.subspan(8, 146));
        std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.begin() + 154);
        auto destination = fixture();
        destination.request_sequence++;
        const auto sentinel = destination;
        require(decode_authority_barrier(kAuthorityBarrierEntryType, bytes,
                                         destination).error ==
                    AuthorityBarrierCodecError::invalid_value,
                "zero decoded field accepted");
        require(destination == sentinel, "zero field changed destination");
    }
}

}

int main() {
    try {
        test_golden_round_trip_and_type();
        test_every_truncation_and_trailing_byte();
        test_header_digest_and_field_mutations();
        test_zero_fields_and_encode_preservation();
        test_zero_field_decode_with_recomputed_digest();
        std::cout << "v2 authority barrier tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v2 authority barrier test failure: " << error.what() << '\n';
        return 1;
    }
}
