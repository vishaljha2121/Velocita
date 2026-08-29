#pragma once

#include "velocita/counter_fsm.hpp"
#include "velocita/storage.hpp"
#include "velocita/wire.hpp"

#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
#include "velocita/benchmark_timing.hpp"
#endif

extern "C" {
#include "raft.h"
}

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace velocita {

struct NodeStatus {
    std::int32_t node_id{};
    std::int32_t leader_id{-1};
    std::string_view role;
    std::int64_t term{};
    std::int64_t first_index{1};
    std::int64_t current_index{};
    std::int64_t commit_index{};
    std::int64_t applied_index{};
    std::int64_t counter_value{};
};

class RaftNode {
public:
    RaftNode(std::int32_t node_id, std::filesystem::path data_dir,
             std::vector<std::int32_t> members = {}, bool enable_trace = false);
    ~RaftNode();
    RaftNode(const RaftNode&) = delete;
    RaftNode& operator=(const RaftNode&) = delete;

    void tick(int elapsed_ms);
    [[nodiscard]] bool is_leader() const;
    [[nodiscard]] std::optional<Response> propose(const Command& command);
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    [[nodiscard]] std::optional<Response> propose(const Command& command,
                                                  std::uint64_t ingress_ns);
    void benchmark_record_peer_offer(std::int32_t peer_id, std::int64_t last_index,
                                     std::uint64_t offered_ns) noexcept;
#endif
    [[nodiscard]] Response submit(const Command& command);
    void receive_peer(const PeerMessage& message);
    [[nodiscard]] std::optional<PeerMessage> take_outbound_message();
    [[nodiscard]] std::optional<Response> take_completed_response();
    [[nodiscard]] NodeStatus status() const;

private:
    void assert_owner() const;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    void persist_commit_index(std::uint64_t quorum_append_ns = 0);
    [[nodiscard]] std::uint64_t benchmark_peer_round_trip(
        std::int32_t peer_id, std::int64_t current_index, std::uint64_t received_ns) const noexcept;
#else
    void persist_commit_index();
#endif

    static int send_request_vote(raft_server_t*, void*, raft_node_t*, msg_requestvote_t*);
    static int send_append_entries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*);
    static int apply_log(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int persist_vote(raft_server_t*, void*, raft_node_id_t);
    static int persist_term(raft_server_t*, void*, raft_term_t, raft_node_id_t);
    static int log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_poll(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_pop(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_clear(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static void log_message(raft_server_t*, raft_node_t*, void*, const char*);

    std::int32_t node_id_;
    std::vector<std::int32_t> members_;
    std::thread::id owner_thread_;
    MetadataStore metadata_store_;
    WriteAheadLog wal_;
    PersistentMetadata metadata_;
    CounterFsm fsm_;
    raft_server_t* server_{};
    bool restoring_{true};
    std::string callback_error_;
    std::deque<PeerMessage> outbound_messages_;
    std::deque<Response> completed_responses_;
    std::unordered_set<std::uint64_t> pending_requests_;
#ifdef VELOCITA_BENCHMARK_INSTRUMENTATION
    struct BenchmarkPeerOffer {
        std::int64_t last_index{};
        std::uint64_t offered_ns{};
    };
    std::unique_ptr<BenchmarkTraceTable> benchmark_trace_;
    std::unordered_map<std::int32_t, BenchmarkPeerOffer> benchmark_peer_offers_;
#endif
};

}  // namespace velocita
