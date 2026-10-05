#include "velocita/v2/kernel.hpp"
#include "velocita/v2/node_config.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Host {
    std::string address;
    std::uint16_t voter_port, ingress_port;
};
const std::map<std::int32_t, Host> kThreeHosts{
    {1, {"100.72.127.102", 42201, 42301}},
    {2, {"100.84.151.116", 42202, 42302}},
    {3, {"100.85.36.22", 42203, 42303}},
};
const std::map<std::int32_t, Host> kTwoHosts{
    {1, {"100.72.127.102", 42201, 42301}},
    {2, {"100.84.151.116", 42202, 42302}},
};

struct Caller {
    std::int32_t key_id;
    std::uint64_t caller_id;
    Endpoint response;
};
const std::vector<Caller> kCallers{
    {9, 7, {"100.91.80.64", 42400}},
    {10, 8, {"100.72.127.102", 42410}},
    {11, 9, {"100.72.127.102", 42411}},
    {12, 21, {"100.72.127.102", 42412}},
    {13, 22, {"100.72.127.102", 42413}},
};
constexpr std::uint64_t kGatewayCaller = 22;

std::string filled(const std::string& topology, std::int32_t id) {
    const auto path = std::string(VELOCITA_SOURCE_DIR) + "/deploy/v2/" + topology + "/node-" + std::to_string(id) +
                      ".conf.template";
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "missing template " + path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto text = buffer.str();
    const auto fill = [&](const std::string& placeholder, const std::string& value) {
        require(text.find(placeholder) != std::string::npos, "template lacks " + placeholder);
        for (auto at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at))
            text.replace(at, placeholder.size(), value);
    };
    fill("@INCARNATION@", "0123456789abcdef0123456789abcdef");
    fill("@CATALOG_ROOT@", std::string(64, 'a'));
    return text;
}

std::string link_file(std::int32_t a, std::int32_t b) {
    return "/run/velocita-secrets/link-" + std::to_string(std::min(a, b)) + "-" + std::to_string(std::max(a, b)) +
           ".key";
}

void templates_describe_one_cluster(const std::string& topology, const std::map<std::int32_t, Host>& hosts) {
    std::optional<StaticIdentity> shared;
    for (const auto& [id, host] : hosts) {
        const auto parsed = parse_node_config(filled(topology, id));
        require(parsed.error.code == NodeConfigErrorCode::none && parsed.config,
                topology + " node-" + std::to_string(id) + " template refused at line " + std::to_string(parsed.error.line) +
                    " key '" + parsed.error.key + "'");
        const auto& config = *parsed.config;
        require(config.gateway_caller == kGatewayCaller,
                topology + " node-" + std::to_string(id) + " does not name CallerId 22 as its gateway");
        require(config.incarnation[0] == std::byte{0x01} && config.incarnation[15] == std::byte{0xef} &&
                    config.catalog_root.bytes[0] == std::byte{0xaa},
                "node-" + std::to_string(id) + " did not take the filled incarnation and catalog root");
        require(config.node_id == id, "node-" + std::to_string(id) + " template has the wrong node_id");
        require(config.startup == NodeStartup::bootstrap, "template must bootstrap on first start");
        require(config.deployment_mode == DeploymentMode::laboratory, "template left laboratory mode");
        require(config.data_root == "/var/lib/velocita-v2/node-" + std::to_string(id), "wrong data root");
        require(config.ingress_endpoint == Endpoint{host.address, host.ingress_port}, "wrong ingress endpoint");
        require(config.voters.size() == hosts.size(), "template does not list every voter");
        for (const auto& voter : config.voters) {
            const auto& expected = hosts.at(voter.id);
            require(voter.endpoint == Endpoint{expected.address, expected.voter_port},
                    "voter " + std::to_string(voter.id) + " endpoint differs in node-" + std::to_string(id));
            if (voter.id == id)
                require(!voter.key_file, "a voter has a link key to itself");
            else
                require(voter.key_file && *voter.key_file == link_file(id, voter.id),
                        "link " + std::to_string(id) + "-" + std::to_string(voter.id) + " uses the wrong secret");
        }
        auto identity = config.static_identity();
        require(identity.local_node_id == id && identity.voter_count == hosts.size(), "identity lacks a voter");
        identity.local_node_id = 0;
        if (shared) require(identity == *shared, "node-" + std::to_string(id) + " bootstraps a different cluster identity");
        shared = identity;
        require(config.callers.size() == kCallers.size(),
                topology + " node-" + std::to_string(id) + " does not route exactly the known callers");
        for (std::size_t i = 0; i < kCallers.size(); ++i)
            require(config.callers[i].key_id == kCallers[i].key_id &&
                        config.callers[i].caller_id == kCallers[i].caller_id &&
                        config.callers[i].response_endpoint == kCallers[i].response &&
                        config.callers[i].key_file ==
                            "/run/velocita-secrets/caller-" + std::to_string(kCallers[i].key_id) + ".key",
                    topology + " node-" + std::to_string(id) + " routes caller " +
                        std::to_string(kCallers[i].key_id) + " wrongly");
    }
}

void strategy_and_gateway_callers_hold_no_operator_authority() {
    for (const auto& caller : kCallers)
        require(is_operator_caller(CallerId{caller.caller_id}) == (caller.key_id <= 11),
                "caller key " + std::to_string(caller.key_id) + " is on the wrong side of the operator band");
}

}

int main() {
    try {
        templates_describe_one_cluster("two-host", kTwoHosts);
        templates_describe_one_cluster("three-host", kThreeHosts);
        strategy_and_gateway_callers_hold_no_operator_authority();
        std::cout << "deploy template tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "deploy template tests failed: " << error.what() << '\n';
        return 1;
    }
}
