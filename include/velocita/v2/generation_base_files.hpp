#pragma once

#include "velocita/v2/generation_base.hpp"
#include "velocita/v2/local_generation.hpp"

namespace velocita::v2 {

enum class GenerationBaseFileError {
    none, invalid_argument, io_failure, invalid_file, invalid_identity,
    invalid_selector, invalid_base, unexpected_contents,
};

struct SelectedGenerationBase {
    LocalGenerationSelector selector{};
    GenerationBase base{};
    std::size_t snapshot_bytes{};
};

// Diagnostics only: a temporary selector never selects or authorizes a base.
// Malformed/foreign files report invalid; inspection I/O errors leave output unchanged.
[[nodiscard]] GenerationBaseFileError inspect_selector_temporary_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    SelectorTemporaryStatus& destination) noexcept;

// Read-only inspection, NOT startup/voting/publication authority. The caller
// retains exclusive trusted-root ownership. No scan/fallback to another generation.
// SNAPSHOT_BUFFER and SCRATCH must not alias any other argument. Buffer/scratch
// are disposable on error; DESTINATION is unchanged. SNAPSHOT_BUFFER must hold
// kMaxBoundSnapshotBytes; on success its first snapshot_bytes are the full file.
// No descriptor is retained. Mutable metadata/WAL/runtime snapshot are NOT checked.
[[nodiscard]] GenerationBaseFileError read_selected_generation_base_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, std::span<std::byte> snapshot_buffer,
    GenerationBaseScratch& scratch, SelectedGenerationBase& destination) noexcept;

// Same read-only validation for a not-yet-selected candidate. Does not read or
// publish the final selector; caller must independently authorize its transition.
[[nodiscard]] GenerationBaseFileError read_generation_base_at(
    int root_fd, const LocalGenerationSelector& candidate,
    const VerifiedStaticIdentity& identity, const VerifiedExecutionCatalog& catalog,
    std::span<std::byte> snapshot_buffer, GenerationBaseScratch& scratch,
    SelectedGenerationBase& destination) noexcept;

} // namespace velocita::v2
