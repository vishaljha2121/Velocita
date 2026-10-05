#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/generation_base.hpp"
#include "velocita/v2/generation_base_files.hpp"
#include "velocita/v2/local_generation.hpp"
#include "velocita/v2/snapshot_store.hpp"
#include "v2_catalog_test_support.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        std::array<char, 48> pattern{};
        constexpr std::string_view prefix = "/tmp/v2-generation-startup.XXXXXX";
        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        const auto* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "mkdtemp failed");
        path = made;
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

StaticIdentity expected_identity() {
    StaticIdentity value;
    value.incarnation[0] = std::byte{0x6a};
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
        const auto relative = entry.path().lexically_relative(root).string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status)) {
            result.emplace(relative + "/", "");
        } else if (std::filesystem::is_regular_file(status)) {
            std::ifstream input(entry.path(), std::ios::binary);
            result.emplace(relative, std::string(std::istreambuf_iterator<char>(input), {}));
        } else {
            result.emplace(relative, "nonregular");
        }
    }
    return result;
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes;
    bytes.reserve(chars.size());
    for (char value : chars) bytes.push_back(std::byte(static_cast<unsigned char>(value)));
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "fixture write failed");
    output.close();
    require(::chmod(path.c_str(), 0600) == 0, "fixture chmod failed");
}

template<class Operation>
void refused(Operation&& operation) {
    try {
        operation();
    } catch (const IdentityStartupError&) {
        return;
    }
    throw std::runtime_error("generation startup unexpectedly succeeded");
}

bool other_process_can_lock(const std::filesystem::path& directory) {
    const auto child = ::fork();
    require(child >= 0, "lock probe fork failed");
    if (child == 0) {
        const int fd = ::open(directory.c_str(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) ::_exit(3);
        int result;
        do { result = ::flock(fd, LOCK_EX | LOCK_NB); }
        while (result < 0 && errno == EINTR);
        ::_exit(result == 0 ? 0 : 1);
    }
    int status{};
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status),
            "lock probe child failed");
    return WEXITSTATUS(status) == 0;
}

void test_fresh_bootstrap_routes_one_selected_generation() {
    TempDirectory temp;
    const auto root = temp.path / "voter";
    const auto runtime = root / "v2-generations/g-0000000000000001";
    const auto& catalog = catalog_test::standard_catalog();
    {
        auto owner = VerifiedIdentityStartup::bootstrap(root, expected_identity(), catalog);
        require(owner.root_directory() == root, "bootstrap lost deployment-root routing");
        require(owner.directory() == runtime,
                "RED: bootstrap still routes mutable stores to the deployment root");
        require(std::filesystem::is_regular_file(root / "v2-identity.bin"),
                "root identity was not published");
        require(std::filesystem::file_size(root / "v2-active-generation.bin") == 160,
                "exact selector was not published");
        require(!std::filesystem::exists(root / "v2-metadata.bin"),
                "bootstrap left a second root hard-state authority");
        require(std::filesystem::file_size(runtime / "v2-metadata.bin") == 112,
                "selected runtime lacks initialized hard state");
        require(!other_process_can_lock(root), "owning token did not retain root lock");
        require(!other_process_can_lock(runtime), "owning token did not retain runtime lock");
        refused([&] {
            (void)VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
        });
    }
    auto recovered = VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
    require(recovered.root_directory() == root, "recovery changed deployment root");
    require(recovered.directory() == runtime,
            "released-token recovery changed selected runtime routing");
    require(!other_process_can_lock(root) && !other_process_can_lock(runtime),
            "recovered token did not retain both directory locks");
}

void throw_at(void* context, IdentityStartupCheckpoint checkpoint) {
    if (checkpoint == *static_cast<IdentityStartupCheckpoint*>(context))
        throw std::runtime_error("injected generation startup cut");
}

void test_selector_publication_cuts_do_not_promote_partial_state() {
    const auto& catalog = catalog_test::standard_catalog();
    const std::array before_rename{
        IdentityStartupCheckpoint::directory_synced,
        IdentityStartupCheckpoint::generation_created,
        IdentityStartupCheckpoint::base_created,
        IdentityStartupCheckpoint::base_sealed,
        IdentityStartupCheckpoint::selector_created,
        IdentityStartupCheckpoint::selector_partial_write,
        IdentityStartupCheckpoint::selector_written,
        IdentityStartupCheckpoint::selector_synced,
    };
    for (auto cut : before_rename) {
        TempDirectory temp;
        const auto root = temp.path / "voter";
        bool interrupted{};
        try {
            (void)VerifiedIdentityStartup::bootstrap(
                root, expected_identity(), catalog, {&cut, throw_at});
        } catch (const std::runtime_error& error) {
            interrupted = std::string_view(error.what()) == "injected generation startup cut";
        }
        require(interrupted, "bootstrap did not reach pre-rename cut");
        const auto partial = image(root);
        refused([&] {
            (void)VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
        });
        require(image(root) == partial, "recovery changed partial generation evidence");
        refused([&] {
            (void)VerifiedIdentityStartup::bootstrap(root, expected_identity(), catalog);
        });
        require(image(root) == partial, "bootstrap promoted partial generation evidence");
    }

    const std::array after_rename{
        IdentityStartupCheckpoint::selector_renamed,
        IdentityStartupCheckpoint::selector_directory_synced,
        IdentityStartupCheckpoint::selection_verified,
    };
    for (auto cut : after_rename) {
        TempDirectory temp;
        const auto root = temp.path / "voter";
        bool interrupted{};
        try {
            (void)VerifiedIdentityStartup::bootstrap(
                root, expected_identity(), catalog, {&cut, throw_at});
        } catch (const std::runtime_error& error) {
            interrupted = std::string_view(error.what()) == "injected generation startup cut";
        }
        require(interrupted, "bootstrap did not reach post-rename cut");
        const auto published = image(root);
        auto recovered = VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
        require(recovered.directory() == root / "v2-generations/g-0000000000000001",
                "complete post-rename generation did not recover");
        require(image(root) == published, "recovery rewrote published generation bytes");
    }
}

void test_selected_artifact_refusals_and_stale_token() {
    const auto& catalog = catalog_test::standard_catalog();
    for (const std::string missing : {"v2-active-generation.bin", "v2-base-seal.bin"}) {
        TempDirectory temp;
        const auto root = temp.path / "voter";
        std::filesystem::path runtime;
        {
            auto owner = VerifiedIdentityStartup::bootstrap(root, expected_identity(), catalog);
            runtime = owner.directory();
        }
        const auto target = missing == "v2-active-generation.bin"
            ? root / missing : runtime / missing;
        std::filesystem::rename(target, target.string() + ".saved");
        refused([&] {
            (void)VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
        });
    }

    TempDirectory temp;
    const auto root = temp.path / "voter";
    auto owner = VerifiedIdentityStartup::bootstrap(root, expected_identity(), catalog);
    std::filesystem::rename(root / "v2-active-generation.bin",
                            root / "v2-active-generation.bin.saved");
    refused([&] { owner.validate_ownership(); });

    TempDirectory successor_temp;
    const auto successor_root = successor_temp.path / "voter";
    auto stale_owner = VerifiedIdentityStartup::bootstrap(
        successor_root, expected_identity(), catalog);
    const auto identity = stale_owner.identity();
    const auto successor = successor_root / "v2-generations/g-0000000000000002";
    std::filesystem::create_directories(successor / "base");
    std::array<std::byte, kBoundMetadataBytes> initial_metadata{};
    BoundFrameScratch metadata_scratch;
    require(encode_bound_metadata(velocita::PersistentMetadata{}, identity,
                                  initial_metadata, metadata_scratch).error ==
                BoundFrameError::none,
            "successor metadata fixture failed");
    write_bytes(successor / "v2-metadata.bin", initial_metadata);
    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    GenerationBaseScratch base_scratch;
    const auto encoded_base = encode_generation_base(
        GenerationBaseKind::genesis, 2, identity, catalog, {}, seal, base_scratch);
    require(encoded_base.error == GenerationBaseError::none,
            "successor base fixture failed");
    write_bytes(successor / "v2-base-seal.bin", seal);
    std::array<std::byte, kLocalGenerationSelectorBytes> selector{};
    require(encode_local_generation_selector(
                {2, 2, 1, encoded_base.base.digest}, identity, selector).error ==
                LocalGenerationError::none,
            "successor selector fixture failed");
    write_bytes(successor_root / "v2-active-generation.bin", selector);
    std::array<std::byte, kMaxBoundSnapshotBytes> snapshot{};
    SelectedGenerationBase selected;
    GenerationBaseScratch selected_scratch;
    const int root_fd = ::open(successor_root.c_str(),
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    require(root_fd >= 0, "successor root verification open failed");
    const auto verified = read_selected_generation_base_at(
        root_fd, identity, catalog, snapshot, selected_scratch, selected);
    (void)::close(root_fd);
    require(verified == GenerationBaseFileError::none &&
                selected.selector.generation == 2,
            "successor fixture was not independently readable");
    require(std::filesystem::exists(stale_owner.directory() / "v2-metadata.bin"),
            "stale-token fixture lost generation-1 metadata");
    try {
        stale_owner.validate_ownership();
        throw std::runtime_error("valid successor did not stale the generation-1 token");
    } catch (const IdentityStartupError&) {
    }
}

void test_selector_temporary_is_diagnostic_only() {
    const auto& catalog = catalog_test::standard_catalog();
    auto exercise = [&](auto write_temporary, SelectorTemporaryStatus expected_status) {
        TempDirectory temp;
        const auto root = temp.path / "voter";
        std::filesystem::path selected;
        VerifiedStaticIdentity identity;
        {
            auto owner = VerifiedIdentityStartup::bootstrap(
                root, expected_identity(), catalog);
            selected = owner.directory();
            identity = owner.identity();
        }
        write_temporary(root, identity);
        const auto before = image(root);
        auto recovered = VerifiedIdentityStartup::recover(
            root, expected_identity(), catalog);
        require(recovered.directory() == selected,
                "selector temporary changed the authoritative final selection");
        require(recovered.selector_temporary_status() == expected_status,
                "RED: selector temporary diagnostic status was not retained");
        require(image(root) == before,
                "selector temporary was promoted, deleted, or rewritten");
    };

    exercise([](const std::filesystem::path& root,
                const VerifiedStaticIdentity& identity) {
        Digest256 base{};
        base.bytes[0] = std::byte{0x71};
        std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
        require(encode_local_generation_selector(
                    {2, 2, 1, base}, identity, bytes).error ==
                    LocalGenerationError::none,
                "valid selector temporary fixture failed");
        write_bytes(root / "v2-active-generation.bin.tmp", bytes);
    }, SelectorTemporaryStatus::valid);

    exercise([](const std::filesystem::path& root,
                const VerifiedStaticIdentity&) {
        const std::array malformed{std::byte{'V'}, std::byte{'2'}, std::byte{'L'}};
        write_bytes(root / "v2-active-generation.bin.tmp", malformed);
    }, SelectorTemporaryStatus::invalid);

    exercise([](const std::filesystem::path& root,
                const VerifiedStaticIdentity&) {
        const auto foreign = [&] {
            auto value = expected_identity();
            value.incarnation[0] ^= std::byte{1};
            std::array<std::byte, kMaxStaticIdentityBytes> storage{};
            const auto encoded = encode_static_identity(value, storage);
            require(encoded.error == StaticIdentityCodecError::none,
                    "foreign identity fixture encode failed");
            VerifiedStaticIdentity result;
            require(VerifiedStaticIdentity::decode(
                        std::span(storage).first(encoded.bytes_written), result) ==
                        StaticIdentityCodecError::none,
                    "foreign identity fixture decode failed");
            return result;
        }();
        Digest256 base{};
        base.bytes[0] = std::byte{0x72};
        std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
        require(encode_local_generation_selector(
                    {2, 2, 1, base}, foreign, bytes).error ==
                    LocalGenerationError::none,
                "foreign selector temporary fixture failed");
        write_bytes(root / "v2-active-generation.bin.tmp", bytes);
    }, SelectorTemporaryStatus::invalid);

    TempDirectory temp;
    const auto root = temp.path / "voter";
    {
        auto owner = VerifiedIdentityStartup::bootstrap(
            root, expected_identity(), catalog);
    }
    std::filesystem::rename(root / "v2-active-generation.bin",
                            root / "v2-active-generation.bin.tmp");
    const auto before = image(root);
    refused([&] {
        (void)VerifiedIdentityStartup::recover(root, expected_identity(), catalog);
    });
    require(image(root) == before, "temp-only refusal changed selector evidence");
}

void test_legacy_root_is_not_adopted() {
    TempDirectory temp;
    const auto root = temp.path / "legacy";
    std::filesystem::create_directory(root);
    std::ofstream(root / "opaque-history", std::ios::binary) << "preserve me";
    refused([&] {
        (void)VerifiedIdentityStartup::bootstrap(
            root, expected_identity(), catalog_test::standard_catalog());
    });
    require(std::filesystem::exists(root / "opaque-history"),
            "legacy refusal erased opaque history");
}

struct SnapshotBaseFixture {
    TempDirectory temp;
    std::filesystem::path root = temp.path / "voter";
    std::filesystem::path runtime;
    VerifiedStaticIdentity identity;
    TradingState base_state = catalog_test::state();

    SnapshotBaseFixture() {
        {
            auto owner = VerifiedIdentityStartup::bootstrap(
                root, expected_identity(), catalog_test::standard_catalog());
            runtime = owner.directory();
            identity = owner.identity();
        }
        const auto base_dir = runtime / "base";
        SnapshotStore(base_dir, identity).save(base_state, 5, 3);
        require(::chmod((base_dir / "v2-snapshot.bin").c_str(), 0600) == 0,
                "immutable snapshot chmod failed");
        SnapshotStore(runtime, identity).save(base_state, 5, 3);
        BoundMetadataStore(runtime, identity).save({3, -1, 0});
        const auto snapshot = read_bytes(base_dir / "v2-snapshot.bin");
        std::array<std::byte, kGenerationBaseSealBytes> seal{};
        GenerationBaseScratch scratch;
        const auto encoded = encode_generation_base(
            GenerationBaseKind::snapshot, 1, identity,
            catalog_test::standard_catalog(), snapshot, seal, scratch);
        require(encoded.error == GenerationBaseError::none,
                "snapshot base seal fixture failed");
        write_bytes(runtime / "v2-base-seal.bin", seal);
        std::array<std::byte, kLocalGenerationSelectorBytes> selector{};
        const auto selected = encode_local_generation_selector(
            {1, 1, 0, encoded.base.digest}, identity, selector);
        require(selected.error == LocalGenerationError::none,
                "snapshot selector fixture failed");
        write_bytes(root / "v2-active-generation.bin", selector);
    }
};

void test_snapshot_base_runtime_floor() {
    const auto& catalog = catalog_test::standard_catalog();
    {
        SnapshotBaseFixture fixture;
        auto recovered = VerifiedIdentityStartup::recover(
            fixture.root, expected_identity(), catalog);
        require(recovered.directory() == fixture.runtime,
                "matching base/runtime snapshot refused");
    }
    {
        SnapshotBaseFixture fixture;
        std::filesystem::remove(fixture.runtime / "v2-snapshot.bin");
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
    {
        SnapshotBaseFixture fixture;
        SnapshotStore(fixture.runtime, fixture.identity).save(fixture.base_state, 4, 3);
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
    {
        SnapshotBaseFixture fixture;
        SnapshotStore(fixture.runtime, fixture.identity).save(fixture.base_state, 5, 4);
        BoundMetadataStore(fixture.runtime, fixture.identity).save({4, -1, 0});
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
    {
        SnapshotBaseFixture fixture;
        auto different_state = fixture.base_state;
        different_state.kill_epoch = KillEpoch{different_state.kill_epoch.value() + 1};
        SnapshotStore(fixture.runtime, fixture.identity).save(different_state, 5, 3);
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
    {
        SnapshotBaseFixture fixture;
        auto bytes = read_bytes(fixture.runtime / "v2-snapshot.bin");
        bytes.back() ^= std::byte{1};
        write_bytes(fixture.runtime / "v2-snapshot.bin", bytes);
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
    {
        SnapshotBaseFixture fixture;
        SnapshotStore(fixture.runtime, fixture.identity).save(fixture.base_state, 6, 4);
        BoundMetadataStore(fixture.runtime, fixture.identity).save({4, -1, 0});
        auto recovered = VerifiedIdentityStartup::recover(
            fixture.root, expected_identity(), catalog);
        require(recovered.directory() == fixture.runtime,
                "valid runtime snapshot above immutable floor refused");
    }
    {
        SnapshotBaseFixture fixture;
        SnapshotStore(fixture.runtime, fixture.identity).save(fixture.base_state, 6, 2);
        BoundMetadataStore(fixture.runtime, fixture.identity).save({3, -1, 0});
        refused([&] {
            (void)VerifiedIdentityStartup::recover(
                fixture.root, expected_identity(), catalog);
        });
    }
}

}

int main() {
    try {
        const auto run = [](const char* name, auto operation) {
            try { operation(); }
            catch (const std::exception& error) {
                throw std::runtime_error(std::string{name} + ": " + error.what());
            }
        };
        run("fresh bootstrap", test_fresh_bootstrap_routes_one_selected_generation);
        run("artifact/stale token", test_selected_artifact_refusals_and_stale_token);
        run("selector temporary", test_selector_temporary_is_diagnostic_only);
        run("legacy root", test_legacy_root_is_not_adopted);
        run("publication cuts", test_selector_publication_cuts_do_not_promote_partial_state);
        run("snapshot floor", test_snapshot_base_runtime_floor);
        std::cout << "v2 generation startup tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
