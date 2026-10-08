#include "velocita/v2/generation_base.hpp"
#include "v2_catalog_test_support.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(const std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](const std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

VerifiedStaticIdentity identity() {
    StaticIdentity value;
    value.incarnation[0] = std::byte{0x55};
    value.catalog_root = catalog_test::standard_catalog().root();
    value.voter_count = 2;
    value.voters = {1, 2, 0};
    value.local_node_id = 1;
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    VerifiedStaticIdentity result;
    require(encoded.error == StaticIdentityCodecError::none &&
                VerifiedStaticIdentity::decode(
                    std::span{bytes}.first(encoded.bytes_written), result) ==
                    StaticIdentityCodecError::none,
            "identity fixture failed");
    return result;
}

void put64(std::span<std::byte> output, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
        output[offset + index] = static_cast<std::byte>(value >> (8U * index));
}

std::vector<std::byte> bound_snapshot(const VerifiedStaticIdentity& expected,
                                      std::uint64_t index, std::int64_t term,
                                      const TradingState& state = {}) {
    std::array<std::byte, kMaxStateSnapshotBytes> canonical{};
    const auto encoded = encode_state_snapshot(state, canonical);
    require(encoded.error == StateSnapshotError::none, "state fixture failed");
    std::array<std::byte, 16 + kMaxStateSnapshotBytes> payload{};
    put64(payload, 0, index);
    put64(payload, 8, static_cast<std::uint64_t>(term));
    std::copy_n(canonical.begin(), encoded.bytes_written, payload.begin() + 16);
    std::array<std::byte, kMaxBoundSnapshotBytes> frame{};
    BoundFrameScratch scratch;
    const auto bound = encode_bound_frame(
        BoundFrameKind::snapshot, expected,
        std::span{payload}.first(16 + encoded.bytes_written), frame, scratch);
    require(bound.error == BoundFrameError::none, "bound snapshot fixture failed");
    return {frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(bound.bytes_written)};
}

void run() {
    const auto expected = identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto snapshot = bound_snapshot(expected, 7, 3);
    GenerationBaseScratch scratch;
    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    allocation_probe::enabled = true;
    const auto encoded = encode_generation_base(
        GenerationBaseKind::snapshot, 8, expected, catalog, snapshot, seal, scratch);
    allocation_probe::enabled = false;
    require(encoded.error == GenerationBaseError::none,
            "RED: approved generation base seal did not encode");
    const std::array<std::byte, kGenerationBaseSealBytes> expected_seal{
        std::byte{0x56}, std::byte{0x32}, std::byte{0x47}, std::byte{0x42},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
        std::byte{0x17}, std::byte{0x05}, std::byte{0xe1}, std::byte{0xdc},
        std::byte{0xa7}, std::byte{0xa8}, std::byte{0x8e}, std::byte{0xfe},
        std::byte{0x69}, std::byte{0xc7}, std::byte{0x11}, std::byte{0x2e},
        std::byte{0xf2}, std::byte{0x78}, std::byte{0x81}, std::byte{0x06},
        std::byte{0x7e}, std::byte{0xd3}, std::byte{0x3b}, std::byte{0x95},
        std::byte{0xc7}, std::byte{0x6b}, std::byte{0xdf}, std::byte{0x8f},
        std::byte{0x15}, std::byte{0xd6}, std::byte{0xb6}, std::byte{0xfe},
        std::byte{0x67}, std::byte{0x64}, std::byte{0x9b}, std::byte{0x97}};
    require(seal == expected_seal, "generation base literal golden mismatch");
    require(encoded.bytes_written == seal.size() && allocation_probe::count == 0,
            "generation base encode length/allocation mismatch");
    require(seal[0] == std::byte{'V'} && seal[1] == std::byte{'2'} &&
                seal[2] == std::byte{'G'} && seal[3] == std::byte{'B'} &&
                seal[4] == std::byte{1} && seal[6] == std::byte{1} &&
                seal[7] == std::byte{0},
            "generation base fixed bytes mismatch");
    require(encoded.base.kind == GenerationBaseKind::snapshot &&
                encoded.base.index == 7 && encoded.base.term == 3 &&
                !encoded.base.state_digest.is_zero(),
            "generation base snapshot summary mismatch");

    GenerationBase decoded{};
    require(decode_generation_base(seal, 8, expected, catalog, snapshot,
                                   scratch, decoded) == GenerationBaseError::none &&
                decoded == encoded.base,
            "generation base round trip failed");

    std::array<std::byte, kGenerationBaseSealBytes> unchanged;
    unchanged.fill(std::byte{0x6d});
    const auto unchanged_sentinel = unchanged;
    require(encode_generation_base(GenerationBaseKind::snapshot, 0, expected,
                catalog, snapshot, unchanged, scratch).error ==
                GenerationBaseError::invalid_generation && unchanged == unchanged_sentinel,
            "failed encode changed output");
    auto corrupt_snapshot = snapshot;
    corrupt_snapshot.back() ^= std::byte{1};
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected,
                catalog, corrupt_snapshot, unchanged, scratch).error ==
                GenerationBaseError::invalid_snapshot && unchanged == unchanged_sentinel,
            "corrupt snapshot accepted or changed output");
    const auto zero_index_snapshot = bound_snapshot(expected, 0, 3);
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected,
                catalog, zero_index_snapshot, unchanged, scratch).error ==
                GenerationBaseError::invalid_snapshot,
            "zero snapshot index accepted");
    const auto zero_term_snapshot = bound_snapshot(expected, 7, 0);
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected,
                catalog, zero_term_snapshot, unchanged, scratch).error ==
                GenerationBaseError::invalid_snapshot,
            "zero snapshot term accepted");
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected,
                {}, snapshot, unchanged, scratch).error ==
                GenerationBaseError::catalog_mismatch,
            "missing expected catalog accepted");
    TradingState foreign_dependency_state;
    foreign_dependency_state.execution_catalog_root.bytes[0] = std::byte{0x7e};
    const auto foreign_dependency_snapshot =
        bound_snapshot(expected, 7, 3, foreign_dependency_state);
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected,
                catalog, foreign_dependency_snapshot, unchanged, scratch).error ==
                GenerationBaseError::catalog_dependency_missing,
            "valid snapshot with missing catalog dependency accepted");

    auto destination = encoded.base;
    destination.index = 91;
    const auto destination_sentinel = destination;
    auto corrupt_seal = seal;
    corrupt_seal.back() ^= std::byte{1};
    require(decode_generation_base(corrupt_seal, 8, expected, catalog, snapshot,
                scratch, destination) == GenerationBaseError::digest_mismatch &&
                destination == destination_sentinel,
            "corrupt seal accepted or changed destination");
    require(decode_generation_base(std::span{seal}.first(seal.size() - 1), 8,
                expected, catalog, snapshot, scratch, destination) ==
                GenerationBaseError::length_mismatch &&
                destination == destination_sentinel,
            "short seal accepted or changed destination");

    for (std::size_t size = 0; size < seal.size(); ++size) {
        require(decode_generation_base(std::span(seal).first(size), 8, expected, catalog,
                    snapshot, scratch, destination) == GenerationBaseError::length_mismatch &&
                    destination == destination_sentinel, "seal truncation changed destination");
    }
    for (std::size_t offset = 0; offset < seal.size(); ++offset) {
        auto altered = seal;
        altered[offset] ^= std::byte{1};
        require(decode_generation_base(altered, 8, expected, catalog, snapshot, scratch,
                    destination) != GenerationBaseError::none && destination == destination_sentinel,
                "one-byte seal corruption accepted or changed destination");
    }
    std::array<std::byte, kGenerationBaseSealBytes + 1> trailing{};
    std::copy(seal.begin(), seal.end(), trailing.begin());
    require(decode_generation_base(trailing, 8, expected, catalog, snapshot, scratch, destination) ==
                GenerationBaseError::length_mismatch && destination == destination_sentinel,
            "trailing seal bytes accepted");
    require(encode_generation_base(GenerationBaseKind::snapshot, 8, expected, catalog,
                snapshot, std::span(unchanged).first(39), scratch).error ==
                GenerationBaseError::output_too_small && unchanged == unchanged_sentinel,
            "small encode buffer was overwritten");

    std::array<std::byte, kMaxStaticIdentityBytes> root_identity{};
    const auto root_encoded = encode_static_identity(expected.value(), root_identity);
    require(root_encoded.error == StaticIdentityCodecError::none, "root identity fixture failed");
    for (const auto offset : {0U, 4U, 6U, 7U}) {
        auto altered = seal;
        altered[offset] = std::byte{2};
        std::vector<std::byte> preimage;
        for (char c : std::string_view("velocita.v2.local-generation-base.v1"))
            preimage.push_back(std::byte(c));
        preimage.insert(preimage.end(), altered.begin(), altered.begin() + 8);
        for (unsigned i = 0; i < 8; ++i) preimage.push_back(std::byte(std::uint64_t{8} >> (8*i)));
        preimage.insert(preimage.end(), root_identity.begin(), root_identity.begin() + root_encoded.bytes_written);
        for (unsigned i = 0; i < 4; ++i) preimage.push_back(std::byte(snapshot.size() >> (8*i)));
        preimage.insert(preimage.end(), snapshot.begin(), snapshot.end());
        const auto hash = sha256_digest(preimage);
        std::copy(hash.bytes.begin(), hash.bytes.end(), altered.begin() + 8);
        require(decode_generation_base(altered, 8, expected, catalog, snapshot, scratch,
                    destination) != GenerationBaseError::none && destination == destination_sentinel,
                "resealed unsupported header accepted or changed destination");
    }

    std::array<std::byte, kGenerationBaseSealBytes> genesis_seal{};
    const auto genesis = encode_generation_base(
        GenerationBaseKind::genesis, 1, expected, catalog, {}, genesis_seal, scratch);
    require(genesis.error == GenerationBaseError::none && genesis.base.index == 0 &&
                genesis.base.term == 0 && genesis.base.state_digest.is_zero(),
            "genesis base refused or invented snapshot state");
    require(encode_generation_base(GenerationBaseKind::genesis, 1, expected,
                catalog, snapshot, unchanged, scratch).error ==
                GenerationBaseError::invalid_snapshot,
            "genesis base accepted snapshot bytes");
}

}

int main() {
    try {
        run();
        std::cout << "v2 generation base tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
