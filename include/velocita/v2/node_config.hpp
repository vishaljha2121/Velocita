// Parses and validates a voter's configuration file.
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

inline constexpr std::size_t kMaxNodeConfigBytes = 16 * 1024;
inline constexpr std::size_t kMaxNodeConfigLineBytes = 512;
inline constexpr std::size_t kMaxNodeConfigPathBytes = 1024;
inline constexpr std::size_t kMaxConfiguredCallers = 16;

enum class NodeStartup : std::uint8_t { bootstrap, recover };

struct Endpoint {
    std::string host;
    std::uint16_t port{};
    auto operator<=>(const Endpoint&) const = default;
};

struct VoterConfig {
    std::int32_t id{};
    Endpoint endpoint;
    std::optional<std::filesystem::path> key_file;
};

struct CallerConfig {
    std::int32_t key_id{};
    std::uint64_t caller_id{};
    std::filesystem::path key_file;
    Endpoint response_endpoint;
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
    std::vector<VoterConfig> voters;
    Endpoint ingress_endpoint;
    std::vector<CallerConfig> callers;
    std::optional<std::uint64_t> gateway_caller;
    std::optional<std::uint64_t> signer_caller;
    std::optional<Digest256> archive_destination_identity;
    std::uint64_t max_history_wal_bytes{};
    std::uint32_t compact_at_entries{2048};
    std::uint32_t ingress_rate_per_second{20};
    std::uint32_t ingress_burst{200};
    OwnerLoopBudgets owner;
    Digest256 digest{};

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
    std::size_t line{};
    std::string key;
};

struct ParseNodeConfigResult {
    NodeConfigError error;
    std::optional<NodeConfig> config;
};

[[nodiscard]] ParseNodeConfigResult parse_node_config(std::string_view text);

[[nodiscard]] ParseNodeConfigResult load_node_config(const std::filesystem::path& file);

}
