#include "velocita/v2/digest.hpp"
#include "velocita/v2/static_identity.hpp"

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

std::vector<std::byte> bytes_from_hex(const std::string& hex) {
    require(hex.size() % 2U == 0, "odd test hex length");
    std::vector<std::byte> result;
    result.reserve(hex.size() / 2U);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        result.push_back(static_cast<std::byte>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return result;
}

StaticIdentity fixture(std::int32_t local = 17) {
    StaticIdentity value;
    for (std::size_t i = 0; i < value.incarnation.size(); ++i)
        value.incarnation[i] = static_cast<std::byte>(i + 1);
    for (std::size_t i = 0; i < value.catalog_root.bytes.size(); ++i)
        value.catalog_root.bytes[i] = static_cast<std::byte>(0x20 + i);
    value.voter_count = 3;
    value.voters = {17, 257, 65537};
    value.local_node_id = local;
    return value;
}

const std::vector<std::byte>& golden() {
    static const auto bytes = bytes_from_hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0b000b0001000100010001000101030011000000010100000100010011000000"
        "1b3d30a9e57f85108ab28320f24135276cc02342c3cf0183b618c042aab007bb");
    return bytes;
}

void expect_unchanged_failure(const std::vector<std::byte>& bytes,
                              StaticIdentityCodecError expected,
                              const std::string& context) {
    VerifiedStaticIdentity destination;
    require(VerifiedStaticIdentity::decode(golden(), destination) ==
                StaticIdentityCodecError::none,
            "sentinel decode failed");
    const auto before_value = destination.value();
    const auto before_manifest = destination.manifest_digest();
    const auto before_authority = destination.authority_domain();
    require(VerifiedStaticIdentity::decode(bytes, destination) == expected, context);
    require(destination.valid() && destination.value() == before_value &&
                destination.manifest_digest() == before_manifest &&
                destination.authority_domain() == before_authority,
            context + " changed destination");
}

void test_independent_golden_and_domains() {
    require(golden().size() == 124, "golden is not maximum frame");
    std::array<std::byte, kMaxStaticIdentityBytes + 5> output;
    output.fill(std::byte{0xA5});
    const auto encoded = encode_static_identity(fixture(), output);
    require(encoded.error == StaticIdentityCodecError::none &&
                encoded.bytes_written == golden().size(),
            "golden encode refused");
    require(std::equal(golden().begin(), golden().end(), output.begin()),
            "encoded bytes differ from independent golden");
    require(std::all_of(output.begin() + 124, output.end(),
                        [](std::byte b) { return b == std::byte{0xA5}; }),
            "encode changed output tail");

    VerifiedStaticIdentity verified;
    require(!verified.valid(), "default verified identity is valid");
    require(VerifiedStaticIdentity::decode(golden(), verified) ==
                StaticIdentityCodecError::none,
            "golden decode refused");
    require(verified.valid() && verified.value() == fixture(),
            "golden decoded value differs");
    require(to_hex(verified.manifest_digest()) ==
                "4a614f5d781d5d8fbb67a13c76d98dd5be8b161da30e14ce95c240a61798d30b",
            "manifest digest differs from independent hash");
    require(to_hex(verified.authority_domain()) ==
                "63155d1ff7278542da514d6c13664ae0b04ba68ee6b11150909c4fa4771292ae",
            "authority domain differs from independent hash");
}

void test_lengths_and_failure_preservation() {
    for (std::size_t size = 0; size < kMinStaticIdentityBytes; ++size) {
        std::vector<std::byte> bytes(golden().begin(), golden().begin() + size);
        expect_unchanged_failure(bytes, StaticIdentityCodecError::truncated,
                                 "truncation accepted");
    }
    for (std::size_t size = kMinStaticIdentityBytes; size < golden().size(); ++size) {
        std::vector<std::byte> bytes(golden().begin(), golden().begin() + size);
        expect_unchanged_failure(bytes, StaticIdentityCodecError::length_mismatch,
                                 "partial maximum frame accepted");
    }
    auto trailing = golden();
    trailing.push_back(std::byte{0});
    expect_unchanged_failure(trailing, StaticIdentityCodecError::length_mismatch,
                             "trailing byte accepted");

    for (std::size_t size = 0; size < 124; ++size) {
        std::array<std::byte, 124> output;
        output.fill(std::byte{0x5A});
        const auto before = output;
        require(encode_static_identity(fixture(), std::span{output}.first(size)).error ==
                    StaticIdentityCodecError::output_too_small,
                "short output accepted");
        require(output == before, "short encode changed output");
    }
}

std::vector<std::byte> with_checksum(std::vector<std::byte> bytes) {
    const auto digest = sha256_digest(std::span{bytes}.first(bytes.size() - 32));
    std::copy(digest.bytes.begin(), digest.bytes.end(), bytes.end() - 32);
    return bytes;
}

void test_header_digest_and_semantic_refusals() {
    const auto mutate = [](std::size_t offset, std::byte delta = std::byte{1}) {
        auto bytes = golden(); bytes[offset] ^= delta; return bytes;
    };
    expect_unchanged_failure(mutate(0), StaticIdentityCodecError::invalid_magic,
                             "bad magic accepted");
    expect_unchanged_failure(mutate(4), StaticIdentityCodecError::length_mismatch,
                             "bad length accepted");
    expect_unchanged_failure(mutate(8), StaticIdentityCodecError::unsupported_schema,
                             "bad frame schema accepted");
    expect_unchanged_failure(mutate(10), StaticIdentityCodecError::nonzero_reserved,
                             "reserved byte accepted");
    expect_unchanged_failure(mutate(12), StaticIdentityCodecError::digest_mismatch,
                             "corrupt shared byte accepted");
    expect_unchanged_failure(mutate(123), StaticIdentityCodecError::digest_mismatch,
                             "corrupt checksum accepted");

    constexpr std::array<std::size_t, 6> unsupported_offsets{
        60, 62, 64, 66, 68, 70};
    for (const auto offset : unsupported_offsets) {
        expect_unchanged_failure(with_checksum(mutate(offset)),
                                 StaticIdentityCodecError::unsupported_schema,
                                 "unsupported semantic value accepted");
    }
    for (const auto offset : std::array<std::size_t, 2>{72, 73})
        expect_unchanged_failure(with_checksum(mutate(offset)),
                                 StaticIdentityCodecError::invalid_value,
                                 "unsupported mode accepted");
    expect_unchanged_failure(with_checksum(mutate(74, std::byte{3})),
                             StaticIdentityCodecError::invalid_value,
                             "zero voter count accepted");
    constexpr std::array<std::size_t, 2> zero_ranges{12, 28};
    constexpr std::array<std::size_t, 2> zero_sizes{16, 32};
    for (std::size_t i = 0; i < zero_ranges.size(); ++i) {
        auto bytes = golden();
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(zero_ranges[i]),
                    zero_sizes[i], std::byte{0});
        expect_unchanged_failure(with_checksum(bytes),
                                 StaticIdentityCodecError::invalid_value,
                                 "zero identity digest field accepted");
    }
}

void test_previous_schema_manifest_is_refused() {
    require(golden()[60] == std::byte{11} && golden()[62] == std::byte{11},
            "fixture drifted: the golden identity is not schema 11");
    const auto with_schemas = [](std::uint8_t snapshot, std::uint8_t command) {
        auto bytes = golden();
        bytes[60] = std::byte{snapshot};
        bytes[62] = std::byte{command};
        return with_checksum(bytes);
    };
    expect_unchanged_failure(with_schemas(10, 10), StaticIdentityCodecError::unsupported_schema,
                             "a previous-schema manifest was accepted by this binary");
    expect_unchanged_failure(with_schemas(10, 11), StaticIdentityCodecError::unsupported_schema,
                             "a previous-schema snapshot manifest was accepted");
    expect_unchanged_failure(with_schemas(11, 10), StaticIdentityCodecError::unsupported_schema,
                             "a previous-schema command manifest was accepted");
    StaticIdentity value = fixture();
    value.command_result_schema = 10;
    std::array<std::byte, kMaxStaticIdentityBytes> output{};
    require(encode_static_identity(value, output).error ==
                StaticIdentityCodecError::unsupported_schema,
            "the V9 binary encoded a previous-schema manifest");
}

void test_voter_canonicality_modes_and_sizes() {
    const auto invalid = [](StaticIdentity value, const std::string& context) {
        std::array<std::byte, 124> output; output.fill(std::byte{0x77});
        const auto before = output;
        require(encode_static_identity(value, output).error ==
                    StaticIdentityCodecError::invalid_value,
                context);
        require(output == before, context + " changed output");
    };
    { auto v = fixture(); v.voter_count = 0; invalid(v, "zero voters encoded"); }
    { auto v = fixture(); v.voter_count = 4; invalid(v, "four voters encoded"); }
    { auto v = fixture(); v.voters = {17, 17, 65537}; invalid(v, "duplicate encoded"); }
    { auto v = fixture(); v.voters = {17, 0, 65537}; invalid(v, "zero voter encoded"); }
    { auto v = fixture(); v.voters = {257, 17, 65537}; invalid(v, "unsorted encoded"); }
    { auto v = fixture(); v.local_node_id = 19; invalid(v, "foreign local encoded"); }
    { auto v = fixture(); v.deployment_mode = DeploymentMode::two_host_pilot;
      invalid(v, "three-voter pilot encoded"); }
    { auto v = fixture(); v.voter_count = 2; v.voters[2] = 99;
      invalid(v, "nonzero unused voter encoded"); }

    const auto decode_invalid = [](std::size_t offset, std::uint32_t value,
                                   const std::string& context) {
        auto bytes = golden();
        for (std::size_t i = 0; i < 4; ++i)
            bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
        expect_unchanged_failure(with_checksum(bytes),
                                 StaticIdentityCodecError::invalid_value, context);
    };
    decode_invalid(76, 0, "decoded zero voter accepted");
    decode_invalid(76, 0xffffffffU, "decoded negative voter accepted");
    decode_invalid(80, 17, "decoded duplicate voter accepted");
    decode_invalid(80, 16, "decoded unsorted voter accepted");
    decode_invalid(88, 19, "decoded foreign local ID accepted");
    decode_invalid(88, 0xffffffffU, "decoded negative local ID accepted");
    {
        auto bytes = golden();
        bytes[74] = std::byte{4};
        expect_unchanged_failure(bytes, StaticIdentityCodecError::invalid_value,
                                 "decoded four-voter count accepted");
    }

    for (std::uint16_t count = 1; count <= 3; ++count) {
        auto v = fixture();
        v.voter_count = count;
        for (std::size_t i = count; i < v.voters.size(); ++i) v.voters[i] = 0;
        v.local_node_id = v.voters[count - 1];
        if (count == 2) v.deployment_mode = DeploymentMode::two_host_pilot;
        std::array<std::byte, 124> bytes{};
        const auto result = encode_static_identity(v, bytes);
        require(result.error == StaticIdentityCodecError::none &&
                    result.bytes_written == 112 + 4 * count,
                "valid voter count size changed");
        VerifiedStaticIdentity decoded;
        require(VerifiedStaticIdentity::decode(
                    std::span{bytes}.first(result.bytes_written), decoded) ==
                    StaticIdentityCodecError::none && decoded.value() == v,
                "valid voter fixture did not round trip");

        if (count != 2) {
            bytes[73] = static_cast<std::byte>(DeploymentMode::two_host_pilot);
            auto framed = std::vector<std::byte>(
                bytes.begin(), bytes.begin() +
                                   static_cast<std::ptrdiff_t>(result.bytes_written));
            expect_unchanged_failure(with_checksum(framed),
                                     StaticIdentityCodecError::invalid_value,
                                     "non-two-voter pilot decoded");
        }
    }
}

void test_valid_shared_changes_rebind_domains() {
    const auto verify = [](const StaticIdentity& value) {
        std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
        const auto encoded = encode_static_identity(value, bytes);
        require(encoded.error == StaticIdentityCodecError::none,
                "valid shared change did not encode");
        VerifiedStaticIdentity result;
        require(VerifiedStaticIdentity::decode(
                    std::span{bytes}.first(encoded.bytes_written), result) ==
                    StaticIdentityCodecError::none,
                "valid shared change did not decode");
        require(result.value() == value, "valid shared change decoded differently");
        return result;
    };
    const auto baseline = verify(fixture());
    const auto require_rebound = [&](StaticIdentity changed,
                                     const std::string& context) {
        const auto verified = verify(changed);
        require(verified.manifest_digest() != baseline.manifest_digest(),
                context + " did not change manifest digest");
        require(verified.authority_domain() != baseline.authority_domain(),
                context + " did not change authority domain");
    };
    {
        auto changed = fixture();
        changed.incarnation[0] ^= std::byte{0x80};
        require_rebound(changed, "incarnation change");
    }
    {
        auto changed = fixture();
        changed.catalog_root.bytes[31] ^= std::byte{0x80};
        require_rebound(changed, "catalog root change");
    }
    {
        auto changed = fixture();
        changed.voters = {19, 257, 65537};
        changed.local_node_id = 19;
        require_rebound(changed, "voter set change");
    }

    auto laboratory = fixture();
    laboratory.voter_count = 2;
    laboratory.voters[2] = 0;
    laboratory.local_node_id = laboratory.voters[0];
    const auto lab_verified = verify(laboratory);
    auto pilot = laboratory;
    pilot.deployment_mode = DeploymentMode::two_host_pilot;
    const auto pilot_verified = verify(pilot);
    require(pilot_verified.manifest_digest() != lab_verified.manifest_digest() &&
                pilot_verified.authority_domain() != lab_verified.authority_domain(),
            "laboratory-to-pilot change did not rebind shared domains");
}

void test_shared_vs_local_digests() {
    std::array<std::byte, 124> first{}, second{};
    require(encode_static_identity(fixture(17), first).error ==
                StaticIdentityCodecError::none,
            "first local encode failed");
    require(encode_static_identity(fixture(257), second).error ==
                StaticIdentityCodecError::none,
            "second local encode failed");
    VerifiedStaticIdentity a, b;
    require(VerifiedStaticIdentity::decode(first, a) == StaticIdentityCodecError::none &&
                VerifiedStaticIdentity::decode(second, b) == StaticIdentityCodecError::none,
            "local variants refused");
    require(a.manifest_digest() == b.manifest_digest() &&
                a.authority_domain() == b.authority_domain(),
            "local ID changed shared digests");
    require(to_hex(sha256_digest(std::span{second}.first(92))) ==
                "863b66148ae97ce0b3bd3e254295f3326ba728f9e009c10a8e354ed3d2c496b7",
            "second local file checksum differs from independent hash");
    require(!std::equal(first.end() - 32, first.end(), second.end() - 32),
            "local ID did not change file checksum");
}

}

int main() {
    try {
        test_independent_golden_and_domains();
        test_lengths_and_failure_preservation();
        test_header_digest_and_semantic_refusals();
        test_previous_schema_manifest_is_refused();
        test_voter_canonicality_modes_and_sizes();
        test_valid_shared_changes_rebind_domains();
        test_shared_vs_local_digests();
        std::cout << "v2 static identity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v2 static identity test failure: " << error.what() << '\n';
        return 1;
    }
}
