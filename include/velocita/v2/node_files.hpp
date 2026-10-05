// Loads a voter's secret and catalog files with ownership checks.
#pragma once

#include "velocita/v2/auth_envelope.hpp"
#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/node.hpp"

#include <filesystem>
#include <optional>

namespace velocita::v2 {

enum class NodeFileError : std::uint8_t {
    none, missing, io_failure, not_regular, wrong_owner, insecure_permissions, wrong_size, invalid_catalog,
};

struct LoadSecretResult {
    NodeFileError error{NodeFileError::none};
    std::optional<AuthSecret> secret;
};

[[nodiscard]] LoadSecretResult load_secret_file(const std::filesystem::path& file) noexcept;

struct LoadSecretSetResult {
    NodeFileError error{NodeFileError::none};
    std::filesystem::path failed_file;
    std::optional<SecretSet> secrets;
};

[[nodiscard]] LoadSecretSetResult load_secret_set(const std::filesystem::path& file) noexcept;

struct LoadCatalogResult {
    NodeFileError error{NodeFileError::none};
    VerifiedExecutionCatalog catalog{};
};

[[nodiscard]] LoadCatalogResult load_catalog_file(const std::filesystem::path& file,
                                                  const Digest256& expected_root) noexcept;

}
