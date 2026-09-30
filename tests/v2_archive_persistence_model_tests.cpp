#include "velocita/v2/archive_store.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace archive_disk_model {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Temp {
    std::filesystem::path path;
    Temp() {
        char name[] = "/tmp/v2-archive-disk-XXXXXX";
        check(::mkdtemp(name) != nullptr, "mkdtemp failed"); path = name;
    }
    ~Temp() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
std::vector<char> read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "image read failed");
    return {std::istreambuf_iterator<char>{in}, {}};
}
using Image = std::map<std::string, std::vector<char>>;
Image image(const std::filesystem::path& root) {
    Image result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        auto name = entry.path().lexically_relative(root).string();
        if (entry.is_directory()) result[name + "/"] = {};
        else result[name] = read(entry.path());
    }
    return result;
}
struct Inode {
    bool directory{};
    mode_t mode{};
    std::map<std::string, ino_t> names;
    std::vector<char> bytes;
};
// A sync persists this inode's bytes OR directory names, never both implicitly.
// Unsynced changes may disappear. This is a bounded model, not a device claim.
struct Model {
    std::filesystem::path root;
    ino_t root_inode{};
    std::map<ino_t, Inode> stable;
    int events{}, cut{-1};
    bool crashed{}, eager_names{};
    std::vector<std::string> trace;
    explicit Model(const std::filesystem::path& path, bool eager) : root(path), eager_names(eager) {
        seed(root);
        struct stat info{}; check(::stat(root.c_str(), &info) == 0, "root stat failed");
        root_inode = info.st_ino;
    }
    void seed(const std::filesystem::path& path) {
        struct stat info{}; check(::lstat(path.c_str(), &info) == 0, "seed stat failed");
        auto& node = stable[info.st_ino];
        node.directory = S_ISDIR(info.st_mode); node.mode = info.st_mode & 0777;
        if (!node.directory) { node.bytes = read(path); return; }
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            struct stat child{}; check(::lstat(entry.path().c_str(), &child) == 0, "seed child stat failed");
            node.names[entry.path().filename().string()] = child.st_ino;
            seed(entry.path());
        }
    }
    std::filesystem::path named(ino_t inode) const {
        if (inode == root_inode) return root;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            struct stat info{}; check(::lstat(entry.path().c_str(), &info) == 0, "lookup stat failed");
            if (info.st_ino == inode) return entry.path();
        }
        throw std::runtime_error("synced inode has no current name");
    }
    void persist(int fd) {
        struct stat info{}; check(::fstat(fd, &info) == 0, "sync fstat failed");
        auto& node = stable[info.st_ino];
        node.directory = S_ISDIR(info.st_mode); node.mode = info.st_mode & 0777;
        const auto path = named(info.st_ino);
        if (!node.directory) { node.bytes = read(path); return; }
        node.names.clear();
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            struct stat child{}; check(::lstat(entry.path().c_str(), &child) == 0, "sync child stat failed");
            node.names[entry.path().filename().string()] = child.st_ino;
            auto& child_node = stable[child.st_ino];
            child_node.directory = S_ISDIR(child.st_mode);
            child_node.mode = child.st_mode & 0777;
        }
    }
    bool after(const char* operation) {
        trace.emplace_back(operation);
        if (++events != cut) return true;
        crashed = true;
        errno = EIO;
        return false;
    }
    void materialize(ino_t inode, const std::filesystem::path& path) const {
        const auto& node = stable.at(inode);
        if (node.directory) {
            std::filesystem::create_directory(path);
            for (const auto& [name, child] : node.names) materialize(child, path / name);
        } else {
            std::ofstream out(path, std::ios::binary);
            out.write(node.bytes.data(), static_cast<std::streamsize>(node.bytes.size()));
            check(bool(out), "materialize write failed"); out.close();
        }
        check(::chmod(path.c_str(), node.mode) == 0, "materialize chmod failed");
    }
};
thread_local Model* active{};
bool stopped() { if (active && active->crashed) { errno = EIO; return true; } return false; }
}

int archive_disk_fsync(int fd) {
    using namespace archive_disk_model;
    if (stopped()) return -1;
    const auto result = ::fsync(fd);
    if (active && result == 0) {
        active->persist(fd);
        if (!active->after("fsync")) return -1;
    }
    return result;
}
ssize_t archive_disk_write(int fd, const void* bytes, size_t size) {
    using namespace archive_disk_model;
    if (stopped()) return -1;
    const auto result = ::write(fd, bytes, size);
    if (active && result >= 0 && !active->after("write")) return -1;
    return result;
}
int archive_disk_mkdirat(int fd, const char* name, mode_t mode) {
    using namespace archive_disk_model;
    if (stopped()) return -1;
    const auto result = ::mkdirat(fd, name, mode);
    if (active && result == 0) {
        if (active->eager_names) active->persist(fd);
        if (!active->after("mkdirat")) return -1;
    }
    return result;
}
int archive_disk_openat(int directory, const char* name, int flags, ...) {
    using namespace archive_disk_model;
    mode_t mode{};
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args);
        if (stopped()) return -1;
    }
    const int fd = ::openat(directory, name, flags, mode);
    if (active && fd >= 0 && (flags & O_CREAT)) {
        if (active->eager_names) active->persist(directory);
        if (!active->after("create")) { (void)::close(fd); errno = EIO; return -1; }
    }
    return fd;
}
int archive_disk_renameat(int from, const char* old_name, int to, const char* new_name) {
    using namespace archive_disk_model;
    if (stopped()) return -1;
    const auto result = ::renameat(from, old_name, to, new_name);
    if (active && result == 0) {
        if (active->eager_names) active->persist(to);
        if (!active->after("replace")) return -1;
    }
    return result;
}
int archive_disk_noreplace(int from, const char* old_name, int to, const char* new_name, unsigned flags) {
    using namespace archive_disk_model;
    if (stopped()) return -1;
#if defined(__APPLE__)
    const auto result = ::renameatx_np(from, old_name, to, new_name, flags);
#else
    const auto result = ::syscall(SYS_renameat2, from, old_name, to, new_name, flags);
#endif
    if (active && result == 0) {
        if (active->eager_names) active->persist(to);
        if (!active->after("no-replace")) return -1;
    }
    return static_cast<int>(result);
}
#if defined(__linux__)
long archive_disk_syscall(long number, int from, const char* old_name, int to, const char* new_name, unsigned flags) {
    archive_disk_model::check(number == SYS_renameat2, "unexpected syscall");
    return archive_disk_noreplace(from, old_name, to, new_name, flags);
}
#endif

#define fsync archive_disk_fsync
#define write archive_disk_write
#define mkdirat archive_disk_mkdirat
#define openat archive_disk_openat
#define renameat archive_disk_renameat
#define renameatx_np archive_disk_noreplace
#define syscall archive_disk_syscall
#ifndef ARCHIVE_STORE_IMPLEMENTATION
#define ARCHIVE_STORE_IMPLEMENTATION "../src/v2_archive_store.cpp"
#endif
#include ARCHIVE_STORE_IMPLEMENTATION
#undef syscall
#undef renameatx_np
#undef renameat
#undef mkdirat
#undef openat
#undef write
#undef fsync

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static auto publish(ArchiveStore& store, const ArchivePublicationContext& context, const ArchiveRecordSource& source) {
        return store.publish(context, source);
    }
};
}
namespace {
using namespace velocita::v2;
using namespace archive_disk_model;
struct Fixture {
    VerifiedStaticIdentity identity = identity_test::verified_identity(identity_test::identity(1, {1}));
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::array<std::byte, kMaxExecutionCatalogBytes> execution_bytes{};
    std::size_t identity_length{}, execution_length{};
    const VerifiedExecutionCatalog& catalog = catalog_test::standard_catalog();
    std::vector<std::byte> command;
    Fixture() {
        const auto id = encode_static_identity(identity.value(), identity_bytes);
        const auto ec = catalog.encode(execution_bytes);
        check(id.error == StaticIdentityCodecError::none && ec.error == ExecutionCatalogError::none, "fixture encode failed");
        identity_length = id.bytes_written; execution_length = ec.bytes_written;
        // Authored schema-8 command, independent of the archive encoder.
        constexpr std::string_view hex = "56324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b";
        for (std::size_t at = 0; at < hex.size(); at += 2)
            command.push_back(std::byte(std::stoul(std::string(hex.substr(at, 2)), nullptr, 16)));
    }
    auto open(const std::filesystem::path& root, ArchiveStore& store) const {
        return ArchiveStore::open(root, identity, std::span(identity_bytes).first(identity_length),
            std::span(execution_bytes).first(execution_length), catalog, {}, store);
    }
};
struct Source {
    const Fixture* fixture{};
    std::uint64_t index{};
    static bool get(void* raw, std::uint32_t at, ArchiveRecordView& out) noexcept {
        const auto& self = *static_cast<Source*>(raw);
        if (at) return false;
        out = {self.index, 3, static_cast<std::int32_t>(self.index), 0, self.fixture->command};
        return true;
    }
};
auto publish(ArchiveStore& store, const Fixture& fixture, std::uint64_t index) {
    Source value{&fixture, index};
    return ArchiveStoreTestAccess::publish(store, {index, 1, index, index, 3, index == 1 ? 0 : 3, 0},
        {&value, 1, &Source::get});
}
void verify_payloads(const std::filesystem::path& root, const Fixture& fixture, std::uint64_t through) {
    std::uint64_t count{};
    for (const auto& entry : std::filesystem::directory_iterator(root / "v2-archive")) {
        if (!entry.path().filename().string().starts_with("s-")) continue;
        auto bytes = read(entry.path());
        struct Seen { const Fixture* fixture; std::uint64_t* count; bool valid{true}; } seen{&fixture, &count};
        ArchiveSegmentScratch scratch;
        ArchiveSegmentMetadata metadata;
        const auto decoded = decode_archive_segment(std::as_bytes(std::span(bytes)), fixture.identity,
            [](const ArchiveRecordView& record, void* raw) noexcept {
                auto& seen = *static_cast<Seen*>(raw);
                ++*seen.count;
                seen.valid &= record.term == 3 && record.entry_id == static_cast<std::int32_t>(record.index) &&
                    record.entry_type == 0 && std::ranges::equal(record.payload, seen.fixture->command);
                return true;
            }, &seen, scratch, metadata);
        check(decoded.error == ArchiveSegmentError::none && seen.valid, "durable archive changed authored record bytes");
    }
    check(count == through, "durable archive lost or duplicated authored records");
}
int exercise(const Fixture& fixture, bool replacement, bool eager, int cut) {
    Temp live, recovered;
    const auto index = replacement ? 2ULL : 1ULL;
    if (replacement) {
        ArchiveStore seed;
        check(fixture.open(live.path, seed) == ArchiveStoreError::none, "seed open failed");
        check(publish(seed, fixture, 1).error == ArchiveStoreError::none, "seed publication failed");
    }
    Model model(live.path, eager); model.cut = cut;
    bool returned{};
    {
        ArchiveStore store;
        active = &model;
        if (fixture.open(live.path, store) == ArchiveStoreError::none) {
            auto result = publish(store, fixture, index);
            returned = result.error == ArchiveStoreError::none;
            check(!returned || result.capability.local_publication_durable(), "success lacks capability");
        }
        active = nullptr;
    }
    check(cut < 0 ? returned : model.crashed && !returned, "intended model cut was missed or returned success");
    model.materialize(model.root_inode, recovered.path);
    const auto before = image(recovered.path);
    bool present{};
    const auto admitted = ArchiveStore::verify_existing(recovered.path, fixture.identity,
        std::span(fixture.identity_bytes).first(fixture.identity_length),
        std::span(fixture.execution_bytes).first(fixture.execution_length), fixture.catalog, {}, present);
    check(image(recovered.path) == before, "restart rewrote modeled evidence");
    if (admitted == ArchiveStoreError::none && present) {
        ArchiveStore store;
        check(fixture.open(recovered.path, store) == ArchiveStoreError::none, "second verification failed");
        const auto through = store.covered_through();
        check(through <= index && (!replacement || through >= 1), "restart invented or regressed archive coverage");
        verify_payloads(recovered.path, fixture, through);
        if (returned) check(through == index, "acknowledged publication vanished after modeled power loss");
    } else {
        check(!returned, "successful publication had no restartable durable evidence");
    }
    return model.events;
}
}
int main() {
    try {
        const Fixture fixture;
        int cuts{};
        for (bool replacement : {false, true}) {
            for (bool eager : {false, true}) {
                const int count = exercise(fixture, replacement, eager, -1);
                for (int cut = 1; cut <= count; ++cut) { exercise(fixture, replacement, eager, cut); ++cuts; }
                std::cout << "archive disk model replacement=" << replacement << " eager_names=" << eager
                          << " cuts=" << count << '\n';
            }
        }
        std::cout << "archive persistence model passed " << cuts << " sync-sensitive cuts\n";
        return 0;
    } catch (const std::exception& error) {
        active = nullptr;
        std::cerr << "archive persistence model: " << error.what() << '\n'; return 1;
    }
}
