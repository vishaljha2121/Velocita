#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <type_traits>

namespace velocita::v2 {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'A'}, std::byte{'B'}};
constexpr std::size_t kLengthOffset = 4;
constexpr std::size_t kSchemaOffset = 8;
constexpr std::size_t kIncarnationOffset = 10;
constexpr std::size_t kRequestSequenceOffset = 26;
constexpr std::size_t kEffectIdOffset = 34;
constexpr std::size_t kEffectDigestOffset = 42;
constexpr std::size_t kKillEpochOffset = 74;
constexpr std::size_t kExecutorGenerationOffset = 82;
constexpr std::size_t kCatalogRootOffset = 90;
constexpr std::size_t kAuthorityDomainOffset = 122;
constexpr std::size_t kDigestOffset = 154;

template <typename T>
void write_le(const std::span<std::byte> bytes,
              const std::size_t offset,
              const T value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

template <typename T>
[[nodiscard]] T read_le(const std::span<const std::byte> bytes,
                        const std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<T>);
    T value{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<T>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
[[nodiscard]] bool all_zero(const std::array<std::byte, Size>& bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(),
                       [](const std::byte byte) { return byte == std::byte{0}; });
}

[[nodiscard]] bool valid(const AuthorityBarrier& value) noexcept {
    return !all_zero(value.incarnation) && value.request_sequence != 0 &&
           value.effect_id.value() != 0 &&
           !value.expected_effect_payload_digest.is_zero() &&
           value.expected_kill_epoch.value() != 0 &&
           value.expected_executor_generation.value() != 0 &&
           !value.catalog_root.is_zero() && !value.authority_domain.is_zero();
}

void write_digest(const std::span<std::byte> output,
                  const std::size_t offset,
                  const Digest256& digest) noexcept {
    std::copy(digest.bytes.begin(), digest.bytes.end(),
              output.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] Digest256 read_digest(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
    Digest256 digest;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), kDigestBytes,
                digest.bytes.begin());
    return digest;
}

}

EncodeAuthorityBarrierResult
encode_authority_barrier(const std::int32_t entry_type,
                         const AuthorityBarrier& value,
                         const std::span<std::byte> output) noexcept {
    if (entry_type != kAuthorityBarrierEntryType) {
        return {AuthorityBarrierCodecError::wrong_entry_type, 0, {}};
    }
    if (!valid(value)) {
        return {AuthorityBarrierCodecError::invalid_value, 0, {}};
    }
    if (output.size() < kAuthorityBarrierBytes) {
        return {AuthorityBarrierCodecError::output_too_small, 0, {}};
    }

    std::array<std::byte, kAuthorityBarrierBytes> scratch{};
    const std::span<std::byte> frame{scratch};
    std::copy(kMagic.begin(), kMagic.end(), frame.begin());
    write_le(frame, kLengthOffset,
             static_cast<std::uint32_t>(kAuthorityBarrierBodyBytes));
    write_le(frame, kSchemaOffset, kAuthorityBarrierSchemaVersion);
    std::copy(value.incarnation.begin(), value.incarnation.end(),
              frame.begin() + static_cast<std::ptrdiff_t>(kIncarnationOffset));
    write_le(frame, kRequestSequenceOffset, value.request_sequence);
    write_le(frame, kEffectIdOffset, value.effect_id.value());
    write_digest(frame, kEffectDigestOffset, value.expected_effect_payload_digest);
    write_le(frame, kKillEpochOffset, value.expected_kill_epoch.value());
    write_le(frame, kExecutorGenerationOffset,
             value.expected_executor_generation.value());
    write_digest(frame, kCatalogRootOffset, value.catalog_root);
    write_digest(frame, kAuthorityDomainOffset, value.authority_domain);
    const auto frame_digest = sha256_digest(
        std::span<const std::byte>{scratch}.subspan(kSchemaOffset,
                                                   kDigestOffset - kSchemaOffset));
    write_digest(frame, kDigestOffset, frame_digest);
    std::copy(scratch.begin(), scratch.end(), output.begin());
    return {AuthorityBarrierCodecError::none, kAuthorityBarrierBytes, frame_digest};
}

DecodeAuthorityBarrierResult
decode_authority_barrier(const std::int32_t entry_type,
                         const std::span<const std::byte> bytes,
                         AuthorityBarrier& destination) noexcept {
    if (entry_type != kAuthorityBarrierEntryType) {
        return {AuthorityBarrierCodecError::wrong_entry_type, {}};
    }
    if (bytes.size() < kAuthorityBarrierBytes) {
        return {AuthorityBarrierCodecError::truncated, {}};
    }
    if (bytes.size() != kAuthorityBarrierBytes) {
        return {AuthorityBarrierCodecError::length_mismatch, {}};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return {AuthorityBarrierCodecError::invalid_magic, {}};
    }
    if (read_le<std::uint32_t>(bytes, kLengthOffset) != kAuthorityBarrierBodyBytes) {
        return {AuthorityBarrierCodecError::length_mismatch, {}};
    }
    if (read_le<std::uint16_t>(bytes, kSchemaOffset) !=
        kAuthorityBarrierSchemaVersion) {
        return {AuthorityBarrierCodecError::unsupported_schema, {}};
    }
    const auto frame_digest = sha256_digest(
        bytes.subspan(kSchemaOffset, kDigestOffset - kSchemaOffset));
    if (!std::equal(frame_digest.bytes.begin(), frame_digest.bytes.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(kDigestOffset))) {
        return {AuthorityBarrierCodecError::digest_mismatch, {}};
    }

    AuthorityBarrier candidate;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(kIncarnationOffset),
                candidate.incarnation.size(), candidate.incarnation.begin());
    candidate.request_sequence =
        read_le<std::uint64_t>(bytes, kRequestSequenceOffset);
    candidate.effect_id = EffectId{read_le<std::uint64_t>(bytes, kEffectIdOffset)};
    candidate.expected_effect_payload_digest =
        read_digest(bytes, kEffectDigestOffset);
    candidate.expected_kill_epoch =
        KillEpoch{read_le<std::uint64_t>(bytes, kKillEpochOffset)};
    candidate.expected_executor_generation = ExecutorGeneration{
        read_le<std::uint64_t>(bytes, kExecutorGenerationOffset)};
    candidate.catalog_root = read_digest(bytes, kCatalogRootOffset);
    candidate.authority_domain = read_digest(bytes, kAuthorityDomainOffset);
    if (!valid(candidate)) {
        return {AuthorityBarrierCodecError::invalid_value, {}};
    }
    destination = candidate;
    return {AuthorityBarrierCodecError::none, frame_digest};
}

}
