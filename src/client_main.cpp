#include "velocita/client_session.hpp"
#include "velocita/wire.hpp"

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
    std::vector<std::string> request_channels;
    std::string response_channel{"aeron:udp?endpoint=0.0.0.0:40101"};
};

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("usage: raft-client get|increment [amount] "
                                 "[--request-channel URI]... [--response-channel URI]");
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
        else if (key == "--request-channel") options.request_channels.push_back(value);
        else if (key == "--response-channel") options.response_channel = value;
        else throw std::runtime_error("unknown option: " + key);
    }
    if (options.request_channels.empty()) {
        options.request_channels.push_back("aeron:udp?endpoint=127.0.0.1:40100");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::optional<velocita::Response> successful;
        std::optional<velocita::Response> last_rejection;
        velocita::ClientSessionOptions session_options;
        session_options.aeron_dir = options.aeron_dir;
        session_options.request_channels = options.request_channels;
        session_options.response_channel = options.response_channel;
        velocita::AeronClientSession session(
            std::move(session_options), [&](const velocita::Response& response) {
                if (response.request_id != options.request_id) return;
                if (response.status == velocita::ResponseStatus::ok) successful = response;
                else last_rejection = response;
            });

        const velocita::Command command{options.command, options.request_id, options.amount};

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto next_offer = std::chrono::steady_clock::time_point::min();
        while (std::chrono::steady_clock::now() < deadline && !successful) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_offer) {
                for (std::size_t channel = 0; channel < session.channel_count(); ++channel) {
                    (void)session.offer(channel, command);
                }
                next_offer = now + std::chrono::milliseconds(250);
            }
            session.poll(10);
            if (!successful) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!successful) {
            if (options.request_channels.size() == 1 && last_rejection) {
                std::cout << "status=" << velocita::to_string(last_rejection->status)
                          << " request_id=" << last_rejection->request_id
                          << " value=" << last_rejection->value
                          << " term=" << last_rejection->term
                          << " index=" << last_rejection->index << '\n';
                return 2;
            }
            throw std::runtime_error("timed out waiting for a successful matching response; command outcome is unknown");
        }

        std::cout << "status=" << velocita::to_string(successful->status)
                  << " request_id=" << successful->request_id
                  << " value=" << successful->value
                  << " term=" << successful->term
                  << " index=" << successful->index << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-client failed: " << error.what() << '\n';
        return 1;
    }
}
