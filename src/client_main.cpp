// V1: raft-client.
#include "velocita/client_recovery.hpp"
#include "velocita/client_routing.hpp"
#include "velocita/client_session.hpp"
#include "velocita/wire.hpp"

#include "Publication.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <limits>
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
    std::uint64_t timeout_ms{10'000};
    std::uint64_t retry_base_ms{5};
    std::uint64_t retry_cap_ms{250};
    std::uint64_t response_timeout_ms{250};
};

std::uint64_t parse_positive_u64(const std::string& value, const std::string& name) {
    const auto parsed = std::stoull(value);
    if (parsed == 0) throw std::runtime_error(name + " must be positive");
    return parsed;
}

std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t milliseconds_to_ns(const std::uint64_t value, const std::string& name) {
    constexpr std::uint64_t scale = 1'000'000ULL;
    if (value > std::numeric_limits<std::uint64_t>::max() / scale) {
        throw std::runtime_error(name + " is too large");
    }
    return value * scale;
}

std::uint64_t saturating_add(const std::uint64_t left, const std::uint64_t right) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("usage: raft-client get|increment [amount] "
                                 "[--members ID,ID,...] "
                                 "[--request-target ID=URI]... [--response-channel URI] "
                                 "[--timeout-ms N] [--retry-base-ms N] [--retry-cap-ms N] "
                                 "[--response-timeout-ms N]");
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
        else if (key == "--timeout-ms") options.timeout_ms = parse_positive_u64(value, key);
        else if (key == "--retry-base-ms") options.retry_base_ms = parse_positive_u64(value, key);
        else if (key == "--retry-cap-ms") options.retry_cap_ms = parse_positive_u64(value, key);
        else if (key == "--response-timeout-ms") {
            options.response_timeout_ms = parse_positive_u64(value, key);
        }
        else throw std::runtime_error("unknown option: " + key);
    }
    if (options.request_targets.empty()) {
        options.request_targets.push_back({1, "aeron:udp?endpoint=127.0.0.1:40100"});
    }
    if (options.members.empty()) {
        for (const auto& target : options.request_targets) options.members.push_back(target.node_id);
    }
    velocita::validate_request_targets(options.request_targets, options.members);
    if (options.retry_cap_ms < options.retry_base_ms) {
        throw std::runtime_error("--retry-cap-ms must be greater than or equal to --retry-base-ms");
    }
    return options;
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::optional<velocita::Response> successful;
        std::optional<velocita::Response> last_rejection;
        std::optional<std::string> protocol_error;
        velocita::LeaderRouter router(options.request_targets, options.members);
        velocita::ClientRecoveryPolicy recovery({
            milliseconds_to_ns(options.retry_base_ms, "--retry-base-ms"),
            milliseconds_to_ns(options.retry_cap_ms, "--retry-cap-ms"),
            1,
            milliseconds_to_ns(options.response_timeout_ms, "--response-timeout-ms"),
        });
        std::size_t current_target = router.preferred_target();
        std::uint64_t transport_attempts = 0;
        std::uint32_t retry_count = 0;
        std::uint64_t next_offer_ns = 0;
        bool awaiting_response = false;
        const auto started_ns = now_ns();
        const auto deadline_ns = saturating_add(
            started_ns, milliseconds_to_ns(options.timeout_ms, "--timeout-ms"));
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
                    awaiting_response = false;
                    return;
                }
                last_rejection = response;
                if (response.status != velocita::ResponseStatus::not_leader) return;
                const auto hint = router.observe(response);
                if (hint == velocita::LeaderHintResult::invalid) {
                    protocol_error = "not_leader response carried an invalid leader hint";
                } else if (router.leader_id()) {
                    current_target = router.preferred_target();
                    awaiting_response = false;
                    next_offer_ns = now_ns();
                } else {
                    current_target = router.next_discovery_target(current_target);
                    const auto current_ns = now_ns();
                    awaiting_response = false;
                    next_offer_ns = recovery.next_retry_ns(
                        options.request_id, retry_count, current_ns, deadline_ns);
                }
            });

        const velocita::Command command{options.command, options.request_id, options.amount};

        while (!velocita::ClientRecoveryPolicy::deadline_reached(now_ns(), deadline_ns) &&
               !successful && !protocol_error) {
            const auto current_ns = now_ns();
            if (velocita::ClientRecoveryPolicy::retry_due(
                    current_ns, next_offer_ns, deadline_ns)) {
                if (awaiting_response) {
                    router.mark_target_unavailable(current_target);
                    current_target = router.next_discovery_target(current_target);
                    awaiting_response = false;
                }
                if (transport_attempts != 0) ++retry_count;
                ++transport_attempts;
                const auto result = session.offer(current_target, command);
                if (result == aeron::NOT_CONNECTED || result == aeron::PUBLICATION_CLOSED) {
                    awaiting_response = false;
                    router.mark_target_unavailable(current_target);
                    current_target = router.preferred_target();
                    next_offer_ns = recovery.next_retry_ns(
                        options.request_id, retry_count, current_ns, deadline_ns);
                } else if (result < 0) {
                    awaiting_response = false;
                    next_offer_ns = recovery.next_retry_ns(
                        options.request_id, retry_count, current_ns, deadline_ns);
                } else {
                    awaiting_response = true;
                    next_offer_ns = recovery.response_deadline_ns(current_ns, deadline_ns);
                }
            }
            session.poll(10);
            if (!successful) std::this_thread::yield();
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
            throw std::runtime_error(
                "timed out waiting for a successful matching response; command outcome is unknown; "
                "attempts=" + std::to_string(transport_attempts) +
                " leader_state=" + std::string(velocita::to_string(router.state())));
        }

        std::cout << "status=" << velocita::to_string(successful->status)
                  << " leader_id=" << successful->leader_id
                  << " request_id=" << successful->request_id
                  << " value=" << successful->value
                  << " term=" << successful->term
                  << " index=" << successful->index
                  << " attempts=" << transport_attempts
                  << " retries=" << retry_count
                  << " leader_state=" << velocita::to_string(router.state()) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-client failed: " << error.what() << '\n';
        return 1;
    }
}
