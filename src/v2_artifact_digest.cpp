#include "velocita/v2/artifact_digest.hpp"

#include "velocita/v2/payload.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace velocita::v2 {
namespace {

inline constexpr std::size_t kFeatureSchemaSelfDigestOffset =
    sizeof(std::uint16_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kExecutablePolicySelfDigestOffset =
    sizeof(std::uint16_t) + 2 * sizeof(std::uint64_t) + kDigestBytes +
    sizeof(std::uint64_t) + kDigestBytes;

Digest256 nonzero_validation_digest() noexcept {
    Digest256 value;
    value.bytes[0] = std::byte{1};
    return value;
}

ArtifactDigestResult invalid_contract(const ContractError error) noexcept {
    return {ArtifactDigestError::invalid_contract, error, {}};
}

ArtifactDigestResult digest_preimage(
    const std::string_view domain,
    const std::span<const std::byte> canonical,
    const std::size_t self_digest_offset) {
    std::vector<std::byte> preimage;
    preimage.reserve(domain.size() + canonical.size() - kDigestBytes);
    for (const auto character : domain) {
        preimage.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    preimage.insert(preimage.end(), canonical.begin(),
                    canonical.begin() + self_digest_offset);
    preimage.insert(preimage.end(),
                    canonical.begin() + self_digest_offset + kDigestBytes,
                    canonical.end());
    return {ArtifactDigestError::none, ContractError::none,
            sha256_digest(preimage)};
}

template <typename Artifact>
ArtifactDigestResult verify(
    const Artifact& artifact,
    ArtifactDigestResult (*compute)(const Artifact&)) {
    const auto contract_error = validate(artifact);
    if (contract_error != ContractError::none) {
        return invalid_contract(contract_error);
    }
    auto result = compute(artifact);
    if (result.error != ArtifactDigestError::none) return result;
    if (result.digest != artifact.artifact_digest) {
        result.error = ArtifactDigestError::digest_mismatch;
    }
    return result;
}

}

ArtifactDigestResult
compute_artifact_digest(const FeatureSchemaArtifact& artifact) {
    auto canonical_candidate = artifact;
    canonical_candidate.artifact_digest = nonzero_validation_digest();
    const auto encoded = encode_payload(canonical_candidate);
    if (encoded.error != PayloadError::none) {
        return invalid_contract(encoded.contract_error);
    }
    return digest_preimage(kFeatureSchemaDigestDomain, encoded.bytes,
                           kFeatureSchemaSelfDigestOffset);
}

ArtifactDigestResult
compute_artifact_digest(const ExecutablePolicyArtifact& artifact) {
    auto canonical_candidate = artifact;
    canonical_candidate.artifact_digest = nonzero_validation_digest();
    const auto encoded = encode_payload(canonical_candidate);
    if (encoded.error != PayloadError::none) {
        return invalid_contract(encoded.contract_error);
    }
    return digest_preimage(kExecutablePolicyDigestDomain, encoded.bytes,
                           kExecutablePolicySelfDigestOffset);
}

ArtifactDigestResult
verify_artifact_digest(const FeatureSchemaArtifact& artifact) {
    return verify(artifact,
                  static_cast<ArtifactDigestResult (*)(
                      const FeatureSchemaArtifact&)>(compute_artifact_digest));
}

ArtifactDigestResult
verify_artifact_digest(const ExecutablePolicyArtifact& artifact) {
    return verify(artifact,
                  static_cast<ArtifactDigestResult (*)(
                      const ExecutablePolicyArtifact&)>(compute_artifact_digest));
}

static_assert(kFeatureSchemaSelfDigestOffset == 10);
static_assert(kExecutablePolicySelfDigestOffset == 90);

}
