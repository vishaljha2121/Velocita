#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/identity_startup.hpp"
#include "v2_catalog_test_support.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace velocita::v2;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-generation-guard.XXXXXX").string();
        pattern.push_back('\0');
        const auto* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "mkdtemp failed");
        path = made;
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

StaticIdentity expected_identity() {
    StaticIdentity value;
    value.incarnation[0] = std::byte{0x71};
    value.catalog_root = catalog_test::standard_catalog().root();
    value.voter_count = 2;
    value.voters = {1, 2, 0};
    value.local_node_id = 1;
    return value;
}

using Image = std::map<std::string, std::string>;
Image image(const std::filesystem::path& root) {
    Image result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status))
            result.emplace(name, "symlink:" + std::filesystem::read_symlink(entry.path()).string());
        else if (std::filesystem::is_directory(status)) result.emplace(name + "/", "");
        else if (std::filesystem::is_regular_file(status)) {
            std::ifstream input(entry.path(), std::ios::binary);
            result.emplace(name, std::string(std::istreambuf_iterator<char>(input), {}));
        } else result.emplace(name, "nonregular");
    }
    return result;
}

template <class F>
void refused(F&& operation, IdentityStartupErrorCode expected) {
    try { operation(); }
    catch (const IdentityStartupError& error) {
        require(error.code() == expected, "strict startup returned wrong refusal");
        return;
    }
    throw std::runtime_error("strict startup accepted an unsafe root");
}

void write_legacy_root(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto value = expected_identity();
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    require(encoded.error == StaticIdentityCodecError::none, "legacy identity encode failed");
    std::ofstream output(root / kStaticIdentityFileName, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), encoded.bytes_written);
    output.close();
    VerifiedStaticIdentity identity;
    require(VerifiedStaticIdentity::decode(
                std::span{bytes}.first(encoded.bytes_written), identity) ==
                StaticIdentityCodecError::none, "legacy identity decode failed");
    std::array<std::byte, kBoundMetadataBytes> metadata{};
    BoundFrameScratch scratch;
    const auto metadata_encoded = encode_bound_metadata({7, 2, 3}, identity, metadata, scratch);
    require(metadata_encoded.error == BoundFrameError::none,
            "legacy metadata encode failed");
    std::ofstream metadata_output(root / "v2-metadata.bin", std::ios::binary);
    metadata_output.write(reinterpret_cast<const char*>(metadata.data()),
                          metadata_encoded.bytes_written);
    require(metadata_output.good(), "legacy metadata write failed");
    metadata_output.close();
    std::ofstream(root / "v2-wal-blocks.bin", std::ios::binary)
        << "preserved opaque legacy history";
}

void legacy_unselected_refuses_unchanged() {
    TempDirectory temp;
    const auto root = temp.path / "legacy";
    write_legacy_root(root);
    const auto before = image(root);
    refused([&] { (void)VerifiedIdentityStartup::recover(
        root, expected_identity(), catalog_test::standard_catalog()); },
        IdentityStartupErrorCode::unexpected_contents);
    require(image(root) == before, "legacy recovery refusal changed evidence");
    refused([&] { (void)VerifiedIdentityStartup::bootstrap(
        root, expected_identity(), catalog_test::standard_catalog()); },
        IdentityStartupErrorCode::unexpected_contents);
    require(image(root) == before, "bootstrap adopted legacy evidence");
}

void selected_root_recovers_runtime() {
    TempDirectory temp;
    const auto root = temp.path / "selected";
    std::filesystem::path runtime;
    {
        auto owner = VerifiedIdentityStartup::bootstrap(
            root, expected_identity(), catalog_test::standard_catalog());
        runtime = owner.directory();
        require(owner.root_directory() == root && runtime != root,
                "bootstrap did not separate root and runtime");
        BoundMetadataStore(runtime, owner.identity()).save({7, 2, 3});
    }
    const auto before = image(root);
    auto recovered = VerifiedIdentityStartup::recover(
        root, expected_identity(), catalog_test::standard_catalog());
    require(recovered.root_directory() == root && recovered.directory() == runtime,
            "recovery selected a different runtime");
    const auto metadata = BoundMetadataStore(runtime, recovered.identity()).load();
    require(metadata.term == 7 && metadata.voted_for == 2 && metadata.commit_index == 3,
            "cold-start ownership recovery changed hard state");
    require(image(root) == before, "selected recovery changed bytes or names");
}

void existing_token_revalidates_selector() {
    TempDirectory temp;
    const auto root = temp.path / "selected";
    auto owner = VerifiedIdentityStartup::bootstrap(
        root, expected_identity(), catalog_test::standard_catalog());
    std::fstream file(root / "v2-active-generation.bin",
                      std::ios::binary | std::ios::in | std::ios::out);
    require(file.good(), "selector fixture open failed");
    file.seekp(159); file.put('\x01'); file.close();
    const auto before = image(root);
    refused([&] { owner.validate_ownership(); }, IdentityStartupErrorCode::invalid_file);
    require(image(root) == before, "ownership refusal changed selector evidence");
}
}

int main() {
    try {
        legacy_unselected_refuses_unchanged();
        selected_root_recovers_runtime();
        existing_token_revalidates_selector();
        std::cout << "v2 generation startup guard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
