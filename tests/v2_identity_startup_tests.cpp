#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "v2_catalog_test_support.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace velocita::v2;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class TempDirectory {
public:
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "v2-identity-startup.XXXXXX").string();
        pattern.push_back('\0');
        const char* value = ::mkdtemp(pattern.data());
        if (!value) throw std::runtime_error("mkdtemp failed");
        path = value;
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};

StaticIdentity expected_identity() {
    StaticIdentity value;
    value.incarnation[0] = std::byte{0x35};
    value.catalog_root = catalog_test::standard_catalog().root();
    value.voter_count = 2;
    value.voters = {1, 2, 0};
    value.local_node_id = 1;
    return value;
}

using Image = std::map<std::string, std::string>;
Image image(const std::filesystem::path& root) {
    Image result;
    if (!std::filesystem::exists(root)) return result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status)) {
            result.emplace(name, "symlink:" + std::filesystem::read_symlink(entry.path()).string());
            continue;
        }
        if (std::filesystem::is_directory(status)) { result.emplace(name + "/", ""); continue; }
        if (!std::filesystem::is_regular_file(status)) { result.emplace(name, "nonregular"); continue; }
        std::ifstream input(entry.path(), std::ios::binary);
        result.emplace(name, std::string(std::istreambuf_iterator<char>(input), {}));
    }
    return result;
}

template<class F> void refused(F&& operation, IdentityStartupErrorCode code) {
    try { operation(); }
    catch (const IdentityStartupError& error) {
        require(error.code() == code, "startup refused for the wrong reason");
        return;
    }
    throw std::runtime_error("unsafe startup was accepted; expected refusal code " +
                             std::to_string(static_cast<int>(code)));
}

void test_owned_roundtrip_and_mismatch() {
    TempDirectory temp;
    const auto dir = temp.path / "voter";
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    {
        auto owner = VerifiedIdentityStartup::bootstrap(dir, value, catalog);
        require(owner.owns_directory(), "bootstrap failed to produce a verified directory owner");
        require(owner.identity().valid() && owner.identity().value() == value,
                "bootstrap changed the identity");
        require(owner.catalog().root() == value.catalog_root, "bootstrap lost catalog binding");
        auto moved = std::move(owner);
        require(!owner.owns_directory() && moved.owns_directory(), "startup move duplicated lock ownership");
        const auto before = image(dir);
        refused([&] { (void)VerifiedIdentityStartup::recover(dir, value, catalog); },
                IdentityStartupErrorCode::directory_owned);
        const auto child = ::fork();
        require(child >= 0, "lock-contender fork failed");
        if (child == 0) {
            try {
                (void)VerifiedIdentityStartup::recover(dir, value, catalog);
                ::_exit(94);
            } catch (const IdentityStartupError& error) {
                ::_exit(error.code() == IdentityStartupErrorCode::directory_owned ? 93 : 95);
            } catch (...) { ::_exit(96); }
        }
        int status{};
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 93,
                "separate process acquired an already owned directory");
        require(image(dir) == before, "second owner changed persisted bytes or names");
    }
    const auto before = image(dir);
    const auto runtime_name = "v2-generations/g-0000000000000001/v2-metadata.bin";
    require(before.contains(runtime_name) && before.at(runtime_name).size() == 112,
            "bootstrap published identity without bound zero hard state");
    require(before.at(kStaticIdentityFileName).size() == 120 &&
                before.contains("v2-active-generation.bin"),
            "bootstrap published wrong identity or omitted selector");
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    {
        auto restarted = VerifiedIdentityStartup::recover(dir, value, catalog);
        require(restarted.identity().value() == value, "healthy recovery changed identity");
    }
    auto wrong_local = value; wrong_local.local_node_id = 2;
    refused([&] { (void)VerifiedIdentityStartup::recover(dir, wrong_local, catalog); },
            IdentityStartupErrorCode::identity_mismatch);
    auto smaller = value; smaller.voter_count = 1; smaller.voters = {1, 0, 0};
    refused([&] { (void)VerifiedIdentityStartup::recover(dir, smaller, catalog); },
            IdentityStartupErrorCode::identity_mismatch);
    require(image(dir) == before, "mismatched recovery rewrote identity or created a lockfile");
    std::ofstream(VerifiedIdentityStartup::recover(dir, value, catalog).directory() /
                  "v2-wal-blocks.bin") << "history begins";
    const auto history = image(dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(dir) == history, "bootstrap retry adopted a running-history lineage");
    auto recovered_history = VerifiedIdentityStartup::recover(dir, value, catalog);
    require(recovered_history.owns_directory(), "recover refused selected directory with history");
}

void test_refusal_before_creation_or_adoption() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto missing = temp.path / "missing";
    refused([&] { (void)VerifiedIdentityStartup::recover(missing, value, catalog); },
            IdentityStartupErrorCode::directory_missing);
    require(!std::filesystem::exists(missing), "recover implicitly created a directory");
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(missing, value, {}); },
            IdentityStartupErrorCode::catalog_mismatch);
    require(!std::filesystem::exists(missing), "missing catalog caused bootstrap side effects");
    auto wrong_root = value; wrong_root.catalog_root.bytes[0] ^= std::byte{1};
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(missing, wrong_root, catalog); },
            IdentityStartupErrorCode::catalog_mismatch);
    require(!std::filesystem::exists(missing), "wrong catalog caused bootstrap side effects");
    auto invalid = value; invalid.local_node_id = 0;
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(missing, invalid, catalog); },
            IdentityStartupErrorCode::invalid_identity);
    require(!std::filesystem::exists(missing), "invalid identity caused bootstrap side effects");

    std::ofstream(temp.path / "v2-raft.wal") << "retained diagnostic history";
    const auto before = image(temp.path);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(temp.path, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    refused([&] { (void)VerifiedIdentityStartup::recover(temp.path, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(temp.path) == before, "legacy history was adopted or altered");
}

void test_symlink_and_oversize_refusal() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto target = temp.path / "target";
    { auto owner = VerifiedIdentityStartup::bootstrap(target, value, catalog); }
    const auto before = image(target);
    std::filesystem::create_directory_symlink(target, temp.path / "alias");
    refused([&] { (void)VerifiedIdentityStartup::recover(temp.path / "alias", value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    require(image(target) == before, "symlink refusal changed target");
    std::filesystem::resize_file(target / kStaticIdentityFileName, 2 * 1024 * 1024);
    const auto large = image(target);
    refused([&] { (void)VerifiedIdentityStartup::recover(target, value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    require(image(target) == large, "oversized identity refusal changed file");
}

void throw_after_sync(void*, IdentityStartupCheckpoint checkpoint) {
    if (checkpoint == IdentityStartupCheckpoint::selection_verified)
        throw std::runtime_error("injected startup interruption");
}

void throw_after_rename(void*, IdentityStartupCheckpoint checkpoint) {
    if (checkpoint == IdentityStartupCheckpoint::selector_renamed)
        throw std::runtime_error("injected post-rename interruption");
}

void test_recovery_requires_confirmed_publication() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto dir = temp.path / "voter";
    bool interrupted{};
    try { auto owner = VerifiedIdentityStartup::bootstrap(dir, value, catalog, {nullptr, throw_after_rename}); }
    catch (const std::runtime_error& error) { interrupted = std::string_view(error.what()) == "injected post-rename interruption"; }
    require(interrupted, "did not reach selector post-rename ambiguity");
    const auto before = image(dir);
    interrupted = false;
    try { auto owner = VerifiedIdentityStartup::recover(dir, value, catalog, {nullptr, throw_after_sync}); }
    catch (const std::runtime_error& error) { interrupted = std::string_view(error.what()) == "injected startup interruption"; }
    require(interrupted, "recovery returned ownership before final selection verification");
    require(image(dir) == before, "recovery verification rewrote selected bytes");
    auto recovered = VerifiedIdentityStartup::recover(dir, value, catalog);
    require(recovered.owns_directory(), "recovery fence failure leaked the directory lock");
}

void test_owned_temporary_retry_and_file_types() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto dir = temp.path / "voter";
    bool interrupted{};
    const auto stop_at_identity = [](void*, IdentityStartupCheckpoint checkpoint) {
        if (checkpoint == IdentityStartupCheckpoint::identity_created)
            throw std::runtime_error("injected startup interruption");
    };
    try { auto owner = VerifiedIdentityStartup::bootstrap(
        dir, value, catalog, {nullptr, stop_at_identity}); }
    catch (const std::runtime_error& error) { interrupted = std::string_view(error.what()) == "injected startup interruption"; }
    require(interrupted, "did not reach startup exception checkpoint");
    require(image(dir).contains(kStaticIdentityTemporaryName) && !image(dir).contains(kStaticIdentityFileName),
            "interrupted bootstrap did not retain the unpublished identity");
    const auto partial = image(dir);
    bool partial_refused{};
    try { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); }
    catch (const IdentityStartupError&) { partial_refused = true; }
    require(partial_refused && image(dir) == partial,
            "nonempty partial initialization was retried or changed");

    const auto healthy_dir = temp.path / "healthy";
    { auto owner = VerifiedIdentityStartup::bootstrap(healthy_dir, value, catalog); }
    const auto final = healthy_dir / kStaticIdentityFileName;
    const auto temporary = healthy_dir / kStaticIdentityTemporaryName;
    std::filesystem::copy_file(final, temporary);
    const auto ambiguous = image(healthy_dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(healthy_dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(healthy_dir) == ambiguous, "ambiguous identity generations were erased");
    std::filesystem::remove(temporary);
    std::filesystem::rename(final, temp.path / "saved-identity");
    std::filesystem::create_symlink(temp.path / "saved-identity", final);
    const auto symlinked = image(healthy_dir);
    refused([&] { (void)VerifiedIdentityStartup::recover(healthy_dir, value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    require(image(healthy_dir) == symlinked, "identity symlink refusal changed directory");
    std::filesystem::remove(final);
    require(::mkfifo(final.c_str(), 0600) == 0, "mkfifo fixture failed");
    refused([&] { (void)VerifiedIdentityStartup::recover(healthy_dir, value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    require(image(healthy_dir).at(kStaticIdentityFileName) == "nonregular", "FIFO refusal changed its entry");
    std::filesystem::remove(final);
    std::filesystem::create_hard_link(temp.path / "saved-identity", final);
    refused([&] { (void)VerifiedIdentityStartup::recover(healthy_dir, value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    std::filesystem::remove(final);
    std::filesystem::rename(temp.path / "saved-identity", final);
    { auto recovered = VerifiedIdentityStartup::recover(healthy_dir, value, catalog); }
    require(::chmod(healthy_dir.c_str(), 0777) == 0, "chmod fixture failed");
    refused([&] { (void)VerifiedIdentityStartup::recover(healthy_dir, value, catalog); },
            IdentityStartupErrorCode::invalid_file);
    require(::chmod(healthy_dir.c_str(), 0700) == 0, "chmod restore failed");
}

void exit_at(void* context, IdentityStartupCheckpoint actual) {
    if (actual == *static_cast<IdentityStartupCheckpoint*>(context)) ::_exit(90);
}

void test_process_cut_retry_matrix() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const std::array cuts{IdentityStartupCheckpoint::parent_synced,
        IdentityStartupCheckpoint::identity_created, IdentityStartupCheckpoint::partial_write,
        IdentityStartupCheckpoint::identity_written, IdentityStartupCheckpoint::file_synced,
        IdentityStartupCheckpoint::identity_renamed, IdentityStartupCheckpoint::directory_synced,
        IdentityStartupCheckpoint::generation_created,
        IdentityStartupCheckpoint::metadata_created, IdentityStartupCheckpoint::metadata_partial_write,
        IdentityStartupCheckpoint::metadata_written, IdentityStartupCheckpoint::metadata_synced,
        IdentityStartupCheckpoint::metadata_renamed, IdentityStartupCheckpoint::metadata_directory_synced,
        IdentityStartupCheckpoint::base_created, IdentityStartupCheckpoint::base_sealed,
        IdentityStartupCheckpoint::selector_created, IdentityStartupCheckpoint::selector_partial_write,
        IdentityStartupCheckpoint::selector_written, IdentityStartupCheckpoint::selector_synced,
        IdentityStartupCheckpoint::selector_renamed,
        IdentityStartupCheckpoint::selector_directory_synced,
        IdentityStartupCheckpoint::selection_verified};
    for (auto cut : cuts) {
        const auto dir = temp.path / std::to_string(static_cast<int>(cut));
        const auto child = ::fork();
        require(child >= 0, "fork failed");
        if (child == 0) {
            try { auto owner = VerifiedIdentityStartup::bootstrap(dir, value, catalog, {&cut, exit_at}); }
            catch (...) { ::_exit(91); }
            ::_exit(92);
        }
        int status{};
        require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 90,
                "process-cut fixture did not reach intended checkpoint");
        if (cut == IdentityStartupCheckpoint::parent_synced) {
            { auto retry = VerifiedIdentityStartup::bootstrap(dir, value, catalog); }
            auto recovered = VerifiedIdentityStartup::recover(dir, value, catalog);
            require(recovered.identity().value() == value, "cut retry changed complete identity");
            const auto metadata = BoundMetadataStore(recovered.directory(), recovered.identity()).load();
            require(metadata.term == 0 && metadata.voted_for == -1 && metadata.commit_index == 0,
                    "genesis retry invented nonzero voting memory");
            continue;
        }

        const auto before = image(dir);
        bool bootstrap_refused{};
        try { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); }
        catch (...) { bootstrap_refused = true; }
        require(bootstrap_refused && image(dir) == before,
                "nonempty partial initialization was retried or changed");

        const bool selector_is_final =
            cut == IdentityStartupCheckpoint::selector_renamed ||
            cut == IdentityStartupCheckpoint::selector_directory_synced ||
            cut == IdentityStartupCheckpoint::selection_verified;
        if (selector_is_final) {
            auto recovered = VerifiedIdentityStartup::recover(dir, value, catalog);
            require(recovered.identity().value() == value,
                    "complete selected generation was not recoverable after a process cut");
            require(image(dir) == before, "recovery changed a complete selected generation");
        } else {
            bool recover_refused{};
            try { (void)VerifiedIdentityStartup::recover(dir, value, catalog); }
            catch (...) { recover_refused = true; }
            require(recover_refused && image(dir) == before,
                    "recovery adopted or changed an unselected partial generation");
        }
    }
}

void test_missing_or_nonzero_metadata_never_bootstraps() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto dir = temp.path / "voter";
    {
        auto owner = VerifiedIdentityStartup::bootstrap(dir, value, catalog);
        BoundMetadataStore(owner.directory(), owner.identity()).save({7, 2, 3});
    }
    const auto participating = image(dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(dir) == participating, "bootstrap reset participating metadata");
    {
        auto owner = VerifiedIdentityStartup::recover(dir, value, catalog);
        const auto restored = BoundMetadataStore(owner.directory(), owner.identity()).load();
        require(restored.term == 7 && restored.voted_for == 2 && restored.commit_index == 3,
                "recovery changed voting memory");
    }
    const auto runtime = dir / "v2-generations/g-0000000000000001";
    std::filesystem::rename(runtime / "v2-metadata.bin", temp.path / "retained-metadata");
    const auto missing = image(dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    bool rejected{};
    try { (void)VerifiedIdentityStartup::recover(dir, value, catalog); }
    catch (const BoundMetadataError& error) {
        rejected = error.kind() == BoundMetadataErrorKind::absent;
    }
    require(rejected && image(dir) == missing, "missing hard state was reset or mutated on recovery");
}

void test_bootstrap_ambiguous_metadata_and_foreign_identity_temp() {
    TempDirectory temp;
    const auto value = expected_identity();
    const auto& catalog = catalog_test::standard_catalog();
    const auto dir = temp.path / "voter";
    { auto owner = VerifiedIdentityStartup::bootstrap(dir, value, catalog); }
    const auto runtime = dir / "v2-generations/g-0000000000000001";
    std::filesystem::copy_file(runtime / "v2-metadata.bin", runtime / "v2-metadata.bin.tmp");
    auto before = image(dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(dir) == before, "ambiguous metadata was overwritten");
    { auto owner = VerifiedIdentityStartup::recover(dir, value, catalog); }
    require(image(dir) == before, "recovery consumed the metadata temp");
    std::filesystem::remove(runtime / "v2-metadata.bin.tmp");
    std::filesystem::rename(dir / kStaticIdentityFileName, temp.path / "saved-identity");
    auto foreign = value; foreign.incarnation[0] ^= std::byte{1};
    { auto owner = VerifiedIdentityStartup::bootstrap(temp.path / "foreign", foreign, catalog); }
    std::filesystem::copy_file(temp.path / "foreign" / kStaticIdentityFileName,
                               dir / kStaticIdentityTemporaryName);
    before = image(dir);
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(dir, value, catalog); },
            IdentityStartupErrorCode::unexpected_contents);
    require(image(dir) == before, "bootstrap adopted foreign identity temp over healthy metadata");
}
}

int main() {
    try {
        test_owned_roundtrip_and_mismatch();
        test_refusal_before_creation_or_adoption();
        test_symlink_and_oversize_refusal();
        test_owned_temporary_retry_and_file_types();
        test_recovery_requires_confirmed_publication();
        test_process_cut_retry_matrix();
        test_missing_or_nonzero_metadata_never_bootstraps();
        test_bootstrap_ambiguous_metadata_and_foreign_identity_temp();
        std::cout << "identity startup tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "identity startup tests failed: " << error.what() << '\n';
        return 1;
    }
}
