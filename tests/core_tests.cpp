#include "velocita/client_recovery.hpp"
#include "velocita/client_routing.hpp"
#include "velocita/owner_loop_scheduler.hpp"
#include "velocita/raft_node.hpp"
#include "velocita/storage.hpp"
#include "velocita/wire.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void require_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

template <typename T>
void append_test_le(std::vector<std::byte>& bytes, T value) {
    using U = std::make_unsigned_t<T>;
    const auto bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::byte>((bits >> (index * 8U)) & 0xFFU));
    }
}

std::vector<std::byte> legacy_response(const std::uint16_t version,
                                       const velocita::ResponseStatus status) {
    std::vector<std::byte> bytes;
    append_test_le<std::uint32_t>(bytes, 0x31505352U);
    append_test_le(bytes, version);
    append_test_le(bytes, static_cast<std::uint16_t>(status));
    append_test_le<std::uint64_t>(bytes, 42);
    append_test_le<std::int64_t>(bytes, 9);
    append_test_le<std::int64_t>(bytes, 3);
    append_test_le<std::int64_t>(bytes, 8);
    if (version == 2) {
        for (std::uint64_t timing = 10; timing <= 60; timing += 10) {
            append_test_le(bytes, timing);
        }
    }
    return bytes;
}

void elect(velocita::RaftNode& node) {
    for (int i = 0; i < 30 && !node.is_leader(); ++i) node.tick(100);
    require(node.is_leader(), "single node did not elect itself");
}

void test_wire() {
    const velocita::Command command{velocita::CommandType::increment, 42, -7};
    const auto decoded = velocita::decode_command(velocita::encode_command(command));
    const auto decoded_fixed = velocita::decode_command(velocita::encode_command_fixed(command));
    require(decoded.type == command.type && decoded.request_id == 42 && decoded.amount == -7,
            "command wire round trip failed");
    require(decoded_fixed.type == command.type && decoded_fixed.request_id == 42 &&
                decoded_fixed.amount == -7,
            "fixed command wire round trip failed");

    velocita::Response response{velocita::ResponseStatus::ok, 2, 42, 9, 3, 8};
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    response.benchmark = {velocita::kBenchmarkTimingValid, 10, 20, 30, 40, 50};
#endif
    const auto decoded_response = velocita::decode_response(velocita::encode_response(response));
    require(decoded_response.status == response.status && decoded_response.value == 9 &&
                decoded_response.leader_id == 2 && decoded_response.request_id == 42 &&
                decoded_response.term == 3 && decoded_response.index == 8,
            "response wire round trip failed");
    require(velocita::encode_response(response).size() ==
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
                96,
#else
                48,
#endif
            "V3 response has an unexpected encoded size");
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    require(decoded_response.benchmark.flags == velocita::kBenchmarkTimingValid &&
                decoded_response.benchmark.ingress_to_admission_ns == 10 &&
                decoded_response.benchmark.local_wal_append_ns == 20 &&
                decoded_response.benchmark.quorum_append_ns == 30 &&
                decoded_response.benchmark.admission_to_commit_ns == 40 &&
                decoded_response.benchmark.commit_to_apply_ns == 50,
            "benchmark response timing wire round trip failed");
#endif

    const auto decoded_v1 = velocita::decode_response(
        legacy_response(1, velocita::ResponseStatus::not_leader));
    require(decoded_v1.status == velocita::ResponseStatus::not_leader &&
                decoded_v1.wire_version == 1 && decoded_v1.leader_id == -1 &&
                decoded_v1.request_id == 42 &&
                decoded_v1.value == 9 && decoded_v1.term == 3 && decoded_v1.index == 8,
            "legacy V1 response compatibility failed");
    const auto decoded_v2 = velocita::decode_response(
        legacy_response(2, velocita::ResponseStatus::ok));
    require(decoded_v2.status == velocita::ResponseStatus::ok &&
                decoded_v2.wire_version == 2 && decoded_v2.leader_id == -1 &&
                decoded_v2.request_id == 42,
            "legacy V2 response compatibility failed");

    for (const auto status : {velocita::ResponseStatus::ok,
                              velocita::ResponseStatus::not_leader,
                              velocita::ResponseStatus::invalid_request,
                              velocita::ResponseStatus::internal_error}) {
        response.status = status;
        require(velocita::decode_response(velocita::encode_response(response)).status == status,
                "V3 response status round trip failed");
    }

    auto malformed = velocita::encode_response(response);
    malformed.pop_back();
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "truncated V3 response was accepted");
    malformed = velocita::encode_response(response);
    malformed[0] = std::byte{0};
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "bad response magic was accepted");
    malformed = velocita::encode_response(response);
    malformed[4] = std::byte{99};
    malformed[5] = std::byte{0};
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "unsupported response version was accepted");
    malformed = velocita::encode_response(response);
    malformed[6] = std::byte{99};
    malformed[7] = std::byte{0};
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "unknown response status was accepted");
    malformed = velocita::encode_response(response);
    malformed[11] = std::byte{0x80};
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "unknown response extension was accepted");
    malformed = velocita::encode_response(response);
    malformed.push_back(std::byte{0});
    require_throws([&] { (void)velocita::decode_response(malformed); },
                   "V3 response trailing data was accepted");

    velocita::PeerMessage peer{1, 2, velocita::AppendEntries{
        4, 7, 3, 6, {{4, 99, 0, velocita::encode_command(command)}}}};
    const auto decoded_peer = velocita::decode_peer_message(velocita::encode_peer_message(peer));
    const auto& append = std::get<velocita::AppendEntries>(decoded_peer.payload);
    require(decoded_peer.sender_id == 1 && decoded_peer.recipient_id == 2 && append.term == 4 &&
                append.entries.size() == 1 &&
                velocita::decode_command(append.entries.front().payload).request_id == 42,
            "peer wire round trip failed");
}

void test_client_routing() {
    const auto parsed = velocita::parse_request_target(
        "2=aeron:udp?endpoint=127.0.0.1:40112|term-length=65536");
    require(parsed.node_id == 2 &&
                parsed.channel == "aeron:udp?endpoint=127.0.0.1:40112|term-length=65536",
            "request target parser damaged an Aeron URI containing equals signs");
    require_throws([] { (void)velocita::parse_request_target("aeron:udp"); },
                   "request target without node ID was accepted");
    require_throws([] { (void)velocita::parse_request_target("0=aeron:udp"); },
                   "non-positive request target node ID was accepted");
    require_throws([] { (void)velocita::parse_member_ids("1,2,2"); },
                   "duplicate Raft member was accepted");
    require_throws([] { (void)velocita::parse_member_ids("1,2,"); },
                   "trailing empty Raft member was accepted");

    const std::vector<velocita::RequestTarget> targets{
        {1, "node-1"}, {2, "node-2"}, {3, "node-3"}};
    velocita::validate_request_targets(targets, std::vector<std::int32_t>{1, 2, 3});
    require_throws([&] {
        velocita::validate_request_targets(
            std::vector<velocita::RequestTarget>{{1, "node-1"}, {1, "node-x"}});
    }, "duplicate request target node ID was accepted");
    require_throws([&] {
        velocita::validate_request_targets(
            std::vector<velocita::RequestTarget>{{1, "same"}, {2, "same"}});
    }, "duplicate request target channel was accepted");
    require_throws([&] {
        velocita::validate_request_targets(
            std::vector<velocita::RequestTarget>{{1, "node-1"}, {2, "node-2"}},
            std::vector<std::int32_t>{1, 2, 3});
    }, "missing member request target was accepted");
    require_throws([&] {
        velocita::validate_request_targets(targets, std::vector<std::int32_t>{1, 2});
    }, "non-member request target was accepted");

    velocita::LeaderRouter router(targets, {1, 2, 3});
    require(router.preferred_target() == 0 && !router.leader_id() &&
                router.state() == velocita::LeaderKnowledgeState::unknown,
            "router did not start in discovery mode");
    require(router.observe({velocita::ResponseStatus::not_leader, 2, 10, 0, 5, 0}) ==
                velocita::LeaderHintResult::updated &&
                router.leader_id() == 2 && router.preferred_target() == 1 &&
                router.state() == velocita::LeaderKnowledgeState::suspect,
            "router did not accept a valid leader hint");
    require(router.observe({velocita::ResponseStatus::not_leader, 3, 10, 0, 4, 0}) ==
                velocita::LeaderHintResult::stale && router.leader_id() == 2,
            "stale leader hint replaced newer routing knowledge");
    require(router.observe({velocita::ResponseStatus::not_leader, 99, 10, 0, 5, 0}) ==
                velocita::LeaderHintResult::invalid && router.leader_id() == 2,
            "same-term non-member hint damaged valid routing knowledge");
    require(router.observe({velocita::ResponseStatus::not_leader, 99, 10, 0, 6, 0}) ==
                velocita::LeaderHintResult::invalid && !router.leader_id() &&
                router.observed_term() == 6 &&
                router.state() == velocita::LeaderKnowledgeState::unknown,
            "new-term invalid hint did not invalidate stale leadership");
    require(router.observe({velocita::ResponseStatus::ok, 3, 10, 0, 7, 0}) ==
                velocita::LeaderHintResult::confirmed && router.leader_id() == 3 &&
                router.state() == velocita::LeaderKnowledgeState::known,
            "successful response did not confirm its leader");
    router.mark_target_unavailable(router.preferred_target());
    require(!router.leader_id() && router.preferred_target() == 0 &&
                router.state() == velocita::LeaderKnowledgeState::unknown,
            "unavailable leader target was not invalidated");
    require(router.observe({velocita::ResponseStatus::not_leader, -1, 10, 0, 8, 0}) ==
                velocita::LeaderHintResult::unknown && !router.leader_id(),
            "unknown leader hint was not represented explicitly");
    const auto legacy_ok = velocita::decode_response(
        legacy_response(1, velocita::ResponseStatus::ok));
    require(router.observe(legacy_ok) == velocita::LeaderHintResult::stale,
            "legacy response with an older term was not treated as stale");
    velocita::LeaderRouter legacy_router(targets, {1, 2, 3});
    require(legacy_router.observe(legacy_ok) == velocita::LeaderHintResult::unknown,
            "legacy successful response was rejected instead of accepted without a hint");
    velocita::Response invalid_v3_ok{velocita::ResponseStatus::ok, -1, 11, 0, 9, 0};
    require(legacy_router.observe(invalid_v3_ok) == velocita::LeaderHintResult::invalid,
            "V3 successful response without a leader ID was accepted");
    velocita::LeaderRouter zero_id_router(targets, {1, 2, 3});
    require(zero_id_router.observe(
                {velocita::ResponseStatus::not_leader, 2, 12, 0, 2, 0}) ==
                velocita::LeaderHintResult::updated,
            "zero-ID test router did not establish a baseline leader");
    require(zero_id_router.observe(
                {velocita::ResponseStatus::not_leader, 0, 12, 0, 3, 0}) ==
                velocita::LeaderHintResult::invalid && !zero_id_router.leader_id() &&
                zero_id_router.observed_term() == 3,
            "newer term with an invalid zero leader retained stale leadership");
}

void test_client_recovery() {
    require_throws([] {
        (void)velocita::ClientRecoveryPolicy({0, 250, 1, 250});
    }, "zero retry base was accepted");
    require_throws([] {
        (void)velocita::ClientRecoveryPolicy({10, 9, 1, 250});
    }, "retry cap below base was accepted");
    require_throws([] {
        (void)velocita::ClientRecoveryPolicy({10, 250, 0, 250});
    }, "zero unconfirmed admission limit was accepted");
    require_throws([] {
        (void)velocita::ClientRecoveryPolicy({10, 250, 1, 0});
    }, "zero response timeout was accepted");

    const velocita::ClientRecoveryPolicy recovery({10, 80, 1, 50});
    require(recovery.retry_delay_ns(42, 0) == 10,
            "first retry did not use the configured positive base");
    for (std::uint32_t retry = 0; retry < 12; ++retry) {
        const auto delay = recovery.retry_delay_ns(42, retry);
        const auto repeated = recovery.retry_delay_ns(42, retry);
        const auto exponential = retry >= 3 ? 80ULL : 10ULL << retry;
        const auto upper = std::min<std::uint64_t>(80, exponential);
        require(delay == repeated, "retry jitter was not deterministic");
        require(delay >= 10 && delay <= upper,
                "retry delay escaped its positive exponential bounds");
    }

    constexpr std::uint64_t now = 1'000;
    constexpr std::uint64_t deadline = 2'000;
    const auto next = recovery.next_retry_ns(99, 8, now, deadline);
    require(next > now && next <= deadline,
            "next retry escaped its current-time/deadline bounds");
    require(!velocita::ClientRecoveryPolicy::retry_due(next - 1, next, deadline),
            "retry became eligible before its monotonic deadline");
    require(velocita::ClientRecoveryPolicy::retry_due(next, next, deadline),
            "retry was not eligible at its monotonic deadline");
    require(recovery.next_retry_ns(99, 8, now, 1'025) == 1'025,
            "retry schedule was not clamped to the command deadline");
    require(recovery.response_deadline_ns(now, deadline) == 1'050 &&
                recovery.response_deadline_ns(now, 1'025) == 1'025,
            "response-silence deadline was not bounded by the command deadline");
    require(!velocita::ClientRecoveryPolicy::retry_due(deadline, next, deadline) &&
                velocita::ClientRecoveryPolicy::deadline_reached(deadline, deadline),
            "command deadline permitted another retry");

    require(recovery.may_admit(velocita::LeaderKnowledgeState::unknown, 0),
            "bounded discovery admission was rejected");
    require(!recovery.may_admit(velocita::LeaderKnowledgeState::unknown, 1) &&
                !recovery.may_admit(velocita::LeaderKnowledgeState::suspect, 1),
            "unconfirmed leadership admitted beyond its configured bound");
    require(recovery.may_admit(velocita::LeaderKnowledgeState::known, 100),
            "known leadership was incorrectly constrained by the discovery bound");
}

void test_owner_loop_scheduler() {
    using namespace std::chrono_literals;
    using Scheduler = velocita::OwnerLoopScheduler;

    static_assert(std::is_trivially_destructible_v<Scheduler>);

    velocita::OwnerLoopConfig config;
    const Scheduler::TimePoint started{};
    Scheduler scheduler(config, started);

    require(scheduler.config().peer_fragment_limit == 64 &&
                scheduler.config().client_fragment_limit == 1 &&
                scheduler.config().raft_output_drain_limit == 128 &&
                scheduler.config().completed_response_drain_limit == 64 &&
                scheduler.config().peer_offer_limit == 64 &&
                scheduler.config().response_offer_limit == 16 &&
                scheduler.config().peer_queue_capacity == 4096 &&
                scheduler.config().response_queue_capacity == 4096,
            "owner-loop scheduler lost a configured work bound");
    require(!scheduler.consume_due_tick(started + 999us),
            "owner-loop tick became due before its deadline");
    require(scheduler.client_poll_decision(started, true).fragment_limit == 1,
            "owner-loop rejected client work with a full tick interval available");
    require(scheduler.client_poll_decision(started + 749us, true).fragment_limit == 1 &&
                scheduler.client_poll_decision(started + 750us, true).blocked_by ==
                    velocita::ClientPollBlockReason::tick_slack,
            "owner-loop client safety margin boundary is incorrect");
    require(scheduler.client_poll_decision(started, false).blocked_by ==
                velocita::ClientPollBlockReason::response_backlog,
            "owner-loop did not distinguish response backlog from tick slack");

    const auto first_tick = scheduler.consume_due_tick(started + 1ms);
    require(first_tick && *first_tick == 1 &&
                scheduler.next_tick_deadline() == started + 2ms &&
                scheduler.maximum_tick_gap_nanoseconds() == 1'000'000,
            "owner-loop did not consume its first one-millisecond tick");

    const auto delayed_tick = scheduler.consume_due_tick(started + 3400us);
    require(delayed_tick && *delayed_tick == 2 &&
                scheduler.next_tick_deadline() == started + 4ms &&
                scheduler.maximum_tick_gap_nanoseconds() == 2'400'000,
            "owner-loop did not retain the sub-millisecond remainder after a delayed tick");
    require(scheduler.client_poll_decision(started + 3400us, true).fragment_limit == 1 &&
                scheduler.client_poll_decision(started + 3800us, true).fragment_limit == 0,
            "owner-loop tick slack did not track the retained remainder");

    auto invalid = config;
    invalid.tick_interval = 0ms;
    require_throws([&] { (void)Scheduler(invalid, started); },
                   "zero owner-loop tick interval was accepted");
    invalid = config;
    invalid.client_safety_margin = 1ms;
    require_throws([&] { (void)Scheduler(invalid, started); },
                   "owner-loop safety margin covering the tick interval was accepted");
    invalid = config;
    invalid.client_fragment_limit = 0;
    require_throws([&] { (void)Scheduler(invalid, started); },
                   "zero owner-loop client budget was accepted");
    invalid = config;
    invalid.response_queue_capacity = 0;
    require_throws([&] { (void)Scheduler(invalid, started); },
                   "zero owner-loop response queue capacity was accepted");

    config.raft_output_drain_limit = 4;
    config.completed_response_drain_limit = 3;
    config.peer_offer_limit = 2;
    Scheduler ordered(config, started);
    std::vector<std::string> calls;
    auto fake_now = started + 1ms;
    int drain_calls{};
    const auto iteration = velocita::run_owner_loop_iteration(
        ordered,
        [&] { return fake_now; },
        [&](const int elapsed) {
            calls.emplace_back("tick");
            require(elapsed == 1, "ordered iteration received the wrong tick duration");
        },
        [&](const int limit) {
            calls.emplace_back("peer-poll");
            require(limit == config.peer_fragment_limit,
                    "ordered iteration lost the peer fragment bound");
            return limit;
        },
        [&](std::size_t& peer_remaining, std::size_t& response_remaining) {
            calls.emplace_back(drain_calls++ == 0 ? "drain-before" : "drain-after");
            const auto peer_work = std::min<std::size_t>(3, peer_remaining);
            const auto response_work = std::min<std::size_t>(2, response_remaining);
            peer_remaining -= peer_work;
            response_remaining -= response_work;
            return peer_work + response_work;
        },
        [&](std::size_t& remaining) {
            calls.emplace_back(remaining == 2 ? "peer-offer-before" : "peer-offer-after");
            const auto work = std::min<std::size_t>(1, remaining);
            remaining -= work;
            return work;
        },
        [&] {
            calls.emplace_back("response-capacity");
            return true;
        },
        [&](const int limit) {
            calls.emplace_back("client-poll");
            require(limit == 1, "ordered iteration exceeded the client fragment bound");
            return limit;
        },
        [&](const std::size_t limit) {
            calls.emplace_back("response-offer");
            require(limit == config.response_offer_limit,
                    "ordered iteration lost the response offer bound");
            return std::size_t{1};
        });
    const std::vector<std::string> expected{
        "tick", "peer-poll", "drain-before", "peer-offer-before",
        "response-capacity", "client-poll", "drain-after",
        "peer-offer-after", "response-offer"};
    require(calls == expected, "owner-loop control-plane ordering changed");
    require(iteration.ticked && iteration.peer_fragments == config.peer_fragment_limit &&
                iteration.client_fragments == 1 && iteration.progressed == 10 &&
                iteration.remaining.raft_outputs_remaining == 0 &&
                iteration.remaining.completed_responses_remaining == 0 &&
                iteration.remaining.peer_offers_remaining == 0,
            "owner-loop pre/post service points did not share one iteration budget");

    Scheduler tick_slack(config, started);
    fake_now = started + 800us;
    bool client_polled{};
    const auto slack_iteration = velocita::run_owner_loop_iteration(
        tick_slack,
        [&] { return fake_now; },
        [](int) {},
        [&](int) {
            fake_now = started + 900us;
            return 1;
        },
        [](std::size_t&, std::size_t&) { return std::size_t{}; },
        [](std::size_t&) { return std::size_t{}; },
        [] { return true; },
        [&](int) {
            client_polled = true;
            return 1;
        },
        [](std::size_t) { return std::size_t{}; });
    require(!client_polled && slack_iteration.peer_fragments == 1 &&
                slack_iteration.client_blocked_by ==
                    velocita::ClientPollBlockReason::tick_slack,
            "peer work near a tick deadline did not suppress client admission");
}

void route_messages(std::vector<std::unique_ptr<velocita::RaftNode>>& nodes) {
    std::vector<velocita::PeerMessage> messages;
    for (auto& node : nodes) {
        while (auto message = node->take_outbound_message()) {
            messages.push_back(std::move(*message));
        }
    }
    for (const auto& message : messages) {
        require(message.recipient_id >= 1 && message.recipient_id <= static_cast<int>(nodes.size()),
                "peer message has unknown recipient");
        nodes[static_cast<std::size_t>(message.recipient_id - 1)]->receive_peer(
            velocita::decode_peer_message(velocita::encode_peer_message(message)));
    }
}

void test_three_node_cluster(const std::filesystem::path& root) {
    std::vector<std::unique_ptr<velocita::RaftNode>> nodes;
    for (int id = 1; id <= 3; ++id) {
        nodes.push_back(std::make_unique<velocita::RaftNode>(id, root / std::to_string(id),
                                                            std::vector<std::int32_t>{1, 2, 3}));
    }
    velocita::RaftNode* leader = nullptr;
    for (int round = 0; round < 1000 && leader == nullptr; ++round) {
        for (auto& node : nodes) node->tick(10);
        route_messages(nodes);
        for (auto& node : nodes) {
            if (node->is_leader()) {
                require(leader == nullptr, "cluster elected multiple leaders in one term");
                leader = node.get();
            }
        }
    }
    require(leader != nullptr, "three-node cluster did not elect a leader");
    for (int round = 0; round < 20; ++round) {
        for (auto& node : nodes) node->tick(10);
        route_messages(nodes);
    }
    const auto leader_id = leader->status().node_id;
    for (auto& node : nodes) {
        if (node.get() == leader) continue;
        const auto rejection = node->propose({velocita::CommandType::get, 8999, 0});
        require(rejection && rejection->status == velocita::ResponseStatus::not_leader &&
                    rejection->leader_id == leader_id,
                "follower did not return the current leader ID");
        break;
    }
    require(!leader->propose({velocita::CommandType::increment, 9001, 11}),
            "new multi-node proposal completed before quorum acknowledgement");

    for (int round = 0; round < 1000; ++round) {
        for (auto& node : nodes) node->tick(10);
        route_messages(nodes);
        bool converged = true;
        for (const auto& node : nodes) {
            const auto status = node->status();
            converged = converged && status.commit_index == 1 && status.applied_index == 1 &&
                        status.counter_value == 11;
        }
        if (converged) {
            const auto completion = leader->take_completed_response();
            require(completion && completion->request_id == 9001 &&
                        completion->leader_id == leader_id && completion->value == 11 &&
                        !leader->take_completed_response(),
                    "leader did not complete the client request after quorum commit");
            return;
        }
    }
    throw std::runtime_error("three-node cluster did not converge after a proposal");
}

void test_restart_and_dedup(const std::filesystem::path& data_dir) {
    {
        velocita::RaftNode node(1, data_dir);
        elect(node);
        const auto first = node.submit({velocita::CommandType::increment, 1001, 5});
        require(first.status == velocita::ResponseStatus::ok && first.value == 5, "increment failed");
        const auto duplicate = node.submit({velocita::CommandType::increment, 1001, 5});
        require(duplicate.value == 5, "duplicate command was applied twice");
        const auto read = node.submit({velocita::CommandType::get, 1002, 0});
        require(read.value == 5, "read returned wrong value");
        require(node.status().current_index == 2, "duplicate unexpectedly grew the log");
    }
    {
        velocita::RaftNode node(1, data_dir);
        require(node.status().counter_value == 5, "committed state was not replayed after restart");
        require(node.status().commit_index == 2 && node.status().applied_index == 2,
                "commit/applied indexes were not restored");
        elect(node);
        const auto duplicate = node.submit({velocita::CommandType::increment, 1001, 5});
        require(duplicate.value == 5, "dedup state did not survive restart");
    }

    const velocita::WriteAheadLog wal(data_dir);
    require(wal.read_all().size() == 2, "WAL record count mismatch");
}

}

int main() {
    try {
        test_wire();
        test_client_routing();
        test_client_recovery();
        test_owner_loop_scheduler();
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto data_dir = std::filesystem::temp_directory_path() /
                              ("velocita-raft-test-" + std::to_string(unique));
        std::filesystem::create_directories(data_dir);
        try {
            test_restart_and_dedup(data_dir);
            test_three_node_cluster(data_dir / "cluster");
            std::filesystem::remove_all(data_dir);
        } catch (...) {
            std::filesystem::remove_all(data_dir);
            throw;
        }
        std::cout << "all core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return 1;
    }
}
