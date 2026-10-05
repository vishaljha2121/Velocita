#include "velocita/v2/archive_destination.hpp"

#include "velocita/v2/archive_store.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>

namespace velocita::v2 {
namespace {

Digest256 identity_digest(const VerifiedStaticIdentity& identity) noexcept {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(identity.value(), bytes);
    if (encoded.error != StaticIdentityCodecError::none) return {};
    return sha256_digest(std::span{bytes}.first(encoded.bytes_written));
}

}

ArchiveDestinationEvidence::ArchiveDestinationEvidence(
    ArchiveDestinationEvidence&& other) noexcept
    : valid_(other.valid_), request_(other.request_) {
    other.valid_ = false;
}

ArchiveDestinationEvidence& ArchiveDestinationEvidence::operator=(
    ArchiveDestinationEvidence&& other) noexcept {
    if (this != &other) {
        valid_ = other.valid_;
        request_ = other.request_;
        other.valid_ = false;
    }
    return *this;
}

bool ArchiveDestinationEvidence::consume(
    const ArchiveDestinationRequest& expected) noexcept {
    const bool matches = valid_ && request_ == expected;
    valid_ = false;
    return matches;
}

ArchiveDestinationResult ArchiveDestinationVerifier::verify_existing(
    const std::filesystem::path& destination_root,
    const VerifiedStaticIdentity& pinned_destination_identity,
    const VerifiedStaticIdentity& publisher_identity,
    const std::span<const std::byte> canonical_publisher_identity_bytes,
    const std::span<const std::byte> canonical_execution_catalog_bytes,
    const VerifiedExecutionCatalog& execution_catalog,
    const ArchiveStoreConfig& config,
    const ArchiveDestinationRequest& request) noexcept {
    ArchiveDestinationResult result;
    try {
        if (destination_root.empty() || !pinned_destination_identity.valid() ||
            !publisher_identity.valid() || request.publisher_id <= 0 ||
            request.catalog_generation == 0 || request.first_index != 1 ||
            request.last_index == 0 || request.publication_storage_generation == 0 ||
            request.local_storage_generation == 0 || request.attempt == 0 ||
            std::ranges::all_of(request.challenge.bytes,
                                [](const std::byte byte) { return byte == std::byte{}; })) {
            result.error = ArchiveDestinationError::invalid_argument;
            return result;
        }
        const auto& destination = pinned_destination_identity.value();
        const auto& publisher = publisher_identity.value();
        const auto destination_is_voter = std::find(
            publisher.voters.begin(),
            publisher.voters.begin() + publisher.voter_count,
            destination.local_node_id) !=
            publisher.voters.begin() + publisher.voter_count;
        if (publisher.deployment_mode != DeploymentMode::two_host_pilot ||
            destination.deployment_mode != DeploymentMode::two_host_pilot ||
            destination.local_node_id == publisher.local_node_id ||
            publisher_identity.manifest_digest() !=
                pinned_destination_identity.manifest_digest() ||
            !destination_is_voter ||
            request.local_storage_generation <
                request.publication_storage_generation) {
            result.error = ArchiveDestinationError::destination_mismatch;
            return result;
        }
        if (request.destination_identity !=
            identity_digest(pinned_destination_identity)) {
            result.error = ArchiveDestinationError::destination_mismatch;
            return result;
        }
        if (request.publisher_id != publisher_identity.value().local_node_id ||
            request.publisher_manifest != publisher_identity.manifest_digest()) {
            result.error = ArchiveDestinationError::request_mismatch;
            return result;
        }
        bool present{};
        const auto verified = ArchiveStore::verify_existing(
            destination_root, publisher_identity,
            canonical_publisher_identity_bytes,
            canonical_execution_catalog_bytes, execution_catalog, config, present);
        if (verified != ArchiveStoreError::none) {
            result.error = verified == ArchiveStoreError::dependency_failed
                ? ArchiveDestinationError::dependency_mismatch
                : ArchiveDestinationError::archive_invalid;
            return result;
        }
        if (!present) {
            result.error = ArchiveDestinationError::archive_missing;
            return result;
        }
        ArchiveStore store;
        const auto opened = ArchiveStore::open(
            destination_root, publisher_identity,
            canonical_publisher_identity_bytes,
            canonical_execution_catalog_bytes, execution_catalog, config, store);
        if (opened != ArchiveStoreError::none) {
            result.error = opened == ArchiveStoreError::dependency_failed
                ? ArchiveDestinationError::dependency_mismatch
                : ArchiveDestinationError::archive_invalid;
            return result;
        }
        if (store.catalog_generation() != request.catalog_generation ||
            store.catalog_digest() != request.catalog_digest ||
            store.covered_through() != request.last_index ||
            store.publication_storage_generation() !=
                request.publication_storage_generation) {
            result.error = ArchiveDestinationError::request_mismatch;
            return result;
        }
        result.evidence.request_ = request;
        result.evidence.valid_ = true;
        return result;
    } catch (...) {
        result.error = ArchiveDestinationError::archive_invalid;
        return result;
    }
}

}
