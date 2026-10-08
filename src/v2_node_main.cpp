// velocita-v2-node: runs one voter.
#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/aeron_node_transport.hpp"
#include "velocita/v2/election_jitter.hpp"
#include "velocita/v2/node.hpp"
#include "velocita/v2/node_config.hpp"
#include "velocita/v2/node_files.hpp"
#include "velocita/v2/node_health.hpp"
#include "velocita/v2/session_fence.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using namespace velocita::v2;
using Clock = std::chrono::steady_clock;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_reload{false};
void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }
void on_reload(int) { g_reload.store(true, std::memory_order_relaxed); }

struct Usage final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Arguments {
    std::filesystem::path config;
    std::optional<std::filesystem::path> aeron_dir;
    std::optional<std::filesystem::path> health;
};

Arguments parse(int argc, char** argv) {
    Arguments arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (i + 1 >= argc) throw Usage("missing value for " + key);
        const std::filesystem::path value = argv[++i];
        if (key == "--config") arguments.config = value;
        else if (key == "--aeron-dir") arguments.aeron_dir = value;
        else if (key == "--health") arguments.health = value;
        else throw Usage("unknown argument " + key);
    }
    if (arguments.config.empty()) throw Usage("usage: velocita-v2-node --config FILE [--aeron-dir DIR] [--health FILE]");
    return arguments;
}

std::optional<NodeSecrets> load_secrets(const NodeConfig& config, std::string& refusal) {
    NodeSecrets secrets;
    const auto load = [&](const std::filesystem::path& file) -> std::optional<SecretSet> {
        auto loaded = load_secret_set(file);
        if (loaded.error == NodeFileError::none) return std::move(loaded.secrets);
        refusal = "file=" + loaded.failed_file.string() + " code=" + std::to_string(static_cast<int>(loaded.error));
        return std::nullopt;
    };
    for (const auto& voter : config.voters) {
        if (!voter.key_file) continue;
        auto set = load(*voter.key_file);
        if (!set) return std::nullopt;
        secrets.voters.emplace(voter.id, std::move(*set));
    }
    for (const auto& caller : config.callers) {
        auto set = load(caller.key_file);
        if (!set) return std::nullopt;
        secrets.callers.emplace(caller.key_id, std::move(*set));
    }
    return secrets;
}

struct ReloadCounters {
    std::uint64_t applied{};
    std::uint64_t refused{};
};

NodeHealth health_of(const NodeStatus& status, std::uint64_t turns, bool failed, const ReloadCounters& reloads) {
    return node_health_of(status, NodeHealthContext{
        static_cast<std::uint64_t>(::getpid()), turns,
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()),
        failed, reloads.applied, reloads.refused});
}

int run(const Arguments& arguments) {
    const auto parsed = load_node_config(arguments.config);
    if (!parsed.config) {
        throw Usage("configuration refused: code=" + std::to_string(static_cast<int>(parsed.error.code)) +
                    " line=" + std::to_string(parsed.error.line) + " key=" + parsed.error.key);
    }
    const auto& config = *parsed.config;
    const auto catalog = load_catalog_file(config.catalog_file, config.catalog_root);
    if (catalog.error != NodeFileError::none) throw Usage("catalog file refused: " + config.catalog_file.string());

    std::string refusal;
    auto secrets = load_secrets(config, refusal);
    if (!secrets) throw Usage("secret refused: " + refusal);

    const auto epoch_file = config.data_root / "session-epoch.bin";
    SessionEpochResult epoch;
    if (config.startup == NodeStartup::bootstrap) {
        std::filesystem::create_directories(config.data_root);
        std::filesystem::permissions(config.data_root, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);
        epoch = create_session_epoch_file(epoch_file);
    } else {
        epoch = advance_session_epoch_file(epoch_file);
    }
    if (epoch.error != SessionEpochError::none)
        throw Usage("session epoch refused: code=" + std::to_string(static_cast<int>(epoch.error)));

    const auto health_file = arguments.health.value_or(config.data_root / "health.page");
    auto health = NodeHealthPage::create(health_file);
    velocita::AeronRuntime aeron(arguments.aeron_dir.value_or(config.data_root / "aeron").string());
    AeronNodeTransport transport(aeron, config);
    if (!seed_election_jitter()) throw std::runtime_error("entropy unavailable for election jitter");
    V2Node node(config, catalog.catalog, std::move(*secrets), epoch.epoch, transport);
    ReloadCounters reloads;

    std::cout << "READY node=" << config.node_id << " epoch=" << epoch.epoch << " config=" << to_hex(config.digest)
              << std::endl;

    std::uint64_t turns = 0;
    auto last = Clock::now();
    auto last_health = Clock::time_point{};
    const auto tick = std::chrono::milliseconds(config.owner.tick_interval_ms);
    try {
        while (!g_stop.load(std::memory_order_relaxed)) {
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
            if (elapsed > 0) last = now;
            if (g_reload.exchange(false, std::memory_order_relaxed)) {
                std::string why;
                auto fresh = load_secrets(config, why);
                if (fresh && node.replace_secrets(std::move(*fresh))) {
                    ++reloads.applied;
                    std::cout << "RELOADED secrets node=" << config.node_id << std::endl;
                } else {
                    ++reloads.refused;
                    std::cout << "RELOAD_REFUSED node=" << config.node_id << " " << (fresh ? "incomplete" : why)
                              << std::endl;
                }
            }
            node.turn(static_cast<int>(elapsed));
            ++turns;
            if (now - last_health >= std::chrono::milliseconds(100)) {
                health->publish(health_of(node.status(), turns, false, reloads));
                last_health = now;
            }
            std::this_thread::sleep_for(tick);
        }
        node.close_ingress();
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (Clock::now() < deadline && !node.quiescent()) {
            const auto now = Clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
            if (elapsed > 0) last = now;
            node.turn(static_cast<int>(elapsed));
            std::this_thread::sleep_for(tick);
        }
        health->publish(health_of(node.status(), turns, false, reloads));
        std::cout << "STOPPED node=" << config.node_id << " quiescent=" << node.quiescent() << std::endl;
        return 0;
    } catch (const std::exception& error) {
        try { health->publish(health_of(node.status(), turns, true, reloads)); } catch (...) {}
        std::cerr << "FAILED node=" << config.node_id << ": " << error.what() << std::endl;
        return 2;
    }
}

}

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_reload);
    try {
        return run(parse(argc, argv));
    } catch (const Usage& error) {
        std::cerr << "velocita-v2-node: " << error.what() << std::endl;
        return 64;
    } catch (const std::exception& error) {
        std::cerr << "velocita-v2-node FAILED: " << error.what() << std::endl;
        return 2;
    }
}
