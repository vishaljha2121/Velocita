#include "velocita/client_session.hpp"
#include "velocita/client_routing.hpp"
#include "velocita/wire.hpp"

#include "Publication.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Options {
    velocita::CommandType command{velocita::CommandType::get};
    std::int64_t amount{};
    std::uint64_t request_id{};
    std::string aeron_dir{"/dev/shm/aeron-raft-client"};
    std::vector<velocita::RequestTarget> request_targets;
    std::vector<std::int32_t> members;
    std::string response_channel{"aeron:udp?endpoint=0.0.0.0:40101"};
};

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("usage: raft-client get|increment [amount] "
                                 "[--members ID,ID,...] "
                                 "[--request-target ID=URI]... [--response-channel URI]");
    }
    Options options;
    const std::string operation = argv[1];
    int i = 2;
    if (operation == "increment") {
        options.command = velocita::CommandType::increment;
        if (i >= argc) throw std::runtime_error("increment requires an amount");
        options.amount = std::stoll(argv[i++]);
    } else if (operation == "get") {
        options.command = velocita::CommandType::get;
    } else {
        throw std::runtime_error("operation must be get or increment");
    }
    options.request_id = static_cast<std::uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    for (; i < argc; ++i) {
        const std::string key = argv[i];
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--request-id") options.request_id = std::stoull(value);
        else if (key == "--aeron-dir") options.aeron_dir = value;
        else if (key == "--members") options.members = velocita::parse_member_ids(value);
        else if (key == "--request-target") {
            options.request_targets.push_back(velocita::parse_request_target(value));
        }
        else if (key == "--response-channel") options.response_channel = value;
        else throw std::runtime_error("unknown option: " + key);
    }
    if (options.request_targets.empty()) {
        options.request_targets.push_back({1, "aeron:udp?endpoint=127.0.0.1:40100"});
    }
    if (options.members.empty()) {
        for (const auto& target : options.request_targets) options.members.push_back(target.node_id);
    }
    velocita::validate_request_targets(options.request_targets, options.members);
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::optional<velocita::Response> successful;
        std::optional<velocita::Response> last_rejection;
        std::optional<std::string> protocol_error;
        velocita::LeaderRouter router(options.request_targets, options.members);
        std::size_t current_target = router.preferred_target();
        std::uint64_t transport_attempts = 0;
        auto next_offer = std::chrono::steady_clock::time_point::min();
        velocita::ClientSessionOptions session_options;
        session_options.aeron_dir = options.aeron_dir;
        session_options.request_targets = options.request_targets;
        session_options.required_members = options.members;
        session_options.response_channel = options.response_channel;
        velocita::AeronClientSession session(
            std::move(session_options), [&](const velocita::Response& response) {
                if (response.request_id != options.request_id) return;
                if (response.status == velocita::ResponseStatus::ok) {
                    if (router.observe(response) == velocita::LeaderHintResult::invalid) {
                        protocol_error = "successful response carried an invalid leader hint";
                        return;
                    }
                    successful = response;
                    return;
                }
                last_rejection = response;
                if (response.status != velocita::ResponseStatus::not_leader) return;
                const auto hint = router.observe(response);
                if (hint == velocita::LeaderHintResult::invalid) {
                    protocol_error = "not_leader response carried an invalid leader hint";
                    current_target = router.next_discovery_target(current_target);
                } else if (router.leader_id()) {
                    current_target = router.preferred_target();
                } else {
                    current_target = router.next_discovery_target(current_target);
                }
                next_offer = std::chrono::steady_clock::time_point::min();
            });

        const velocita::Command command{options.command, options.request_id, options.amount};

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline && !successful && !protocol_error) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_offer) {
                ++transport_attempts;
                const auto result = session.offer(current_target, command);
                if (result == aeron::NOT_CONNECTED || result == aeron::PUBLICATION_CLOSED) {
                    router.mark_target_unavailable(current_target);
                    current_target = router.preferred_target();
                }
                next_offer = now + std::chrono::milliseconds(250);
            }
            session.poll(10);
            if (!successful) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!successful) {
            if (protocol_error) throw std::runtime_error(*protocol_error);
            if (options.request_targets.size() == 1 && last_rejection) {
                std::cout << "status=" << velocita::to_string(last_rejection->status)
                          << " leader_id=" << last_rejection->leader_id
                          << " request_id=" << last_rejection->request_id
                          << " value=" << last_rejection->value
                          << " term=" << last_rejection->term
                          << " index=" << last_rejection->index << '\n';
                return 2;
            }
            throw std::runtime_error("timed out waiting for a successful matching response; command outcome is unknown");
        }

        std::cout << "status=" << velocita::to_string(successful->status)
                  << " leader_id=" << successful->leader_id
                  << " request_id=" << successful->request_id
                  << " value=" << successful->value
                  << " term=" << successful->term
                  << " index=" << successful->index
                  << " attempts=" << transport_attempts << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-client failed: " << error.what() << '\n';
        return 1;
    }
}
