#include "velocita/v2/digest.hpp"
#include "velocita/v2/snapshot_store.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
bool measure_allocations{};
std::size_t largest_allocation{};
std::size_t total_allocation{};
}
void* operator new(std::size_t size) {
    if (measure_allocations) {
        largest_allocation = std::max(largest_allocation, size);
        total_allocation += size;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }

namespace {
using namespace velocita::v2;
constexpr std::size_t kLoadAllocationBudget = kMaxBoundSnapshotBytes + 1 +
    sizeof(BoundFrameScratch) + sizeof(StateSnapshotScratch) + sizeof(StoredSnapshot) + 1024;
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}
std::vector<std::byte> from_hex(const std::string& hex) {
    std::vector<std::byte> result;
    require(hex.size() % 2 == 0, "odd hex fixture");
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result.push_back(static_cast<std::byte>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return result;
}
VerifiedStaticIdentity identity() {
    const auto bytes = from_hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0a000a0001000100010001000101030011000000010100000100010011000000"
        "08a4324b57b69508e3785b815ea368a65919f96695b0afcdb09ae5a85503f21d");
    VerifiedStaticIdentity result;
    require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
            "identity fixture refused");
    return result;
}
class TempDirectory {
public:
    TempDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() /
                        "v2-snapshot-bounds.XXXXXX").string();
        pattern.push_back('\0');
        const auto* made = ::mkdtemp(pattern.data());
        if (!made) throw std::runtime_error("mkdtemp failed");
        path = made;
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};
void write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "fixture write failed");
}
std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> characters{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> bytes(characters.size());
    std::transform(characters.begin(), characters.end(), bytes.begin(),
                   [](char value) { return static_cast<std::byte>(value); });
    return bytes;
}
void put32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::byte>(value >> (8U * i));
}
BoundFrameError thrown_code(const SnapshotStore& store) {
    try { (void)store.load(); }
    catch (const SnapshotStoreError& error) { return error.code(); }
    throw std::runtime_error("snapshot incompatibility did not throw");
}
void test_roundtrip_absence_and_coordinates(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto id = identity();
    SnapshotStore store(directory, id);
    require(!store.load(), "absent snapshot did not remain absent");
    store.save(TradingState{}, 11, 7);
    const auto good = store.load();
    require(good && good->last_included_index == 11 && good->last_included_term == 7 &&
                good->state.state_version == 0, "bound snapshot did not round-trip");
    require(std::filesystem::file_size(store.path()) <= kMaxBoundSnapshotBytes,
            "snapshot exceeded approved bound");
    for (const auto [index, term] : std::array<std::pair<std::uint64_t, std::int64_t>, 3>{
             {{0, 1}, {1, 0}, {1, -1}}}) {
        bool refused{};
        try { store.save(TradingState{}, index, term); }
        catch (const SnapshotStoreError& error) {
            refused = error.code() == BoundFrameError::invalid_payload;
        }
        require(refused, "invalid snapshot coordinate was persisted");
    }
}
void test_bounded_supported_corruption(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto id = identity(); SnapshotStore store(directory, id);
    store.save(TradingState{}, 11, 7);
    auto bytes = read_file(store.path()); bytes.back() ^= std::byte{1};
    write_file(store.path(), bytes);
    require(!store.load(), "supported digest corruption did not fall back");
    std::filesystem::resize_file(store.path(), 2U * 1024U * 1024U);
    largest_allocation = 0; total_allocation = 0; measure_allocations = true;
    const auto loaded = store.load(); measure_allocations = false;
    require(!loaded, "oversized supported frame loaded");
    require(largest_allocation <= kLoadAllocationBudget && total_allocation <= kLoadAllocationBudget,
            "snapshot reader exceeded fixed decoder workspace budget");
    require(std::filesystem::file_size(store.path()) == 2U * 1024U * 1024U,
            "corruption handling rewrote evidence");
}
void test_incompatible_and_foreign_are_typed(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto id = identity(); SnapshotStore store(directory, id);
    const std::array legacy{std::byte{'V'}, std::byte{'S'}, std::byte{'N'}, std::byte{'2'}};
    write_file(store.path(), legacy);
    require(thrown_code(store) == BoundFrameError::invalid_magic,
            "legacy snapshot was confused with optional corruption");
    store.save(TradingState{}, 11, 7);
    std::fstream mutate(store.path(), std::ios::binary | std::ios::in | std::ios::out);
    mutate.seekp(4); mutate.put(2); mutate.close();
    require(thrown_code(store) == BoundFrameError::unsupported_schema,
            "unknown outer schema was confused with corruption");
    std::filesystem::resize_file(store.path(), 2U * 1024U * 1024U);
    largest_allocation = 0; total_allocation = 0; measure_allocations = true;
    const auto future_error = thrown_code(store); measure_allocations = false;
    require(future_error == BoundFrameError::unsupported_schema,
            "oversized future frame fell through to corruption fallback");
    require(largest_allocation <= kLoadAllocationBudget && total_allocation <= kLoadAllocationBudget,
            "future-frame refusal exceeded fixed decoder workspace budget");

    store.save(TradingState{}, 11, 7);
    auto future_canonical = read_file(store.path());
    put32(future_canonical, 8, static_cast<std::uint32_t>(kMaxBoundSnapshotBytes + 1));
    put32(future_canonical, 12, static_cast<std::uint32_t>(
        kMaxBoundSnapshotBytes + 1 - kBoundFrameOverheadBytes));
    constexpr auto canonical_schema = kBoundFrameHeaderBytes + 16 + kStateSnapshotSchemaOffset;
    future_canonical[canonical_schema] = std::byte{kStateSnapshotSchemaVersion + 1};
    future_canonical[canonical_schema + 1] = std::byte{0};
    future_canonical.resize(2U * 1024U * 1024U);
    write_file(store.path(), future_canonical);
    require(thrown_code(store) == BoundFrameError::unsupported_schema,
            "oversized future canonical schema fell through to WAL fallback");

    const auto semantic_truncation = from_hex(
        "56324246010003006800000010000000"
        "599621a2f6f5336362106d12208b6491336fcb75e596af59b534a91019493ee0"
        "11000000000000000b000000000000000700000000000000"
        "5fada866fb73a60e451a47c1c5e0aba78a92a2b8cade973a34bfe91f2cd5552d");
    BoundFrameView literal_view{};
    BoundFrameScratch literal_scratch{};
    require(decode_bound_frame(semantic_truncation, BoundFrameKind::snapshot, id,
                               literal_view, literal_scratch) == BoundFrameError::none &&
                literal_view.payload.size() == 16,
            "independent snapshot frame vector is not valid V2BF");
    write_file(store.path(), semantic_truncation);
    require(!store.load(), "supported semantic truncation did not fall back");

    store.save(TradingState{}, 11, 7);
    auto foreign = read_file(store.path()); foreign[16] ^= std::byte{1};
    static constexpr char domain[] = "velocita.v2.bound-frame.v1";
    std::vector<std::byte> preimage;
    for (std::size_t i = 0; i < 26; ++i) preimage.push_back(static_cast<std::byte>(domain[i]));
    preimage.insert(preimage.end(), foreign.begin(), foreign.end() - 32);
    const auto digest = sha256_digest(preimage);
    std::copy(digest.bytes.begin(), digest.bytes.end(), foreign.end() - 32);
    write_file(store.path(), foreign);
    require(thrown_code(store) == BoundFrameError::foreign_manifest,
            "foreign snapshot was confused with corruption");
}
void test_special_files_refuse_without_blocking_or_damage(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto id = identity(); SnapshotStore store(directory, id);
    const auto dangling = directory / "missing-target";
    std::filesystem::create_symlink(dangling, store.path());
    bool refused{};
    try { (void)store.load(); } catch (const std::runtime_error&) { refused = true; }
    require(refused, "dangling snapshot symlink was treated as absence");
    std::filesystem::remove(store.path());
    require(::mkfifo(store.path().c_str(), 0600) == 0, "snapshot FIFO fixture failed");
    refused = false;
    try { (void)store.load(); } catch (const std::runtime_error&) { refused = true; }
    require(refused, "snapshot FIFO was accepted or blocked");
    std::filesystem::remove(store.path());

    const auto witness = directory / "witness";
    const std::array witness_bytes{std::byte{0x5a}, std::byte{0xa5}};
    write_file(witness, witness_bytes);
    std::filesystem::create_hard_link(witness, store.path().string() + ".tmp");
    refused = false;
    try { store.save(TradingState{}, 1, 1); } catch (const std::runtime_error&) { refused = true; }
    require(refused && read_file(witness) ==
                std::vector<std::byte>(witness_bytes.begin(), witness_bytes.end()),
            "snapshot save truncated an unverified hard-linked temporary");
}
}
int main() {
    try {
        TempDirectory root;
        test_roundtrip_absence_and_coordinates(root.path / "roundtrip");
        test_bounded_supported_corruption(root.path / "corruption");
        test_incompatible_and_foreign_are_typed(root.path / "typed");
        test_special_files_refuse_without_blocking_or_damage(root.path / "special");
        std::cout << "snapshot recovery allocation and binding tests passed\n";
    } catch (const std::exception& error) {
        measure_allocations = false;
        std::cerr << "snapshot bounds failed: " << error.what() << '\n';
        return 1;
    }
}
