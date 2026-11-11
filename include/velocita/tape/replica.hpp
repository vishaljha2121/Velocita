// One tape voter's Raft replica: the pinned willemt/raft fork, the tape's write-ahead log and the reducer.
#pragma once

#include "velocita/peer_wire.hpp"
#include "velocita/tape/reducer.hpp"
#include "velocita/tape/wal.hpp"

extern "C" {
#include "raft.h"
}

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <tuple>
#include <vector>

namespace velocita::tape {

struct ReplicaConfig {
    std::int32_t node_id{};
    std::vector<std::int32_t> members;
    std::filesystem::path data_dir;
    int election_timeout_ms{1000};
    int request_timeout_ms{200};
    Config tape;
};

enum class ProposeStatus : std::uint8_t { proposed = 0, not_leader = 1, refused = 2 };

using RequestKey = std::tuple<std::uint8_t, std::uint64_t, std::uint64_t>;

struct ReplicaStatus {
    std::int32_t node_id{};
    std::int32_t leader_id{-1};
    bool leader{};
    std::int64_t term{};
    std::uint64_t current_index{};
    std::uint64_t commit_index{};
    std::uint64_t applied_index{};
    std::uint64_t synced_index{};
    std::uint64_t wal_bytes{};
};

class TapeReplica {
public:
    using Send = std::function<void(const PeerMessage&)>;
    using Applied = std::function<void(std::uint64_t index, std::span<const std::byte> bytes, const ApplyResult&)>;

    TapeReplica(ReplicaConfig config, Send send, Applied applied = {});
    ~TapeReplica();
    TapeReplica(const TapeReplica&) = delete;
    TapeReplica& operator=(const TapeReplica&) = delete;

    void tick(int elapsed_ms);
    void receive(const PeerMessage& message);
    [[nodiscard]] ProposeStatus propose(std::span<const std::byte> entry_bytes);

    [[nodiscard]] bool is_leader() const;
    [[nodiscard]] ReplicaStatus status() const;
    [[nodiscard]] const TapeState& state() const noexcept { return *state_; }
    [[nodiscard]] bool request_in_log(const RequestKey& key) const { return in_log_.contains(key); }
    [[nodiscard]] std::uint64_t highest_request_in_log(std::uint8_t recorder, std::uint64_t session) const;
    [[nodiscard]] std::uint64_t highest_tick_in_log() const;
    [[nodiscard]] bool head_signature_in_log(std::uint64_t tick, std::uint8_t voter) const {
        return head_sigs_in_log_.contains({tick, voter});
    }
    [[nodiscard]] const std::filesystem::path& data_dir() const noexcept { return config_.data_dir; }

private:
    static int cb_send_requestvote(raft_server_t*, void*, raft_node_t*, msg_requestvote_t*);
    static int cb_send_appendentries(raft_server_t*, void*, raft_node_t*, msg_appendentries_t*);
    static int cb_applylog(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int cb_persist_vote(raft_server_t*, void*, raft_node_id_t);
    static int cb_persist_term(raft_server_t*, void*, raft_term_t, raft_node_id_t);
    static int cb_log_offer(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int cb_log_poll(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int cb_log_pop(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static int cb_log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t);
    static raft_index_t cb_durable_idx(raft_server_t*, void*);

    void index_entry(std::uint64_t index, std::span<const std::byte> bytes);
    void unindex_entry(std::uint64_t index);
    void flush();

    ReplicaConfig config_;
    Send send_;
    Applied applied_;
    TapeWal wal_;
    std::unique_ptr<TapeState> state_;
    raft_server_t* server_{};
    bool restoring_{true};
    std::uint64_t base_index_{1};
    std::deque<std::vector<std::byte>> payloads_;
    std::map<std::uint64_t, RequestKey> requests_by_index_;
    std::set<RequestKey> in_log_;
    std::map<std::uint64_t, std::uint64_t> ticks_by_index_;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint8_t>> heads_by_index_;
    std::set<std::pair<std::uint64_t, std::uint8_t>> head_sigs_in_log_;
};

}
