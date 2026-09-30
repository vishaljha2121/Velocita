#pragma once

#include "velocita/v2/archive_catalog.hpp"
#include "velocita/v2/archive_destination.hpp"
#include "velocita/v2/archive_segment.hpp"
#include "velocita/v2/execution_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace velocita::v2 {

class ArchiveDestinationVerifier;

inline constexpr std::uint64_t kDefaultArchiveLogicalByteLimit = 1ULL << 30;
inline constexpr std::size_t kMaxArchiveDirectoryEntries = 4102;

enum class ArchiveStoreError : std::uint8_t {
    none,
    invalid_argument,
    ownership_failed,
    lock_failed,
    unexpected_entry,
    dependency_failed,
    catalog_failed,
    segment_failed,
    capacity_exceeded,
    publication_failed,
    conflict,
    stale_generation,
};

struct ArchiveStoreConfig {
    std::uint64_t logical_byte_limit{kDefaultArchiveLogicalByteLimit};
    std::uint32_t object_limit{kMaxArchiveCatalogRows};
};

struct ArchivePublicationContext {
    std::uint64_t catalog_generation{};
    std::uint64_t storage_generation{};
    std::uint64_t committed_index{};
    std::uint64_t durable_index{};
    std::int64_t current_term{};
    std::int64_t predecessor_term{};
    std::uint32_t remaining_pending_records{};
};

// Process-local, nonserializable evidence. Only ArchiveStore can mint it.
class ArchiveCoverageCapability final {
public:
    ArchiveCoverageCapability() noexcept = default;
    ArchiveCoverageCapability(const ArchiveCoverageCapability&) = delete;
    ArchiveCoverageCapability& operator=(const ArchiveCoverageCapability&) = delete;
    ArchiveCoverageCapability(ArchiveCoverageCapability&&) noexcept;
    ArchiveCoverageCapability& operator=(ArchiveCoverageCapability&&) noexcept;

    [[nodiscard]] std::uint64_t last_index() const noexcept { return last_index_; }
    [[nodiscard]] std::int64_t last_term() const noexcept { return last_term_; }
    [[nodiscard]] std::uint64_t catalog_generation() const noexcept { return catalog_generation_; }
    [[nodiscard]] bool local_publication_durable() const noexcept { return valid_; }

private:
    friend class ArchiveStore;
    bool valid_{};
    std::uint64_t storage_generation_{};
    std::uint64_t catalog_generation_{};
    std::uint64_t last_index_{};
    std::int64_t last_term_{};
    Digest256 manifest_digest_{};
    Digest256 catalog_digest_{};
    Digest256 terminal_record_digest_{};
};

struct ArchivePublishResult {
    ArchiveStoreError error{ArchiveStoreError::none};
    ArchiveCoverageCapability capability{};
};

struct ArchiveWalBridgeResult {
    ArchiveStoreError error{ArchiveStoreError::none};
    std::uint64_t covered_through{};
    std::int64_t last_term{};
    std::uint32_t published_objects{};
    std::uint64_t published_bytes{};
};

// Cold-start/worker-only synchronous filesystem owner. Not owner-thread safe.
class ArchiveStore final {
public:
    ArchiveStore() noexcept;
    ~ArchiveStore();
    ArchiveStore(const ArchiveStore&) = delete;
    ArchiveStore& operator=(const ArchiveStore&) = delete;
    ArchiveStore(ArchiveStore&&) noexcept;
    ArchiveStore& operator=(ArchiveStore&&) noexcept;

    [[nodiscard]] static ArchiveStoreError open(
        const std::filesystem::path& verified_startup_root,
        const VerifiedStaticIdentity& identity,
        std::span<const std::byte> canonical_identity_bytes,
        std::span<const std::byte> canonical_execution_catalog_bytes,
        const VerifiedExecutionCatalog& execution_catalog,
        const ArchiveStoreConfig& config, ArchiveStore& destination) noexcept;

    // Cold startup validation/resync without content mutation. A missing archive is reported as none and
    // leaves PRESENT false; the caller decides whether retained history permits it.
    [[nodiscard]] static ArchiveStoreError verify_existing(
        const std::filesystem::path& verified_startup_root,
        const VerifiedStaticIdentity& identity,
        std::span<const std::byte> canonical_identity_bytes,
        std::span<const std::byte> canonical_execution_catalog_bytes,
        const VerifiedExecutionCatalog& execution_catalog,
        const ArchiveStoreConfig& config, bool& present) noexcept;

    [[nodiscard]] bool consume_for_reclamation(
        ArchiveCoverageCapability&& capability,
        std::uint64_t current_storage_generation,
        std::uint64_t cut) noexcept;

    [[nodiscard]] bool can_admit(std::uint32_t pending_record_count,
                                 bool protected_lane) const noexcept;
    [[nodiscard]] bool can_publish(std::uint32_t candidate_bytes,
                                   std::uint32_t remaining_record_count,
                                   bool protected_lane) const noexcept;

    [[nodiscard]] std::uint64_t covered_through() const noexcept;
    [[nodiscard]] std::int64_t last_term() const noexcept;
    [[nodiscard]] std::uint64_t catalog_generation() const noexcept;
    [[nodiscard]] std::uint64_t publication_storage_generation() const noexcept;

private:
    friend class ReplicatedKernel;
    friend class ArchiveDestinationVerifier;
    friend struct ArchiveStoreTestAccess;
    [[nodiscard]] ArchiveStoreError verify_current() noexcept;
    [[nodiscard]] ArchivePublishResult publish(
        const ArchivePublicationContext& context,
        const ArchiveRecordSource& records) noexcept;
    // Worker-only post-selection bridge. Publishes existing canonical archive
    // segments/catalog rows from the selected generation's WAL without deleting
    // WAL bytes or minting reclamation authority. The entire bridge and suffix
    // reservation are preflighted before the first catalog mutation.
    [[nodiscard]] ArchiveWalBridgeResult publish_wal_bridge(
        const std::filesystem::path& selected_runtime_directory,
        const ArchivePublicationContext& context,
        std::uint64_t snapshot_cut,
        std::uint64_t candidate_wal_byte_cap) noexcept;
    [[nodiscard]] const Digest256& catalog_digest() const noexcept;
    [[nodiscard]] ArchiveDestinationRequest make_destination_request(
        std::uint64_t local_storage_generation,
        std::uint64_t attempt,
        const Digest256& challenge,
        const Digest256& destination_identity) const noexcept;
    [[nodiscard]] bool consume_for_reclamation(
        ArchiveCoverageCapability&& local,
        ArchiveDestinationEvidence&& independent,
        const ArchiveDestinationRequest& expected,
        std::uint64_t current_storage_generation,
        std::uint64_t cut) noexcept;
    [[nodiscard]] ArchiveCoverageCapability coverage_for_reclamation(
        std::uint64_t current_storage_generation, std::uint64_t cut) const noexcept;
    struct State;
    explicit ArchiveStore(std::unique_ptr<State>) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace velocita::v2
