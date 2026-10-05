#include "velocita/v2/node_files.hpp"
#include "v2_catalog_test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;
namespace fs = std::filesystem;
using Bytes = std::vector<std::byte>;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDir {
    fs::path path;
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "v2-node-files-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
        path = fs::canonical(bytes.data());
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path, ignored); }
};

void write(const fs::path& path, const Bytes& bytes, mode_t mode) {
    { std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    require(::chmod(path.c_str(), mode) == 0, "fixture chmod failed");
}

Bytes secret_bytes() {
    Bytes out(kAuthSecretBytes);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(0xA0 + i);
    return out;
}

Bytes envelope_with(const AuthSecret& secret) {
    const std::array<std::byte, 16> incarnation{std::byte{1}};
    const auto key = AuthKey::derive(secret, {AuthRole::peer, 1, 2}, incarnation);
    Bytes out(kAuthOverheadBytes + 1);
    const std::array<std::byte, 1> body{std::byte{7}};
    require(key && encode_auth_envelope(*key, 1, 1, body, out).error == AuthEnvelopeError::none, "probe failed");
    return out;
}

void secrets_load_only_when_private_and_exact() {
    TempDir dir;
    const auto file = dir.path / "link.key";
    write(file, secret_bytes(), 0600);
    auto loaded = load_secret_file(file);
    require(loaded.error == NodeFileError::none && loaded.secret, "private 32-byte secret refused");
    require(envelope_with(*loaded.secret) == envelope_with(*AuthSecret::from_bytes(secret_bytes())),
            "loaded secret differs from the file bytes");
    for (const mode_t mode : {0640, 0604, 0660, 0644}) {
        write(file, secret_bytes(), mode);
        require(load_secret_file(file).error == NodeFileError::insecure_permissions,
                "secret readable by others accepted, mode " + std::to_string(mode));
    }
    write(file, Bytes(31), 0600);
    require(load_secret_file(file).error == NodeFileError::wrong_size, "short secret accepted");
    write(file, Bytes(33), 0600);
    require(load_secret_file(file).error == NodeFileError::wrong_size, "long secret accepted");
    write(file, secret_bytes(), 0600);
    fs::create_symlink(file, dir.path / "linked.key");
    require(load_secret_file(dir.path / "linked.key").error != NodeFileError::none, "symlinked secret followed");
    fs::create_directory(dir.path / "dir.key");
    require(::chmod((dir.path / "dir.key").c_str(), 0700) == 0, "chmod failed");
    require(load_secret_file(dir.path / "dir.key").error == NodeFileError::not_regular, "directory accepted");
    require(load_secret_file(dir.path / "absent.key").error == NodeFileError::missing, "missing file not reported");
}

Bytes secret_bytes(std::uint8_t seed) {
    Bytes out(kAuthSecretBytes);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::byte>(seed + i);
    return out;
}
bool same_secret(const AuthSecret& secret, std::uint8_t seed) {
    return envelope_with(secret) == envelope_with(*AuthSecret::from_bytes(secret_bytes(seed)));
}

void secret_sets_follow_the_rotation_layout() {
    TempDir dir;
    const auto file = dir.path / "link.key";
    const auto next = dir.path / "link.key.next";
    const auto previous = dir.path / "link.key.previous";
    require(load_secret_set(file).error == NodeFileError::missing, "set without a current secret loaded");

    write(file, secret_bytes(0x10), 0600);
    auto only = load_secret_set(file);
    require(only.error == NodeFileError::none && only.secrets && same_secret(only.secrets->current, 0x10) &&
                !only.secrets->next && !only.secrets->previous,
            "current-only set differs");

    write(next, secret_bytes(0x30), 0600);
    write(previous, secret_bytes(0x50), 0600);
    auto all = load_secret_set(file);
    require(all.error == NodeFileError::none && all.secrets && same_secret(all.secrets->current, 0x10) &&
                all.secrets->next && same_secret(*all.secrets->next, 0x30) && all.secrets->previous &&
                same_secret(*all.secrets->previous, 0x50),
            "full rotation set differs");

    write(next, Bytes(31), 0600);
    const auto torn = load_secret_set(file);
    require(torn.error == NodeFileError::wrong_size && torn.failed_file == next && !torn.secrets,
            "a torn .next did not refuse the whole set");
    write(next, secret_bytes(0x30), 0644);
    const auto exposed = load_secret_set(file);
    require(exposed.error == NodeFileError::insecure_permissions && exposed.failed_file == next,
            "a world-readable .next did not refuse the whole set");
    write(next, secret_bytes(0x30), 0600);
    write(previous, secret_bytes(0x50), 0640);
    require(load_secret_set(file).error == NodeFileError::insecure_permissions, "an exposed .previous was accepted");
}

void catalog_loads_only_against_its_pinned_root() {
    TempDir dir;
    const auto& standard = catalog_test::standard_catalog();
    Bytes bytes(kMaxExecutionCatalogBytes);
    const auto encoded = standard.encode(bytes);
    require(encoded.error == ExecutionCatalogError::none, "fixture catalog did not encode");
    bytes.resize(encoded.bytes_written);
    const auto file = dir.path / "catalog.bin";
    write(file, bytes, 0644);
    const auto loaded = load_catalog_file(file, standard.root());
    require(loaded.error == NodeFileError::none && loaded.catalog.root() == standard.root() &&
                loaded.catalog.row_count() == standard.row_count(),
            "valid catalog refused or changed");
    auto other_root = standard.root();
    other_root.bytes[0] ^= std::byte{1};
    require(load_catalog_file(file, other_root).error == NodeFileError::invalid_catalog, "wrong root accepted");
    auto corrupt = bytes;
    corrupt[corrupt.size() / 2] ^= std::byte{1};
    write(file, corrupt, 0644);
    require(load_catalog_file(file, standard.root()).error == NodeFileError::invalid_catalog, "corrupt catalog accepted");
    write(file, Bytes(kMaxExecutionCatalogBytes + 1), 0644);
    require(load_catalog_file(file, standard.root()).error == NodeFileError::wrong_size, "oversized catalog accepted");
}

}

int main() {
    try {
        secrets_load_only_when_private_and_exact();
        secret_sets_follow_the_rotation_layout();
        catalog_loads_only_against_its_pinned_root();
        std::cout << "node file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "node file tests failed: " << error.what() << '\n';
        return 1;
    }
}
