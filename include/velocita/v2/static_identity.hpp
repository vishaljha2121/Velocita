// The cluster's immutable identity: incarnation, members, catalog root, deployment mode.
#pragma once

#include "velocita/v2/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kStaticIdentitySchemaVersion = 1;
inline constexpr std::size_t kMaxStaticVoters = 3;
inline constexpr std::size_t kMinStaticIdentityBytes = 116;
inline constexpr std::size_t kMaxStaticIdentityBytes = 124;

enum class DeploymentMode : std::uint8_t {
    laboratory = 1,
    two_host_pilot = 2,
};

struct StaticIdentity {
    std::array<std::byte, 16> incarnation{};
    Digest256 catalog_root{};
    std::uint16_t state_snapshot_schema{12};
    std::uint16_t command_result_schema{12};
    std::uint16_t authority_marker_schema{1};
    std::uint16_t execution_catalog_schema{1};
    std::uint16_t replication_semantics_epoch{2};
    std::uint16_t storage_lineage_epoch{1};
    std::uint8_t quorum_durability_mode{1};
    DeploymentMode deployment_mode{DeploymentMode::laboratory};
    std::uint16_t voter_count{};
    std::array<std::int32_t, kMaxStaticVoters> voters{};
    std::int32_t local_node_id{};

    constexpr auto operator<=>(const StaticIdentity&) const noexcept = default;
};

enum class StaticIdentityCodecError : std::uint8_t {
    none = 0,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    nonzero_reserved,
    length_mismatch,
    digest_mismatch,
    invalid_value,
};

struct EncodeStaticIdentityResult {
    StaticIdentityCodecError error{StaticIdentityCodecError::none};
    std::size_t bytes_written{};
};

[[nodiscard]] EncodeStaticIdentityResult
encode_static_identity(const StaticIdentity& value,
                       std::span<std::byte> output) noexcept;

class VerifiedStaticIdentity final {
public:
    constexpr VerifiedStaticIdentity() noexcept = default;

    [[nodiscard]] static StaticIdentityCodecError
    decode(std::span<const std::byte> bytes,
           VerifiedStaticIdentity& destination) noexcept;

    [[nodiscard]] constexpr const StaticIdentity& value() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr const Digest256& manifest_digest() const noexcept {
        return manifest_digest_;
    }
    [[nodiscard]] constexpr const Digest256& authority_domain() const noexcept {
        return authority_domain_;
    }
    [[nodiscard]] constexpr bool valid() const noexcept { return valid_; }

private:
    StaticIdentity value_{};
    Digest256 manifest_digest_{};
    Digest256 authority_domain_{};
    bool valid_{};
};

static_assert(sizeof(StaticIdentity) <= 96,
              "static identity exceeded its reviewed bounded representation");

}
