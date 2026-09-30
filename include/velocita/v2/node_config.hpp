#pragma once

#include "velocita/v2/digest.hpp"
#include "velocita/v2/static_identity.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace velocita::v2 {

// Owner-approved node configuration grammar (M2_NODE_PROCESS_REVIEW.md
// decision E): `key = value` lines, `#` comment lines, LF endings, no inline
// comments. Unknown, duplicate or missing keys refuse. Local file only; never
// replicated. The digest of the exact bytes is reported for audit.
inline constexpr std::size_t kMaxNodeConfigBytes = 16 * 1024;
inline constexpr std::size_t kMaxNodeConfigLineBytes = 512;
inline constexpr std::size_t kMaxNodeConfigPathBytes = 1024;
inline constexpr std::size_t kMaxConfiguredCallers = 16;

enum class NodeStartup : std::uint8_t { bootstrap, recover };

struct Endpoint {
    std::string host;   // dotted IPv4 or lowercase hostname
    std::uint16_t port{};
    auto operator<=>(const Endpoint&) const = default;
};

struct VoterConfig {
    std::int32_t id{};
    Endpoint endpoint;
    std::optional<std::filesystem::path> key_file;  // required for peers, absent for self
};

struct CallerConfig {
    std::int32_t key_id{};
    std::uint64_t caller_id{};
    std::filesystem::path key_file;
    Endpoint response_endpoint;  // UDP has no reply address; responses go here
};

struct OwnerLoopBudgets {
    std::uint32_t tick_interval_ms{1};
    std::uint32_t peer_fragment_limit{64};
    std::uint32_t client_fragment_limit{1};
};

struct NodeConfig {
    std::int32_t node_id{};
    std::filesystem::path data_root;
    NodeStartup startup{NodeStartup::recover};
    std::array<std::byte, 16> incarnation{};
    Digest256 catalog_root{};
    std::filesystem::path catalog_file;
    DeploymentMode deployment_mode{DeploymentMode::laboratory};
    std::vector<VoterConfig> voters;        // ascending id, includes self
    Endpoint ingress_endpoint;
    std::vector<CallerConfig> callers;      // ascending key id
    // `gateway.caller`: the only CallerId whose gateway requests (state read,
    // authority barrier) this node serves. Must be a configured caller outside
    // the operator band. Absent: every gateway request is refused.
    std::optional<std::uint64_t> gateway_caller;
    std::optional<Digest256> archive_destination_identity;
    std::uint64_t max_history_wal_bytes{};
    // Retained log entries at which the node compacts (below the floor only).
    std::uint32_t compact_at_entries{2048};
    OwnerLoopBudgets owner;
    Digest256 digest{};                     // SHA-256 of the exact file bytes

    // The expected identity both bootstrap and recover must match exactly.
    [[nodiscard]] StaticIdentity static_identity() const;
};

enum class NodeConfigErrorCode : std::uint8_t {
    none,
    too_large,
    invalid_line,
    invalid_key,
    unknown_key,
    duplicate_key,
    missing_key,
    invalid_value,
    inconsistent,
    io_failure,
};

struct NodeConfigError {
    NodeConfigErrorCode code{NodeConfigErrorCode::none};
    std::size_t line{};   // 1-based; 0 when the error is not tied to one line
    std::string key;
};

struct ParseNodeConfigResult {
    NodeConfigError error;
    std::optional<NodeConfig> config;  // present only when error.code == none
};

[[nodiscard]] ParseNodeConfigResult parse_node_config(std::string_view text);

// Reads at most kMaxNodeConfigBytes + 1 bytes without following a symlink.
[[nodiscard]] ParseNodeConfigResult load_node_config(const std::filesystem::path& file);

}  // namespace velocita::v2
