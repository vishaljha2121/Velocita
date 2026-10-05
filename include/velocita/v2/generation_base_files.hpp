// Files that select and hold a storage generation's base.
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

[[nodiscard]] GenerationBaseFileError inspect_selector_temporary_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    SelectorTemporaryStatus& destination) noexcept;

[[nodiscard]] GenerationBaseFileError read_selected_generation_base_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, std::span<std::byte> snapshot_buffer,
    GenerationBaseScratch& scratch, SelectedGenerationBase& destination) noexcept;

[[nodiscard]] GenerationBaseFileError read_generation_base_at(
    int root_fd, const LocalGenerationSelector& candidate,
    const VerifiedStaticIdentity& identity, const VerifiedExecutionCatalog& catalog,
    std::span<std::byte> snapshot_buffer, GenerationBaseScratch& scratch,
    SelectedGenerationBase& destination) noexcept;

}
