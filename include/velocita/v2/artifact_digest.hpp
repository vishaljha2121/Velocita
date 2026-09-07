#pragma once

#include "velocita/v2/digest.hpp"
#include "velocita/v2/executable_policy.hpp"

#include <cstdint>
#include <string_view>

namespace velocita::v2 {

inline constexpr std::string_view kFeatureSchemaDigestDomain =
    "velocita.feature-schema.v1";
inline constexpr std::string_view kExecutablePolicyDigestDomain =
    "velocita.executable-policy.v1";

enum class ArtifactDigestError : std::uint8_t {
    none = 0,
    invalid_contract,
    digest_mismatch,
};

struct ArtifactDigestResult {
    ArtifactDigestError error{ArtifactDigestError::none};
    ContractError contract_error{ContractError::none};
    Digest256 digest{};
};

// Computation permits only the artifact's own digest to be zero because it is
// excluded from its preimage. Every other structural and bound-digest contract
// must already be valid.
[[nodiscard]] ArtifactDigestResult
compute_artifact_digest(const FeatureSchemaArtifact& artifact);
[[nodiscard]] ArtifactDigestResult
compute_artifact_digest(const ExecutablePolicyArtifact& artifact);

// Verification first requires the complete structural contract, including a
// nonzero claimed digest, and then performs exact digest comparison.
[[nodiscard]] ArtifactDigestResult
verify_artifact_digest(const FeatureSchemaArtifact& artifact);
[[nodiscard]] ArtifactDigestResult
verify_artifact_digest(const ExecutablePolicyArtifact& artifact);

}  // namespace velocita::v2
