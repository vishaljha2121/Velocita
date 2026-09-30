#include "velocita/v2/generation_base_files.hpp"
#include "velocita/v2/digest.hpp"
#include "v2_identity_test_support.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace velocita::v2;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

struct Fixture {
    std::filesystem::path root;
    int fd{-1};
    VerifiedStaticIdentity identity = identity_test::verified_identity(identity_test::identity(1, {1, 2}));
    std::vector<std::byte> identity_bytes;
    std::vector<std::byte> snapshot;
    std::array<std::byte, 40> seal{};
    LocalGenerationSelector selector{};
    std::filesystem::path generation() const { return root / "v2-generations/g-0000000000000008"; }
    static void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        require(bool(output), "fixture write failed");
        output.close();
        require(::chmod(path.c_str(), 0600) == 0, "fixture chmod failed");
    }
    static void put(std::span<std::byte> bytes, std::size_t at, unsigned n, std::uint64_t value) {
        for (unsigned i = 0; i != n; ++i) bytes[at + i] = std::byte(value >> (8*i));
    }
    void reseal() {
        seal = {std::byte{'V'}, std::byte{'2'}, std::byte{'G'}, std::byte{'B'}, std::byte{1},
                std::byte{0}, std::byte(snapshot.empty() ? 0 : 1), std::byte{0}};
        std::vector<std::byte> preimage;
        constexpr std::string_view domain = "velocita.v2.local-generation-base.v1";
        for (const char c : domain) preimage.push_back(std::byte(c));
        preimage.insert(preimage.end(), seal.begin(), seal.begin() + 8);
        const auto offset = preimage.size(); preimage.resize(offset + 8);
        put(preimage, offset, 8, 8);
        preimage.insert(preimage.end(), identity_bytes.begin(), identity_bytes.end());
        const auto length_offset = preimage.size(); preimage.resize(length_offset + 4);
        put(preimage, length_offset, 4, snapshot.size());
        preimage.insert(preimage.end(), snapshot.begin(), snapshot.end());
        const auto digest = sha256_digest(preimage);
        std::copy(digest.bytes.begin(), digest.bytes.end(), seal.begin() + 8);
        selector = {8, 1, 0, digest};
        write(generation() / "v2-base-seal.bin", seal);
        std::array<std::byte, kLocalGenerationSelectorBytes> encoded{};
        require(encode_local_generation_selector(selector, identity, encoded).error == LocalGenerationError::none,
                "fixture selector encoding failed");
        write(root / "v2-active-generation.bin", encoded);
    }
    explicit Fixture(bool with_snapshot = false) {
        std::array<char, 48> pattern{};
        constexpr std::string_view prefix = "/tmp/v2-base-files.XXXXXX";
        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        require(::mkdtemp(pattern.data()) != nullptr, "fixture mkdtemp failed");
        root = pattern.data();
        std::filesystem::create_directories(generation() / "base");
        std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
        const auto encoded = encode_static_identity(identity.value(), bytes);
        require(encoded.error == StaticIdentityCodecError::none, "fixture identity encode failed");
        identity_bytes.assign(bytes.begin(), bytes.begin() + encoded.bytes_written);
        write(root / "v2-identity.bin", identity_bytes);
        if (with_snapshot) {
            SnapshotStore(generation() / "base", identity).save(catalog_test::state(), 5, 3);
            std::ifstream input(generation() / "base/v2-snapshot.bin", std::ios::binary);
            const std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
            for (auto c : raw) snapshot.push_back(std::byte(static_cast<unsigned char>(c)));
            require(::chmod((generation() / "base/v2-snapshot.bin").c_str(), 0600) == 0, "snapshot chmod failed");
        }
        reseal();
        fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        require(fd >= 0, "fixture root open failed");
    }
    ~Fixture() {
        if (fd >= 0) ::close(fd);
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

void run() {
    std::array<std::byte, kMaxBoundSnapshotBytes> buffer{};
    GenerationBaseScratch scratch;
    SelectedGenerationBase output;
    const auto read = [&](Fixture& f) {
        return read_selected_generation_base_at(f.fd, f.identity, catalog_test::standard_catalog(), buffer, scratch, output);
    };
    const auto refuses = [&](Fixture& f, const char* reason) {
        output.snapshot_bytes = 12345;
        output.selector = {91, 92, 90, {}};
        const auto before = output;
        require(read(f) != GenerationBaseFileError::none, reason);
        require(output.snapshot_bytes == before.snapshot_bytes && output.selector == before.selector,
                "file refusal published partial selection");
        require(output.base == before.base, "file refusal changed base metadata");
    };
    {
        Fixture f;
        require(read(f) == GenerationBaseFileError::none,
                "RED: sealed genesis base cannot be read through the final selector");
        require(output.selector == f.selector && output.snapshot_bytes == 0,
                "genesis selection differs from fixture");
        Fixture::write(f.generation() / "base/unexpected", {});
        refuses(f, "genesis accepted unexpected immutable entry");
    }
    {
        Fixture f(true);
        require(read(f) == GenerationBaseFileError::none, "valid snapshot base refused");
        require(output.snapshot_bytes == f.snapshot.size() &&
                std::equal(f.snapshot.begin(), f.snapshot.end(), buffer.begin()),
                "snapshot reader did not preserve the complete payload");
        require(output.base.kind == GenerationBaseKind::snapshot && output.base.index == 5 &&
                output.base.term == 3, "base reader lost snapshot coordinates");
        f.snapshot.back() ^= std::byte{1};
        Fixture::write(f.generation() / "base/v2-snapshot.bin", f.snapshot);
        f.reseal();
        refuses(f, "fresh seal hid corrupt canonical snapshot");
    }
    {
        Fixture f;
        std::filesystem::rename(f.root / "v2-active-generation.bin", f.root / "v2-active-generation.bin.tmp");
        refuses(f, "temporary selector was promoted");
    }
    {
        Fixture f;
        std::filesystem::rename(f.generation(), f.generation().parent_path() / "g-0000000000000009");
        refuses(f, "missing generation fell back to a different directory");
    }
    {
        Fixture f;
        std::filesystem::rename(f.generation() / "v2-base-seal.bin", f.generation() / "saved-seal");
        std::filesystem::create_symlink("saved-seal", f.generation() / "v2-base-seal.bin");
        refuses(f, "base seal symlink accepted");
    }
    {
        Fixture f;
        std::filesystem::create_hard_link(f.generation() / "v2-base-seal.bin", f.root / "linked-seal");
        refuses(f, "multiply linked base seal accepted");
    }
    {
        Fixture f;
        auto foreign = identity_test::verified_identity(identity_test::identity(2, {1, 2}));
        std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
        auto encoded = encode_static_identity(foreign.value(), bytes);
        Fixture::write(f.root / "v2-identity.bin", std::span(bytes).first(encoded.bytes_written));
        refuses(f, "foreign root identity was ignored");
    }
    {
        Fixture f;
        require(::chmod((f.generation() / "v2-base-seal.bin").c_str(), 0644) == 0, "chmod failed");
        refuses(f, "base seal not owner-only accepted");
    }
    for (const auto* name : {"v2-generations", "v2-generations/g-0000000000000008",
                            "v2-generations/g-0000000000000008/base"}) {
        Fixture f;
        const auto path = f.root / name;
        const auto moved = std::filesystem::path(path.string() + "-saved");
        std::filesystem::rename(path, moved);
        std::filesystem::create_directory_symlink(moved, path);
        refuses(f, "a generation directory symlink was followed");
    }
    {
        Fixture f;
        f.selector.prepared_base_digest.bytes[0] ^= std::byte{1};
        std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
        require(encode_local_generation_selector(f.selector, f.identity, bytes).error == LocalGenerationError::none,
                "mismatching digest selector fixture failed");
        Fixture::write(f.root / "v2-active-generation.bin", bytes);
        refuses(f, "selector digest did not bind the base seal");
    }
    {
        Fixture f;
        f.selector.generation = 9;
        std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
        require(encode_local_generation_selector(f.selector, f.identity, bytes).error == LocalGenerationError::none,
                "relabelled selector fixture failed");
        Fixture::write(f.root / "v2-active-generation.bin", bytes);
        std::filesystem::rename(f.generation(), f.generation().parent_path() / "g-0000000000000009");
        refuses(f, "generation relabelling escaped base digest binding");
    }
    {
        Fixture f(true);
        std::filesystem::remove(f.generation() / "base/v2-snapshot.bin");
        refuses(f, "snapshot seal accepted a missing snapshot");
    }
    {
        Fixture f(true);
        std::filesystem::resize_file(f.generation() / "base/v2-snapshot.bin", kMaxBoundSnapshotBytes + 1);
        refuses(f, "oversized immutable snapshot accepted");
    }
    {
        Fixture f(true);
        std::filesystem::remove(f.generation() / "base/v2-snapshot.bin");
        require(::mkfifo((f.generation() / "base/v2-snapshot.bin").c_str(), 0600) == 0, "mkfifo failed");
        refuses(f, "nonregular snapshot accepted");
    }
    {
        Fixture f(true);
        Fixture::write(f.generation() / "base/unexpected", {});
        refuses(f, "snapshot namespace accepted an extra file");
    }
}
} // namespace

int main() {
    try { run(); std::cout << "generation base files tests passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
