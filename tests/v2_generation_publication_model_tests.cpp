#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/generation_base_files.hpp"
#include "velocita/v2/snapshot_store.hpp"
#include "v2_catalog_test_support.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace publication_model {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Cut {};
struct Temp {
    std::filesystem::path path;
    Temp() {
        char pattern[] = "/tmp/v2-generation-persistence.XXXXXX";
        require(::mkdtemp(pattern) != nullptr, "mkdtemp failed"); path = pattern;
    }
    ~Temp() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct Node {
    bool directory{};
    std::map<std::string, ino_t> names;
    std::vector<char> bytes;
};
struct Model {
    std::filesystem::path parent;
    ino_t parent_inode{};
    std::map<ino_t, Node> stable;
    int events{}, cut_after{-1}, fail_before{-1};
    bool crashed{}, short_write{}, interrupted_write{};
    int closes{}, fail_close{-1};

    explicit Model(const std::filesystem::path& path) : parent(path) {
        struct stat info{};
        require(::stat(path.c_str(), &info) == 0, "model parent stat failed");
        parent_inode = info.st_ino;
        stable[parent_inode].directory = true;
    }
    bool before() {
        if (events + 1 == fail_before) { ++events; errno = EIO; return false; }
        return true;
    }
    void after() { if (++events == cut_after) { crashed = true; throw Cut{}; } }
    std::filesystem::path name_of(ino_t inode) const {
        if (inode == parent_inode) return parent;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
            struct stat info{};
            require(::lstat(entry.path().c_str(), &info) == 0, "model inode lookup failed");
            if (info.st_ino == inode) return entry.path();
        }
        throw std::runtime_error("model could not name a synced inode");
    }
    void persist(int fd) {
        struct stat info{};
        require(::fstat(fd, &info) == 0, "model fd stat failed");
        auto& node = stable[info.st_ino];
        const auto path = name_of(info.st_ino);
        node.directory = S_ISDIR(info.st_mode);
        if (node.directory) {
            node.names.clear();
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                struct stat child{};
                require(::lstat(entry.path().c_str(), &child) == 0, "model child stat failed");
                node.names.emplace(entry.path().filename().string(), child.st_ino);
                stable[child.st_ino].directory = S_ISDIR(child.st_mode);
            }
        } else {
            std::ifstream input(path, std::ios::binary);
            require(bool(input), "model synced file read failed");
            node.bytes.assign(std::istreambuf_iterator<char>{input}, {});
        }
    }
    void materialize(ino_t inode, const std::filesystem::path& path) const {
        const auto& node = stable.at(inode);
        if (node.directory) {
            std::filesystem::create_directory(path);
            for (const auto& [name, child] : node.names) materialize(child, path / name);
        } else {
            std::ofstream output(path, std::ios::binary);
            output.write(node.bytes.data(), node.bytes.size());
            require(bool(output), "model image write failed");
            output.close();
            require(::chmod(path.c_str(), 0600) == 0, "model file chmod failed");
        }
    }
    bool has_selector() const {
        const auto& parent_names = stable.at(parent_inode).names;
        const auto root = parent_names.find("voter");
        return root != parent_names.end() && stable.at(root->second).names.contains("v2-active-generation.bin");
    }
};
thread_local Model* active{};
} // namespace publication_model

int generation_model_fsync(int fd) {
    auto* m = publication_model::active;
    if (m && !m->before()) return -1;
    const int result = ::fsync(fd);
    if (m && result == 0) { m->persist(fd); m->after(); }
    return result;
}
ssize_t generation_model_write(int fd, const void* bytes, size_t size) {
    auto* m = publication_model::active;
    if (m && !m->before()) return -1;
    if (m && std::exchange(m->interrupted_write, false)) { errno = EINTR; return -1; }
    if (m && std::exchange(m->short_write, false) && size > 1) size /= 2;
    const auto result = ::write(fd, bytes, size);
    if (m && result >= 0) m->after();
    return result;
}
int generation_model_mkdirat(int fd, const char* path, mode_t mode) {
    auto* m = publication_model::active;
    if (m && !m->before()) return -1;
    const int result = ::mkdirat(fd, path, mode);
    if (m && result == 0) m->after();
    return result;
}
int generation_model_renameat(int from, const char* old_name, int to, const char* new_name) {
    auto* m = publication_model::active;
    if (m && !m->before()) return -1;
    const int result = ::renameat(from, old_name, to, new_name);
    if (m && result == 0) m->after();
    return result;
}
int generation_model_close(int fd) {
    const int result = ::close(fd);
    auto* m = publication_model::active;
    if (m && !m->crashed && ++m->closes == m->fail_close) { errno = EIO; return -1; }
    return result;
}

// Execute the production transaction; only persistence syscalls are observed/faulted.
#define fsync generation_model_fsync
#define write generation_model_write
#define mkdirat generation_model_mkdirat
#define renameat generation_model_renameat
#define close generation_model_close
#ifndef GENERATION_STARTUP_IMPLEMENTATION
#define GENERATION_STARTUP_IMPLEMENTATION "../src/v2_identity_startup.cpp"
#endif
#include GENERATION_STARTUP_IMPLEMENTATION
#undef close
#undef renameat
#undef mkdirat
#undef write
#undef fsync

namespace {
using namespace velocita::v2;
using namespace publication_model;
StaticIdentity expected_identity() {
    StaticIdentity id;
    id.incarnation[0] = std::byte{0x64};
    id.catalog_root = catalog_test::standard_catalog().root();
    id.voter_count = 2; id.voters = {1, 2, 0}; id.local_node_id = 1;
    return id;
}

int exercise(int cut, int fail, bool short_write = false, bool interrupt_write = false,
             int fail_close = -1) {
    Temp live, recovered;
    Model model(live.path);
    model.cut_after = cut; model.fail_before = fail;
    model.short_write = short_write; model.interrupted_write = interrupt_write;
    model.fail_close = fail_close;
    active = &model;
    bool returned{};
    try {
        auto token = VerifiedIdentityStartup::bootstrap(live.path / "voter", expected_identity(),
                                                        catalog_test::standard_catalog());
        returned = true;
    } catch (const Cut&) {
    } catch (const IdentityStartupError&) {
    }
    active = nullptr;
    const bool selected = model.has_selector();
    require(!returned || selected, "startup returned before selector namespace was durable");
    for (const auto& [name, inode] : model.stable.at(model.parent_inode).names)
        model.materialize(inode, recovered.path / name);
    bool recovered_ok{};
    try {
        auto token = VerifiedIdentityStartup::recover(recovered.path / "voter", expected_identity(),
                                                      catalog_test::standard_catalog());
        recovered_ok = true;
        require(token.directory() == recovered.path / "voter/v2-generations/g-0000000000000001",
                "recovery selected the wrong generation");
        const auto hard = BoundMetadataStore(token.directory(), token.identity()).load();
        require(hard.term == 0 && hard.voted_for == -1 && hard.commit_index == 0,
                "recovery invented genesis voting history");
    } catch (const IdentityStartupError&) {
    } catch (const BoundMetadataError&) {
    }
    require(recovered_ok == selected,
            "persisted selector did not name a complete durable base, or recovery promoted an unselected tree");
    if (cut < 0 && fail < 0 && fail_close < 0) require(returned, "normal/short/interrupted write bootstrap failed");
    if (fail_close > 0) require(model.closes >= fail_close, "requested close failure was not reached");
    if (fail > 0 || fail_close > 0) require(!returned, "startup hid a persistence error");
    return model.events;
}
} // namespace

int main() {
    try {
        const int count = exercise(-1, -1);
        for (int cut = 1; cut <= count; ++cut) exercise(cut, -1);
        for (int fail = 1; fail <= count; ++fail) exercise(-1, fail);
        exercise(-1, -1, true);
        exercise(-1, -1, false, true);
        // Four checked output closes: metadata, identity, seal, selector.
        for (int close = 1; close <= 4; ++close) exercise(-1, -1, false, false, close);
        std::cout << "generation publication model passed " << count << " persistence cuts and EIO points\n";
        return 0;
    } catch (const std::exception& error) { active = nullptr; std::cerr << error.what() << '\n'; return 1; }
}
