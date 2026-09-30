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

// A secret file must be a regular file (never followed through a symlink),
// owned by the effective user, with no group/other permission bits, holding
// exactly kAuthSecretBytes raw bytes. Anything else refuses before reading.
[[nodiscard]] LoadSecretResult load_secret_file(const std::filesystem::path& file) noexcept;

struct LoadSecretSetResult {
    NodeFileError error{NodeFileError::none};
    std::filesystem::path failed_file;   // which file refused, when error != none
    std::optional<SecretSet> secrets;
};

// Rotation layout (M2_SECRETS_ROTATION_REVIEW.md): FILE is the current secret
// (required); FILE.next and FILE.previous are optional. An absent optional
// file is fine; a present but invalid one refuses the WHOLE set, so a bad push
// can never silently drop a secret the other voter still sends with.
[[nodiscard]] LoadSecretSetResult load_secret_set(const std::filesystem::path& file) noexcept;

struct LoadCatalogResult {
    NodeFileError error{NodeFileError::none};
    VerifiedExecutionCatalog catalog{};
};

// The catalog file is public data but must verify against EXPECTED_ROOT, the
// root pinned in the node configuration.
[[nodiscard]] LoadCatalogResult load_catalog_file(const std::filesystem::path& file,
                                                  const Digest256& expected_root) noexcept;

}  // namespace velocita::v2
