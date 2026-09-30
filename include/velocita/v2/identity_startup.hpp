#pragma once

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/static_identity.hpp"
#include "velocita/v2/local_generation.hpp"

#include <filesystem>
#include <stdexcept>

namespace velocita::v2 {

inline constexpr const char* kStaticIdentityFileName = "v2-identity.bin";
inline constexpr const char* kStaticIdentityTemporaryName = "v2-identity.bin.tmp";

enum class IdentityStartupErrorCode {
    invalid_identity, catalog_mismatch, directory_missing, directory_owned,
    unexpected_contents, identity_mismatch, invalid_file, io_failure,
    generation_layout_unsupported,
};

class IdentityStartupError final : public std::runtime_error {
public:
    IdentityStartupError(IdentityStartupErrorCode code, const char* message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] IdentityStartupErrorCode code() const noexcept { return code_; }
private:
    IdentityStartupErrorCode code_;
};

enum class IdentityStartupCheckpoint {
    parent_synced, partial_write, file_synced, identity_renamed, directory_synced,
    metadata_created, metadata_partial_write, metadata_written, metadata_synced,
    metadata_renamed, metadata_directory_synced, identity_created, identity_written,
    generation_created, base_created, base_sealed, selector_created,
    selector_partial_write, selector_written, selector_synced, selector_renamed,
    selector_directory_synced, selection_verified,
};

struct IdentityStartupCheckpoints {
    void* context{};
    void (*observe)(void*, IdentityStartupCheckpoint){};
    void operator()(IdentityStartupCheckpoint checkpoint) const {
        if (observe) observe(context, checkpoint);
    }
};

// Cold-start ownership, not serving authority. The kernel integration must keep
// this member alive until every store/writer using the directory is destroyed.
class VerifiedIdentityStartup final {
public:
    [[nodiscard]] static VerifiedIdentityStartup bootstrap(
        const std::filesystem::path& directory, const StaticIdentity& expected,
        const VerifiedExecutionCatalog& catalog, IdentityStartupCheckpoints checkpoints = {});
    [[nodiscard]] static VerifiedIdentityStartup recover(
        const std::filesystem::path& directory, const StaticIdentity& expected,
        const VerifiedExecutionCatalog& catalog, IdentityStartupCheckpoints checkpoints = {});

    ~VerifiedIdentityStartup();
    VerifiedIdentityStartup(VerifiedIdentityStartup&& other) noexcept;
    VerifiedIdentityStartup& operator=(VerifiedIdentityStartup&&) = delete;
    VerifiedIdentityStartup(const VerifiedIdentityStartup&) = delete;
    VerifiedIdentityStartup& operator=(const VerifiedIdentityStartup&) = delete;

    [[nodiscard]] bool owns_directory() const noexcept {
        return root_fd_ >= 0 && generations_fd_ >= 0 && directory_fd_ >= 0;
    }
    [[nodiscard]] const std::filesystem::path& root_directory() const noexcept { return root_; }
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    [[nodiscard]] const VerifiedStaticIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const VerifiedExecutionCatalog& catalog() const noexcept { return catalog_; }
    [[nodiscard]] std::uint64_t storage_generation() const noexcept { return selection_.generation; }
    void validate_ownership() const;
    [[nodiscard]] SelectorTemporaryStatus selector_temporary_status() const;

private:
    VerifiedIdentityStartup(std::filesystem::path root, std::filesystem::path directory,
                            VerifiedStaticIdentity identity, VerifiedExecutionCatalog catalog,
                            LocalGenerationSelector selection);
    int root_fd_{-1};
    int generations_fd_{-1};
    int directory_fd_{-1};
    std::filesystem::path root_;
    std::filesystem::path directory_;
    VerifiedStaticIdentity identity_;
    VerifiedExecutionCatalog catalog_;
    LocalGenerationSelector selection_;
};

} // namespace velocita::v2
