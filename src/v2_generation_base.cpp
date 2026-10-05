#include "velocita/v2/generation_base.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace velocita::v2 {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'G'}, std::byte{'B'}};
constexpr std::string_view kDigestDomain =
    "velocita.v2.local-generation-base.v1";

void put16(std::span<std::byte> output, std::size_t offset,
           std::uint16_t value) noexcept {
    output[offset] = static_cast<std::byte>(value);
    output[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void put32(std::span<std::byte> output, std::size_t offset,
           std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < 4; ++index)
        output[offset + index] = static_cast<std::byte>(value >> (8U * index));
}

void put64(std::span<std::byte> output, std::size_t offset,
           std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < 8; ++index)
        output[offset + index] = static_cast<std::byte>(value >> (8U * index));
}

std::uint16_t get16(std::span<const std::byte> input,
                    std::size_t offset) noexcept {
    return std::to_integer<std::uint8_t>(input[offset]) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset + 1])) << 8U;
}

std::uint64_t get64(std::span<const std::byte> input,
                    std::size_t offset) noexcept {
    std::uint64_t value{};
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(input[offset + index]))
                 << (8U * index);
    return value;
}

bool digest_equal(std::span<const std::byte> encoded,
                  const Digest256& expected) noexcept {
    std::uint8_t difference{};
    for (std::size_t index = 0; index < expected.bytes.size(); ++index)
        difference |= std::to_integer<std::uint8_t>(encoded[index] ^ expected.bytes[index]);
    return difference == 0;
}

GenerationBaseError validate_dependencies(
    const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog) noexcept {
    if (!identity.valid()) return GenerationBaseError::invalid_identity;
    if (catalog.row_count() == 0 || catalog.root() != identity.value().catalog_root)
        return GenerationBaseError::catalog_mismatch;
    return GenerationBaseError::none;
}

GenerationBaseError summarize_snapshot(
    GenerationBaseKind kind, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, std::span<const std::byte> snapshot,
    GenerationBaseScratch& scratch, GenerationBase& base) noexcept {
    if (kind == GenerationBaseKind::genesis) {
        if (!snapshot.empty()) return GenerationBaseError::invalid_snapshot;
        base.kind = kind;
        return GenerationBaseError::none;
    }
    if (kind != GenerationBaseKind::snapshot)
        return GenerationBaseError::invalid_kind;
    if (snapshot.empty() || snapshot.size() > kMaxBoundSnapshotBytes)
        return GenerationBaseError::invalid_snapshot;

    BoundFrameView view;
    if (decode_bound_frame(snapshot, BoundFrameKind::snapshot, identity,
                           view, scratch.bound) != BoundFrameError::none ||
        view.payload.size() < 16)
        return GenerationBaseError::invalid_snapshot;
    const auto index = get64(view.payload, 0);
    const auto term = get64(view.payload, 8);
    if (index == 0 || index > static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max()) ||
        term == 0 || term > static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max()))
        return GenerationBaseError::invalid_snapshot;
    const auto decoded = decode_state_snapshot(view.payload.subspan(16),
                                               scratch.state, scratch.state_decode);
    if (decoded.error != StateSnapshotError::none)
        return GenerationBaseError::invalid_snapshot;
    if (!execution_catalog_bindings_valid(scratch.state, catalog))
        return GenerationBaseError::catalog_dependency_missing;
    base.kind = kind;
    base.index = index;
    base.term = static_cast<std::int64_t>(term);
    base.state_digest = decoded.state_digest;
    return GenerationBaseError::none;
}

GenerationBaseError canonical_identity(
    const VerifiedStaticIdentity& identity, GenerationBaseScratch& scratch,
    std::size_t& bytes_written) noexcept {
    const auto encoded = encode_static_identity(identity.value(), scratch.identity_bytes);
    if (encoded.error != StaticIdentityCodecError::none)
        return GenerationBaseError::invalid_identity;
    bytes_written = encoded.bytes_written;
    return GenerationBaseError::none;
}

Digest256 calculate_digest(
    std::span<const std::byte> seal_prefix, std::uint64_t generation,
    std::span<const std::byte> identity_bytes,
    std::span<const std::byte> snapshot) noexcept {
    std::array<std::byte, 8> generation_bytes{};
    std::array<std::byte, 4> snapshot_length{};
    put64(generation_bytes, 0, generation);
    put32(snapshot_length, 0, static_cast<std::uint32_t>(snapshot.size()));
    Sha256 digest;
    digest.update(std::as_bytes(std::span{kDigestDomain}));
    digest.update(seal_prefix);
    digest.update(generation_bytes);
    digest.update(identity_bytes);
    digest.update(snapshot_length);
    digest.update(snapshot);
    return digest.finish();
}

}

EncodeGenerationBaseResult encode_generation_base(
    const GenerationBaseKind kind, const std::uint64_t generation,
    const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog,
    const std::span<const std::byte> snapshot,
    const std::span<std::byte> output,
    GenerationBaseScratch& scratch) noexcept {
    const auto dependencies = validate_dependencies(identity, catalog);
    if (dependencies != GenerationBaseError::none) return {dependencies, 0, {}};
    if (generation == 0) return {GenerationBaseError::invalid_generation, 0, {}};
    GenerationBase base;
    const auto snapshot_error =
        summarize_snapshot(kind, identity, catalog, snapshot, scratch, base);
    if (snapshot_error != GenerationBaseError::none)
        return {snapshot_error, 0, {}};
    std::size_t identity_length{};
    const auto identity_error = canonical_identity(identity, scratch, identity_length);
    if (identity_error != GenerationBaseError::none)
        return {identity_error, 0, {}};
    if (output.size() < kGenerationBaseSealBytes)
        return {GenerationBaseError::output_too_small, 0, {}};

    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    std::copy(kMagic.begin(), kMagic.end(), seal.begin());
    put16(seal, 4, kGenerationBaseSchemaVersion);
    seal[6] = static_cast<std::byte>(kind);
    const auto digest = calculate_digest(
        std::span{seal}.first(8), generation,
        std::span{scratch.identity_bytes}.first(identity_length), snapshot);
    std::copy(digest.bytes.begin(), digest.bytes.end(), seal.begin() + 8);
    base.digest = digest;
    std::copy(seal.begin(), seal.end(), output.begin());
    return {GenerationBaseError::none, seal.size(), base};
}

GenerationBaseError decode_generation_base(
    const std::span<const std::byte> input, const std::uint64_t generation,
    const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog,
    const std::span<const std::byte> snapshot,
    GenerationBaseScratch& scratch, GenerationBase& destination) noexcept {
    const auto dependencies = validate_dependencies(identity, catalog);
    if (dependencies != GenerationBaseError::none) return dependencies;
    if (generation == 0) return GenerationBaseError::invalid_generation;
    if (input.size() != kGenerationBaseSealBytes)
        return GenerationBaseError::length_mismatch;
    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    std::copy(input.begin(), input.end(), seal.begin());
    if (!std::equal(kMagic.begin(), kMagic.end(), seal.begin()))
        return GenerationBaseError::invalid_magic;
    if (get16(seal, 4) != kGenerationBaseSchemaVersion)
        return GenerationBaseError::unsupported_schema;
    const auto kind_byte = std::to_integer<std::uint8_t>(seal[6]);
    if (kind_byte > static_cast<std::uint8_t>(GenerationBaseKind::snapshot))
        return GenerationBaseError::invalid_kind;
    if (seal[7] != std::byte{0}) return GenerationBaseError::nonzero_reserved;
    const auto kind = static_cast<GenerationBaseKind>(kind_byte);
    GenerationBase base;
    const auto snapshot_error =
        summarize_snapshot(kind, identity, catalog, snapshot, scratch, base);
    if (snapshot_error != GenerationBaseError::none) return snapshot_error;
    std::size_t identity_length{};
    const auto identity_error = canonical_identity(identity, scratch, identity_length);
    if (identity_error != GenerationBaseError::none) return identity_error;
    const auto digest = calculate_digest(
        std::span{seal}.first(8), generation,
        std::span{scratch.identity_bytes}.first(identity_length), snapshot);
    if (!digest_equal(std::span{seal}.subspan(8, 32), digest))
        return GenerationBaseError::digest_mismatch;
    base.digest = digest;
    destination = base;
    return GenerationBaseError::none;
}

}
