#define VELOCITA_RESULT_ADMISSION_FIXTURES_ONLY
#include "v2_result_admission_red.cpp"

namespace {
constexpr std::size_t kCandidateLogEntries = kRetainedLogEntries;
constexpr std::size_t kCandidateControlEntries = kRetainedLogProtectedEntries;

void outbound_response_growth(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    for (int i = 0; i < 512; ++i)
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2,
            velocita::AppendEntries{1, 0, 0, 0, {}}});
    std::size_t responses{};
    while (const auto reply = identity_test::take_outbound_message(node)) {
        const auto* append = std::get_if<velocita::AppendEntriesResponse>(&reply->payload);
        require(append && append->success && append->current_index == 0 &&
                    reply->recipient_id == 1,
                "heartbeat diagnostic produced an unexpected response");
        ++responses;
    }
    std::cout << "heartbeat_requests=512 queued_responses=" << responses << '\n';
    require(responses == 1,
            "heartbeat replies were not coalesced to one owned peer/kind slot");
}

void outbound_payload_growth(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    while (identity_test::take_outbound_message(leader)) {}
    for (std::uint64_t i = 1; i <= 16; ++i)
        require(leader.propose(encoded(command(i, PublishPolicy{policy_artifact()}))).status ==
                    ProposeStatus::admitted,
                "outbound payload fixture could not admit its bounded16 entries");
    for (int i = 0; i < 128; ++i) leader.tick(200);
    require(leader.status().current_index == 16 && leader.status().applied_index == 0,
            "outbound fixture unexpectedly committed or enlarged its source log");
    std::array<std::size_t, 4> counts{}, bytes{};
    std::array<std::byte, kMaxBoundPeerBytes> output{};
    auto scratch = std::make_unique<BoundFrameScratch>();
    while (const auto message = identity_test::take_outbound_message(leader)) {
        const auto encoded_frame = encode_bound_peer(*message, leader.static_identity(), output, *scratch);
        require(encoded_frame.error == BoundFrameError::none,
                "outbound diagnostic could not encode its actual message");
        const auto peer = static_cast<std::size_t>(message->recipient_id);
        require(peer < counts.size(), "outbound diagnostic peer outside fixture");
        ++counts[peer];
        bytes[peer] += encoded_frame.bytes_written;
    }
    const auto largest_count = *std::max_element(counts.begin(), counts.end());
    const auto largest_bytes = *std::max_element(bytes.begin(), bytes.end());
    std::cout << "retained_entries=16 largest_peer_descriptors=" << largest_count
              << " largest_peer_encoded_bytes=" << largest_bytes << '\n';
    require(largest_bytes <= kMaxBoundPeerBytes && largest_count == 1,
            "retry copies were not coalesced to one bounded AppendEntries per peer");
    cluster.settle(100);
    for (auto& node : cluster.nodes) {
        require(node->status().applied_index == 16, "coalesced packet loss wedged replication");
        for (std::int64_t index = 1; index <= 16; ++index) {
            const auto result = node->take_committed_result();
            require(result && result->index == index &&
                        result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(index)},
                    "retry recovery lost or duplicated a committed result");
        }
        require(!node->take_committed_result(), "retry recovery duplicated result");
    }
}

void retained_log_growth(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    constexpr auto ordinary = kCandidateLogEntries - kCandidateControlEntries;
    for (std::uint64_t start = 1; start <= ordinary; start += 16) {
        const auto end = std::min(start + 15, static_cast<std::uint64_t>(ordinary));
        velocita::AppendEntries batch{1, static_cast<std::int64_t>(start - 1),
            start == 1 ? 0 : 1, static_cast<std::int64_t>(end), {}};
        for (auto i = start; i <= end; ++i)
            batch.entries.push_back({1, static_cast<std::int32_t>(i), RAFT_LOGTYPE_NORMAL,
                                     encoded(ordinary_result_command(i))});
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, batch});
        node.flush_durability();
        node.tick(0);
        for (auto i = start; i <= end; ++i) {
            const auto result = node.take_committed_result();
            require(result && result->index == static_cast<std::int64_t>(i),
                    "retained-log fixture did not drain each result");
        }
        while (identity_test::take_outbound_message(node)) {}
    }
    require(node.reserved_result_count() == 0 && node.ready_result_count() == 0,
            "result pressure contaminated retained-log diagnostic");
    const auto next = static_cast<std::int64_t>(ordinary + 1);
    identity_test::deliver_declared_peer(node, PeerMessage{1, 2,
        velocita::AppendEntries{1, next - 1, 1, next, {
            {1, static_cast<std::int32_t>(next), RAFT_LOGTYPE_NORMAL,
             encoded(ordinary_result_command(static_cast<std::uint64_t>(next)))}}}});
    std::cout << "ordinary_log_candidate=" << ordinary << " actual_log_end="
              << node.status().current_index << " ready_results=" << node.ready_result_count() << '\n';
    require(node.status().current_index == static_cast<std::int64_t>(ordinary),
            "draining results does not bound retained ordinary log ownership");
    require(node.retained_log_count() == ordinary && node.log_capacity_refusals() == 1 &&
                node.ready_result_count() == 0, "log refusal accepted a result obligation");
    const auto control = encoded(command(20000, AdvanceKillEpoch{KillEpoch{1}}));
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2,
        velocita::AppendEntries{2, static_cast<std::int64_t>(ordinary), 1,
            static_cast<std::int64_t>(ordinary), {
                {2, 1, RAFT_LOGTYPE_NORMAL, control},
                {2, 2, RAFT_LOGTYPE_NORMAL, encoded(ordinary_result_command(20001))}}}});
    require(node.status().term == 2 && node.retained_log_count() == ordinary &&
                node.ready_result_count() == 0 && node.log_capacity_refusals() == 2,
            "full follower partially appended control+ordinary batch or hid newer term");
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2,
        velocita::AppendEntries{2, static_cast<std::int64_t>(ordinary), 1, 0,
                               {marker_entry(30000, 2)}}});
    require(node.retained_log_count() == ordinary && node.log_capacity_refusals() == 3,
            "authority marker consumed protected log capacity");
    for (std::uint64_t i = ordinary + 1; i <= kCandidateLogEntries; ++i) {
        identity_test::deliver_declared_peer(node, PeerMessage{3, 2,
            velocita::AppendEntries{2, static_cast<std::int64_t>(i - 1), i == ordinary + 1 ? 1 : 2,
                static_cast<std::int64_t>(i), {{2, static_cast<std::int32_t>(i), RAFT_LOGTYPE_NORMAL,
                    encoded(command(20000 + i, AdvanceKillEpoch{KillEpoch{i}}))}}}});
        node.flush_durability();
        node.tick(0);
        const auto result = node.take_committed_result();
        require(result && result->index == static_cast<std::int64_t>(i),
                "ordinary saturation consumed protected log reserve");
    }
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2,
        velocita::AppendEntries{2, static_cast<std::int64_t>(kCandidateLogEntries), 2,
            static_cast<std::int64_t>(kCandidateLogEntries), {{2, 1, RAFT_LOGTYPE_NORMAL, control}}}});
    require(node.retained_log_count() == kCandidateLogEntries && node.log_capacity_refusals() == 4 &&
                node.ready_result_count() == 0, "protected log pool exceeded total capacity");
}

void leader_log_capacity(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 1, {1}));
    node.tick(1000);
    for (std::uint64_t i = 1; i <= kRetainedLogOrdinaryEntries; ++i) {
        require(node.propose(encoded(ordinary_result_command(i))).status == ProposeStatus::admitted,
                "leader refused before ordinary log capacity");
        node.flush_durability(); node.tick(0);
        const auto result = node.take_committed_result();
        require(result && result->index == static_cast<std::int64_t>(i), "leader result fixture did not drain");
    }
    const auto before = directory_image(root);
    require(node.propose(encoded(ordinary_result_command(9000))).status == ProposeStatus::log_capacity_exhausted,
            "leader ordinary admission bypassed log bound");
    auto request = authority_request();
    request.authority_domain = node.static_identity().authority_domain();
    require(node.propose_authority_barrier(request).status == ProposeStatus::log_capacity_exhausted,
            "leader marker bypassed ordinary log bound");
    require(node.retained_log_count() == kRetainedLogOrdinaryEntries && directory_image(root) == before,
            "local log refusal mutated history");
    const auto stopped = node.propose(encoded(command(9001, AdvanceKillEpoch{KillEpoch{1}})), ProposeLane::control);
    require(stopped.status == ProposeStatus::admitted, "leader cannot use protected log capacity");
    node.flush_durability(); node.tick(0);
    const auto result = node.take_committed_result();
    require(result && result->index == static_cast<std::int64_t>(kRetainedLogOrdinaryEntries + 1) &&
                node.state().kill_epoch == KillEpoch{1}, "protected kill was not applied after ordinary prefix");
}

void oversized_committed_recovery(const std::filesystem::path& root, bool markers = false) {
    const std::vector<std::int32_t> members = markers ? std::vector<std::int32_t>{1, 2, 3}
                                                    : std::vector<std::int32_t>{1};
    identity_test::provision(root, 1, members);
    const auto count = kRetainedLogOrdinaryEntries + 1;
    identity_test::metadata_store(root).save({1, -1, markers ? 0 : static_cast<std::int64_t>(count)});
    identity_test::RawWalFixture wal(root);
    for (std::uint64_t i = 1; i <= count; ++i) {
        const auto marker = marker_entry(i);
        wal.append({i, 1, static_cast<std::int32_t>(i), markers ? kAuthorityBarrierEntryType : RAFT_LOGTYPE_NORMAL,
                    markers ? marker.payload : encoded(ordinary_result_command(i))});
    }
    const auto before = directory_image(root);
    bool refused{};
    try { ReplicatedKernel node(identity_test::recover(root, 1, members)); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("retained log capacity") != std::string::npos,
                std::string("oversized log recovery failed for wrong reason: ") + error.what());
        refused = true;
    }
    require(refused && directory_image(root) == before,
            "oversized committed history was accepted or rewritten");
}

void queued_bytes_survive_suffix_replacement(const std::filesystem::path& root) {
    Cluster cluster(root, 3);
    auto& leader = cluster.elect();
    const auto id = leader.status().node_id;
    const auto term = leader.status().term;
    while (identity_test::take_outbound_message(leader)) {}
    const auto original = encoded(ordinary_result_command(1));
    require(leader.propose(original).status == ProposeStatus::admitted, "queued ownership initial proposal");
    leader.flush_durability();
    const auto other = id == 1 ? 2 : 1;
    identity_test::deliver_declared_peer(leader, PeerMessage{other, id,
        velocita::AppendEntries{term + 1, 0, 0, 0, {{term + 1, 2, RAFT_LOGTYPE_NORMAL,
            encoded(ordinary_result_command(2))}}}});
    std::size_t old_packets{};
    while (const auto message = identity_test::take_outbound_message(leader)) {
        const auto* append = std::get_if<velocita::AppendEntries>(&message->payload);
        if (!append || append->term != term) continue;
        require(append->entries.size() == 1 && append->entries[0].payload == original,
                "queued packet borrowed overwritten retained-log bytes");
        ++old_packets;
    }
    require(old_packets == 2, "ownership fixture did not retain both old peer packets");
}

void saturated_marker_suffix_replacement(const std::filesystem::path& root) {
    ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}));
    for (std::uint64_t start = 1; start <= kRetainedLogOrdinaryEntries; start += 16) {
        velocita::AppendEntries batch{1, static_cast<std::int64_t>(start - 1), start == 1 ? 0 : 1, 0, {}};
        for (auto i = start; i < start + 16; ++i) batch.entries.push_back(marker_entry(i));
        identity_test::deliver_declared_peer(node, PeerMessage{1, 2, batch});
        node.flush_durability();
        while (identity_test::take_outbound_message(node)) {}
    }
    require(node.retained_log_count() == kRetainedLogOrdinaryEntries && node.reserved_result_count() == 0,
            "marker saturation fixture consumed result capacity");
    const auto prev = static_cast<std::int64_t>(kRetainedLogOrdinaryEntries - 2);
    velocita::AppendEntries replacement{2, prev, 1, prev + 3, {}};
    for (int i = 1; i <= 3; ++i)
        replacement.entries.push_back({2, i, RAFT_LOGTYPE_NORMAL,
            encoded(ordinary_result_command(static_cast<std::uint64_t>(9000 + i)))});
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, replacement});
    require(node.log_capacity_refusals() == 1 && node.retained_log_count() == kRetainedLogOrdinaryEntries &&
                node.reserved_result_count() == 0 && node.status().applied_index == prev,
            "infeasible replacement partially consumed hypothetical suffix capacity");
    replacement.entries.pop_back();
    replacement.leader_commit = prev + 2;
    identity_test::deliver_declared_peer(node, PeerMessage{3, 2, replacement});
    node.flush_durability(); node.tick(0);
    for (int i = 1; i <= 2; ++i) {
        const auto result = node.take_committed_result();
        require(result && result->index == prev + i &&
                    result->identity.request_sequence == RequestSequence{static_cast<std::uint64_t>(9000 + i)},
                "saturated replacement failed to transfer durable suffix capacity");
    }
    require(node.retained_log_count() == kRetainedLogOrdinaryEntries && !node.take_committed_result(),
            "suffix transfer changed log cardinality or duplicated result");
}
}

int main() {
    try {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-queue-review.XXXXXX").string();
        const auto* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "capacity diagnostic mkdtemp failed");
        const std::filesystem::path root{made};
        const auto* selected = std::getenv("CAPACITY_REVIEW_CASE");
        const std::string name = selected ? selected : "outbound";
        if (name == "outbound") {
            outbound_response_growth(root / "responses");
            outbound_payload_growth(root / "payload");
            queued_bytes_survive_suffix_replacement(root / "ownership");
        }
        else if (name == "responses") outbound_response_growth(root);
        else if (name == "payload") outbound_payload_growth(root);
        else if (name == "log") {
            retained_log_growth(root / "follower");
            {
                ReplicatedKernel recovered(identity_test::recover(root / "follower", 2, {1, 2, 3}));
                require(recovered.retained_log_count() == kRetainedLogEntries &&
                            recovered.state().kill_epoch == KillEpoch{kRetainedLogEntries} &&
                            recovered.ready_result_count() == 0,
                        "full committed log failed restart accounting or replayed notifications");
            }
            leader_log_capacity(root / "leader");
            oversized_committed_recovery(root / "oversized");
            oversized_committed_recovery(root / "oversized-markers", true);
            saturated_marker_suffix_replacement(root / "replacement");
        }
        else throw std::runtime_error("unknown capacity review selector");
        std::filesystem::remove_all(root);
        std::cout << "capacity review diagnostic passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
