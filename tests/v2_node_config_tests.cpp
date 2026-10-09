#include "velocita/v2/node_config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;
using Code = NodeConfigErrorCode;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const std::string kValid =
    "# NUC voter, two-host pilot\n"
    "node_id = 1\n"
    "data_root = /var/lib/velocita/node-1\n"
    "startup = recover\n"
    "cluster.incarnation = ab5a0000000000000000000000000001\n"
    "catalog.root = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n"
    "catalog.file = /etc/velocita/catalog.bin\n"
    "deployment_mode = two_host_pilot\n"
    "voter.1.endpoint = 10.0.0.1:40200\n"
    "voter.2.endpoint = pi.lan:40200\n"
    "voter.2.key_file = /etc/velocita/keys/link-1-2.key\n"
    "ingress.endpoint = 10.0.0.1:40300\n"
    "\n"
    "caller.9.caller_id = 3\n"
    "caller.9.key_file = /etc/velocita/keys/caller-9.key\n"
    "caller.9.response_endpoint = 10.0.0.50:40400\n"
    "caller.4.caller_id = 200\n"
    "caller.4.key_file = /etc/velocita/keys/caller-4.key\n"
    "caller.4.response_endpoint = 10.0.0.51:40400\n"
    "archive.destination_identity = ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\n"
    "history.max_wal_bytes = 1073741824\n"
    "owner.tick_interval_ms = 2\n";

Digest256 digest_of(std::string_view text) {
    return sha256_digest(std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

Digest256 hex_digest(std::string_view hex) {
    auto parsed = parse_digest256(hex);
    require(parsed.has_value(), "fixture digest hex invalid");
    return *parsed;
}

std::string without(std::string text, std::string_view line_prefix) {
    const auto at = text.find(std::string(line_prefix));
    require(at != std::string::npos, "fixture line absent: " + std::string(line_prefix));
    const auto end = text.find('\n', at);
    text.erase(at, end - at + 1);
    return text;
}

std::string replaced(std::string text, std::string_view line_prefix, std::string_view line) {
    const auto at = text.find(std::string(line_prefix));
    require(at != std::string::npos, "fixture line absent: " + std::string(line_prefix));
    const auto end = text.find('\n', at);
    text.replace(at, end - at, std::string(line));
    return text;
}

void expect_error(const std::string& text, Code code, const std::string& what,
                  std::size_t line = 0, std::string_view key = {}) {
    const auto result = parse_node_config(text);
    require(!result.config.has_value(), what + ": refused config still produced a value");
    require(result.error.code == code,
            what + ": wrong error code " + std::to_string(static_cast<int>(result.error.code)));
    if (line != 0) require(result.error.line == line, what + ": wrong line " + std::to_string(result.error.line));
    if (!key.empty()) require(result.error.key == key, what + ": wrong key '" + result.error.key + "'");
}

void valid_config_parses_to_hand_built_values() {
    const auto result = parse_node_config(kValid);
    require(result.error.code == Code::none && result.config.has_value(), "valid config refused");
    const auto& c = *result.config;
    require(c.node_id == 1 && c.data_root == "/var/lib/velocita/node-1" && c.startup == NodeStartup::recover,
            "scalar fields differ");
    std::array<std::byte, 16> incarnation{};
    incarnation[0] = std::byte{0xab};
    incarnation[1] = std::byte{0x5a};
    incarnation[15] = std::byte{0x01};
    require(c.incarnation == incarnation, "incarnation differs");
    require(c.catalog_root == hex_digest("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff") &&
                c.catalog_file == "/etc/velocita/catalog.bin",
            "catalog fields differ");
    require(c.deployment_mode == DeploymentMode::two_host_pilot, "deployment mode differs");
    require(c.voters.size() == 2 && c.voters[0].id == 1 && c.voters[0].endpoint == Endpoint{"10.0.0.1", 40200} &&
                !c.voters[0].key_file && c.voters[1].id == 2 && c.voters[1].endpoint == Endpoint{"pi.lan", 40200} &&
                c.voters[1].key_file == std::filesystem::path("/etc/velocita/keys/link-1-2.key"),
            "voters differ");
    require(c.ingress_endpoint == Endpoint{"10.0.0.1", 40300}, "ingress endpoint differs");
    require(c.callers.size() == 2 && c.callers[0].key_id == 4 && c.callers[0].caller_id == 200 &&
                c.callers[0].key_file == "/etc/velocita/keys/caller-4.key" &&
                c.callers[0].response_endpoint == Endpoint{"10.0.0.51", 40400} && c.callers[1].key_id == 9 &&
                c.callers[1].caller_id == 3 && c.callers[1].key_file == "/etc/velocita/keys/caller-9.key" &&
                c.callers[1].response_endpoint == Endpoint{"10.0.0.50", 40400},
            "callers differ or are not sorted by key id");
    require(c.archive_destination_identity ==
                hex_digest("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"),
            "archive destination differs");
    require(c.max_history_wal_bytes == 1073741824, "WAL budget differs");
    require(c.compact_at_entries == 2048, "compaction threshold default differs");
    const auto tuned = parse_node_config(kValid + "history.compact_at_entries = 64\n");
    require(tuned.config && tuned.config->compact_at_entries == 64, "compaction threshold did not parse");
    require(c.ingress_rate_per_second == 20 && c.ingress_burst == 200, "ingress rate defaults differ");
    const auto paced = parse_node_config(kValid + "ingress.rate_per_second = 6\ningress.burst = 60\n");
    require(paced.config && paced.config->ingress_rate_per_second == 6 && paced.config->ingress_burst == 60,
            "ingress rate did not parse");
    require(parse_node_config(kValid + "ingress.rate_per_second = 1000\ningress.burst = 10000\n").config.has_value(),
            "the largest ingress rate was refused");
    for (const char* bad : {"ingress.rate_per_second = 0\n", "ingress.rate_per_second = 1001\n",
                            "ingress.burst = 0\n", "ingress.burst = 10001\n"})
        require(!parse_node_config(kValid + bad).config, std::string("an ingress rate out of range parsed: ") + bad);
    require(c.owner.tick_interval_ms == 2 && c.owner.peer_fragment_limit == 64 && c.owner.client_fragment_limit == 1,
            "owner budgets or their defaults differ");
    require(c.digest == digest_of(kValid), "config digest is not the digest of the exact bytes");

    StaticIdentity expected;
    expected.incarnation = incarnation;
    expected.catalog_root = c.catalog_root;
    expected.deployment_mode = DeploymentMode::two_host_pilot;
    expected.voter_count = 2;
    expected.voters = {1, 2, 0};
    expected.local_node_id = 1;
    require(c.static_identity() == expected, "static identity differs from the hand-built expectation");
}

void every_required_key_is_required() {
    for (const auto* key : {"node_id", "data_root", "startup", "cluster.incarnation", "catalog.root",
                            "catalog.file", "deployment_mode", "ingress.endpoint", "history.max_wal_bytes"}) {
        expect_error(without(kValid, std::string(key) + " ="), Code::missing_key,
                     std::string("missing ") + key, 0, key);
    }
    expect_error(without(kValid, "voter.2.key_file ="), Code::missing_key, "peer key file", 0, "voter.2.key_file");
    expect_error(without(kValid, "caller.9.key_file ="), Code::missing_key, "caller key file", 0, "caller.9.key_file");
    expect_error(without(kValid, "caller.9.caller_id ="), Code::missing_key, "caller id", 0, "caller.9.caller_id");
    expect_error(without(kValid, "caller.9.response_endpoint ="), Code::missing_key, "caller response endpoint", 0,
                 "caller.9.response_endpoint");
    expect_error(without(kValid, "archive.destination_identity ="), Code::missing_key,
                 "pilot without backup destination", 0, "archive.destination_identity");
    expect_error(without(without(kValid, "voter.1.endpoint ="), "voter.2."), Code::missing_key,
                 "no voters at all", 0, "voter.1.endpoint");
}

void unknown_and_duplicate_keys_refuse() {
    expect_error(kValid + "voter.2.endpiont = pi.lan:40201\n", Code::unknown_key, "misspelt key", 23,
                 "voter.2.endpiont");
    expect_error(kValid + "owner.tick_interval_ms = 3\n", Code::duplicate_key, "duplicate key", 23,
                 "owner.tick_interval_ms");
    expect_error(kValid + "startup_mode = recover\n", Code::unknown_key, "unknown top-level key", 23);
}

void grammar_is_strict() {
    expect_error(replaced(kValid, "node_id =", "node_id = 1\r"), Code::invalid_line, "carriage return", 2);
    expect_error(replaced(kValid, "node_id =", "node_id = 1 # nuc"), Code::invalid_value, "inline comment", 2);
    expect_error(replaced(kValid, "catalog.file =", "catalog.file = /etc/velocita/catalog.bin # primary"),
                 Code::invalid_value, "inline comment inside a path value", 7, "catalog.file");
    expect_error(replaced(kValid, "node_id =", "node_id 1"), Code::invalid_line, "missing equals", 2);
    expect_error(replaced(kValid, "node_id =", "node_id ="), Code::invalid_value, "empty value", 2);
    expect_error(replaced(kValid, "node_id =", "Node_Id = 1"), Code::invalid_key, "uppercase key", 2);
    expect_error(replaced(kValid, "node_id =", "node_id = 1" + std::string(600, ' ')), Code::invalid_line,
                 "overlong line", 2);
    expect_error(replaced(kValid, "node_id =", std::string("node_id = \x01")), Code::invalid_line,
                 "control byte", 2);
    expect_error(replaced(kValid, "node_id =", "node_id = 1\xc3\xa9"), Code::invalid_line, "non-ASCII", 2);
    expect_error(kValid.substr(0, kValid.size() - 1), Code::invalid_line, "missing final newline");
    expect_error(kValid + "#" + std::string(kMaxNodeConfigBytes, 'x') + "\n", Code::too_large, "oversized file");
}

void values_are_strict() {
    const auto value = [](std::string_view key_line, std::string_view line, const char* what) {
        expect_error(replaced(kValid, key_line, line), Code::invalid_value, what);
    };
    value("node_id =", "node_id = 01", "leading zero");
    value("node_id =", "node_id = +1", "explicit sign");
    value("node_id =", "node_id = 0", "zero node id");
    value("node_id =", "node_id = 99999999999", "node id overflow");
    value("startup =", "startup = auto", "startup mode");
    value("deployment_mode =", "deployment_mode = production", "deployment mode");
    value("data_root =", "data_root = var/lib/velocita", "relative path");
    value("data_root =", "data_root = /var/lib/../etc", "dot-dot path component");
    value("cluster.incarnation =", "cluster.incarnation = ab5a", "short incarnation");
    value("cluster.incarnation =", "cluster.incarnation = 00000000000000000000000000000000", "zero incarnation");
    value("cluster.incarnation =", "cluster.incarnation = AB5A0000000000000000000000000001", "uppercase hex");
    value("catalog.root =", "catalog.root = 00", "short catalog root");
    value("ingress.endpoint =", "ingress.endpoint = 10.0.0.1", "endpoint without port");
    value("ingress.endpoint =", "ingress.endpoint = 10.0.0.1:0", "port zero");
    value("ingress.endpoint =", "ingress.endpoint = 10.0.0.1:65536", "port overflow");
    value("ingress.endpoint =", "ingress.endpoint = bad_host:40300", "invalid hostname");
    value("history.max_wal_bytes =", "history.max_wal_bytes = 1073741825", "WAL budget above 1 GiB");
    value("history.max_wal_bytes =", "history.max_wal_bytes = 0", "zero WAL budget");
    value("owner.tick_interval_ms =", "owner.tick_interval_ms = 0", "zero tick interval");
    const auto threshold = [](const char* text, const char* what) {
        expect_error(kValid + text, Code::invalid_value, what);
    };
    threshold("history.compact_at_entries = 0\n", "zero compaction threshold");
    threshold("history.compact_at_entries = 15\n", "compaction threshold below 16");
    threshold("history.compact_at_entries = 8001\n", "compaction threshold leaving no room in the log");
    value("caller.9.caller_id =", "caller.9.caller_id = 0", "zero caller id");
    expect_error(kValid + "voter.0.endpoint = 10.0.0.9:1\n", Code::invalid_key, "voter id zero in key");
}

void cross_field_rules_refuse() {
    const auto inconsistent = [](const std::string& text, const char* what) {
        expect_error(text, Code::inconsistent, what);
    };
    inconsistent(replaced(kValid, "node_id =", "node_id = 3"), "node not among voters");
    inconsistent(kValid + "voter.1.key_file = /etc/velocita/keys/self.key\n", "self voter has a key file");
    inconsistent(kValid + "voter.3.endpoint = 10.0.0.3:40200\nvoter.3.key_file = /k3\n"
                          "voter.4.endpoint = 10.0.0.4:40200\nvoter.4.key_file = /k4\n",
                 "more voters than the static identity allows");
    inconsistent(replaced(kValid, "caller.4.caller_id =", "caller.4.caller_id = 3"), "two keys map to one CallerId");
    inconsistent(replaced(kValid, "voter.2.endpoint =", "voter.2.endpoint = 10.0.0.1:40200"),
                 "two voters share an endpoint");
    inconsistent(replaced(kValid, "ingress.endpoint =", "ingress.endpoint = 10.0.0.1:40200"),
                 "ingress shares the peer endpoint");
    inconsistent(replaced(kValid, "caller.9.response_endpoint =", "caller.9.response_endpoint = 10.0.0.1:40300"),
                 "caller response endpoint shares the ingress endpoint");
    std::string many = kValid;
    for (int i = 20; i < 20 + static_cast<int>(kMaxConfiguredCallers); ++i)
        many += "caller." + std::to_string(i) + ".caller_id = " + std::to_string(1000 + i) +
                "\ncaller." + std::to_string(i) + ".key_file = /k" + std::to_string(i) +
                "\ncaller." + std::to_string(i) + ".response_endpoint = 10.1.0." + std::to_string(i) + ":1\n";
    inconsistent(many, "more callers than the bounded table");
    const auto lab = replaced(without(kValid, "archive.destination_identity ="), "deployment_mode =",
                              "deployment_mode = laboratory");
    require(parse_node_config(lab).error.code == Code::none, "laboratory config without backup refused");
}

void file_loading_is_bounded_and_does_not_follow_links() {
    auto pattern = (std::filesystem::temp_directory_path() / "v2-node-config-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
    const std::filesystem::path dir = bytes.data();
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{dir};
    const auto file = dir / "node.conf";
    { std::ofstream(file, std::ios::binary) << kValid; }
    const auto loaded = load_node_config(file);
    require(loaded.error.code == Code::none && loaded.config && loaded.config->digest == digest_of(kValid),
            "loaded config differs from parsed text");
    std::filesystem::create_symlink(file, dir / "link.conf");
    require(load_node_config(dir / "link.conf").error.code == Code::io_failure, "symlinked config followed");
    require(load_node_config(dir / "absent.conf").error.code == Code::io_failure, "missing config not io failure");
    { std::ofstream(dir / "big.conf", std::ios::binary) << kValid << '#' << std::string(kMaxNodeConfigBytes, 'x') << '\n'; }
    require(load_node_config(dir / "big.conf").error.code == Code::too_large, "oversized file not refused");
}

}

int main() {
    try {
        valid_config_parses_to_hand_built_values();
        every_required_key_is_required();
        unknown_and_duplicate_keys_refuse();
        grammar_is_strict();
        values_are_strict();
        cross_field_rules_refuse();
        file_loading_is_bounded_and_does_not_follow_links();
        std::cout << "node config tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node config tests failed: " << error.what() << '\n';
        return 1;
    }
}
