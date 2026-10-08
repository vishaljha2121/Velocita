#include "velocita/v2/static_identity.hpp"

#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/kernel_codec.hpp"
#include "velocita/v2/state_snapshot.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>

namespace velocita::v2 {
namespace {

static_assert(kStateSnapshotSchemaVersion == 11);
static_assert(kKernelCodecSchemaVersion == 11);
static_assert(kAuthorityBarrierSchemaVersion == 1);
static_assert(kExecutionCatalogSchemaVersion == 1);

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'I'}, std::byte{'D'}};
constexpr std::size_t kLengthOffset = 4;
constexpr std::size_t kFrameSchemaOffset = 8;
constexpr std::size_t kReservedOffset = 10;
constexpr std::size_t kIncarnationOffset = 12;
constexpr std::size_t kCatalogRootOffset = 28;
constexpr std::size_t kIdentityStateSnapshotSchemaOffset = 60;
constexpr std::size_t kCommandResultSchemaOffset = 62;
constexpr std::size_t kAuthorityMarkerSchemaOffset = 64;
constexpr std::size_t kExecutionCatalogSchemaOffset = 66;
constexpr std::size_t kReplicationEpochOffset = 68;
constexpr std::size_t kStorageEpochOffset = 70;
constexpr std::size_t kQuorumModeOffset = 72;
constexpr std::size_t kDeploymentModeOffset = 73;
constexpr std::size_t kVoterCountOffset = 74;
constexpr std::size_t kVotersOffset = 76;
constexpr std::size_t kFixedBytesWithoutVoters = 112;
constexpr std::string_view kManifestDomain =
    "velocita.v2.cluster-manifest.v1";
constexpr std::string_view kAuthorityDomain =
    "velocita.v2.authority-domain.v1";

template <typename T>
void write_le(std::span<std::byte> bytes, std::size_t offset, T value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xFFU);
}

template <typename T>
[[nodiscard]] T read_le(std::span<const std::byte> bytes,
                        std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<T>);
    T value{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(std::to_integer<unsigned char>(bytes[offset + i]))
                 << (i * 8U);
    return value;
}

void write_i32(std::span<std::byte> bytes, std::size_t offset,
               std::int32_t value) noexcept {
    write_le(bytes, offset, static_cast<std::uint32_t>(value));
}

[[nodiscard]] std::int32_t read_i32(std::span<const std::byte> bytes,
                                    std::size_t offset) noexcept {
    return static_cast<std::int32_t>(read_le<std::uint32_t>(bytes, offset));
}

void write_digest(std::span<std::byte> bytes, std::size_t offset,
                  const Digest256& digest) noexcept {
    std::copy(digest.bytes.begin(), digest.bytes.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] Digest256 read_digest(std::span<const std::byte> bytes,
                                    std::size_t offset) noexcept {
    Digest256 digest;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                digest.bytes.size(), digest.bytes.begin());
    return digest;
}

template <std::size_t Size>
[[nodiscard]] bool all_zero(const std::array<std::byte, Size>& bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(),
                       [](std::byte value) { return value == std::byte{0}; });
}

[[nodiscard]] StaticIdentityCodecError validate(const StaticIdentity& value) noexcept {
    if (all_zero(value.incarnation) || value.catalog_root.is_zero())
        return StaticIdentityCodecError::invalid_value;
    if (value.state_snapshot_schema != kStateSnapshotSchemaVersion ||
        value.command_result_schema != kKernelCodecSchemaVersion ||
        value.authority_marker_schema != 1 ||
        value.execution_catalog_schema != 1 ||
        value.replication_semantics_epoch != 1 ||
        value.storage_lineage_epoch != 1)
        return StaticIdentityCodecError::unsupported_schema;
    if (value.quorum_durability_mode != 1 || value.voter_count == 0 ||
        value.voter_count > kMaxStaticVoters)
        return StaticIdentityCodecError::invalid_value;
    if (value.deployment_mode != DeploymentMode::laboratory &&
        value.deployment_mode != DeploymentMode::two_host_pilot)
        return StaticIdentityCodecError::invalid_value;
    if (value.deployment_mode == DeploymentMode::two_host_pilot &&
        value.voter_count != 2)
        return StaticIdentityCodecError::invalid_value;

    bool local_present = false;
    for (std::size_t i = 0; i < value.voters.size(); ++i) {
        if (i < value.voter_count) {
            if (value.voters[i] <= 0 ||
                (i != 0 && value.voters[i - 1] >= value.voters[i]))
                return StaticIdentityCodecError::invalid_value;
            local_present |= value.voters[i] == value.local_node_id;
        } else if (value.voters[i] != 0) {
            return StaticIdentityCodecError::invalid_value;
        }
    }
    return local_present ? StaticIdentityCodecError::none
                         : StaticIdentityCodecError::invalid_value;
}

[[nodiscard]] Digest256 prefixed_digest(
    std::string_view prefix, std::span<const std::byte> payload) noexcept {
    std::array<std::byte, kManifestDomain.size() + 76> scratch{};
    const auto prefix_bytes = std::as_bytes(std::span{prefix});
    std::copy(prefix_bytes.begin(), prefix_bytes.end(), scratch.begin());
    std::copy(payload.begin(), payload.end(),
              scratch.begin() + static_cast<std::ptrdiff_t>(prefix.size()));
    return sha256_digest(std::span<const std::byte>{scratch}.first(
        prefix.size() + payload.size()));
}

}

EncodeStaticIdentityResult
encode_static_identity(const StaticIdentity& value,
                       std::span<std::byte> output) noexcept {
    const auto validation = validate(value);
    if (validation != StaticIdentityCodecError::none) return {validation, 0};
    const auto frame_size = kFixedBytesWithoutVoters + 4U * value.voter_count;
    if (output.size() < frame_size)
        return {StaticIdentityCodecError::output_too_small, 0};

    std::array<std::byte, kMaxStaticIdentityBytes> scratch{};
    const auto frame = std::span<std::byte>{scratch}.first(frame_size);
    std::copy(kMagic.begin(), kMagic.end(), frame.begin());
    write_le(frame, kLengthOffset, static_cast<std::uint32_t>(frame_size));
    write_le(frame, kFrameSchemaOffset, kStaticIdentitySchemaVersion);
    std::copy(value.incarnation.begin(), value.incarnation.end(),
              frame.begin() + static_cast<std::ptrdiff_t>(kIncarnationOffset));
    write_digest(frame, kCatalogRootOffset, value.catalog_root);
    write_le(frame, kIdentityStateSnapshotSchemaOffset,
             value.state_snapshot_schema);
    write_le(frame, kCommandResultSchemaOffset, value.command_result_schema);
    write_le(frame, kAuthorityMarkerSchemaOffset, value.authority_marker_schema);
    write_le(frame, kExecutionCatalogSchemaOffset, value.execution_catalog_schema);
    write_le(frame, kReplicationEpochOffset, value.replication_semantics_epoch);
    write_le(frame, kStorageEpochOffset, value.storage_lineage_epoch);
    frame[kQuorumModeOffset] = static_cast<std::byte>(value.quorum_durability_mode);
    frame[kDeploymentModeOffset] = static_cast<std::byte>(value.deployment_mode);
    write_le(frame, kVoterCountOffset, value.voter_count);
    for (std::size_t i = 0; i < value.voter_count; ++i)
        write_i32(frame, kVotersOffset + 4 * i, value.voters[i]);
    const auto local_offset = kVotersOffset + 4U * value.voter_count;
    write_i32(frame, local_offset, value.local_node_id);
    write_digest(frame, local_offset + 4, sha256_digest(frame.first(local_offset + 4)));
    std::copy(frame.begin(), frame.end(), output.begin());
    return {StaticIdentityCodecError::none, frame_size};
}

StaticIdentityCodecError VerifiedStaticIdentity::decode(
    std::span<const std::byte> bytes,
    VerifiedStaticIdentity& destination) noexcept {
    if (bytes.size() < kMinStaticIdentityBytes)
        return StaticIdentityCodecError::truncated;
    if (bytes.size() > kMaxStaticIdentityBytes)
        return StaticIdentityCodecError::length_mismatch;
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
        return StaticIdentityCodecError::invalid_magic;
    if (read_le<std::uint32_t>(bytes, kLengthOffset) != bytes.size())
        return StaticIdentityCodecError::length_mismatch;
    if (read_le<std::uint16_t>(bytes, kFrameSchemaOffset) !=
        kStaticIdentitySchemaVersion)
        return StaticIdentityCodecError::unsupported_schema;
    if (read_le<std::uint16_t>(bytes, kReservedOffset) != 0)
        return StaticIdentityCodecError::nonzero_reserved;

    const auto voter_count = read_le<std::uint16_t>(bytes, kVoterCountOffset);
    if (voter_count == 0 || voter_count > kMaxStaticVoters)
        return StaticIdentityCodecError::invalid_value;
    const auto expected_size = kFixedBytesWithoutVoters + 4U * voter_count;
    if (bytes.size() != expected_size)
        return StaticIdentityCodecError::length_mismatch;
    const auto checksum_offset = expected_size - kDigestBytes;
    if (sha256_digest(bytes.first(checksum_offset)) !=
        read_digest(bytes, checksum_offset))
        return StaticIdentityCodecError::digest_mismatch;

    StaticIdentity candidate;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(kIncarnationOffset),
                candidate.incarnation.size(), candidate.incarnation.begin());
    candidate.catalog_root = read_digest(bytes, kCatalogRootOffset);
    candidate.state_snapshot_schema =
        read_le<std::uint16_t>(bytes, kIdentityStateSnapshotSchemaOffset);
    candidate.command_result_schema =
        read_le<std::uint16_t>(bytes, kCommandResultSchemaOffset);
    candidate.authority_marker_schema =
        read_le<std::uint16_t>(bytes, kAuthorityMarkerSchemaOffset);
    candidate.execution_catalog_schema =
        read_le<std::uint16_t>(bytes, kExecutionCatalogSchemaOffset);
    candidate.replication_semantics_epoch =
        read_le<std::uint16_t>(bytes, kReplicationEpochOffset);
    candidate.storage_lineage_epoch =
        read_le<std::uint16_t>(bytes, kStorageEpochOffset);
    candidate.quorum_durability_mode =
        std::to_integer<std::uint8_t>(bytes[kQuorumModeOffset]);
    candidate.deployment_mode = static_cast<DeploymentMode>(
        std::to_integer<std::uint8_t>(bytes[kDeploymentModeOffset]));
    candidate.voter_count = voter_count;
    for (std::size_t i = 0; i < voter_count; ++i)
        candidate.voters[i] = read_i32(bytes, kVotersOffset + 4 * i);
    candidate.local_node_id = read_i32(bytes, kVotersOffset + 4U * voter_count);
    const auto validation = validate(candidate);
    if (validation != StaticIdentityCodecError::none) return validation;

    const auto shared = bytes.subspan(kIncarnationOffset,
                                      kVotersOffset + 4U * voter_count -
                                          kIncarnationOffset);
    VerifiedStaticIdentity verified;
    verified.value_ = candidate;
    verified.manifest_digest_ = prefixed_digest(kManifestDomain, shared);
    verified.authority_domain_ = prefixed_digest(
        kAuthorityDomain, verified.manifest_digest_.bytes);
    verified.valid_ = true;
    destination = verified;
    return StaticIdentityCodecError::none;
}

}
