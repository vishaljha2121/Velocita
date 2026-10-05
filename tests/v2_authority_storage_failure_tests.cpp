#include "velocita/v2/authority_barrier.hpp"
#include "velocita/v2/replication.hpp"

#include "v2_identity_test_support.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Digest256 digest(const std::uint8_t tag) {
    Digest256 value;
    value.bytes[0] = static_cast<std::byte>(tag);
    value.bytes[1] = std::byte{0x5a};
    return value;
}

AuthorityBarrier marker(const std::uint64_t sequence, const Digest256 domain) {
    AuthorityBarrier value;
    value.incarnation[0] = std::byte{1};
    value.request_sequence = sequence;
    value.effect_id = EffectId{sequence};
    value.expected_effect_payload_digest = digest(2);
    value.expected_kill_epoch = KillEpoch{1};
    value.expected_executor_generation = ExecutorGeneration{1};
    value.catalog_root = digest(3);
    value.authority_domain = domain;
    return value;
}

std::vector<std::byte> encoded_marker(const AuthorityBarrier& value) {
    std::array<std::byte, kAuthorityBarrierBytes> bytes{};
    const auto encoded =
        encode_authority_barrier(kAuthorityBarrierEntryType, value, bytes);
    require(encoded.error == AuthorityBarrierCodecError::none,
            "authority storage-failure fixture did not encode");
    return {bytes.begin(), bytes.end()};
}

struct FailRewrite {
    bool armed{};
    bool observed{};
};

struct FailFlush {
    bool armed{};
    bool observed{};
};

struct FailMetadata {
    bool armed{};
    bool observed{};
    std::size_t fail_on{1};
    std::size_t calls{};
    velocita::MetadataCheckpoint checkpoint{velocita::MetadataCheckpoint::before_write};
};

struct ThreadFailFlush {
    std::mutex mutex;
    std::condition_variable observed;
    bool armed{};
    bool reached{};
};

void fail_thread_before_flush_sync(const StorageCheckpoint point, void* raw) {
    auto& state = *static_cast<ThreadFailFlush*>(raw);
    if (point != StorageCheckpoint::before_file_sync) return;
    {
        std::lock_guard lock(state.mutex);
        if (!state.armed) return;
        state.reached = true;
    }
    state.observed.notify_one();
    throw std::runtime_error("injected threaded WAL flush failure");
}

void fail_before_metadata_write(const velocita::MetadataCheckpoint point, void* raw) {
    auto& state = *static_cast<FailMetadata*>(raw);
    if (!state.armed || point != state.checkpoint) return;
    if (++state.calls != state.fail_on) return;
    state.observed = true;
    throw std::runtime_error("injected metadata write failure");
}

void fail_before_flush_sync(const StorageCheckpoint point, void* raw) {
    auto& state = *static_cast<FailFlush*>(raw);
    if (!state.armed || point != StorageCheckpoint::before_file_sync) return;
    state.observed = true;
    throw std::runtime_error("injected WAL flush failure");
}

void fail_after_rewrite_sync(const StorageCheckpoint point, void* raw) {
    auto& state = *static_cast<FailRewrite*>(raw);
    if (!state.armed || point != StorageCheckpoint::file_synced) return;
    state.observed = true;
    throw std::runtime_error("injected suffix rewrite failure");
}

void require_protocol_closed(ReplicatedKernel& node, const AuthorityBarrier& request,
                             const std::string& path);

void elect_with_one_remote_vote(ReplicatedKernel& node) {
    for (int attempt = 0; attempt < 100 && !node.is_leader(); ++attempt) {
        node.tick(100);
        while (auto outbound = identity_test::take_outbound_message(node)) {
            const auto* request = std::get_if<velocita::RequestVote>(&outbound->payload);
            if (request == nullptr) continue;
            velocita::RequestVoteResponse grant{request->term, true};
            grant.prevote = request->prevote;
            identity_test::deliver_declared_peer(node, velocita::PeerMessage{1, 2, grant});
            break;
        }
    }
    require(node.is_leader(), "authority storage-failure fixture did not elect");
}

void test_failed_suffix_rewrite_latches_node(
    const std::filesystem::path& root) {
    const auto domain = identity_test::authority_domain({1, 2, 3});
    FailRewrite failure;
    std::int64_t original_index{};
    {
        ReplicatedKernel node(identity_test::bootstrap(root, 2, {1, 2, 3}),
                              {fail_after_rewrite_sync, &failure});
        elect_with_one_remote_vote(node);

        const auto original = node.propose_authority_barrier(marker(1, domain));
        require(original.status == ProposeStatus::admitted,
                "original authority marker was not admitted");
        original_index = original.index;
        node.flush_durability();
        require(node.durable_index() == static_cast<std::uint64_t>(original.index),
                "original authority marker did not become durable");

        while (identity_test::take_outbound_message(node)) {
        }

        velocita::AppendEntries replacement;
        replacement.term = node.status().term + 1;
        replacement.entries.push_back(
            {replacement.term, 2, kAuthorityBarrierEntryType,
             encoded_marker(marker(2, domain))});
        failure.armed = true;
        bool failed = false;
        try {
            identity_test::deliver_declared_peer(node, velocita::PeerMessage{3, 2, replacement});
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && failure.observed,
                "suffix rewrite failure was not reached through public peer receive");

        require(!node.is_leader(),
                "storage-failed node continued serving as leader");

        bool marker_refused = false;
        try {
            (void)node.propose_authority_barrier(marker(3, domain));
        } catch (const std::runtime_error&) {
            marker_refused = true;
        }
        require(marker_refused,
                "storage-failed node accepted a follow-on authority operation");

        bool ordinary_refused = false;
        try {
            std::array<std::byte, 1> invalid{std::byte{0}};
            (void)node.propose(invalid);
        } catch (const std::runtime_error&) {
            ordinary_refused = true;
        }
        require(ordinary_refused,
                "storage-failed node accepted a follow-on ordinary admission");

        bool tick_refused = false;
        try {
            node.tick(10'000);
        } catch (const std::runtime_error&) {
            tick_refused = true;
        }
        require(tick_refused && !node.is_leader(),
                "storage-failed node could campaign after failed truncation");
    }

    failure.armed = false;
    const auto records = identity_test::read_wal(root);
    require(records.size() == 1 && records[0].index == static_cast<std::uint64_t>(original_index) &&
                records[0].entry_type == kAuthorityBarrierEntryType &&
                records[0].payload == encoded_marker(marker(1, domain)),
            "failed pre-rename rewrite changed the original durable marker bytes");
    ReplicatedKernel restarted(identity_test::recover(root, 2, {1, 2, 3}));
    require(restarted.authority_drain_index() == original_index &&
                restarted.status().applied_index < original_index &&
                !restarted.take_committed_result(),
            "restart did not recover the original unapplied marker obligation");
}

void test_failed_wal_flush_latches_node(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xac;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    FailFlush failure;
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {fail_before_flush_sync, &failure});
    elect_with_one_remote_vote(node);

    const auto offered = node.propose_authority_barrier(marker(10, domain));
    require(offered.status == ProposeStatus::admitted,
            "WAL flush failure fixture did not stage an entry");
    failure.armed = true;
    bool flush_failed = false;
    try {
        node.flush_durability();
    } catch (const std::runtime_error&) {
        flush_failed = true;
    }
    require(flush_failed && failure.observed,
            "WAL flush failure was not reached through public flush");
    require(!node.is_leader(),
            "storage-failed node continued serving as leader after WAL flush failure");

    bool follow_on_refused = false;
    try {
        (void)node.propose_authority_barrier(marker(11, domain));
    } catch (const std::runtime_error&) {
        follow_on_refused = true;
    }
    require(follow_on_refused,
            "storage-failed node accepted authority work after WAL flush failure");

    bool outbound_refused = false;
    try {
        (void)identity_test::take_outbound_message(node);
    } catch (const std::runtime_error&) {
        outbound_refused = true;
    }
    require(outbound_refused,
            "storage-failed node released an outbound message after WAL flush failure");
}

void test_failed_threaded_wal_flush_reaches_owner(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xb2;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    ThreadFailFlush failure;
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {fail_thread_before_flush_sync, &failure});
    elect_with_one_remote_vote(node);
    const auto offered = node.propose_authority_barrier(marker(40, domain));
    require(offered.status == ProposeStatus::admitted,
            "threaded WAL failure fixture did not stage an entry");

    {
        std::lock_guard lock(failure.mutex);
        failure.armed = true;
    }
    node.start_writer();
    node.flush_durability();
    {
        std::unique_lock lock(failure.mutex);
        require(failure.observed.wait_for(lock, std::chrono::seconds(5),
                                         [&failure] { return failure.reached; }),
                "threaded failure fixture never reached sync");
    }
    node.stop_writer();

    require_protocol_closed(node, marker(41, domain), "threaded WAL failure");
    bool result_refused = false;
    try { (void)node.take_committed_result(); }
    catch (const std::runtime_error&) { result_refused = true; }
    require(result_refused, "storage-failed node released a result after threaded WAL failure");
    bool proof_refused = false;
    try { (void)node.take_authority_proof(); }
    catch (const std::runtime_error&) { proof_refused = true; }
    require(proof_refused, "storage-failed node released a proof after threaded WAL failure");
    bool restart_refused = false;
    try { node.start_writer(); }
    catch (const std::runtime_error&) { restart_refused = true; }
    require(restart_refused, "failed WAL writer restarted after its failure latch");
}

void test_failed_writer_cannot_resume_through_poll(const std::filesystem::path& root) {
    identity_test::provision(root, 1, {1});
    ThreadFailFlush failure;
    WalBlockArena arena;
    auto segment = identity_test::wal_segment(root, {fail_thread_before_flush_sync, &failure});
    WalWriter writer(segment, arena);
    const auto record = encode_bound_wal_record({1, 1, 1, kAuthorityBarrierEntryType,
        encoded_marker(marker(1, identity_test::authority_domain({1})))},
        identity_test::stored_identity(root));
    require(arena.stage(record, 1) == StageOutcome::staged, "direct writer fixture failed to stage");
    arena.seal_open_block();
    const auto descriptor = arena.next_unsubmitted();
    require(descriptor.has_value() && writer.submit(*descriptor), "direct writer fixture failed to submit");
    {
        std::lock_guard lock(failure.mutex);
        failure.armed = true;
    }
    writer.start();
    {
        std::unique_lock lock(failure.mutex);
        require(failure.observed.wait_for(lock, std::chrono::seconds(5),
                                         [&failure] { return failure.reached; }),
                "direct writer fixture never reached sync");
    }
    writer.stop();
    require(writer.failed(), "writer failed to publish its permanent error");
    {
        std::lock_guard lock(failure.mutex);
        failure.armed = false;
    }
    const bool resubmitted = writer.submit(*descriptor);
    bool poll_refused = false;
    try { (void)writer.poll(); }
    catch (const std::runtime_error& error) {
        poll_refused = std::string(error.what()) == "injected threaded WAL flush failure";
    }
    BlockDescriptor completion{};
    require(!writer.take_completion(completion) && writer.blocks_written() == 0,
            "failed writer published durability through direct poll retry");
    require(!resubmitted && poll_refused,
            "failed writer accepts work or permits direct poll after permanent failure");
}

void require_protocol_closed(ReplicatedKernel& node, const AuthorityBarrier& request,
                             const std::string& path) {
    require(!node.is_leader(), "storage-failed node remained leader after " + path);
    bool proposal_refused = false;
    try {
        (void)node.propose_authority_barrier(request);
    } catch (const std::runtime_error&) {
        proposal_refused = true;
    }
    require(proposal_refused, "storage-failed node accepted authority work after " + path);
    bool outbound_refused = false;
    try {
        (void)identity_test::take_outbound_message(node);
    } catch (const std::runtime_error&) {
        outbound_refused = true;
    }
    require(outbound_refused, "storage-failed node released protocol output after " + path);
}

void test_failed_term_vote_persist_latches_node(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xad;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    FailMetadata failure{true, false, 1, 0,
                         velocita::MetadataCheckpoint::before_write};
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {}, {fail_before_metadata_write, &failure});
    bool election_failed = false;
    node.tick(10'000);
    require(!failure.observed, "a pre-vote round wrote term/vote metadata");
    try {
        identity_test::grant_prevote(node, 1);
    } catch (const std::runtime_error&) {
        election_failed = true;
    }
    require(election_failed && failure.observed,
            "term/vote metadata failure was not reached through election");
    require_protocol_closed(node, marker(20, domain), "term/vote metadata failure");
}

void test_failed_vote_persist_latches_node(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xaf;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    FailMetadata failure{true, false, 2, 0,
                         velocita::MetadataCheckpoint::before_write};
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {}, {fail_before_metadata_write, &failure});
    bool vote_failed = false;
    try {
        identity_test::deliver_declared_peer(node, velocita::PeerMessage{
            1, 2, velocita::RequestVote{1, 1, 0, 0}});
    } catch (const std::runtime_error&) {
        vote_failed = true;
    }
    require(vote_failed && failure.observed,
            "vote metadata failure was not reached through peer request");
    require_protocol_closed(node, marker(21, domain), "vote metadata failure");
}

void test_failed_self_vote_is_reported_by_tick(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xb1;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    FailMetadata failure{true, false, 2, 0,
                         velocita::MetadataCheckpoint::before_write};
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {}, {fail_before_metadata_write, &failure});
    node.tick(10'000);
    bool refused = false;
    try { identity_test::grant_prevote(node, 1); }
    catch (const std::runtime_error&) { refused = true; }
    require(failure.observed, "self-vote fixture missed the metadata failure");
    require(refused, "self-vote persistence failure was swallowed by its owner call");
    require_protocol_closed(node, marker(23, domain), "self-vote metadata failure");
}

void test_failed_term_persist_after_rename_latches_node(
    const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xb0;
    const auto domain = identity_test::authority_domain(
        {1, 2, 3}, catalog_test::standard_catalog(), lineage_tag);
    FailMetadata failure{true, false, 1, 0,
                         velocita::MetadataCheckpoint::file_renamed};
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {1, 2, 3},
                              catalog_test::standard_catalog(), lineage_tag),
                          {}, {fail_before_metadata_write, &failure});
    bool request_failed = false;
    try {
        identity_test::deliver_declared_peer(node, velocita::PeerMessage{
            1, 2, velocita::RequestVote{1, 1, 0, 0}});
    } catch (const std::runtime_error&) {
        request_failed = true;
    }
    require(request_failed && failure.observed,
            "post-rename term metadata failure was not reached");
    const auto persisted =
        identity_test::metadata_store(root).load();
    require(persisted.term == 1 && persisted.voted_for == -1 &&
                persisted.commit_index == 0,
            "post-rename failure did not expose the complete intended metadata tuple");
    require_protocol_closed(node, marker(22, domain),
                            "post-rename term metadata failure");
}

void test_failed_commit_persist_latches_node(const std::filesystem::path& root) {
    constexpr std::uint8_t lineage_tag = 0xae;
    const auto domain = identity_test::authority_domain(
        {2}, catalog_test::standard_catalog(), lineage_tag);
    FailMetadata failure;
    ReplicatedKernel node(identity_test::bootstrap(
                              root, 2, {2}, catalog_test::standard_catalog(), lineage_tag),
                          {}, {fail_before_metadata_write, &failure});
    for (int attempt = 0; attempt < 100 && !node.is_leader(); ++attempt) node.tick(100);
    require(node.is_leader(), "commit metadata failure fixture did not elect");
    const auto offered = node.propose_authority_barrier(marker(30, domain));
    require(offered.status == ProposeStatus::admitted,
            "commit metadata failure fixture did not stage an entry");
    node.flush_durability();
    failure.armed = true;
    bool persist_failed = false;
    try {
        node.tick(0);
    } catch (const std::runtime_error&) {
        persist_failed = true;
    }
    require(persist_failed && failure.observed,
            "commit metadata failure was not reached after durability");
    require_protocol_closed(node, marker(31, domain), "commit metadata failure");
}

}

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("v2-authority-storage-failure-" +
                           std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const std::string selected = std::getenv("F6_CASE") ? std::getenv("F6_CASE") : "all";
        require(selected == "all" || selected == "rewrite" || selected == "flush" ||
                    selected == "term" || selected == "vote" || selected == "post-rename" ||
                    selected == "commit" || selected == "self-vote" ||
                    selected == "threaded-flush" || selected == "writer-retry",
                "unknown F6_CASE selector");
        if (selected == "all" || selected == "rewrite")
            test_failed_suffix_rewrite_latches_node(root / "rewrite");
        if (selected == "all" || selected == "flush")
            test_failed_wal_flush_latches_node(root / "flush");
        if (selected == "all" || selected == "threaded-flush")
            test_failed_threaded_wal_flush_reaches_owner(root / "threaded-flush");
        if (selected == "all" || selected == "writer-retry")
            test_failed_writer_cannot_resume_through_poll(root / "writer-retry");
        if (selected == "all" || selected == "term")
            test_failed_term_vote_persist_latches_node(root / "term");
        if (selected == "all" || selected == "vote")
            test_failed_vote_persist_latches_node(root / "vote");
        if (selected == "all" || selected == "self-vote")
            test_failed_self_vote_is_reported_by_tick(root / "self-vote");
        if (selected == "all" || selected == "post-rename")
            test_failed_term_persist_after_rename_latches_node(root / "post-rename");
        if (selected == "all" || selected == "commit")
            test_failed_commit_persist_latches_node(root / "commit");
        std::filesystem::remove_all(root);
        std::cout << "v2 authority storage failure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v2 authority storage failure test failed: " << error.what()
                  << '\n';
        return 1;
    }
}
