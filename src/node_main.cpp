#include "velocita/aeron_runtime.hpp"
#include "velocita/owner_loop_scheduler.hpp"
#include "velocita/raft_node.hpp"
#include "velocita/shared_metrics.hpp"
#include "velocita/wire.hpp"

#include "FragmentAssembler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

std::atomic_bool running{true};

void stop(int) {
    running.store(false);
}

struct Options {
    std::int32_t node_id{1};
    std::filesystem::path data_dir{"/var/lib/velocita-raft"};
    std::string aeron_dir{"/dev/shm/aeron-raft-node"};
    std::string request_channel{"aeron:udp?endpoint=0.0.0.0:40100"};
    std::string response_channel{"aeron:udp?endpoint=127.0.0.1:40101"};
    std::int32_t request_stream{1001};
    std::int32_t response_stream{1002};
    std::vector<std::int32_t> members;
    std::string peer_channel;
    std::unordered_map<std::int32_t, std::string> peer_targets;
    std::filesystem::path metrics_file;
    bool raft_trace{};
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    std::uint64_t benchmark_stall_every{};
    std::uint64_t benchmark_stall_ms{};
#endif
};

std::vector<std::int32_t> parse_members(const std::string& text) {
    std::vector<std::int32_t> result;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(',', begin);
        result.push_back(std::stoi(text.substr(begin, end - begin)));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "raft-node [--node-id N] [--data-dir PATH] [--aeron-dir PATH] "
                         "[--request-channel URI] [--response-channel URI] "
                         "[--members ID,ID,...] [--peer-channel URI] "
                         "[--peer-target ID=URI]... [--metrics-file PATH] "
                         "[--raft-trace 0|1]"
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                         " [--benchmark-stall-every N] [--benchmark-stall-ms N]"
#endif
                         "\n";
            std::exit(0);
        }
        if (i + 1 >= argc) {
            throw std::runtime_error("missing value for " + key);
        }
        const std::string value = argv[++i];
        if (key == "--node-id") options.node_id = std::stoi(value);
        else if (key == "--data-dir") options.data_dir = value;
        else if (key == "--aeron-dir") options.aeron_dir = value;
        else if (key == "--request-channel") options.request_channel = value;
        else if (key == "--response-channel") options.response_channel = value;
        else if (key == "--members") options.members = parse_members(value);
        else if (key == "--peer-channel") options.peer_channel = value;
        else if (key == "--peer-target") {
            const auto separator = value.find('=');
            if (separator == std::string::npos) throw std::runtime_error("peer target must be ID=URI");
            options.peer_targets.emplace(std::stoi(value.substr(0, separator)), value.substr(separator + 1));
        }
        else if (key == "--metrics-file") options.metrics_file = value;
        else if (key == "--raft-trace") {
            if (value != "0" && value != "1") throw std::runtime_error("raft trace must be 0 or 1");
            options.raft_trace = value == "1";
        }
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
        else if (key == "--benchmark-stall-every") options.benchmark_stall_every = std::stoull(value);
        else if (key == "--benchmark-stall-ms") options.benchmark_stall_ms = std::stoull(value);
#endif
        else throw std::runtime_error("unknown option: " + key);
    }
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    if ((options.benchmark_stall_every == 0) != (options.benchmark_stall_ms == 0)) {
        throw std::runtime_error("benchmark stall interval and duration must both be zero or positive");
    }
#endif
    return options;
}

velocita::OfferOutcome offer_outcome(const std::int64_t result) noexcept {
    if (result >= 0) return velocita::OfferOutcome::success;
    if (result == aeron::NOT_CONNECTED) return velocita::OfferOutcome::not_connected;
    if (result == aeron::BACK_PRESSURED) return velocita::OfferOutcome::back_pressured;
    if (result == aeron::ADMIN_ACTION) return velocita::OfferOutcome::admin_action;
    return velocita::OfferOutcome::failed;
}

void print_status(const velocita::NodeStatus& status) {
    std::cout << "status node=" << status.node_id
              << " role=" << status.role
              << " leader=" << status.leader_id
              << " term=" << status.term
              << " log=" << status.first_index << ".." << status.current_index
              << " commit=" << status.commit_index
              << " applied=" << status.applied_index
              << " counter=" << status.counter_value << std::endl;
}

struct PeerQueueEntry {
    std::vector<std::byte> bytes;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    std::int64_t last_index{};
#endif
};

struct OfferWork {
    std::size_t attempted{};
    std::size_t progressed{};
};

}  // namespace

int main(int argc, char** argv) {
    try {
        auto options = parse_options(argc, argv);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);

        if (options.members.empty()) options.members.push_back(options.node_id);
        if (options.members.size() > 1 && options.peer_channel.empty()) {
            throw std::runtime_error("a multi-node cluster requires --peer-channel");
        }
        velocita::RaftNode node(options.node_id, options.data_dir, options.members, options.raft_trace);
        velocita::SharedMetrics metrics(options.node_id, options.metrics_file);
        metrics.record_status(node.status());
        velocita::AeronRuntime aeron(options.aeron_dir);
        auto responses = aeron.add_publication(options.response_channel, options.response_stream);
        auto requests = aeron.add_subscription(options.request_channel, options.request_stream);
        std::deque<std::vector<std::byte>> response_queue;
        std::uint64_t response_queue_depth{};
        std::uint64_t peer_queue_depth{};
        const velocita::OwnerLoopConfig owner_loop_config;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
        std::uint64_t benchmark_request_count{};
#endif

        std::shared_ptr<aeron::Subscription> peer_subscription;
        std::unordered_map<std::int32_t, std::shared_ptr<aeron::Publication>> peer_publications;
        std::unordered_map<std::int32_t, std::deque<PeerQueueEntry>> peer_queues;
        std::vector<std::int32_t> peer_ids;
        if (options.members.size() > 1) {
            peer_subscription = aeron.add_subscription(options.peer_channel, 2001);
            for (const auto member : options.members) {
                if (member == options.node_id) continue;
                const auto target = options.peer_targets.find(member);
                if (target == options.peer_targets.end()) {
                    throw std::runtime_error("missing --peer-target for node " + std::to_string(member));
                }
                peer_publications.emplace(member, aeron.add_publication(target->second, 2001));
                peer_queues.emplace(member, std::deque<PeerQueueEntry>{});
                peer_ids.push_back(member);
            }
            std::sort(peer_ids.begin(), peer_ids.end());
        }

        aeron::FragmentAssembler assembler(
            [&](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset,
                aeron::util::index_t length, aeron::Header&) {
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                const auto ingress_ns = velocita::benchmark_monotonic_nanoseconds();
#endif
                metrics.record_request(static_cast<std::size_t>(length));
                try {
                    const auto* begin = reinterpret_cast<const std::byte*>(buffer.buffer() + offset);
                    const auto command = velocita::decode_command(
                        std::span<const std::byte>(begin, static_cast<std::size_t>(length)));
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                    ++benchmark_request_count;
                    if (options.benchmark_stall_every > 0 &&
                        benchmark_request_count % options.benchmark_stall_every == 0) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(options.benchmark_stall_ms));
                    }
                    auto immediate = node.propose(command, ingress_ns);
#else
                    auto immediate = node.propose(command);
#endif
                    if (immediate) {
                        if (response_queue.size() >= owner_loop_config.response_queue_capacity) {
                            throw std::runtime_error("response queue capacity exhausted");
                        }
                        response_queue.push_back(velocita::encode_response(*immediate));
                        ++response_queue_depth;
                        metrics.set_response_queue_depth(response_queue_depth);
                    }
                } catch (const std::exception& error) {
                    metrics.record_rejected_request();
                    std::cerr << "request rejected: " << error.what() << '\n';
                }
            });
        auto handler = assembler.handler();

        aeron::FragmentAssembler peer_assembler(
            [&](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset,
                aeron::util::index_t length, aeron::Header&) {
                metrics.record_peer_fragment(static_cast<std::size_t>(length));
                try {
                    const auto* begin = reinterpret_cast<const std::byte*>(buffer.buffer() + offset);
                    node.receive_peer(velocita::decode_peer_message(
                        std::span<const std::byte>(begin, static_cast<std::size_t>(length))));
                } catch (const std::exception& error) {
                    metrics.record_rejected_peer_message();
                    std::cerr << "peer message rejected: " << error.what() << '\n';
                }
            });
        auto peer_handler = peer_assembler.handler();

        auto drain_raft_outputs = [&](std::size_t& remaining_peer,
                                      std::size_t& remaining_responses) {
            std::size_t drained{};
            while (remaining_peer > 0) {
                auto message = node.take_outbound_message();
                if (!message) break;
                --remaining_peer;
                metrics.record_outbound_peer_message();
                const auto queue_position = peer_queues.find(message->recipient_id);
                if (queue_position == peer_queues.end()) {
                    throw std::runtime_error("outbound message targets unknown peer");
                }
                auto& queue = queue_position->second;
                if (queue.size() == owner_loop_config.peer_queue_capacity) {
                    queue.pop_front();
                    metrics.record_dropped_peer_message();
                } else {
                    ++peer_queue_depth;
                }
                PeerQueueEntry queued{velocita::encode_peer_message(*message)};
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                if (const auto* append = std::get_if<velocita::AppendEntries>(&message->payload);
                    append != nullptr && !append->entries.empty()) {
                    queued.last_index = append->previous_log_index +
                        static_cast<std::int64_t>(append->entries.size());
                }
#endif
                queue.push_back(std::move(queued));
                ++drained;
            }
            metrics.set_peer_queue_depth(peer_queue_depth);

            const auto available = owner_loop_config.response_queue_capacity -
                response_queue.size();
            const auto bounded_responses = std::min(remaining_responses, available);
            for (std::size_t count = 0; count < bounded_responses; ++count) {
                auto response = node.take_completed_response();
                if (!response) break;
                --remaining_responses;
                response_queue.push_back(velocita::encode_response(*response));
                ++response_queue_depth;
                metrics.record_completed_response();
                ++drained;
            }
            metrics.set_response_queue_depth(response_queue_depth);
            return drained;
        };

        std::size_t peer_offer_cursor{};
        auto offer_peer_messages = [&](std::size_t& remaining) {
            OfferWork work;
            for (std::size_t visited = 0;
                 visited < peer_ids.size() && remaining > 0; ++visited) {
                const auto peer_id = peer_ids[peer_offer_cursor];
                peer_offer_cursor = (peer_offer_cursor + 1) % peer_ids.size();
                auto& queue = peer_queues.at(peer_id);
                if (queue.empty()) continue;
                const auto publication = peer_publications.find(peer_id);
                if (publication == peer_publications.end()) {
                    throw std::runtime_error("outbound message targets unknown peer");
                }
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                const auto offer_started_ns = velocita::benchmark_monotonic_nanoseconds();
#endif
                const auto result = velocita::AeronRuntime::offer(
                    *publication->second, queue.front().bytes);
                --remaining;
                ++work.attempted;
                metrics.record_offer(velocita::OfferChannel::peer, offer_outcome(result));
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                if (result >= 0 && queue.front().last_index > 0) {
                    node.benchmark_record_peer_offer(
                        peer_id, queue.front().last_index, offer_started_ns);
                }
#endif
                if (result >= 0 || result == aeron::NOT_CONNECTED) {
                    queue.pop_front();
                    --peer_queue_depth;
                    ++work.progressed;
                } else if (result == aeron::PUBLICATION_CLOSED ||
                           result == aeron::MAX_POSITION_EXCEEDED) {
                    throw std::runtime_error("Aeron peer publication became unusable");
                }
            }
            metrics.set_peer_queue_depth(peer_queue_depth);
            return work;
        };

        auto offer_responses = [&](const std::size_t limit) {
            OfferWork work;
            while (work.attempted < limit && !response_queue.empty()) {
                const auto result = velocita::AeronRuntime::offer(
                    *responses, response_queue.front());
                ++work.attempted;
                metrics.record_offer(velocita::OfferChannel::response, offer_outcome(result));
                if (result >= 0) {
                    response_queue.pop_front();
                    --response_queue_depth;
                    ++work.progressed;
                } else if (result == aeron::PUBLICATION_CLOSED ||
                           result == aeron::MAX_POSITION_EXCEEDED) {
                    throw std::runtime_error("Aeron response publication became unusable");
                } else {
                    break;
                }
            }
            metrics.set_response_queue_depth(response_queue_depth);
            return work;
        };

        std::cout << "native C++ Raft node listening on " << options.request_channel << std::endl;
        std::cout << "owner-loop tick_ms=" << owner_loop_config.tick_interval.count()
                  << " client_margin_ns=" << owner_loop_config.client_safety_margin.count()
                  << " peer_fragments=" << owner_loop_config.peer_fragment_limit
                  << " client_fragments=" << owner_loop_config.client_fragment_limit
                  << " raft_drain=" << owner_loop_config.raft_output_drain_limit
                  << " completion_drain=" << owner_loop_config.completed_response_drain_limit
                  << std::endl;
        const auto started = velocita::OwnerLoopScheduler::Clock::now();
        velocita::OwnerLoopScheduler scheduler(owner_loop_config, started);
        std::uint64_t owner_loop_iterations{};
        std::uint64_t client_budget_exhausted{};
        std::uint64_t peer_budget_exhausted{};
        std::uint64_t client_polls_skipped_tick_slack{};
        std::uint64_t client_polls_skipped_response_backlog{};
        auto last_metrics_status = started;
        while (running.load()) {
            const auto iteration = velocita::run_owner_loop_iteration(
                scheduler,
                [] { return velocita::OwnerLoopScheduler::Clock::now(); },
                [&](const int elapsed) {
                    node.tick(elapsed);
                    const auto completed = velocita::OwnerLoopScheduler::Clock::now();
                    metrics.record_tick(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            completed.time_since_epoch()).count()));
                },
                [&](const int limit) {
                    return peer_subscription ? peer_subscription->poll(peer_handler, limit) : 0;
                },
                drain_raft_outputs,
                [&](std::size_t& remaining) {
                    return offer_peer_messages(remaining).progressed;
                },
                [&] {
                    return response_queue.size() < owner_loop_config.response_queue_capacity;
                },
                [&](const int limit) { return requests->poll(handler, limit); },
                [&](const std::size_t limit) {
                    return offer_responses(limit).progressed;
                });
            ++owner_loop_iterations;
            if (iteration.client_fragments == owner_loop_config.client_fragment_limit) {
                ++client_budget_exhausted;
            }
            if (iteration.peer_fragments == owner_loop_config.peer_fragment_limit) {
                ++peer_budget_exhausted;
            }

            if (iteration.client_blocked_by ==
                velocita::ClientPollBlockReason::tick_slack) {
                ++client_polls_skipped_tick_slack;
            } else if (iteration.client_blocked_by ==
                       velocita::ClientPollBlockReason::response_backlog) {
                ++client_polls_skipped_response_backlog;
            }

            const auto now = velocita::OwnerLoopScheduler::Clock::now();
            if (now - last_metrics_status >= std::chrono::milliseconds(100)) {
                metrics.record_owner_loop(
                    owner_loop_iterations,
                    scheduler.maximum_tick_gap_nanoseconds(),
                    client_budget_exhausted,
                    peer_budget_exhausted,
                    client_polls_skipped_tick_slack,
                    client_polls_skipped_response_backlog);
                metrics.record_status(node.status());
                last_metrics_status = now;
            }
            if (!iteration.ticked && iteration.client_fragments == 0 &&
                iteration.peer_fragments == 0 && iteration.progressed == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::cout << "owner-loop-stats node=" << options.node_id
                  << " max_tick_gap_ns=" << scheduler.maximum_tick_gap_nanoseconds()
                  << " client_polls_skipped_tick_slack=" << client_polls_skipped_tick_slack
                  << " client_polls_skipped_response_backlog="
                  << client_polls_skipped_response_backlog << std::endl;
        print_status(node.status());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-node failed: " << error.what() << '\n';
        return 1;
    }
}
