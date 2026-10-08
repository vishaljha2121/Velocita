#include "velocita/v2/replication.hpp"

#include <algorithm>
#include <limits>
#include <array>
#include <stdexcept>
#include <unistd.h>
#include <sys/random.h>
#include "velocita/v2/digest.hpp"

namespace velocita::v2 {
namespace {

Digest256 archive_challenge(const Digest256& session, std::uint64_t attempt) noexcept {
    constexpr std::string_view domain = "velocita.archive.verify.v1";
    std::array<std::byte, domain.size() + 40> bytes{};
    for (std::size_t i = 0; i < domain.size(); ++i) bytes[i] = std::byte(domain[i]);
    std::copy(session.bytes.begin(), session.bytes.end(), bytes.begin() + domain.size());
    for (std::size_t i = 0; i < 8; ++i)
        bytes[domain.size() + 32 + i] = std::byte((attempt >> (8 * i)) & 0xff);
    return sha256_digest(bytes);
}

VerifiedIdentityStartup take_startup(VerifiedIdentityStartup&& startup) {
    startup.validate_ownership();
    return std::move(startup);
}

std::string_view role_name(raft_server_t* server) {
    switch (raft_get_state(server)) {
        case RAFT_STATE_FOLLOWER: return "follower";
        case RAFT_STATE_CANDIDATE: return "candidate";
        case RAFT_STATE_LEADER: return "leader";
        default: return "none";
    }
}

bool control_eligible(const Command& command) noexcept {
    return is_operator_caller(command.identity.caller_id) &&
        (std::holds_alternative<AdvanceKillEpoch>(command.body) ||
         std::holds_alternative<SetExecutorGeneration>(command.body) ||
         std::holds_alternative<SetStrategyMode>(command.body) ||
         std::holds_alternative<RequestMassCancel>(command.body));
}

bool same_metadata(const PersistentMetadata& lhs, const PersistentMetadata& rhs) noexcept {
    return lhs.term == rhs.term && lhs.voted_for == rhs.voted_for &&
        lhs.commit_index == rhs.commit_index;
}

}

struct ReplicatedKernel::CompactionJob {
    ReplicatedKernel* owner{};
    TradingState state{};
    std::vector<WalRecord> records;
    std::uint64_t cut{};
    std::int64_t term{};
    ArchivePublicationContext publication{};
    ArchiveCoverageCapability coverage{};
    bool snapshot_verified{};
    bool reclamation_authorized{};
    bool rewrite_pending{};
    bool deferred{};
    bool local_completion_acquired{};
};

struct ReplicatedKernel::PersistenceJob {
    enum class Operation : std::uint8_t { hard_state, truncate_suffix };
    Operation operation{Operation::hard_state};
    std::uint64_t storage_generation{};
    std::uint64_t sequence{};
    std::uint64_t captured_durable_index{};
    std::uint64_t captured_staged_index{};
    PersistentMetadata desired{};
    BoundMetadataStore* store{};
    std::uint64_t truncate_from{};
    WalSegment* segment{};
    struct Completion {
        Operation operation{Operation::hard_state};
        std::uint64_t storage_generation{};
        std::uint64_t sequence{};
        std::uint64_t captured_durable_index{};
        std::uint64_t captured_staged_index{};
        PersistentMetadata durable{};
        std::uint64_t truncate_from{};
        bool succeeded{};
    } completion{};
};

struct ReplicatedKernel::HistoryRecoveryJob {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::uint64_t cut{};
    std::int64_t applied{};
    ArchivePublicationContext publication{};
    PersistentMetadata metadata{};
    std::filesystem::path root;
    std::filesystem::path runtime;
    VerifiedStaticIdentity identity;
    VerifiedExecutionCatalog catalog;
    ArchiveStore* archive{};
    ArchiveStoreConfig config{};
    std::uint64_t wal_cap{};
    bool archive_was_open{};
    StorageCheckpoints checkpoints{};
    struct Completion {
        std::uint64_t generation{};
        std::uint64_t sequence{};
        std::uint64_t cut{};
        ArchiveWalBridgeResult publication{};
    } completion{};
};

void ReplicatedKernel::open_archive() {
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::array<std::byte, kMaxExecutionCatalogBytes> catalog_bytes{};
    const auto identity = encode_static_identity(startup_.identity().value(), identity_bytes);
    const auto catalog = catalog_.encode(catalog_bytes);
    if (identity.error != StaticIdentityCodecError::none ||
        catalog.error != ExecutionCatalogError::none ||
        ArchiveStore::open(startup_.root_directory(), startup_.identity(),
            std::span(identity_bytes).first(identity.bytes_written),
            std::span(catalog_bytes).first(catalog.bytes_written), catalog_, archive_config_, archive_) !=
                ArchiveStoreError::none)
        throw std::runtime_error("verified archive open refused");
    archive_open_ = true;
}

bool ReplicatedKernel::archive_admission(std::uint64_t resulting_last_index,
                                         bool protected_lane) const noexcept {
    const auto covered = archive_open_ ? archive_.covered_through() : 0;
    const auto pending = resulting_last_index > covered ? resulting_last_index - covered : 0;
    if (pending > kRetainedLogEntries) return false;
    if (!archive_open_) {
        constexpr auto per_object = (kMaxArchiveSegmentBytes - 128) / kMaxArchiveRecordBytes;
        const auto objects = (pending + per_object - 1) / per_object;
        const auto bytes = protected_lane ? archive_config_.logical_byte_limit :
            (archive_config_.logical_byte_limit >= 16ULL * 1024 * 1024 ?
             archive_config_.logical_byte_limit - 16ULL * 1024 * 1024 : 0);
        const auto slots = protected_lane ? archive_config_.object_limit :
            (archive_config_.object_limit >= 4 ? archive_config_.object_limit - 4 : 0);
        return objects <= slots && objects * kMaxArchiveSegmentBytes <= bytes;
    }
    return archive_.can_admit(static_cast<std::uint32_t>(pending), protected_lane);
}

Digest256 state_digest_of(const TradingState& state) {
    static thread_local std::array<std::byte, kMaxStateSnapshotBytes> storage{};
    const auto encoded = encode_state_snapshot(state, storage);
    if (encoded.error != StateSnapshotError::none) {
        throw std::runtime_error("replica state is not encodable");
    }
    return encoded.state_digest;
}

ReplicatedKernel::ReplicatedKernel(VerifiedIdentityStartup startup,
                                   StorageCheckpoints checkpoints,
                                   MetadataCheckpoints metadata_checkpoints,
                                   ArchiveStoreConfig archive_config,
                                   HistoryRecoveryConfig history_config,
                                   Digest256 archive_destination_identity)
    : startup_(take_startup(std::move(startup))),
      node_id_(startup_.identity().value().local_node_id),
      members_(startup_.identity().value().voters.begin(),
               startup_.identity().value().voters.begin() + startup_.identity().value().voter_count),
      catalog_(startup_.catalog()),
      authority_domain_(startup_.identity().authority_domain()),
      metadata_store_(startup_.directory(), startup_.identity(), metadata_checkpoints),
      storage_checkpoints_(checkpoints),
      snapshots_(startup_.directory(), startup_.identity()),
      metadata_(metadata_store_.load()),
      desired_metadata_(metadata_),
      scratch_(std::make_unique<ApplyScratch>()),
      codec_scratch_(std::make_unique<KernelCommandCodecScratch>()),
      peer_scratch_(std::make_unique<BoundFrameScratch>()),
      results_(std::make_unique<ResultLedger>()),
      outbound_(std::make_unique<OutboundMailboxes>(startup_.identity())),
      log_payloads_(std::make_unique<RetainedLog>()),
      archive_config_(archive_config),
      history_config_(history_config),
      archive_destination_identity_(archive_destination_identity),
      server_(raft_new()) {
    const auto& data_dir = startup_.directory();
    if (server_ == nullptr) throw std::runtime_error("raft_new failed");
    const auto release_server = [](raft_server_t* server) { raft_free(server); };
    std::unique_ptr<raft_server_t, decltype(release_server)> startup_guard(server_, release_server);
    if (archive_destination_identity_ != Digest256{}) {
        if (::getentropy(archive_session_nonce_.bytes.data(), archive_session_nonce_.bytes.size()) != 0 ||
            archive_session_nonce_ == Digest256{})
            throw std::runtime_error("archive verification session entropy unavailable");
    }
    if (archive_config_.object_limit == 0 || archive_config_.object_limit > kMaxArchiveCatalogRows ||
        archive_config_.logical_byte_limit == 0)
        throw std::runtime_error("invalid archive deployment budget");
    if (history_config_.maximum_wal_bytes == 0 || history_config_.maximum_wal_bytes > (1ULL << 30))
        throw std::runtime_error("invalid history recovery WAL budget");

    const auto stored = snapshots_.load();
    if (std::filesystem::exists(std::filesystem::symlink_status(startup_.root_directory() / "v2-archive")))
        open_archive();
    if (archive_open_ && archive_.publication_storage_generation() > startup_.storage_generation())
        throw std::runtime_error("recovery refused: archive belongs to a future storage generation");
    const auto archived_at_start = archive_open_ ? archive_.covered_through() : 0;
    if (stored && stored->last_included_index > archived_at_start) {
        const auto wal_path = data_dir / "v2-wal-blocks.bin";
        if (std::filesystem::exists(wal_path) &&
            std::filesystem::file_size(wal_path) > history_config_.maximum_wal_bytes)
            throw std::runtime_error("history recovery WAL exceeds candidate budget");
    }
    if (stored && !execution_catalog_bindings_valid(stored->state, catalog_))
        throw CatalogDependencyUnavailable{};
    if (stored && stored->last_included_term > metadata_.term)
        throw SnapshotStoreError(BoundFrameError::invalid_payload, "metadata term is behind snapshot term");
    std::uint64_t previous_index{};
    std::uint64_t first_wal_index{};
    std::int64_t first_wal_term{};
    std::int64_t previous_term{};
    WalSegment::visit_existing_records(data_dir, startup_.identity(), [&](const WalRecord& record) {
        if (!first_wal_index) {
            first_wal_index = record.index;
            first_wal_term = record.term;
        }
        if (record.term > metadata_.term)
            throw BoundWalError(BoundFrameError::invalid_payload, "metadata term is behind retained WAL term");
        if (archive_open_ && record.index == archive_.covered_through() && record.term != archive_.last_term())
            throw BoundWalError(BoundFrameError::invalid_payload, "archive terminal term conflicts with retained WAL");
        if ((previous_index && record.index != previous_index + 1) || record.term < previous_term)
            throw BoundWalError(BoundFrameError::invalid_payload, "retained WAL ordering is inconsistent");
        if (stored && ((record.index == stored->last_included_index && record.term != stored->last_included_term) ||
                      (record.index < stored->last_included_index && record.term > stored->last_included_term) ||
                      (record.index > stored->last_included_index && record.term < stored->last_included_term)))
            throw BoundWalError(BoundFrameError::invalid_payload, "retained WAL conflicts with snapshot term");
        validate_log_payload(record.entry_type, record.payload);
        previous_index = record.index;
        previous_term = record.term;
    });
    const auto archived = archive_open_ ? archive_.covered_through() : 0;
    const auto recovered_commit = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(std::max<std::int64_t>(metadata_.commit_index, 0)),
        stored ? stored->last_included_index : 0);
    if (archived > recovered_commit || (archive_open_ && archive_.last_term() > metadata_.term))
        throw std::runtime_error("recovery refused: archive is ahead of recovered committed history");
    if (stored && archived == stored->last_included_index && archive_.last_term() != stored->last_included_term)
        throw std::runtime_error("recovery refused: archive terminal term conflicts with snapshot");
    if (archive_open_ && first_wal_index == archived + 1 && first_wal_term < archive_.last_term())
        throw BoundWalError(BoundFrameError::invalid_payload,
                            "retained WAL term precedes archive terminal term");
    if (stored && archive_open_ && archived < stored->last_included_index &&
        archive_.last_term() > stored->last_included_term)
        throw SnapshotStoreError(BoundFrameError::invalid_payload,
                                 "snapshot term precedes archive terminal term");
    if (stored && archived < stored->last_included_index &&
        (first_wal_index == 0 || first_wal_index > archived + 1 || previous_index < stored->last_included_index))
        throw std::runtime_error("recovery refused: reclaimed history has no verified archive prefix");
    if (stored && archived < stored->last_included_index)
        history_recovery_cut_ = stored->last_included_index;
    if (startup_.identity().value().deployment_mode == DeploymentMode::two_host_pilot &&
        archived > 0 && (first_wal_index == 0 || first_wal_index > 1)) {
        if (archive_destination_identity_ == Digest256{})
            throw std::runtime_error("reclaimed live history requires a pinned backup destination");
        archive_recovery_pending_ = true;
        const auto attempt = next_persistence_sequence_++;
        archive_verification_request_ = archive_.make_destination_request(
            startup_.storage_generation(), attempt, archive_challenge(archive_session_nonce_, attempt),
            archive_destination_identity_);
    }

    raft_cbs_t table{};
    table.send_requestvote = &ReplicatedKernel::send_request_vote;
    table.send_appendentries = &ReplicatedKernel::send_append_entries;
    table.applylog = &ReplicatedKernel::apply_log;
    table.before_apply = &ReplicatedKernel::before_apply;
    table.persist_vote = &ReplicatedKernel::persist_vote;
    table.persist_term = &ReplicatedKernel::persist_term;
    table.log_offer = &ReplicatedKernel::log_offer;
    table.log_poll = &ReplicatedKernel::log_poll;
    table.log_prepare_truncate = &ReplicatedKernel::log_prepare_truncate;
    table.log_pop = &ReplicatedKernel::log_pop;
    table.log_clear = &ReplicatedKernel::log_clear;
    table.log_get_node_id = &ReplicatedKernel::log_get_node_id;
    table.durable_idx = &ReplicatedKernel::durable_idx;
    raft_set_callbacks(server_, &table, this);
    raft_set_prevote(server_, 1);

    if (raft_add_node(server_, nullptr, node_id_, 1) == nullptr) {
        throw std::runtime_error("failed to add Raft member " + std::to_string(node_id_));
    }
    std::uint64_t installed_through = 0;
    if (stored) {
        const int began = raft_begin_load_snapshot(
            server_, stored->last_included_term,
            static_cast<raft_index_t>(stored->last_included_index));
        if (began != 0) throw std::runtime_error("stored snapshot installation refused");
        if (began == 0) {
            state_ = stored->state;
            installed_through = stored->last_included_index;
            if (raft_end_load_snapshot(server_) != 0) {
                throw std::runtime_error("failed to install the stored snapshot");
            }
        }
    }

    for (const auto member : members_) {
        if (member == node_id_) continue;
        if (member <= 0 ||
            raft_add_node(server_, nullptr, member, member == node_id_) == nullptr) {
            throw std::runtime_error("failed to add Raft member " + std::to_string(member));
        }
    }

    if (raft_set_current_term(server_, metadata_.term) != 0) {
        throw std::runtime_error("failed to restore Raft term");
    }
    if (metadata_.voted_for >= 0 &&
        raft_vote_for_nodeid(server_, metadata_.voted_for) != 0) {
        throw std::runtime_error("failed to restore Raft vote");
    }

    auto expected_index = installed_through + 1;
    std::uint64_t recovered_through = installed_through;
    WalSegment::visit_existing_records(data_dir, startup_.identity(), [&](const WalRecord& record) {
        if (record.index <= installed_through) return;
        if (record.index != expected_index++) {
            throw std::runtime_error("WAL history is not a contiguous snapshot suffix");
        }
        raft_entry_t entry{};
        entry.term = record.term;
        entry.id = record.entry_id;
        entry.type = record.entry_type;
        entry.data.buf = const_cast<std::byte*>(record.payload.data());
        entry.data.len = static_cast<unsigned int>(record.payload.size());
        if (raft_append_entry(server_, &entry) != 0) {
            throw std::runtime_error("failed to restore WAL entry " +
                                     std::to_string(record.index) + ": " + callback_error_);
        }
        recovered_through = record.index;
    });
    arena_.seed_durable_index(recovered_through);

    if (metadata_.commit_index < 0) {
        throw std::runtime_error("metadata holds a negative commit index");
    }

    const auto log_end = raft_get_current_idx(server_);
    if (metadata_.commit_index > log_end) {
        throw std::runtime_error("recovery refused: committed WAL history is missing; preserve storage for repair");
    }
    raft_set_commit_idx(server_, std::max(static_cast<raft_index_t>(metadata_.commit_index),
        static_cast<raft_index_t>(installed_through)));
    if (raft_apply_all(server_) != 0) {
        throw std::runtime_error("failed to replay committed WAL entries: " +
                                 callback_error_);
    }
    if (!history_recovery_pending() &&
        !archive_admission(static_cast<std::uint64_t>(raft_get_current_idx(server_)), true))
        throw std::runtime_error("recovery refused: deployment quota cannot cover accepted archive obligations");
    segment_ = std::make_unique<WalSegment>(data_dir, startup_.identity(), checkpoints);
    writer_ = std::make_unique<WalWriter>(
        *segment_, arena_, startup_.storage_generation());
    restoring_ = false;
    (void)startup_guard.release();
}

ReplicatedKernel::~ReplicatedKernel() {
    if (writer_) writer_->stop();
    if (server_ != nullptr) raft_free(server_);
}

void ReplicatedKernel::tick(const int elapsed_ms) {
    require_usable();
    if (compaction_ || persistence_pending()) return;
    if (const int result = raft_periodic(server_, elapsed_ms);
        result != 0 && result != RAFT_ERR_PERSIST_PENDING) {
        throw std::runtime_error("raft_periodic failed: " + std::to_string(result));
    }
    require_usable();

    if (raft_is_leader(server_) != 0 && raft_get_current_term(server_) == 0) {
        if (raft_set_current_term(server_, 1) != 0) {
            throw std::runtime_error("failed to raise a leader's term above zero");
        }
    }

    persist_commit_index();
    if (const int result = raft_apply_all(server_);
        result != 0 && result != RAFT_ERR_PERSIST_PENDING) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(result) +
                                 " " + callback_error_);
    }
    revoke_stale_authority();
}

void ReplicatedKernel::flush_durability() {
    require_usable();
    if (compaction_ || archive_recovery_pending_) return;
    try {
        if (history_recovery_pending()) {
            pump_history_recovery();
            return;
        }
        collect_persistence();
        if (writer_running_ && !persistence_ && !same_metadata(desired_metadata_, metadata_))
            submit_hard_state();

        if (persistence_) {
            BlockDescriptor finished{};
            while (writer_->take_completion(finished)) (void)arena_.complete(finished);
            return;
        }

        if (truncate_requested_) {
            arena_.seal_open_block();
            while (const auto descriptor = arena_.next_unsubmitted()) {
                if (!writer_->submit(*descriptor)) break;
                ++blocks_submitted_;
            }
            BlockDescriptor finished{};
            while (writer_->take_completion(finished)) (void)arena_.complete(finished);
            if (arena_.sealed_count() == 0 && arena_.durable_index() >= highest_staged_index_)
                submit_truncate_suffix();
            return;
        }

        if (retry_frame_size_ != 0) {
            const auto size = retry_frame_size_;
            retry_frame_size_ = 0;
            const auto retried = receive_peer(std::span<const std::byte>(retry_frame_).first(size));
            if (retried.status == PeerReceiveStatus::busy) {
                storage_failed_ = true;
                throw std::runtime_error("owned peer continuation was unexpectedly busy");
            }
            if (retried.frame_error != BoundFrameError::none) {
                storage_failed_ = true;
                throw std::runtime_error("owned peer continuation no longer decodes");
            }
            if (truncate_grant_) {
                storage_failed_ = true;
                throw std::runtime_error("owned peer continuation did not consume its truncate grant");
            }
            if (!same_metadata(desired_metadata_, metadata_)) {
                submit_hard_state();
                return;
            }
        }

        arena_.seal_open_block();

        bool submitted_any = false;
        while (const auto descriptor = arena_.next_unsubmitted()) {
            if (!writer_->submit(*descriptor)) break;
            ++blocks_submitted_;
            submitted_any = true;
        }

        if (!writer_running_) {
            (void)writer_->poll();
        }
        (void)submitted_any;

        BlockDescriptor finished{};
        while (writer_->take_completion(finished)) {
            (void)arena_.complete(finished);
        }
        send_early_ack();
    } catch (...) {
        storage_failed_ = true;
        throw;
    }
}

std::optional<ArchiveDestinationRequest> ReplicatedKernel::archive_verification_request() const noexcept {
    return archive_verification_request_;
}

bool ReplicatedKernel::accept_archive_verification(ArchiveDestinationEvidence&& evidence) {
    require_usable();
    if (!archive_verification_request_ || archive_verification_evidence_) {
        (void)evidence.consume({});
        return false;
    }
    const auto expected = archive_.make_destination_request(startup_.storage_generation(),
        archive_verification_request_->attempt,
        archive_challenge(archive_session_nonce_, archive_verification_request_->attempt), archive_destination_identity_);
    if (expected != *archive_verification_request_ || !evidence.verified() ||
        evidence.request() != expected) {
        (void)evidence.consume(expected);
        storage_failed_ = true;
        throw std::runtime_error("archive verification completion does not match fenced owner");
    }
    if (archive_recovery_pending_) {
        if (!evidence.consume(expected)) {
            storage_failed_ = true;
            throw std::runtime_error("archive recovery evidence was already consumed");
        }
        archive_verification_request_.reset();
        archive_recovery_pending_ = false;
    } else {
        archive_verification_evidence_.emplace(std::move(evidence));
    }
    return true;
}

bool ReplicatedKernel::persistence_pending() const noexcept {
    return archive_recovery_pending_ || history_recovery_pending() || !!persistence_ || !same_metadata(desired_metadata_, metadata_) ||
        !!truncate_requested_ || retry_frame_size_ != 0;
}

void ReplicatedKernel::pump_history_recovery() {
    if (!history_recovery_) {
        if (persistence_ || compaction_ || truncate_requested_ || retry_frame_size_ ||
            !same_metadata(desired_metadata_, metadata_) || arena_.sealed_count() != 0)
            throw std::runtime_error("history recovery overlaps live persistence");
        if (next_persistence_sequence_ == UINT64_MAX)
            throw std::runtime_error("history recovery operation identity exhausted");
        const auto durable = arena_.durable_index();
        if (durable < history_recovery_cut_ || durable - history_recovery_cut_ > kRetainedLogEntries)
            throw std::runtime_error("history recovery suffix exceeds live capacity");
        auto job = std::make_unique<HistoryRecoveryJob>();
        job->generation = startup_.storage_generation();
        job->sequence = next_persistence_sequence_++;
        job->cut = history_recovery_cut_;
        job->applied = raft_get_last_applied_idx(server_);
        job->publication = {archive_open_ ? archive_.catalog_generation() + 1 : 1,
            job->generation, static_cast<std::uint64_t>(raft_get_commit_idx(server_)),
            durable, raft_get_current_term(server_), archive_open_ ? archive_.last_term() : 0,
            static_cast<std::uint32_t>(durable - job->cut)};
        job->metadata = metadata_;
        job->root = startup_.root_directory();
        job->runtime = startup_.directory();
        job->identity = startup_.identity();
        job->catalog = catalog_;
        job->archive = &archive_;
        job->config = archive_config_;
        job->wal_cap = history_config_.maximum_wal_bytes;
        job->archive_was_open = archive_open_;
        job->checkpoints = storage_checkpoints_;
        start_writer();
        history_recovery_ = std::move(job);
        if (!writer_->submit_maintenance(&ReplicatedKernel::persist_history_recovery,
                                         history_recovery_.get()))
            throw std::runtime_error("history recovery maintenance admission failed");
        return;
    }
    if (!writer_->take_maintenance_completion()) return;
    (void)accept_history_recovery_completion();
}

bool ReplicatedKernel::accept_history_recovery_completion() {
    if (!history_recovery_) return false;
    const auto& job = *history_recovery_;
    const auto& completed = job.completion;
    if (completed.generation != job.generation || completed.sequence != job.sequence ||
        completed.cut != job.cut || job.generation != startup_.storage_generation() ||
        job.cut != history_recovery_cut_ || job.archive != &archive_ ||
        completed.publication.error != ArchiveStoreError::none ||
        completed.publication.covered_through != job.cut ||
        archive_.covered_through() != job.cut ||
        archive_.last_term() != completed.publication.last_term ||
        job.publication.committed_index != static_cast<std::uint64_t>(raft_get_commit_idx(server_)) ||
        job.publication.durable_index != arena_.durable_index() ||
        job.publication.current_term != raft_get_current_term(server_) ||
        job.applied != raft_get_last_applied_idx(server_) ||
        !same_metadata(job.metadata, metadata_) || !same_metadata(metadata_, desired_metadata_)) {
        storage_failed_ = true;
        throw std::runtime_error("history recovery completion does not match fenced owner");
    }
    archive_open_ = true;
    if (!archive_admission(static_cast<std::uint64_t>(raft_get_current_idx(server_)), true)) {
        storage_failed_ = true;
        throw std::runtime_error("history recovery left unaccounted live obligations");
    }
    history_recovery_.reset();
    history_recovery_cut_ = 0;
    return true;
}

void ReplicatedKernel::persist_history_recovery(void* context) {
    auto& job = *static_cast<HistoryRecoveryJob*>(context);
    if (!job.archive || !job.generation || !job.sequence || !job.cut)
        throw std::runtime_error("invalid history recovery job");
    job.checkpoints(StorageCheckpoint::history_recovery_before_publication);
    if (!job.archive_was_open) {
        std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
        std::array<std::byte, kMaxExecutionCatalogBytes> catalog_bytes{};
        const auto identity = encode_static_identity(job.identity.value(), identity_bytes);
        const auto catalog = job.catalog.encode(catalog_bytes);
        if (identity.error != StaticIdentityCodecError::none ||
            catalog.error != ExecutionCatalogError::none ||
            ArchiveStore::open(job.root, job.identity,
                std::span(identity_bytes).first(identity.bytes_written),
                std::span(catalog_bytes).first(catalog.bytes_written),
                job.catalog, job.config, *job.archive) != ArchiveStoreError::none)
            throw std::runtime_error("history recovery archive open failed");
    }
    const auto published = job.archive->publish_wal_bridge(
        job.runtime, job.publication, job.cut, job.wal_cap);
    if (published.error != ArchiveStoreError::none)
        throw std::runtime_error("history recovery archive publication refused: " +
                                 std::to_string(static_cast<unsigned>(published.error)));
    job.checkpoints(StorageCheckpoint::history_recovery_published);
    job.completion = {job.generation, job.sequence, job.cut, published};
}

void ReplicatedKernel::stage_hard_state(const PersistentMetadata& desired) {
    desired_metadata_ = desired;
    if (!writer_running_) {
        metadata_store_.save(desired_metadata_);
        metadata_ = desired_metadata_;
    }
}

void ReplicatedKernel::submit_hard_state() {
    static_assert(sizeof(PersistenceJob) <= 256);
    if (!writer_running_ || persistence_ || same_metadata(desired_metadata_, metadata_)) return;
    if (next_persistence_sequence_ == 0 ||
        next_persistence_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        storage_failed_ = true;
        throw std::runtime_error("persistence operation sequence exhausted");
    }
    auto job = std::make_unique<PersistenceJob>();
    job->storage_generation = startup_.storage_generation();
    job->sequence = next_persistence_sequence_++;
    job->captured_durable_index = arena_.durable_index();
    job->captured_staged_index = highest_staged_index_;
    job->desired = desired_metadata_;
    job->store = &metadata_store_;
    if (!writer_->submit_maintenance(&ReplicatedKernel::persist_hard_state, job.get())) {
        storage_failed_ = true;
        throw std::runtime_error("hard-state persistence submission refused");
    }
    persistence_ = std::move(job);
}

void ReplicatedKernel::submit_truncate_suffix() {
    if (!writer_running_ || persistence_ || !truncate_requested_) return;
    if (next_persistence_sequence_ == 0 ||
        next_persistence_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        storage_failed_ = true;
        throw std::runtime_error("persistence operation sequence exhausted");
    }
    auto job = std::make_unique<PersistenceJob>();
    job->operation = PersistenceJob::Operation::truncate_suffix;
    job->storage_generation = startup_.storage_generation();
    job->sequence = next_persistence_sequence_++;
    job->captured_durable_index = arena_.durable_index();
    job->captured_staged_index = highest_staged_index_;
    job->truncate_from = *truncate_requested_;
    job->segment = segment_.get();
    if (!writer_->submit_maintenance(&ReplicatedKernel::persist_truncate_suffix, job.get())) {
        storage_failed_ = true;
        throw std::runtime_error("suffix-truncation persistence submission refused");
    }
    persistence_ = std::move(job);
}

void ReplicatedKernel::collect_persistence() {
    if (!persistence_ || !writer_->take_maintenance_completion()) return;
    if (!accept_persistence_completion()) {
        storage_failed_ = true;
        throw std::runtime_error("persistence completion disappeared after acquisition");
    }
}

bool ReplicatedKernel::accept_persistence_completion() {
    if (!persistence_) return false;
    const auto& completion = persistence_->completion;
    if (!completion.succeeded || completion.operation != persistence_->operation ||
        completion.storage_generation != persistence_->storage_generation ||
        completion.storage_generation != startup_.storage_generation() ||
        completion.sequence != persistence_->sequence || completion.sequence == 0 ||
        completion.captured_durable_index != persistence_->captured_durable_index ||
        completion.captured_staged_index != persistence_->captured_staged_index ||
        completion.truncate_from != persistence_->truncate_from) {
        storage_failed_ = true;
        throw std::runtime_error("persistence completion identity mismatch");
    }
    if (persistence_->operation == PersistenceJob::Operation::hard_state) {
        if (!same_metadata(completion.durable, persistence_->desired) ||
            !same_metadata(persistence_->desired, desired_metadata_) ||
            persistence_->store != &metadata_store_ || persistence_->segment != nullptr) {
            storage_failed_ = true;
            throw std::runtime_error("hard-state completion tuple mismatch");
        }
        metadata_ = persistence_->desired;
    } else {
        if (!truncate_requested_ || *truncate_requested_ != persistence_->truncate_from ||
            persistence_->truncate_from == 0 || persistence_->segment != segment_.get() ||
            persistence_->store != nullptr) {
            storage_failed_ = true;
            throw std::runtime_error("truncate completion tuple mismatch");
        }
        arena_.rewind_durable_index(persistence_->truncate_from - 1);
        highest_staged_index_ = persistence_->truncate_from - 1;
        truncate_grant_ = persistence_->truncate_from;
        truncate_requested_.reset();
    }
    persistence_.reset();
    return true;
}

void ReplicatedKernel::persist_hard_state(void* context) {
    auto& job = *static_cast<PersistenceJob*>(context);
    if (job.operation != PersistenceJob::Operation::hard_state || job.store == nullptr ||
        job.storage_generation == 0 || job.sequence == 0)
        throw std::runtime_error("invalid hard-state persistence job");
    job.store->save(job.desired);
    job.completion = {job.operation, job.storage_generation, job.sequence,
                      job.captured_durable_index, job.captured_staged_index,
                      job.desired, job.truncate_from, true};
}

void ReplicatedKernel::persist_truncate_suffix(void* context) {
    auto& job = *static_cast<PersistenceJob*>(context);
    if (job.operation != PersistenceJob::Operation::truncate_suffix || job.segment == nullptr ||
        job.store != nullptr || job.storage_generation == 0 || job.sequence == 0 ||
        job.truncate_from == 0)
        throw std::runtime_error("invalid suffix-truncation persistence job");
    job.segment->truncate_from(job.truncate_from);
    job.completion = {job.operation, job.storage_generation, job.sequence,
                      job.captured_durable_index, job.captured_staged_index,
                      job.desired, job.truncate_from, true};
}

void ReplicatedKernel::start_writer() {
    require_usable();
    if (writer_running_) return;
    writer_running_ = true;
    writer_->start();
}

void ReplicatedKernel::stop_writer() {
    if (compaction_ || persistence_pending())
        throw std::runtime_error("cannot join writer while a protocol persistence fence is pending");
    if (!writer_running_) return;
    writer_->stop();
    writer_running_ = false;
    BlockDescriptor finished{};
    while (writer_->take_completion(finished)) {
        (void)arena_.complete(finished);
    }
}

WalMetrics ReplicatedKernel::wal_metrics() const {
    WalMetrics metrics;
    metrics.blocks_submitted = blocks_submitted_;
    metrics.blocks_durable = writer_->blocks_written();
    metrics.backpressure_refusals = backpressure_refusals_;
    metrics.highest_staged_index = highest_staged_index_;
    metrics.durable_index = arena_.durable_index();
    return metrics;
}

void ReplicatedKernel::require_usable() const {
    if (log_ownership_failed_)
        throw std::runtime_error("replica log ownership failure; restart required");
    if (outbound_failed_)
        throw std::runtime_error("replica outbound encoding failure; restart required");
    if (result_ownership_failed_)
        throw std::runtime_error("replica result ownership failure; restart required");
    if (storage_failed_ || (writer_ && writer_->failed()))
        throw std::runtime_error("replica storage failure; restart required");
    if (catalog_dependency_failed_ ||
        !execution_catalog_ready(state_.execution_catalog_root, &catalog_))
        throw CatalogDependencyUnavailable{};
}

bool ReplicatedKernel::is_leader() const {
    return !compaction_ && !persistence_pending() && !log_ownership_failed_ && !outbound_failed_ && !result_ownership_failed_ && !storage_failed_ && (!writer_ || !writer_->failed()) && !catalog_dependency_failed_ &&
        execution_catalog_ready(state_.execution_catalog_root, &catalog_) &&
        raft_is_leader(server_) != 0;
}

ProposeOutcome ReplicatedKernel::propose(const std::span<const std::byte> command_bytes,
                                         const ProposeLane lane) {
    require_usable();
    ProposeOutcome outcome;
    outcome.leader_id = raft_get_current_leader(server_);
    outcome.term = raft_get_current_term(server_);
    outcome.index = raft_get_current_idx(server_);
    if (compaction_ || persistence_pending()) { outcome.status = ProposeStatus::persistence_busy; return outcome; }
    if (!is_leader()) {
        outcome.status = ProposeStatus::not_leader;
        return outcome;
    }

    Command probe;
    const auto decoded = decode_kernel_command(command_bytes, probe, *codec_scratch_);
    if (decoded.error != KernelCodecError::none) {
        outcome.status = lane == ProposeLane::control
            ? ProposeStatus::not_a_control_command : ProposeStatus::refused;
        return outcome;
    }

    if (lane == ProposeLane::control) {
        if (!control_eligible(probe)) {
            outcome.status = ProposeStatus::not_a_control_command;
            return outcome;
        }
    }

    if (lane == ProposeLane::normal &&
        arena_.free_blocks() <= kControlLaneReservedBlocks) {
        outcome.status = ProposeStatus::normal_lane_exhausted;
        return outcome;
    }

    const std::array<bool, 1> result_class{control_eligible(probe)};
    if (!log_payloads_->can_admit(result_class) ||
        !archive_admission(static_cast<std::uint64_t>(raft_get_current_idx(server_)) + 1,
                           result_class[0])) {
        ++log_capacity_refusals_;
        outcome.status = ProposeStatus::log_capacity_exhausted;
        return outcome;
    }
    if (!results_->can_reserve(result_class)) {
        ++result_capacity_refusals_;
        outcome.status = ProposeStatus::result_capacity_exhausted;
        return outcome;
    }

    std::uint64_t seed = 0;
    for (const auto byte : command_bytes) {
        seed = seed * 1099511628211ULL ^ static_cast<std::uint64_t>(byte);
    }

    std::vector<std::byte> payload(command_bytes.begin(), command_bytes.end());
    msg_entry_t entry{};
    entry.id = static_cast<raft_entry_id_t>(seed & 0x7FFFFFFFU);
    entry.type = RAFT_LOGTYPE_NORMAL;
    entry.data.buf = payload.data();
    entry.data.len = static_cast<unsigned int>(payload.size());
    msg_entry_response_t raft_response{};
    const int result = raft_recv_entry(server_, &entry, &raft_response);
    require_usable();
    if (result == RAFT_ERR_NOT_LEADER) {
        outcome.status = ProposeStatus::not_leader;
        outcome.leader_id = raft_get_current_leader(server_);
        return outcome;
    }
    if (result != 0) {
        outcome.status = ProposeStatus::refused;
        return outcome;
    }

    persist_commit_index();
    if (const int applied = raft_apply_all(server_);
        applied != 0 && applied != RAFT_ERR_PERSIST_PENDING) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(applied) +
                                 " " + callback_error_);
    }
    outcome.status = ProposeStatus::admitted;
    outcome.index = raft_response.idx;
    outcome.term = raft_response.term;
    return outcome;
}

std::optional<CommittedResult> ReplicatedKernel::take_committed_result() {
    require_usable();
    if (compaction_ || archive_recovery_pending_ || history_recovery_pending()) return std::nullopt;
    return results_->take();
}

void ReplicatedKernel::validate_log_payload(const std::int32_t type,
                                           const std::span<const std::byte> bytes) const {
    if (type == kAuthorityBarrierEntryType) {
        AuthorityBarrier marker;
        if (decode_authority_barrier(type, bytes, marker).error != AuthorityBarrierCodecError::none ||
            marker.authority_domain != authority_domain_)
            throw std::runtime_error("invalid authority marker or authority domain");
        return;
    }
    Command command;
    if (type != RAFT_LOGTYPE_NORMAL ||
        decode_kernel_command(bytes, command, *codec_scratch_).error != KernelCodecError::none)
        throw std::runtime_error("incompatible or invalid typed WAL command");
}

ProposeOutcome ReplicatedKernel::propose_authority_barrier(const AuthorityBarrier& request) {
    require_usable();
    if (compaction_ || persistence_pending()) return {ProposeStatus::persistence_busy};
    revoke_stale_authority();
    ProposeOutcome outcome{ProposeStatus::refused, raft_get_current_leader(server_),
                           raft_get_current_term(server_), raft_get_current_idx(server_)};
    if (node_id_ <= 0) return outcome;
    if (!is_leader() || outcome.term <= 0) {
        pending_authority_barrier_.reset();
        outcome.status = ProposeStatus::not_leader;
        return outcome;
    }
    std::array<std::byte, kAuthorityBarrierBytes> bytes{};
    if (request.authority_domain != authority_domain_ ||
        encode_authority_barrier(kAuthorityBarrierEntryType, request, bytes).error !=
            AuthorityBarrierCodecError::none)
        return outcome;

    if (pending_authority_barrier_ &&
        (pending_authority_barrier_->term != outcome.term ||
         pending_authority_barrier_->index <= raft_get_last_applied_idx(server_)))
        pending_authority_barrier_.reset();
    if (pending_authority_barrier_) {
        const auto& pending = *pending_authority_barrier_;
        if (request.incarnation == pending.request.incarnation &&
            request.request_sequence == pending.request.request_sequence) {
            if (request == pending.request) {
                outcome.status = ProposeStatus::admitted;
                outcome.index = pending.index;
            }
            return outcome;
        }
    }
    if (authority_proof_) {
        outcome.status = ProposeStatus::authority_busy;
        return outcome;
    }
    if (authority_drain_index() != 0) {
        outcome.status = ProposeStatus::authority_busy;
        return outcome;
    }
    if (arena_.free_blocks() <= kControlLaneReservedBlocks) {
        outcome.status = ProposeStatus::normal_lane_exhausted;
        return outcome;
    }
    msg_entry_t entry{};
    entry.id = static_cast<raft_entry_id_t>(request.request_sequence & 0x7FFFFFFFU);
    const std::array<bool, 1> ordinary{false};
    if (!log_payloads_->can_admit(ordinary) ||
        !archive_admission(static_cast<std::uint64_t>(raft_get_current_idx(server_)) + 1, false)) {
        ++log_capacity_refusals_;
        outcome.status = ProposeStatus::log_capacity_exhausted;
        return outcome;
    }
    entry.type = kAuthorityBarrierEntryType;
    entry.data.buf = bytes.data();
    entry.data.len = static_cast<unsigned int>(bytes.size());
    msg_entry_response_t response{};
    const auto result = raft_recv_entry(server_, &entry, &response);
    require_usable();
    if (result == RAFT_ERR_NOT_LEADER) {
        outcome.status = ProposeStatus::not_leader;
        outcome.leader_id = raft_get_current_leader(server_);
        return outcome;
    }
    if (result != 0) return outcome;
    pending_authority_barrier_ = PendingAuthorityBarrier{request, response.term, response.idx};
    persist_commit_index();
    if (const auto applied = raft_apply_all(server_);
        applied != 0 && applied != RAFT_ERR_PERSIST_PENDING)
        throw std::runtime_error("authority marker apply failed: " + callback_error_);
    outcome.status = ProposeStatus::admitted;
    outcome.index = response.idx;
    outcome.term = response.term;
    return outcome;
}

bool ReplicatedKernel::authority_proof_is_current(const AuthorityProofView& proof) const noexcept {
    if (raft_is_leader(server_) == 0 || raft_get_current_term(server_) != proof.term ||
        raft_get_commit_idx(server_) < proof.barrier_index ||
        raft_get_last_applied_idx(server_) < proof.barrier_index ||
        arena_.durable_index() < static_cast<std::uint64_t>(proof.barrier_index) ||
        state_.strategy_mode != StrategyMode::normal ||
        state_.kill_epoch != proof.request.expected_kill_epoch ||
        state_.executor_generation != proof.request.expected_executor_generation ||
        state_.execution_catalog_root != proof.request.catalog_root)
        return false;
    const auto binding = materialize_bound_order_effect(state_, proof.request.effect_id, catalog_);
    if (!binding || *binding != proof.binding ||
        binding->effect.operation != EffectOperation::submit ||
        binding->effect.payload_digest != proof.request.expected_effect_payload_digest ||
        binding->catalog_root != proof.request.catalog_root)
        return false;
    for (std::size_t i = 0; i < state_.outbox_effect_count; ++i) {
        const auto& record = state_.outbox_effects[i];
        if (record.effect_id == proof.request.effect_id)
            return record.state == OutboxState::pending &&
                record.operation == EffectOperation::submit &&
                record.payload_digest == proof.request.expected_effect_payload_digest;
    }
    return false;
}

void ReplicatedKernel::revoke_stale_authority() {
    const auto term = raft_get_current_term(server_);
    if (pending_authority_barrier_ &&
        (raft_is_leader(server_) == 0 || pending_authority_barrier_->term != term))
        pending_authority_barrier_.reset();
    if (authority_proof_ && !authority_proof_is_current(authority_proof_->view()))
        authority_proof_.reset();
}

std::optional<AuthorityProof> ReplicatedKernel::take_authority_proof() {
    require_usable();
    if (compaction_ || persistence_pending()) return std::nullopt;
    revoke_stale_authority();
    if (!authority_proof_) return std::nullopt;
    auto result = std::move(authority_proof_);
    authority_proof_.reset();
    return result;
}

void ReplicatedKernel::abandon_authority_request(const AuthorityBarrier& request) {
    require_usable();
    if (compaction_ || persistence_pending()) return;
    if (pending_authority_barrier_ && pending_authority_barrier_->request == request)
        pending_authority_barrier_.reset();
    if (authority_proof_ && authority_proof_->view().request == request)
        authority_proof_.reset();
}

std::int64_t ReplicatedKernel::authority_drain_index() const {
    require_usable();
    const auto applied = raft_get_last_applied_idx(server_);
    for (auto index = log_payloads_->last_index();
         index > static_cast<std::uint64_t>(applied) && index >= log_payloads_->first_index(); --index) {
        const auto* entry = raft_get_entry_from_idx(server_, static_cast<raft_index_t>(index));
        if (entry && entry->type == kAuthorityBarrierEntryType)
            return static_cast<std::int64_t>(index);
    }
    return 0;
}

OutboundTakeResult ReplicatedKernel::take_outbound_frame(
    std::span<std::byte> output, std::span<const std::int32_t> held_peers) {
    require_usable();
    if (compaction_ || persistence_pending()) return {};
    return outbound_->take(output, held_peers);
}

std::int64_t ReplicatedKernel::compaction_floor() const noexcept {
    auto floor = static_cast<std::int64_t>(arena_.durable_index());
    for (int i = 0; i < raft_get_num_nodes(server_); ++i) {
        auto* node = raft_get_node_from_idx(server_, static_cast<raft_index_t>(i));
        if (node == nullptr || raft_node_get_id(node) == node_id_ || raft_node_is_voting(node) == 0) continue;
        floor = std::min<std::int64_t>(floor, raft_node_get_match_idx(node));
    }
    return std::max<std::int64_t>(floor, known_floor_);
}

void ReplicatedKernel::send_early_ack() {
    if (storage_failed_ || outbound_failed_) return;
    const auto term = static_cast<std::int64_t>(raft_get_current_term(server_));
    const auto due = follower_ack_.due(term, raft_get_current_leader(server_), raft_is_follower(server_) != 0,
                                       static_cast<std::int64_t>(arena_.durable_index()));
    if (!due) return;
    if (queue_outbound({node_id_, follower_ack_.leader(),
                        AppendEntriesResponse{term, true, *due, *due + 1}}) == 0)
        follower_ack_.sent(*due);
}

int ReplicatedKernel::queue_outbound(const PeerMessageView& message) noexcept {
    if (outbound_failed_) return RAFT_ERR_SHUTDOWN;
    const auto queued = outbound_->enqueue(message, *peer_scratch_);
    if (queued.error == OutboundEnqueueError::none ||
        queued.error == OutboundEnqueueError::stale_term) return 0;
    outbound_failed_ = true;
    return RAFT_ERR_SHUTDOWN;
}

ReplicaStatus ReplicatedKernel::status() const {
    ReplicaStatus value;
    value.node_id = node_id_;
    value.leader_id = raft_get_current_leader(server_);
    value.role = role_name(server_);
    value.term = raft_get_current_term(server_);
    value.current_index = raft_get_current_idx(server_);
    value.commit_index = raft_get_commit_idx(server_);
    value.applied_index = raft_get_last_applied_idx(server_);
    value.state_version = state_.state_version;
    value.outbox_effect_count = state_.outbox_effect_count;
    value.state_digest = state_digest_of(state_);
    value.retained_ordinary = log_payloads_->ordinary_count();
    value.retained_protected = log_payloads_->protected_count();
    value.compaction_floor = compaction_floor();
    value.snapshot_index = raft_get_snapshot_last_idx(server_);
    value.receipts = state_.receipt_count;
    value.pinned_receipts = pinned_receipt_count(state_);
    value.pinned_receipts_peak = pinned_receipts_peak_;
    value.archive_bytes = archive_open_ ? archive_.logical_bytes() : 0;
    value.archive_objects = archive_open_ ? archive_.object_count() : 0;
    value.archive_byte_limit = archive_config_.logical_byte_limit;
    value.archive_object_limit = archive_config_.object_limit;
    value.policies = state_.policy_count;
    value.ledger_count = state_.risk_account_count;
    for (std::size_t index = 0; index < state_.risk_account_count; ++index) {
        const auto& risk = state_.risk_accounts[index];
        value.ledgers[index] = {risk.account_id.value(), risk.instrument_id.value(),
                                risk.position_quantity.atoms(), risk.cash.atoms(), risk.cost_basis.atoms(),
                                risk.realized_pnl.atoms(), risk.fees_paid.atoms(),
                                risk.window_realized_pnl.atoms(), risk.max_window_loss.atoms()};
    }
    if (raft_is_leader(server_) != 0) {
        const auto snapshot = raft_get_snapshot_last_idx(server_);
        for (int i = 0; i < raft_get_num_nodes(server_); ++i) {
            auto* node = raft_get_node_from_idx(server_, static_cast<raft_index_t>(i));
            if (node == nullptr || raft_node_get_id(node) == node_id_) continue;
            if (0 < snapshot && raft_node_get_next_idx(node) < snapshot) ++value.stranded_voters;
        }
    }
    return value;
}

CommittedOutboxView ReplicatedKernel::committed_outbox() const {
    require_usable();
    CommittedOutboxView view;
    view.authoritative = is_leader() && !persistence_pending();
    view.leader_id = raft_get_current_leader(server_);
    view.term = raft_get_current_term(server_);
    view.applied_index = raft_get_last_applied_idx(server_);
    view.effect_count = state_.outbox_effect_count;
    for (std::size_t index = 0; index < state_.outbox_effect_count; ++index) {
        view.effects[index] = state_.outbox_effects[index];
    }
    return view;
}

std::uint64_t ReplicatedKernel::compact_within_floor() {
    require_usable();
    if (compaction_) return compact();
    if (raft_get_last_applied_idx(server_) > compaction_floor()) return 0;
    return compact();
}

std::uint64_t ReplicatedKernel::compact() {
    require_usable();
    if (persistence_pending()) return 0;
    if (compaction_) {
        if (!compaction_->local_completion_acquired || compaction_->rewrite_pending) {
            if (!writer_->take_maintenance_completion()) return 0;
            compaction_->local_completion_acquired = true;
        }
        const auto cut = compaction_->cut;
        if (compaction_->publication.storage_generation != startup_.storage_generation() ||
            compaction_->publication.committed_index != static_cast<std::uint64_t>(raft_get_commit_idx(server_)) ||
            compaction_->publication.durable_index != arena_.durable_index() ||
            compaction_->publication.current_term != raft_get_current_term(server_) ||
            cut != static_cast<std::uint64_t>(raft_get_last_applied_idx(server_))) {
            storage_failed_ = true;
            throw std::runtime_error("compaction completion no longer matches fenced owner");
        }
        if (compaction_->deferred) {
            if (raft_cancel_snapshot(server_) != 0) {
                storage_failed_ = true;
                throw std::runtime_error("failed to cancel a capacity-deferred snapshot");
            }
            compaction_.reset();
            return 0;
        }
        if (!compaction_->snapshot_verified) {
            storage_failed_ = true;
            throw std::runtime_error("compaction completion lacks verified snapshot");
        }
        if (compaction_->rewrite_pending) {
            compaction_.reset();
            return cut;
        }
        const bool independent_required =
            startup_.identity().value().deployment_mode == DeploymentMode::two_host_pilot;
        if (independent_required) {
            if (!archive_verification_request_) {
                if (next_persistence_sequence_ == UINT64_MAX) {
                    storage_failed_ = true;
                    throw std::runtime_error("archive verification attempt identity exhausted");
                }
                const auto attempt = next_persistence_sequence_++;
                archive_verification_request_ = archive_.make_destination_request(
                    startup_.storage_generation(), attempt, archive_challenge(archive_session_nonce_, attempt),
                    archive_destination_identity_);
            }
            if (!archive_verification_evidence_) return 0;
        }
        for (auto index = log_payloads_->first_index(); index <= cut; ++index) {
            const auto* owned = log_payloads_->find(index);
            const auto* entry = raft_get_entry_from_idx(server_, static_cast<raft_index_t>(index));
            if (!owned || owned->result || !entry || entry->data.buf != log_payloads_->payload(index).data() ||
                entry->data.len != owned->length) {
                log_ownership_failed_ = true;
                throw std::runtime_error("compaction release still has an ownership obligation");
            }
        }
        const bool covered = independent_required
            ? archive_.consume_for_reclamation(std::move(compaction_->coverage),
                std::move(*archive_verification_evidence_), *archive_verification_request_,
                startup_.storage_generation(), cut)
            : archive_.consume_for_reclamation(std::move(compaction_->coverage),
                startup_.storage_generation(), cut);
        archive_verification_evidence_.reset();
        archive_verification_request_.reset();
        if (!covered) {
            storage_failed_ = true;
            throw std::runtime_error("compaction coverage no longer matches current archive");
        }
        compaction_->reclamation_authorized = true;
        polled_through_ = 0;
        if (raft_end_snapshot(server_) != 0) {
            log_ownership_failed_ = true;
            throw std::runtime_error("failed to finish verified compaction: " + callback_error_);
        }
        compaction_->rewrite_pending = true;
        if (!writer_->submit_maintenance(&ReplicatedKernel::persist_compaction, compaction_.get())) {
            storage_failed_ = true;
            throw std::runtime_error("WAL continuation refused after Raft prefix release");
        }
        return 0;
    }
    if (startup_.identity().value().deployment_mode != DeploymentMode::laboratory &&
        archive_destination_identity_ == Digest256{}) return 0;
    if (log_payloads_->size() <= 1) return 0;
    const auto applied = raft_get_last_applied_idx(server_);
    if (applied <= 0 || applied != raft_get_commit_idx(server_) ||
        static_cast<std::uint64_t>(applied) > arena_.durable_index() ||
        highest_staged_index_ > arena_.durable_index() || arena_.sealed_count() != 0)
        return 0;
    const auto* last = raft_get_entry_from_idx(server_, applied);
    if (last == nullptr) return 0;
    auto job = std::make_unique<CompactionJob>();
    job->owner = this;
    job->state = state_;
    job->cut = static_cast<std::uint64_t>(applied);
    job->term = last->term;
    const auto covered = archive_open_ ? archive_.covered_through() : 0;
    job->publication = {archive_open_ ? archive_.catalog_generation() + 1 : 1,
        startup_.storage_generation(), static_cast<std::uint64_t>(raft_get_commit_idx(server_)),
        arena_.durable_index(), raft_get_current_term(server_), 0};
    if (covered < job->cut) {
        if (job->cut - covered > kRetainedLogEntries)
            throw std::runtime_error("archive source exceeds bounded retained history");
        job->records.reserve(static_cast<std::size_t>(job->cut - covered));
        for (auto index = covered + 1; index <= job->cut; ++index) {
            const auto* entry = raft_get_entry_from_idx(server_, static_cast<raft_index_t>(index));
            const auto payload = log_payloads_->payload(index);
            if (!entry || !log_payloads_->find(index) || entry->data.len != payload.size() ||
                entry->data.buf != payload.data())
                throw std::runtime_error("archive source does not match owned Raft payload");
            job->records.push_back(WalRecord{index, entry->term, entry->id, entry->type,
                std::vector<std::byte>(payload.begin(), payload.end())});
        }
    }
    job->publication.remaining_pending_records = static_cast<std::uint32_t>(
        std::max<std::int64_t>(0, raft_get_current_idx(server_) - std::max<std::int64_t>(applied, covered)));
    start_writer();
    if (raft_begin_snapshot(server_, 0) != 0) return 0;
    compaction_ = std::move(job);
    if (!writer_->submit_maintenance(&ReplicatedKernel::persist_compaction, compaction_.get())) {
        (void)raft_cancel_snapshot(server_);
        compaction_.reset();
        return 0;
    }
    return 0;
}

void ReplicatedKernel::persist_compaction(void* context) {
    auto& job = *static_cast<CompactionJob*>(context);
    auto& self = *job.owner;
    if (job.rewrite_pending) {
        self.storage_checkpoints_(StorageCheckpoint::compaction_before_wal_rewrite);
        self.segment_->discard_through(job.cut);
        self.storage_checkpoints_(StorageCheckpoint::compaction_wal_rewritten);
        return;
    }
    std::size_t matched{};
    WalSegment::visit_existing_records(self.startup_.directory(), self.startup_.identity(),
        [&](const WalRecord& record) {
            if (job.records.empty() || record.index < job.records.front().index || record.index > job.cut) return;
            if (matched >= job.records.size()) throw std::runtime_error("duplicate archive WAL source");
            const auto& expected = job.records[matched++];
            if (record.index != expected.index || record.term != expected.term ||
                record.entry_id != expected.entry_id || record.entry_type != expected.entry_type ||
                record.payload != expected.payload)
                throw std::runtime_error("archive source differs from durable WAL");
        });
    if (matched != job.records.size()) throw std::runtime_error("archive durable WAL source missing");
    if (!self.archive_open_) self.open_archive();
    if (!job.records.empty()) {
        job.publication.predecessor_term = self.archive_.last_term();
        const ArchiveRecordSource source{&job.records, static_cast<std::uint32_t>(job.records.size()),
            [](void* p, std::uint32_t at, ArchiveRecordView& out) noexcept {
                const auto& records = *static_cast<const std::vector<WalRecord>*>(p);
                if (at >= records.size()) return false;
                const auto& record = records[at];
                out = {record.index, record.term, record.entry_id, record.entry_type, record.payload};
                return true;
            }};
        auto published = self.archive_.publish(job.publication, source);
        if (published.error == ArchiveStoreError::capacity_exceeded) {
            job.deferred = true;
            return;
        }
        if (published.error != ArchiveStoreError::none) throw std::runtime_error("archive publication refused");
        job.coverage = std::move(published.capability);
    } else {
        if (self.archive_.verify_incremental() != ArchiveStoreError::none)
            throw std::runtime_error("previously published archive evidence no longer verifies");
        job.coverage = self.archive_.coverage_for_reclamation(job.publication.storage_generation, job.cut);
    }
    self.storage_checkpoints_(StorageCheckpoint::compaction_archive_verified);
    self.snapshots_.save(job.state, job.cut, job.term);
    const auto reloaded = self.snapshots_.load();
    if (!reloaded || reloaded->last_included_index != job.cut ||
        reloaded->last_included_term != job.term ||
        reloaded->state_digest != state_digest_of(job.state))
        throw std::runtime_error("published snapshot did not reload as fenced state");
    job.snapshot_verified = true;
    self.storage_checkpoints_(StorageCheckpoint::compaction_snapshot_verified);
}

void ReplicatedKernel::persist_commit_index() {
    const auto durable = static_cast<raft_index_t>(arena_.durable_index());
    const auto raw = raft_get_commit_idx(server_);
    const auto commit = std::min(durable, raw);
    if (commit == desired_metadata_.commit_index &&
        raft_get_current_term(server_) == desired_metadata_.term &&
        raft_get_voted_for(server_) == desired_metadata_.voted_for) return;
    auto next = desired_metadata_;
    next.term = raft_get_current_term(server_);
    next.voted_for = raft_get_voted_for(server_);
    next.commit_index = commit;
    try {
        stage_hard_state(next);
    } catch (...) {
        storage_failed_ = true;
        throw;
    }
}

ReplicatedKernel::AdmissionCapacity ReplicatedKernel::admission_capacity(const AppendEntries& append) const {
    if (append.term < raft_get_current_term(server_)) return {};
    const auto prev = append.previous_log_index;
    if (prev > 0) {
        if (prev == raft_get_snapshot_last_idx(server_)) {
            if (append.previous_log_term != raft_get_snapshot_last_term(server_)) return {};
        } else {
            const auto* entry = raft_get_entry_from_idx(server_, prev);
            if (!entry || entry->term != append.previous_log_term) return {};
        }
    }

    std::array<ResultHandle, kResultLedgerSlots> released{};
    std::size_t released_count{};
    std::size_t first_new{};
    std::optional<std::uint64_t> replaced_suffix;
    for (; first_new < append.entries.size(); ++first_new) {
        const auto index = prev + 1 + static_cast<std::int64_t>(first_new);
        const auto* existing = raft_get_entry_from_idx(server_, index);
        if (!existing) break;
        if (existing->term == append.entries[first_new].term) continue;
        if (index <= raft_get_commit_idx(server_)) return {};
        replaced_suffix = static_cast<std::uint64_t>(index);
        for (auto retained = *replaced_suffix; retained <= log_payloads_->last_index(); ++retained) {
            const auto* owned = log_payloads_->find(retained);
            if (!owned) throw std::runtime_error("log ownership gap during admission");
            if (!owned->result) continue;
            if (released_count == released.size())
                throw std::runtime_error("result reservation ownership exceeds ledger");
            released[released_count++] = *owned->result;
        }
        break;
    }
    std::array<bool, kMaxBoundPeerEntries> classes{};
    std::size_t count{};
    std::array<bool, kMaxBoundPeerEntries> log_classes{};
    std::size_t log_count{};
    bool archive_capacity = true;
    for (auto i = first_new; i < append.entries.size(); ++i) {
        const auto& entry = append.entries[i];
        if (entry.type == kAuthorityBarrierEntryType) {
            log_classes[log_count++] = false;
            archive_capacity = archive_capacity && archive_admission(
                static_cast<std::uint64_t>(prev + 1 + static_cast<std::int64_t>(i)), false);
            continue;
        }
        Command command;
        if (decode_kernel_command(entry.payload, command, *codec_scratch_).error != KernelCodecError::none)
            throw std::runtime_error("result plan command did not decode");
        classes[count++] = control_eligible(command);
        log_classes[log_count++] = control_eligible(command);
        archive_capacity = archive_capacity && archive_admission(
            static_cast<std::uint64_t>(prev + 1 + static_cast<std::int64_t>(i)), control_eligible(command));
    }
    return {results_->can_reserve(std::span(classes).first(count),
                                 std::span(released).first(released_count)),
            archive_capacity && log_payloads_->can_admit(std::span(log_classes).first(log_count), replaced_suffix)};
}

PeerReceiveResult ReplicatedKernel::receive_peer(std::span<const std::byte> bound_frame) {
    require_usable();
    if (compaction_ || persistence_pending())
        return {PeerReceiveStatus::busy, BoundFrameError::none};
    PeerMessage message;
    const auto admission = decode_bound_peer(bound_frame, static_identity(), message, *peer_scratch_);
    if (admission != BoundFrameError::none) return {PeerReceiveStatus::processed, admission};
    auto* sender = raft_get_node(server_, message.sender_id);
    if (sender == nullptr) throw std::runtime_error("peer message came from a non-member");

    int result = 0;
    if (const auto* value = std::get_if<RequestVote>(&message.payload)) {
        msg_requestvote_t request{value->term, value->candidate_id, value->last_log_index,
                                  value->last_log_term, value->prevote ? 1 : 0};
        msg_requestvote_response_t response{};
        result = raft_recv_requestvote(server_, sender, &request, &response);
        if (result == 0) {
            (void)queue_outbound(
                {node_id_, message.sender_id,
                 RequestVoteResponse{response.term, response.vote_granted != 0, response.prevote != 0}});
        }
    } else if (const auto* value = std::get_if<RequestVoteResponse>(&message.payload)) {
        msg_requestvote_response_t response{value->term, value->vote_granted ? 1 : 0,
                                            value->prevote ? 1 : 0};
        result = raft_recv_requestvote_response(server_, sender, &response);
    } else if (const auto* value = std::get_if<AppendEntries>(&message.payload)) {
        std::vector<raft_entry_t> entries(value->entries.size());
        for (std::size_t i = 0; i < value->entries.size(); ++i) {
            const auto& source = value->entries[i];
            validate_log_payload(source.type, source.payload);
            entries[i].term = source.term;
            entries[i].id = source.id;
            entries[i].type = source.type;
            entries[i].data.buf = const_cast<std::byte*>(source.payload.data());
            entries[i].data.len = static_cast<unsigned int>(source.payload.size());
        }
        msg_appendentries_t request{};
        request.term = value->term;
        request.prev_log_idx = value->previous_log_index;
        request.prev_log_term = value->previous_log_term;
        const auto capacity = admission_capacity(*value);
        if (!capacity.results || !capacity.log) {
            if (!capacity.results) ++result_capacity_refusals_;
            if (!capacity.log) ++log_capacity_refusals_;
            entries.clear();
        }
        const auto verified_end = value->previous_log_index +
            static_cast<std::int64_t>(entries.size());
        request.leader_commit = std::min(value->leader_commit, verified_end);
        request.n_entries = static_cast<int>(entries.size());
        request.entries = entries.data();
        msg_appendentries_response_t response{};
        result = raft_recv_appendentries(server_, sender, &request, &response);

        auto reported = response.current_idx;
        const auto durable = static_cast<raft_index_t>(arena_.durable_index());
        if (response.success != 0 && reported > durable) reported = durable;

        if (result == 0) {
            (void)queue_outbound(
                {node_id_, message.sender_id,
                 AppendEntriesResponse{response.term, response.success != 0, reported,
                                       response.first_idx}});
            follower_ack_.record(response.term, message.sender_id, response.success != 0,
                                 response.current_idx, reported);
            if (response.term == value->term) known_floor_ = std::max(known_floor_, value->compaction_floor);
        }
    } else if (const auto* value = std::get_if<AppendEntriesResponse>(&message.payload)) {
        msg_appendentries_response_t response{};
        response.term = value->term;
        response.success = value->success ? 1 : 0;
        response.current_idx = value->current_index;
        response.first_idx = value->first_index;
        result = raft_recv_appendentries_response(server_, sender, &response);
    }
    if (result == RAFT_ERR_PERSIST_PENDING) {
        if (bound_frame.size() > retry_frame_.size() || retry_frame_size_ != 0) {
            storage_failed_ = true;
            throw std::runtime_error("bounded peer continuation ownership exhausted");
        }
        std::copy(bound_frame.begin(), bound_frame.end(), retry_frame_.begin());
        retry_frame_size_ = bound_frame.size();
        return {PeerReceiveStatus::accepted_deferred, BoundFrameError::none};
    }
    if (result != 0 && result != RAFT_ERR_NOT_LEADER) {
        throw std::runtime_error("raft peer handling failed: " + std::to_string(result) +
                                 " " + callback_error_);
    }
    require_usable();
    revoke_stale_authority();
    persist_commit_index();
    if (const int applied = raft_apply_all(server_);
        applied != 0 && applied != RAFT_ERR_PERSIST_PENDING) {
        throw std::runtime_error("raft_apply_all failed: " + std::to_string(applied) +
                                 " " + callback_error_);
    }
    return {persistence_pending() ? PeerReceiveStatus::accepted_deferred
                                  : PeerReceiveStatus::processed,
            BoundFrameError::none};
}

int ReplicatedKernel::before_apply(raft_server_t*, void* user_data, raft_entry_t* entry,
                                   const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    try {
        self.require_usable();
        if (!self.restoring_ && self.writer_running_) {
            const auto durable = static_cast<raft_index_t>(self.arena_.durable_index());
            if (index > durable) return RAFT_ERR_PERSIST_PENDING;
            if (self.desired_metadata_.commit_index < index) {
                auto next = self.desired_metadata_;
                next.term = raft_get_current_term(self.server_);
                next.voted_for = raft_get_voted_for(self.server_);
                next.commit_index = index;
                self.desired_metadata_ = next;
            }
            if (!same_metadata(self.desired_metadata_, self.metadata_))
                return RAFT_ERR_PERSIST_PENDING;
        }
        Command command;
        const auto bytes = std::span<const std::byte>(
            static_cast<const std::byte*>(entry->data.buf), entry->data.len);
        self.validate_log_payload(entry->type, bytes);
        if (entry->type == kAuthorityBarrierEntryType) return 0;
        const auto decoded = decode_kernel_command(bytes, command, *self.codec_scratch_);
        if (decoded.error != KernelCodecError::none ||
            !execution_catalog_ready(required_execution_catalog(self.state_, command),
                                     &self.catalog_)) {
            self.catalog_dependency_failed_ = true;
            return RAFT_ERR_SHUTDOWN;
        }
        if (!self.restoring_) {
            const auto* owned = self.log_payloads_->find(static_cast<std::uint64_t>(index));
            if (!owned || !owned->result ||
                !self.results_->matches(*owned->result,
                    {command.identity, decoded.frame_digest, entry->term, index})) {
                self.result_ownership_failed_ = true;
                return RAFT_ERR_SHUTDOWN;
            }
        }
        return 0;
    } catch (...) {
        self.catalog_dependency_failed_ = true;
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::apply_log(raft_server_t*, void* user_data, raft_entry_t* entry,
                                const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    try {
        const auto bytes = std::span<const std::byte>(
            static_cast<const std::byte*>(entry->data.buf), entry->data.len);

        if (entry->type == kAuthorityBarrierEntryType) {
            if (!self.restoring_ && self.pending_authority_barrier_ &&
                self.pending_authority_barrier_->index == index &&
                self.pending_authority_barrier_->term == entry->term &&
                raft_is_leader(self.server_) != 0 &&
                raft_get_current_term(self.server_) == entry->term &&
                raft_get_commit_idx(self.server_) >= index &&
                self.arena_.durable_index() >= static_cast<std::uint64_t>(index) &&
                !self.authority_proof_) {
                const auto& request = self.pending_authority_barrier_->request;
                const auto binding = materialize_bound_order_effect(
                    self.state_, request.effect_id, self.catalog_);
                bool pending_submit = false;
                for (std::size_t i = 0; i < self.state_.outbox_effect_count; ++i) {
                    const auto& record = self.state_.outbox_effects[i];
                    if (record.effect_id == request.effect_id) {
                        pending_submit = record.state == OutboxState::pending &&
                            record.operation == EffectOperation::submit &&
                            record.payload_digest == request.expected_effect_payload_digest;
                        break;
                    }
                }
                if (binding && pending_submit && self.state_.strategy_mode == StrategyMode::normal &&
                    self.state_.kill_epoch == request.expected_kill_epoch &&
                    self.state_.executor_generation == request.expected_executor_generation &&
                    self.state_.execution_catalog_root == request.catalog_root &&
                    binding->catalog_root == request.catalog_root &&
                    binding->effect.effect_id == request.effect_id &&
                    binding->effect.payload_digest == request.expected_effect_payload_digest &&
                    binding->effect.kill_epoch == request.expected_kill_epoch &&
                    binding->effect.executor_generation == request.expected_executor_generation &&
                    binding->effect.operation == EffectOperation::submit) {
                    const AuthorityProofView view{request, *binding, self.node_id_, entry->term,
                        index, index, self.state_.state_version, self.state_.strategy_mode};
                    self.authority_proof_.emplace(AuthorityProof(view));
                }
            }
            if (self.pending_authority_barrier_ && self.pending_authority_barrier_->index == index)
                self.pending_authority_barrier_.reset();
            return 0;
        }

        Command command;
        const auto decoded =
            decode_kernel_command(bytes, command, *self.codec_scratch_);
        if (decoded.error != KernelCodecError::none) {
            self.callback_error_ = "committed entry did not decode: type=" +
                std::to_string(entry->type) + " len=" +
                std::to_string(entry->data.len) + " codec=" +
                std::to_string(static_cast<int>(decoded.error));
            return RAFT_ERR_SHUTDOWN;
        }
        const auto result = apply(self.state_, command, *self.scratch_, &self.catalog_);
        self.pinned_receipts_peak_ = std::max<std::uint64_t>(self.pinned_receipts_peak_, pinned_receipt_count(self.state_));

        if (!self.restoring_) {
            auto* owned = self.log_payloads_->find(static_cast<std::uint64_t>(index));
            if (!owned || !owned->result || !self.results_->publish(*owned->result,
                    {command.identity, result, entry->term, index})) {
                self.result_ownership_failed_ = true;
                return RAFT_ERR_SHUTDOWN;
            }
            owned->result.reset();
            self.revoke_stale_authority();
        }
        return 0;
    } catch (const std::exception& error) {
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::send_request_vote(raft_server_t*, void* user_data, raft_node_t* node,
                                        msg_requestvote_t* message) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    return self.queue_outbound(
        {self.node_id_, raft_node_get_id(node),
         RequestVote{message->term, message->candidate_id, message->last_log_idx,
                     message->last_log_term, message->prevote != 0}});
}

int ReplicatedKernel::send_append_entries(raft_server_t*, void* user_data,
                                          raft_node_t* node,
                                          msg_appendentries_t* message) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    BorrowedAppendEntries append{};
    if (message->n_entries < 0) {
        self.outbound_failed_ = true;
        return RAFT_ERR_SHUTDOWN;
    }
    append.term = message->term;
    append.previous_log_index = message->prev_log_idx;
    append.previous_log_term = message->prev_log_term;
    append.leader_commit = message->leader_commit;
    append.compaction_floor = self.compaction_floor();
    const auto count = std::min(message->n_entries, static_cast<int>(kMaxBoundPeerEntries));
    std::array<BorrowedPeerLogEntry, kMaxBoundPeerEntries> entries{};
    for (int i = 0; i < count; ++i) {
        const auto& entry = message->entries[i];
        const auto* begin = static_cast<const std::byte*>(entry.data.buf);
        entries[static_cast<std::size_t>(i)] = {entry.term, entry.id, entry.type,
                                              {begin, entry.data.len}};
    }
    append.entries = std::span(entries).first(static_cast<std::size_t>(count));
    return self.queue_outbound({self.node_id_, raft_node_get_id(node), append});
}

int ReplicatedKernel::persist_vote(raft_server_t*, void* user_data, raft_node_id_t vote) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    auto next = self.desired_metadata_;
    next.voted_for = vote;
    if (self.restoring_) {
        self.metadata_ = self.desired_metadata_ = next;
        return 0;
    }
    try {
        self.stage_hard_state(next);
        return 0;
    } catch (const std::exception& error) {
        self.storage_failed_ = true;
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    } catch (...) {
        self.storage_failed_ = true;
        self.callback_error_ = "unknown hard-state persistence failure";
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::persist_term(raft_server_t*, void* user_data, raft_term_t term,
                                   raft_node_id_t vote) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    auto next = self.desired_metadata_;
    next.term = term;
    next.voted_for = vote;
    if (self.restoring_) {
        self.metadata_ = self.desired_metadata_ = next;
        return 0;
    }
    try {
        self.stage_hard_state(next);
        return 0;
    } catch (const std::exception& error) {
        self.storage_failed_ = true;
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    } catch (...) {
        self.storage_failed_ = true;
        self.callback_error_ = "unknown hard-state persistence failure";
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_offer(raft_server_t*, void* user_data, raft_entry_t* entry,
                                const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    std::optional<ResultHandle> reservation;
    bool inserted = false;
    const auto discard = [&] {
        if (reservation && !self.results_->release_reserved(*reservation))
            self.result_ownership_failed_ = true;
        if (inserted && !self.log_payloads_->erase(static_cast<std::uint64_t>(index)))
            self.log_ownership_failed_ = true;
    };
    try {
        if (!self.restoring_ && self.writer_running_ &&
            !same_metadata(self.desired_metadata_, self.metadata_))
            return RAFT_ERR_PERSIST_PENDING;
        const auto* begin = static_cast<const std::byte*>(entry->data.buf);
        const auto bytes = std::span<const std::byte>(begin, entry->data.len);
        Command command;
        DecodeKernelValueResult decoded{};
        bool control = false;
        if (entry->type != kAuthorityBarrierEntryType) {
            decoded = decode_kernel_command(bytes, command, *self.codec_scratch_);
            if (decoded.error != KernelCodecError::none)
                throw std::runtime_error("retained log command did not decode");
            control = control_eligible(command);
        }
        if (!self.log_payloads_->append(static_cast<std::uint64_t>(index), bytes, control)) {
            if (!self.restoring_) self.log_ownership_failed_ = true;
            throw std::runtime_error("retained log capacity or ownership exhausted");
        }
        inserted = true;
        auto* owned = self.log_payloads_->find(static_cast<std::uint64_t>(index));
        const auto payload = self.log_payloads_->payload(static_cast<std::uint64_t>(index));
        if (entry->type != kAuthorityBarrierEntryType &&
            (!self.restoring_ || index > self.metadata_.commit_index)) {
            reservation = self.results_->reserve(
                {command.identity, decoded.frame_digest, entry->term, index}, control);
            if (!reservation) {
                if (!self.restoring_) self.result_ownership_failed_ = true;
                throw std::runtime_error("uncommitted result capacity exhausted");
            }
            owned->result = reservation;
        }
        entry->data.buf = payload.data();

        if (self.restoring_) return 0;

        const auto framed = encode_bound_wal_record({static_cast<std::uint64_t>(index),
                                               entry->term, entry->id, entry->type,
                                               {payload.begin(), payload.end()}}, self.startup_.identity());
        switch (self.arena_.stage(framed, static_cast<std::uint64_t>(index))) {
            case StageOutcome::staged:
                self.highest_staged_index_ =
                    std::max(self.highest_staged_index_,
                             static_cast<std::uint64_t>(index));
                return 0;
            case StageOutcome::backpressure:
            case StageOutcome::record_too_large:
                ++self.backpressure_refusals_;
                self.callback_error_ = "WAL staging refused the entry";
                discard();
                return RAFT_ERR_SHUTDOWN;
        }
        discard();
        return RAFT_ERR_SHUTDOWN;
    } catch (const std::exception& error) {
        discard();
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    }
}

bool ReplicatedKernel::release_log_results(const std::uint64_t from) noexcept {
    if (log_payloads_->empty()) return true;
    for (auto index = std::max(from, log_payloads_->first_index());
         index <= log_payloads_->last_index(); ++index) {
        auto* owned = log_payloads_->find(index);
        if (!owned) { log_ownership_failed_ = true; return false; }
        if (!owned->result) continue;
        if (!results_->release_reserved(*owned->result)) {
            result_ownership_failed_ = true;
            return false;
        }
        owned->result.reset();
    }
    return true;
}

bool ReplicatedKernel::erase_log_suffix(const std::uint64_t from) noexcept {
    if (!release_log_results(from)) return false;
    while (!log_payloads_->empty() && log_payloads_->last_index() >= from) {
        if (!log_payloads_->erase(log_payloads_->last_index())) {
            log_ownership_failed_ = true;
            return false;
        }
    }
    return true;
}

int ReplicatedKernel::log_poll(raft_server_t*, void* user_data, raft_entry_t*,
                               const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    if (!self.compaction_ || !self.compaction_->reclamation_authorized ||
        static_cast<std::uint64_t>(index) > self.compaction_->cut) {
        self.log_ownership_failed_ = true;
        return RAFT_ERR_SHUTDOWN;
    }
    const auto* owned = self.log_payloads_->find(static_cast<std::uint64_t>(index));
    if (owned && owned->result) {
        self.result_ownership_failed_ = true;
        return RAFT_ERR_SHUTDOWN;
    }
    if (!self.log_payloads_->erase(static_cast<std::uint64_t>(index))) {
        self.log_ownership_failed_ = true;
        return RAFT_ERR_SHUTDOWN;
    }
    self.polled_through_ =
        std::max(self.polled_through_, static_cast<std::uint64_t>(index));
    return 0;
}

int ReplicatedKernel::log_prepare_truncate(raft_server_t*, void* user_data,
                                           const raft_index_t from) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    if (self.storage_failed_ || from <= 0) return RAFT_ERR_SHUTDOWN;
    const auto boundary = static_cast<std::uint64_t>(from);
    if (self.restoring_) return 0;
    try {
        if (!self.writer_running_) {
            self.flush_durability();
            self.segment_->truncate_from(boundary);
            self.arena_.rewind_durable_index(boundary - 1);
            self.highest_staged_index_ = boundary - 1;
            return 0;
        }
        if (!same_metadata(self.desired_metadata_, self.metadata_))
            return RAFT_ERR_PERSIST_PENDING;
        if (self.truncate_grant_) {
            if (*self.truncate_grant_ != boundary) {
                self.storage_failed_ = true;
                self.callback_error_ = "suffix-truncation durable grant target mismatch";
                return RAFT_ERR_SHUTDOWN;
            }
            self.truncate_grant_.reset();
            return 0;
        }
        if (self.truncate_requested_ && *self.truncate_requested_ != boundary) {
            self.storage_failed_ = true;
            self.callback_error_ = "concurrent suffix-truncation target mismatch";
            return RAFT_ERR_SHUTDOWN;
        }
        self.truncate_requested_ = boundary;
        return RAFT_ERR_PERSIST_PENDING;
    } catch (const std::exception& error) {
        self.storage_failed_ = true;
        self.callback_error_ = error.what();
        return RAFT_ERR_SHUTDOWN;
    } catch (...) {
        self.storage_failed_ = true;
        self.callback_error_ = "unknown suffix-truncation persistence failure";
        return RAFT_ERR_SHUTDOWN;
    }
}

int ReplicatedKernel::log_pop(raft_server_t*, void* user_data, raft_entry_t*,
                              const raft_index_t index) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    if (self.storage_failed_) return RAFT_ERR_SHUTDOWN;
    if (self.restoring_) {
        if (!self.erase_log_suffix(static_cast<std::uint64_t>(index))) return RAFT_ERR_SHUTDOWN;
        if (self.pending_authority_barrier_ &&
            self.pending_authority_barrier_->index >= index)
            self.pending_authority_barrier_.reset();
        return 0;
    }
    if (!self.erase_log_suffix(static_cast<std::uint64_t>(index))) return RAFT_ERR_SHUTDOWN;
    if (self.pending_authority_barrier_ && self.pending_authority_barrier_->index >= index)
        self.pending_authority_barrier_.reset();
    return 0;
}

int ReplicatedKernel::log_clear(raft_server_t*, void* user_data, raft_entry_t*,
                                raft_index_t) {
    auto& self = *static_cast<ReplicatedKernel*>(user_data);
    if (self.storage_failed_) return RAFT_ERR_SHUTDOWN;
    if (self.restoring_) {
        if (!self.release_log_results(0)) return RAFT_ERR_SHUTDOWN;
        self.log_payloads_->clear();
        self.pending_authority_barrier_.reset();
        return 0;
    }
    self.storage_failed_ = true;
    self.callback_error_ = "runtime Raft log clear is forbidden";
    return RAFT_ERR_SHUTDOWN;
}

int ReplicatedKernel::log_get_node_id(raft_server_t*, void*, raft_entry_t*, raft_index_t) {
    return -1;
}

raft_index_t ReplicatedKernel::durable_idx(raft_server_t*, void* user_data) {
    const auto& self = *static_cast<const ReplicatedKernel*>(user_data);
    return static_cast<raft_index_t>(self.arena_.durable_index());
}

}
