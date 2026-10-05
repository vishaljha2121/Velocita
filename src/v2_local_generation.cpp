#include "velocita/v2/local_generation.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <type_traits>

namespace velocita::v2 {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'L'}, std::byte{'G'}};
constexpr std::string_view kDigestDomain =
    "velocita.v2.local-generation-selector.v1";
constexpr std::size_t kDigestOffset = 128;

template <typename T>
void put(std::span<std::byte> output, std::size_t offset, T value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        output[offset + index] =
            static_cast<std::byte>((value >> (8U * index)) & 0xffU);
}

template <typename T>
T get(std::span<const std::byte> input, std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<T>);
    T value{};
    for (std::size_t index = 0; index < sizeof(T); ++index)
        value |= static_cast<T>(std::to_integer<std::uint8_t>(input[offset + index]))
                 << (8U * index);
    return value;
}

bool zero(std::span<const std::byte> bytes) noexcept {
    std::uint8_t combined{};
    for (const auto value : bytes) combined |= std::to_integer<std::uint8_t>(value);
    return combined == 0;
}

bool equal_digest(std::span<const std::byte> bytes,
                  const Digest256& expected) noexcept {
    std::uint8_t difference{};
    for (std::size_t index = 0; index < expected.bytes.size(); ++index)
        difference |= std::to_integer<std::uint8_t>(bytes[index] ^ expected.bytes[index]);
    return difference == 0;
}

Digest256 selector_digest(std::span<const std::byte> prefix) noexcept {
    Sha256 digest;
    digest.update(std::as_bytes(std::span{kDigestDomain}));
    digest.update(prefix);
    return digest.finish();
}

LocalGenerationError validate_value(const LocalGenerationSelector& value) noexcept {
    if (value.generation == 0 || value.selection_sequence == 0 ||
        value.prepared_base_digest.is_zero())
        return LocalGenerationError::invalid_value;
    if (value.selection_sequence == 1)
        return value.previous_generation == 0 ? LocalGenerationError::none
                                              : LocalGenerationError::invalid_value;
    if (value.previous_generation == 0 ||
        value.previous_generation >= value.generation)
        return LocalGenerationError::invalid_value;
    return LocalGenerationError::none;
}

}

EncodeLocalGenerationResult encode_local_generation_selector(
    const LocalGenerationSelector& input, const VerifiedStaticIdentity& identity,
    const std::span<std::byte> output) noexcept {
    if (!identity.valid()) return {LocalGenerationError::invalid_identity, 0};
    const LocalGenerationSelector value = input;
    const Digest256 manifest = identity.manifest_digest();
    const auto local_node_id = identity.value().local_node_id;
    if (local_node_id <= 0) return {LocalGenerationError::invalid_identity, 0};
    const auto validation = validate_value(value);
    if (validation != LocalGenerationError::none) return {validation, 0};
    if (output.size() < kLocalGenerationSelectorBytes)
        return {LocalGenerationError::output_too_small, 0};

    std::array<std::byte, kLocalGenerationSelectorBytes> frame{};
    std::copy(kMagic.begin(), kMagic.end(), frame.begin());
    put<std::uint16_t>(frame, 4, kLocalGenerationSchemaVersion);
    put<std::uint16_t>(frame, 6, static_cast<std::uint16_t>(kDigestOffset));
    put<std::uint32_t>(frame, 8, kLocalGenerationSelectorBytes);
    std::copy(manifest.bytes.begin(), manifest.bytes.end(), frame.begin() + 16);
    put<std::uint32_t>(frame, 48, static_cast<std::uint32_t>(local_node_id));
    put<std::uint64_t>(frame, 56, value.generation);
    put<std::uint64_t>(frame, 64, value.selection_sequence);
    put<std::uint64_t>(frame, 72, value.previous_generation);
    std::copy(value.prepared_base_digest.bytes.begin(),
              value.prepared_base_digest.bytes.end(), frame.begin() + 80);
    const auto digest = selector_digest(std::span{frame}.first(kDigestOffset));
    std::copy(digest.bytes.begin(), digest.bytes.end(), frame.begin() + kDigestOffset);
    std::copy(frame.begin(), frame.end(), output.begin());
    return {LocalGenerationError::none, frame.size()};
}

LocalGenerationError decode_local_generation_selector(
    const std::span<const std::byte> input,
    const VerifiedStaticIdentity& identity,
    LocalGenerationSelector& destination) noexcept {
    if (!identity.valid()) return LocalGenerationError::invalid_identity;
    if (input.size() != kLocalGenerationSelectorBytes)
        return LocalGenerationError::length_mismatch;
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()))
        return LocalGenerationError::invalid_magic;
    if (get<std::uint16_t>(input, 4) != kLocalGenerationSchemaVersion ||
        get<std::uint16_t>(input, 6) != kDigestOffset ||
        get<std::uint32_t>(input, 8) != kLocalGenerationSelectorBytes)
        return LocalGenerationError::unsupported_schema;
    if (get<std::uint32_t>(input, 12) != 0 ||
        get<std::uint32_t>(input, 52) != 0 || !zero(input.subspan(112, 16)))
        return LocalGenerationError::nonzero_reserved;
    const auto expected_digest = selector_digest(input.first(kDigestOffset));
    if (!equal_digest(input.subspan(kDigestOffset, 32), expected_digest))
        return LocalGenerationError::digest_mismatch;
    if (!equal_digest(input.subspan(16, 32), identity.manifest_digest()))
        return LocalGenerationError::foreign_manifest;
    if (get<std::uint32_t>(input, 48) !=
        static_cast<std::uint32_t>(identity.value().local_node_id))
        return LocalGenerationError::wrong_owner;

    LocalGenerationSelector value;
    value.generation = get<std::uint64_t>(input, 56);
    value.selection_sequence = get<std::uint64_t>(input, 64);
    value.previous_generation = get<std::uint64_t>(input, 72);
    std::copy_n(input.begin() + 80, 32, value.prepared_base_digest.bytes.begin());
    const auto validation = validate_value(value);
    if (validation != LocalGenerationError::none) return validation;
    destination = value;
    return LocalGenerationError::none;
}

LocalGenerationError validate_local_generation_transition(
    const LocalGenerationSelector& old_value,
    const LocalGenerationSelector& new_value) noexcept {
    if (validate_value(old_value) != LocalGenerationError::none ||
        validate_value(new_value) != LocalGenerationError::none ||
        old_value.selection_sequence == std::numeric_limits<std::uint64_t>::max() ||
        new_value.selection_sequence != old_value.selection_sequence + 1 ||
        new_value.generation <= old_value.generation ||
        new_value.previous_generation != old_value.generation)
        return LocalGenerationError::invalid_transition;
    return LocalGenerationError::none;
}

}
