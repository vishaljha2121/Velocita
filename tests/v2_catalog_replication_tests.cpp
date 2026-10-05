#include "velocita/v2/replication.hpp"
#include "velocita/v2/digest.hpp"

#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class TempDirectory final {
public:
    TempDirectory() {
        const auto prefix = (std::filesystem::temp_directory_path() /
                             "v2-catalog-repl.XXXXXX").string();
        std::vector<char> pattern(prefix.begin(), prefix.end());
        pattern.push_back('\0');
        const auto* made = ::mkdtemp(pattern.data());
        if (made == nullptr) throw std::runtime_error("mkdtemp failed");
        path_ = made;
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

using DirectoryImage = std::map<std::string, std::vector<std::byte>>;

DirectoryImage directory_image(const std::filesystem::path& root) {
    DirectoryImage image;
    if (!std::filesystem::exists(root)) return image;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_directory()) {
            image.emplace("D:" +
                std::filesystem::relative(entry.path(), root).generic_string(),
                std::vector<std::byte>{});
            continue;
        }
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        require(input.good(), "could not capture persisted file");
        const std::string raw((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
            bytes[i] = std::byte{static_cast<unsigned char>(raw[i])};
        image.emplace("F:" +
                          std::filesystem::relative(entry.path(), root).generic_string(),
                      std::move(bytes));
    }
    return image;
}

Digest256 identity_digest(const std::uint8_t marker) {
    Digest256 result;
    result.bytes[0] = std::byte{marker};
    result.bytes[1] = std::byte{0x6d};
    return result;
}

Command command(const std::uint64_t sequence, CommandBody body) {
    return {{CallerId{1}, RequestSequence{sequence},
             identity_digest(static_cast<std::uint8_t>(sequence))}, std::move(body)};
}

Command install(const std::uint64_t sequence, const Digest256 root) {
    return command(sequence,
        InstallExecutionCatalog{root, KillEpoch{1}, ExecutorGeneration{1}});
}

std::vector<std::byte> encoded(const Command& command) {
    std::array<std::byte, kMaxKernelCommandBytes> storage{};
    const auto result = encode_kernel_command(command, storage);
    require(result.error == KernelCodecError::none, "fixture command did not encode");
    return {storage.begin(), storage.begin() +
        static_cast<std::ptrdiff_t>(result.bytes_written)};
}

std::vector<std::byte> unsupported_encoded(const Command& command) {
    auto bytes = encoded(command);
    const auto schema = static_cast<std::uint16_t>(kKernelCodecSchemaVersion + 1);
    bytes[kKernelCommandSchemaOffset] = static_cast<std::byte>(schema & 0xffU);
    bytes[kKernelCommandSchemaOffset + 1] =
        static_cast<std::byte>((schema >> 8U) & 0xffU);
    const auto digest = sha256_digest(std::span<const std::byte>(bytes).subspan(
        kKernelCommandDigestInputOffset,
        bytes.size() - kKernelCommandDigestInputOffset - kDigestBytes));
    std::copy(digest.bytes.begin(), digest.bytes.end(),
              bytes.end() - static_cast<std::ptrdiff_t>(kDigestBytes));
    return bytes;
}

VerifiedExecutionCatalog alternate_catalog() {
    auto row = catalog_test::row(AccountId{5},
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc, 1});
    row.routing_key = catalog_test::text("alternate-route");
    const std::array rows{row};
    return catalog_test::catalog(rows);
}

void elect(ReplicatedKernel& node) {
    node.tick(1'001);
    require(node.is_leader(), "single voter did not become leader");
}

CommittedResult commit(ReplicatedKernel& node, const Command& value) {
    const auto outcome = node.propose(encoded(value));
    require(outcome.status == ProposeStatus::admitted, "fixture proposal was not admitted");
    node.flush_durability();
    node.tick(1);
    const auto result = node.take_committed_result();
    require(result.has_value(), "committed fixture produced no result");
    return *result;
}

void initialize_controls(ReplicatedKernel& node, std::uint64_t& sequence) {
    require(commit(node, command(++sequence, AdvanceKillEpoch{KillEpoch{1}})).result.code ==
                ApplyCode::applied,
            "kill epoch initialization failed");
    require(commit(node, command(++sequence,
                SetExecutorGeneration{ExecutorGeneration{1}})).result.code == ApplyCode::applied,
            "executor generation initialization failed");
}

template<class Operation>
void require_dependency_failure(Operation&& operation, const std::string& message) {
    bool failed = false;
    try { std::forward<Operation>(operation)(); }
    catch (const CatalogDependencyUnavailable&) { failed = true; }
    require(failed, message);
}

void verify_latched_surface(ReplicatedKernel& node) {
    require(!node.is_leader(), "dependency-failed node still advertised leadership");
    require_dependency_failure([&] { (void)node.propose({}); },
                               "latched node accepted propose");
    require_dependency_failure([&] { node.tick(1); },
                               "latched node accepted tick");
    require_dependency_failure([&] {
        (void)node.receive_peer(std::span<const std::byte>{});
    }, "latched node accepted peer input");
    require_dependency_failure([&] { (void)identity_test::complete_compaction(node); },
                               "latched node compacted");
    require_dependency_failure([&] { (void)node.take_committed_result(); },
                               "latched node published a result");
    require_dependency_failure([&] { (void)node.committed_outbox(); },
                               "latched node published an outbox view");
}

void test_missing_intended_catalog_refused_before_bootstrap() {
    TempDirectory dir;
    const auto target = dir.path() / "missing-catalog";
    const auto before = directory_image(dir.path());
    bool refused = false;
    try {
        (void)VerifiedIdentityStartup::bootstrap(
            target, identity_test::identity(1, {1}), VerifiedExecutionCatalog{});
    } catch (const IdentityStartupError& error) {
        refused = error.code() == IdentityStartupErrorCode::catalog_mismatch;
    }
    require(refused, "bootstrap did not type missing intended catalog as catalog mismatch");
    require(directory_image(dir.path()) == before,
            "missing-catalog bootstrap refusal changed persisted names or bytes");
}

void test_committed_install_dependency_failure(const VerifiedExecutionCatalog& local,
                                               const std::string& label) {
    TempDirectory dir;
    ReplicatedKernel node(identity_test::bootstrap(dir.path(), 1, {1}, local));
    elect(node);
    std::uint64_t sequence{};
    initialize_controls(node, sequence);
    const auto before = node.status();
    const auto receipt_count = node.state().receipt_count;
    require(node.propose(encoded(install(++sequence,
                catalog_test::standard_catalog().root()))).status == ProposeStatus::admitted,
            label + ": install was not admitted");
    node.flush_durability();
    bool stopped = false;
    try { node.tick(1); }
    catch (const std::runtime_error&) { stopped = true; }
    require(stopped, label + ": committed install did not stop node");
    const auto after = node.status();
    require(after.applied_index == before.applied_index &&
                after.state_version == before.state_version &&
                after.state_digest == before.state_digest &&
                node.state().execution_catalog_root.is_zero() &&
                node.state().receipt_count == receipt_count,
            label + ": dependency failure advanced applied or reducer state");
    verify_latched_surface(node);
}

void build_installed_history(const std::filesystem::path& path,
                             const bool take_snapshot) {
    ReplicatedKernel node(identity_test::bootstrap(path, 1, {1}));
    elect(node);
    std::uint64_t sequence{};
    initialize_controls(node, sequence);
    require(commit(node, install(++sequence,
                catalog_test::standard_catalog().root())).result.code == ApplyCode::applied,
            "catalog installation failed");
    if (take_snapshot)
        require(identity_test::complete_compaction(node) != 0, "installed history did not compact");
}

void test_recovery_dependency_is_read_only(const bool snapshot) {
    TempDirectory dir;
    build_installed_history(dir.path(), snapshot);
    const auto before = directory_image(dir.path());
    for (const auto& local : {VerifiedExecutionCatalog{}, alternate_catalog()}) {
        bool refused = false;
        try {
            ReplicatedKernel restarted(VerifiedIdentityStartup::recover(
                dir.path(), identity_test::identity(1, {1}), local));
        } catch (const IdentityStartupError& error) {
            refused = error.code() == IdentityStartupErrorCode::catalog_mismatch;
        }
        require(refused, "identity startup accepted missing or wrong intended catalog");
        require(directory_image(dir.path()) == before,
                "identity-refused restart changed persisted names or bytes");
    }
    ReplicatedKernel restarted(identity_test::recover(dir.path(), 1, {1}));
    require(restarted.state().execution_catalog_root ==
                catalog_test::standard_catalog().root(),
            "exact catalog did not restore installed state");
}

void test_uncommitted_install_does_not_gate_startup() {
    TempDirectory dir;
    identity_test::provision(dir.path(), 1, {1});
    identity_test::RawWalFixture wal(dir.path());
    const std::array commands{
        command(1, AdvanceKillEpoch{KillEpoch{1}}),
        command(2, SetExecutorGeneration{ExecutorGeneration{1}}),
        install(3, catalog_test::standard_catalog().root())};
    for (std::size_t i = 0; i < commands.size(); ++i)
        wal.append({i + 1, 1, static_cast<std::int32_t>(i + 1), RAFT_LOGTYPE_NORMAL,
                    encoded(commands[i])});
    auto metadata = identity_test::metadata_store(dir.path());
    metadata.save({1, -1, 2});

    ReplicatedKernel restarted(identity_test::recover(dir.path(), 1, {1}));
    require(restarted.status().applied_index == 2 &&
                restarted.state().execution_catalog_root.is_zero(),
            "uncommitted install was applied or artifact-gated startup");
}

void test_unsupported_schema_guard_precedes_catalog_recovery() {
    TempDirectory dir;
    identity_test::provision(dir.path(), 1, {1});
    identity_test::RawWalFixture wal(dir.path());
    wal.append({1, 1, 1, RAFT_LOGTYPE_NORMAL,
                unsupported_encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))});
    auto metadata = identity_test::metadata_store(dir.path());
    metadata.save({1, -1, 1});
    const auto before = directory_image(dir.path());
    bool refused = false;
    try {
        ReplicatedKernel restarted(identity_test::recover(dir.path(), 1, {1}));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    require(refused, "unsupported command schema bypassed compatibility guard");
    require(directory_image(dir.path()) == before,
            "unsupported-schema refusal changed persisted names or bytes");
}

void test_conflicting_replacement_does_not_require_candidate_artifact() {
    TempDirectory dir;
    ReplicatedKernel node(identity_test::bootstrap(dir.path(), 1, {1}));
    elect(node);
    std::uint64_t sequence{};
    initialize_controls(node, sequence);
    require(commit(node, install(++sequence,
                catalog_test::standard_catalog().root())).result.code == ApplyCode::applied,
            "initial install failed");
    const auto conflict = commit(node, install(++sequence, alternate_catalog().root()));
    require(conflict.result.code == ApplyCode::immutable_catalog_conflict &&
                node.state().execution_catalog_root == catalog_test::standard_catalog().root(),
            "conflicting replacement depended on candidate bytes or changed root");
}

void test_node_owns_temporary_catalog() {
    TempDirectory dir;
    ReplicatedKernel node(identity_test::bootstrap(
        dir.path(), 1, {1}, VerifiedExecutionCatalog{catalog_test::standard_catalog()}));
    elect(node);
    std::uint64_t sequence{};
    initialize_controls(node, sequence);
    require(commit(node, install(++sequence,
                catalog_test::standard_catalog().root())).result.code == ApplyCode::applied,
            "node borrowed temporary catalog storage");
}

void test_node_catalog_is_not_aliased_to_caller() {
    TempDirectory dir;
    auto caller_catalog = catalog_test::standard_catalog();
    ReplicatedKernel node(identity_test::bootstrap(dir.path(), 1, {1}, caller_catalog));
    caller_catalog = alternate_catalog();
    elect(node);
    std::uint64_t sequence{};
    initialize_controls(node, sequence);
    require(commit(node, install(++sequence, catalog_test::standard_catalog().root())).result.code ==
                ApplyCode::applied, "caller replacement changed the node's catalog");
}

void test_peer_commit_stops_before_apply_on_missing_artifact() {
    TempDirectory dir;
    const auto local = alternate_catalog();
    identity_test::provision(dir.path(), 2, {1, 2}, local);
    identity_test::RawWalFixture wal(dir.path());
    wal.append({1, 1, 1, RAFT_LOGTYPE_NORMAL,
        encoded(command(1, AdvanceKillEpoch{KillEpoch{1}}))});
    wal.append({2, 1, 2, RAFT_LOGTYPE_NORMAL,
        encoded(command(2, SetExecutorGeneration{ExecutorGeneration{1}}))});
    auto metadata = identity_test::metadata_store(dir.path());
    metadata.save({1, -1, 2});
    ReplicatedKernel follower(
        identity_test::recover(dir.path(), 2, {1, 2}, local));
    const auto before = follower.status();
    velocita::AppendEntries append;
    append.term = 1;
    append.previous_log_index = 2;
    append.previous_log_term = 1;
    append.leader_commit = 3;
    append.entries.push_back({1, 3, RAFT_LOGTYPE_NORMAL,
        encoded(install(3, catalog_test::standard_catalog().root()))});
    bool refused = false;
    try { identity_test::deliver_declared_peer(follower, velocita::PeerMessage{1, 2, append}); }
    catch (const std::runtime_error&) { refused = true; }
    const auto after = follower.status();
    require(refused && after.commit_index == 3 && after.applied_index == 2 &&
                after.state_digest == before.state_digest && after.state_version == before.state_version,
            "peer commit consumed an installation lacking its artifact");
    verify_latched_surface(follower);
}
}

void run_catalog_replication_tests() {
    test_missing_intended_catalog_refused_before_bootstrap();
    test_committed_install_dependency_failure(alternate_catalog(), "wrong artifact");
    test_recovery_dependency_is_read_only(false);
    test_recovery_dependency_is_read_only(true);
    test_uncommitted_install_does_not_gate_startup();
    test_unsupported_schema_guard_precedes_catalog_recovery();
    test_conflicting_replacement_does_not_require_candidate_artifact();
    test_node_owns_temporary_catalog();
    test_node_catalog_is_not_aliased_to_caller();
    test_peer_commit_stops_before_apply_on_missing_artifact();
}
