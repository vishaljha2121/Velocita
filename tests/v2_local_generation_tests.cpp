#include "velocita/v2/local_generation.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

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

Digest256 digest(std::uint8_t marker) {
    Digest256 value{};
    value.bytes[0] = static_cast<std::byte>(marker);
    value.bytes[31] = std::byte{0xa5};
    return value;
}

VerifiedStaticIdentity identity(std::int32_t local = 1) {
    StaticIdentity value;
    value.incarnation[0] = std::byte{1};
    value.catalog_root = digest(2);
    value.voter_count = 2;
    value.voters[0] = 1;
    value.voters[1] = 2;
    value.local_node_id = local;
    value.deployment_mode = DeploymentMode::two_host_pilot;
    std::array<std::byte, kMaxStaticIdentityBytes> storage{};
    const auto encoded = encode_static_identity(value, storage);
    require(encoded.error == StaticIdentityCodecError::none, "identity encode failed");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(
                std::span{storage}.first(encoded.bytes_written), result) ==
                StaticIdentityCodecError::none, "identity decode failed");
    return result;
}

void run() {
    const auto expected = identity();
    const LocalGenerationSelector value{9, 4, 7, digest(3)};
    std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
    allocation_probe::enabled = true;
    const auto encoded = encode_local_generation_selector(value, expected, bytes);
    allocation_probe::enabled = false;
    require(encoded.error == LocalGenerationError::none,
            "RED: approved local generation selector did not encode");
    require(encoded.bytes_written == bytes.size(), "selector length mismatch");
    require(allocation_probe::count == 0, "selector encode allocated");
    require(bytes[0] == std::byte{'V'} && bytes[1] == std::byte{'2'} &&
                bytes[2] == std::byte{'L'} && bytes[3] == std::byte{'G'},
            "selector magic mismatch");
    require(bytes[4] == std::byte{1} && bytes[6] == std::byte{128} &&
                bytes[8] == std::byte{160} && bytes[56] == std::byte{9} &&
                bytes[64] == std::byte{4} && bytes[72] == std::byte{7},
            "selector fixed offsets mismatch");

    LocalGenerationSelector decoded{};
    require(decode_local_generation_selector(bytes, expected, decoded) ==
                LocalGenerationError::none && decoded == value,
            "selector round trip failed");
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    LocalGenerationSelector noalloc_decoded{};
    const auto noalloc_error =
        decode_local_generation_selector(bytes, expected, noalloc_decoded);
    allocation_probe::enabled = false;
    require(noalloc_error == LocalGenerationError::none &&
                noalloc_decoded == value && allocation_probe::count == 0,
            "selector decode allocated or diverged");

    std::array<std::byte, kLocalGenerationSelectorBytes> unchanged;
    unchanged.fill(std::byte{0x6d});
    const auto sentinel = unchanged;
    require(encode_local_generation_selector(
                {0, 4, 7, digest(3)}, expected, unchanged).error ==
                LocalGenerationError::invalid_value && unchanged == sentinel,
            "failed encode changed output");
    LocalGenerationSelector destination{81, 82, 80, digest(9)};
    const auto destination_sentinel = destination;
    auto corrupted = bytes;
    corrupted[159] ^= std::byte{1};
    require(decode_local_generation_selector(corrupted, expected, destination) ==
                LocalGenerationError::digest_mismatch &&
                destination == destination_sentinel,
            "failed decode changed destination");
    require(decode_local_generation_selector(bytes, identity(2), destination) ==
                LocalGenerationError::wrong_owner,
            "foreign local owner accepted");
    VerifiedStaticIdentity invalid_identity;
    require(encode_local_generation_selector(value, invalid_identity, unchanged).error ==
                LocalGenerationError::invalid_identity && unchanged == sentinel,
            "unverified identity accepted by encode");
    require(decode_local_generation_selector(bytes, invalid_identity, destination) ==
                LocalGenerationError::invalid_identity &&
                destination == destination_sentinel,
            "unverified identity accepted by decode");

    alignas(LocalGenerationSelector)
        std::array<std::byte, kLocalGenerationSelectorBytes> overlap{};
    auto* overlapping_value = std::construct_at(
        reinterpret_cast<LocalGenerationSelector*>(overlap.data()), value);
    require(encode_local_generation_selector(*overlapping_value, expected, overlap).error ==
                LocalGenerationError::none,
            "value/output overlap corrupted encode");
    LocalGenerationSelector overlap_decoded{};
    require(decode_local_generation_selector(overlap, expected, overlap_decoded) ==
                LocalGenerationError::none && overlap_decoded == value,
            "overlap encoding did not preserve copied input");

    std::array<std::byte, kLocalGenerationSelectorBytes> maximum_bytes{};
    const LocalGenerationSelector maximum{
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max() - 1, digest(8)};
    require(encode_local_generation_selector(maximum, expected, maximum_bytes).error ==
                LocalGenerationError::none,
            "valid maximum counters refused by codec");
    LocalGenerationSelector maximum_decoded{};
    require(decode_local_generation_selector(maximum_bytes, expected, maximum_decoded) ==
                LocalGenerationError::none && maximum_decoded == maximum,
            "maximum counters did not round trip");
    require(validate_local_generation_transition(
                {7, 3, 5, digest(4)}, value) == LocalGenerationError::none,
            "valid selector transition refused");
    require(validate_local_generation_transition(
                value, {10, 6, 9, digest(5)}) ==
                LocalGenerationError::invalid_transition,
            "sequence gap accepted");
    require(validate_local_generation_transition(
                {9, std::numeric_limits<std::uint64_t>::max(), 7, digest(4)},
                {10, 1, 9, digest(5)}) == LocalGenerationError::invalid_transition,
            "selection sequence wrap accepted");
    require(validate_local_generation_transition(
                {9, 4, 7, digest(4)}, {10, 5, 8, digest(5)}) ==
                LocalGenerationError::invalid_transition,
            "wrong previous generation accepted");
}

}

int main() {
    try {
        run();
        std::cout << "v2 local generation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
