#include "velocita/v2/node_config.hpp"
#include "velocita/v2/kernel.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

using Code = NodeConfigErrorCode;

struct Failure {
    NodeConfigError error;
};

[[noreturn]] void fail(Code code, std::size_t line, std::string key = {}) {
    throw Failure{{code, line, std::move(key)}};
}

bool key_char(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'; }
bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7e; }

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

std::optional<std::uint64_t> decimal(std::string_view text, std::uint64_t maximum) noexcept {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (maximum - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

template <std::size_t N>
std::optional<std::array<std::byte, N>> lower_hex(std::string_view text) noexcept {
    if (text.size() != 2 * N) return std::nullopt;
    std::array<std::byte, N> out{};
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < N; ++i) {
        const int high = nibble(text[2 * i]);
        const int low = nibble(text[2 * i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out[i] = static_cast<std::byte>(high * 16 + low);
    }
    return out;
}

bool all_zero(std::span<const std::byte> bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{0}; });
}

std::optional<std::filesystem::path> absolute_path(std::string_view text) {
    if (text.empty() || text.size() > kMaxNodeConfigPathBytes || text.front() != '/') return std::nullopt;
    const std::filesystem::path path{std::string(text)};
    for (const auto& part : path)
        if (part == "..") return std::nullopt;
    return path;
}

std::optional<Endpoint> endpoint(std::string_view text) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const auto host = text.substr(0, colon);
    const auto port = decimal(text.substr(colon + 1), 65535);
    if (!port || *port == 0 || host.empty() || host.size() > 253) return std::nullopt;
    std::size_t label = 0;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') {
            if (label == 0 || host[i - 1] == '-' || host[i - label] == '-') return std::nullopt;
            label = 0;
            continue;
        }
        const char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return std::nullopt;
        if (++label > 63) return std::nullopt;
    }
    return Endpoint{std::string(host), static_cast<std::uint16_t>(*port)};
}

std::uint32_t bounded_u32(std::string_view value, std::uint64_t maximum, std::size_t line) {
    const auto parsed = decimal(value, maximum);
    if (!parsed || *parsed == 0) fail(Code::invalid_value, line);
    return static_cast<std::uint32_t>(*parsed);
}

struct Indexed {
    std::int32_t id{};
    std::string field;
};

std::optional<Indexed> indexed(std::string_view key, std::string_view prefix, std::size_t line) {
    if (!key.starts_with(prefix)) return std::nullopt;
    const auto rest = key.substr(prefix.size());
    const auto dot = rest.find('.');
    if (dot == std::string_view::npos) fail(Code::invalid_key, line, std::string(key));
    const auto id = decimal(rest.substr(0, dot), std::numeric_limits<std::int32_t>::max());
    if (!id || *id == 0) fail(Code::invalid_key, line, std::string(key));
    return Indexed{static_cast<std::int32_t>(*id), std::string(rest.substr(dot + 1))};
}

struct Draft {
    std::map<std::string, std::size_t> seen;
    std::optional<std::int32_t> node_id;
    std::optional<std::filesystem::path> data_root, catalog_file;
    std::optional<NodeStartup> startup;
    std::optional<std::array<std::byte, 16>> incarnation;
    std::optional<Digest256> catalog_root, archive_destination;
    std::optional<DeploymentMode> mode;
    std::optional<Endpoint> ingress;
    std::optional<std::uint64_t> max_wal;
    std::optional<std::uint64_t> gateway_caller;
    std::optional<std::uint64_t> signer_caller;
    std::uint32_t compact_at_entries{2048};
    std::uint32_t ingress_rate_per_second{20};
    std::uint32_t ingress_burst{200};
    OwnerLoopBudgets owner;
    std::map<std::int32_t, std::optional<Endpoint>> voter_endpoints;
    std::map<std::int32_t, std::optional<std::filesystem::path>> voter_keys;
    std::map<std::int32_t, std::optional<std::uint64_t>> caller_ids;
    std::map<std::int32_t, std::optional<std::filesystem::path>> caller_keys;
    std::map<std::int32_t, Endpoint> caller_endpoints;
};

void assign(Draft& d, const std::string& key, std::string_view value, std::size_t line) {
    const auto invalid = [&] { fail(Code::invalid_value, line, key); };
    const auto path = [&] {
        auto parsed = absolute_path(value);
        if (!parsed) invalid();
        return *parsed;
    };
    if (key == "node_id") {
        const auto id = decimal(value, std::numeric_limits<std::int32_t>::max());
        if (!id || *id == 0) invalid();
        d.node_id = static_cast<std::int32_t>(*id);
    } else if (key == "data_root") {
        d.data_root = path();
    } else if (key == "startup") {
        if (value == "bootstrap") d.startup = NodeStartup::bootstrap;
        else if (value == "recover") d.startup = NodeStartup::recover;
        else invalid();
    } else if (key == "cluster.incarnation") {
        const auto bytes = lower_hex<16>(value);
        if (!bytes || all_zero(*bytes)) invalid();
        d.incarnation = *bytes;
    } else if (key == "catalog.root" || key == "archive.destination_identity") {
        const auto bytes = lower_hex<32>(value);
        if (!bytes || all_zero(*bytes)) invalid();
        Digest256 digest{};
        digest.bytes = *bytes;
        (key == "catalog.root" ? d.catalog_root : d.archive_destination) = digest;
    } else if (key == "catalog.file") {
        d.catalog_file = path();
    } else if (key == "deployment_mode") {
        if (value == "laboratory") d.mode = DeploymentMode::laboratory;
        else if (value == "two_host_pilot") d.mode = DeploymentMode::two_host_pilot;
        else invalid();
    } else if (key == "ingress.endpoint") {
        d.ingress = endpoint(value);
        if (!d.ingress) invalid();
    } else if (key == "gateway.caller") {
        const auto id = decimal(value, std::numeric_limits<std::uint64_t>::max());
        if (!id || *id == 0) invalid();
        d.gateway_caller = *id;
    } else if (key == "signer.caller") {
        const auto id = decimal(value, std::numeric_limits<std::uint64_t>::max());
        if (!id || *id == 0) invalid();
        d.signer_caller = *id;
    } else if (key == "history.max_wal_bytes") {
        const auto bytes = decimal(value, 1ULL << 30);
        if (!bytes || *bytes == 0) invalid();
        d.max_wal = *bytes;
    } else if (key == "history.compact_at_entries") {
        const auto entries = bounded_u32(value, 8000, line);
        if (entries < 16) invalid();
        d.compact_at_entries = entries;
    } else if (key == "ingress.rate_per_second") {
        d.ingress_rate_per_second = bounded_u32(value, 1000, line);
    } else if (key == "ingress.burst") {
        d.ingress_burst = bounded_u32(value, 10000, line);
    } else if (key == "owner.tick_interval_ms") {
        d.owner.tick_interval_ms = bounded_u32(value, 1000, line);
    } else if (key == "owner.peer_fragment_limit") {
        d.owner.peer_fragment_limit = bounded_u32(value, 1024, line);
    } else if (key == "owner.client_fragment_limit") {
        d.owner.client_fragment_limit = bounded_u32(value, 1024, line);
    } else if (const auto voter = indexed(key, "voter.", line)) {
        if (voter->field == "endpoint") {
            auto parsed = endpoint(value);
            if (!parsed) invalid();
            d.voter_endpoints[voter->id] = std::move(parsed);
        } else if (voter->field == "key_file") {
            d.voter_keys[voter->id] = path();
        } else {
            fail(Code::unknown_key, line, key);
        }
    } else if (const auto caller = indexed(key, "caller.", line)) {
        if (caller->field == "caller_id") {
            const auto id = decimal(value, std::numeric_limits<std::uint64_t>::max());
            if (!id || *id == 0) invalid();
            d.caller_ids[caller->id] = *id;
        } else if (caller->field == "key_file") {
            d.caller_keys[caller->id] = path();
        } else if (caller->field == "response_endpoint") {
            const auto parsed = endpoint(value);
            if (!parsed) invalid();
            d.caller_endpoints[caller->id] = *parsed;
        } else {
            fail(Code::unknown_key, line, key);
        }
    } else {
        fail(Code::unknown_key, line, key);
    }
}

NodeConfig finish(Draft& d, std::string_view text) {
    const auto missing = [](const std::string& key) { fail(Code::missing_key, 0, key); };
    if (!d.node_id) missing("node_id");
    if (!d.data_root) missing("data_root");
    if (!d.startup) missing("startup");
    if (!d.incarnation) missing("cluster.incarnation");
    if (!d.catalog_root) missing("catalog.root");
    if (!d.catalog_file) missing("catalog.file");
    if (!d.mode) missing("deployment_mode");
    if (!d.ingress) missing("ingress.endpoint");
    if (!d.max_wal) missing("history.max_wal_bytes");
    if (d.voter_endpoints.empty()) missing("voter." + std::to_string(*d.node_id) + ".endpoint");
    if (!d.voter_endpoints.contains(*d.node_id)) fail(Code::inconsistent, 0, "node_id");
    for (const auto& [id, key] : d.voter_keys)
        if (!d.voter_endpoints.contains(id)) missing("voter." + std::to_string(id) + ".endpoint");
    for (const auto& [id, value] : d.voter_endpoints)
        if (id != *d.node_id && !d.voter_keys.contains(id)) missing("voter." + std::to_string(id) + ".key_file");
    for (const auto& [id, value] : d.caller_ids)
        if (!d.caller_keys.contains(id)) missing("caller." + std::to_string(id) + ".key_file");
    for (const auto& [id, value] : d.caller_keys)
        if (!d.caller_ids.contains(id)) missing("caller." + std::to_string(id) + ".caller_id");
    for (const auto& [id, value] : d.caller_ids)
        if (!d.caller_endpoints.contains(id)) missing("caller." + std::to_string(id) + ".response_endpoint");
    for (const auto& [id, value] : d.caller_endpoints)
        if (!d.caller_ids.contains(id)) missing("caller." + std::to_string(id) + ".caller_id");
    if (*d.mode == DeploymentMode::two_host_pilot && !d.archive_destination)
        missing("archive.destination_identity");

    const auto inconsistent = [](const std::string& key) { fail(Code::inconsistent, 0, key); };
    if (d.voter_endpoints.size() > kMaxStaticVoters) inconsistent("voter");
    if (d.voter_keys.contains(*d.node_id)) inconsistent("voter." + std::to_string(*d.node_id) + ".key_file");
    if (d.caller_ids.size() > kMaxConfiguredCallers) inconsistent("caller");

    std::set<Endpoint> endpoints;
    NodeConfig config;
    for (const auto& [id, value] : d.voter_endpoints) {
        if (!endpoints.insert(*value).second) inconsistent("voter." + std::to_string(id) + ".endpoint");
        config.voters.push_back({id, *value, id == *d.node_id ? std::nullopt : d.voter_keys.at(id)});
    }
    if (endpoints.contains(*d.ingress)) inconsistent("ingress.endpoint");
    std::set<std::uint64_t> caller_values;
    for (const auto& [id, value] : d.caller_ids) {
        if (!caller_values.insert(*value).second) inconsistent("caller." + std::to_string(id) + ".caller_id");
        const auto& response = d.caller_endpoints.at(id);
        if (endpoints.contains(response) || response == *d.ingress)
            inconsistent("caller." + std::to_string(id) + ".response_endpoint");
        config.callers.push_back({id, *value, *d.caller_keys.at(id), response});
    }

    if (d.gateway_caller &&
        (*d.gateway_caller <= kMaxOperatorCallerId || !caller_values.contains(*d.gateway_caller)))
        inconsistent("gateway.caller");
    config.gateway_caller = d.gateway_caller;
    if (d.signer_caller && (*d.signer_caller <= kMaxOperatorCallerId ||
        !caller_values.contains(*d.signer_caller) || d.signer_caller == d.gateway_caller))
        inconsistent("signer.caller");
    config.signer_caller = d.signer_caller;

    config.node_id = *d.node_id;
    config.data_root = *d.data_root;
    config.startup = *d.startup;
    config.incarnation = *d.incarnation;
    config.catalog_root = *d.catalog_root;
    config.catalog_file = *d.catalog_file;
    config.deployment_mode = *d.mode;
    config.ingress_endpoint = *d.ingress;
    config.archive_destination_identity = d.archive_destination;
    config.max_history_wal_bytes = *d.max_wal;
    config.compact_at_entries = d.compact_at_entries;
    config.ingress_rate_per_second = d.ingress_rate_per_second;
    config.ingress_burst = d.ingress_burst;
    config.owner = d.owner;
    config.digest = sha256_digest(std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()));
    return config;
}

}

StaticIdentity NodeConfig::static_identity() const {
    StaticIdentity identity;
    identity.incarnation = incarnation;
    identity.catalog_root = catalog_root;
    identity.deployment_mode = deployment_mode;
    identity.voter_count = static_cast<std::uint16_t>(voters.size());
    for (std::size_t i = 0; i < voters.size() && i < identity.voters.size(); ++i)
        identity.voters[i] = voters[i].id;
    identity.local_node_id = node_id;
    return identity;
}

ParseNodeConfigResult parse_node_config(std::string_view text) {
    const auto original = text;
    try {
        if (text.size() > kMaxNodeConfigBytes) fail(Code::too_large, 0);
        if (!text.empty() && text.back() != '\n') fail(Code::invalid_line, 0);
        Draft draft;
        std::size_t line_number = 0;
        while (!text.empty()) {
            ++line_number;
            const auto end = text.find('\n');
            const auto line = text.substr(0, end);
            text.remove_prefix(end + 1);
            if (line.size() > kMaxNodeConfigLineBytes ||
                !std::all_of(line.begin(), line.end(), printable))
                fail(Code::invalid_line, line_number);
            const auto content = trim(line);
            if (content.empty() || content.front() == '#') continue;
            const auto equals = content.find('=');
            if (equals == std::string_view::npos) fail(Code::invalid_line, line_number);
            const std::string key(trim(content.substr(0, equals)));
            const auto value = trim(content.substr(equals + 1));
            if (key.empty() || !std::all_of(key.begin(), key.end(), key_char))
                fail(Code::invalid_key, line_number, key);
            if (!draft.seen.emplace(key, line_number).second) fail(Code::duplicate_key, line_number, key);
            if (value.empty() || value.find('#') != std::string_view::npos)
                fail(Code::invalid_value, line_number, key);
            assign(draft, key, value, line_number);
        }
        return {{}, finish(draft, original)};
    } catch (const Failure& failure) {
        return {failure.error, std::nullopt};
    }
}

ParseNodeConfigResult load_node_config(const std::filesystem::path& file) {
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return {{Code::io_failure, 0, {}}, std::nullopt};
    std::string bytes(kMaxNodeConfigBytes + 1, '\0');
    std::size_t length = 0;
    bool ok = true;
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) ok = false;
    while (ok && length < bytes.size()) {
        const auto got = ::read(fd, bytes.data() + length, bytes.size() - length);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) ok = false;
        if (got <= 0) break;
        length += static_cast<std::size_t>(got);
    }
    ::close(fd);
    if (!ok) return {{Code::io_failure, 0, {}}, std::nullopt};
    bytes.resize(length);
    return parse_node_config(bytes);
}

}
