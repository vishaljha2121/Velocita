// Proof that an independent destination holds the same archive as this voter.
#pragma once

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/static_identity.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <span>

namespace velocita::v2 {

class AuthenticatedArchiveTransport;
class ReplicatedKernel;
struct ArchiveStoreConfig;
struct ArchiveDestinationTestAccess;

struct ArchiveDestinationRequest {
    std::int32_t publisher_id{};
    Digest256 publisher_manifest{};
    std::uint64_t catalog_generation{};
    Digest256 catalog_digest{};
    std::uint64_t first_index{};
    std::uint64_t last_index{};
    std::uint64_t publication_storage_generation{};
    std::uint64_t local_storage_generation{};
    std::uint64_t attempt{};
    Digest256 challenge{};
    Digest256 destination_identity{};
    constexpr auto operator<=>(const ArchiveDestinationRequest&) const noexcept = default;
};

enum class ArchiveDestinationError : std::uint8_t {
    none,
    invalid_argument,
    destination_mismatch,
    dependency_mismatch,
    archive_missing,
    archive_invalid,
    request_mismatch,
};

class ArchiveDestinationEvidence final {
public:
    ArchiveDestinationEvidence() noexcept = default;
    ArchiveDestinationEvidence(const ArchiveDestinationEvidence&) = delete;
    ArchiveDestinationEvidence& operator=(const ArchiveDestinationEvidence&) = delete;
    ArchiveDestinationEvidence(ArchiveDestinationEvidence&&) noexcept;
    ArchiveDestinationEvidence& operator=(ArchiveDestinationEvidence&&) noexcept;

    [[nodiscard]] bool verified() const noexcept { return valid_; }
    [[nodiscard]] const ArchiveDestinationRequest& request() const noexcept { return request_; }

private:
    friend class ArchiveDestinationVerifier;
    friend class ArchiveStore;
    friend class ReplicatedKernel;
    friend class AuthenticatedArchiveTransport;
    friend struct ArchiveDestinationTestAccess;
    [[nodiscard]] bool consume(const ArchiveDestinationRequest& expected) noexcept;
    bool valid_{};
    ArchiveDestinationRequest request_{};
};

struct ArchiveDestinationResult {
    ArchiveDestinationError error{ArchiveDestinationError::none};
    ArchiveDestinationEvidence evidence{};
};

class ArchiveDestinationVerifier final {
private:
    friend class AuthenticatedArchiveTransport;
    friend struct ArchiveDestinationTestAccess;
    [[nodiscard]] static ArchiveDestinationResult verify_existing(
        const std::filesystem::path& destination_root,
        const VerifiedStaticIdentity& pinned_destination_identity,
        const VerifiedStaticIdentity& publisher_identity,
        std::span<const std::byte> canonical_publisher_identity_bytes,
        std::span<const std::byte> canonical_execution_catalog_bytes,
        const VerifiedExecutionCatalog& execution_catalog,
        const ArchiveStoreConfig& config,
        const ArchiveDestinationRequest& request) noexcept;
};

}
