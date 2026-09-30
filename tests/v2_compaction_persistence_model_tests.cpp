#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <map>
#include <mutex>
#include <pthread.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace compaction_disk_model {
struct Key {
    dev_t device{}; ino_t inode{};
    auto operator<=>(const Key&) const = default;
};
struct Node {
    bool directory{}; mode_t mode{};
    std::map<std::string, Key> names;
    std::vector<char> bytes;
};
enum class Names { explicit_sync, eager_all, eager_wal_replace };

struct Model {
    std::filesystem::path root;
    Key root_key{};
    std::map<Key, Node> durable;
    std::map<Key, int> pins;
    Names names{};
    int cut{-1};
    std::atomic<int> events{};
    std::atomic<bool> frozen{};
    std::atomic<bool> bad{};
    std::atomic<bool> wrong_thread{};
    std::atomic<bool> hook_exception{};
    pthread_t owner{};
    pthread_t worker{};
    bool worker_set{};
    std::mutex lock;
    std::vector<std::string> trace;

    Model(std::filesystem::path value, Names schedule) : root(std::move(value)), names(schedule) {
        owner = pthread_self();
        seed(root);
        struct stat info{};
        if (::stat(root.c_str(), &info) != 0) throw std::runtime_error("model root stat failed");
        root_key = {info.st_dev, info.st_ino};
    }
    ~Model() { for (const auto& [key, fd] : pins) (void)::close(fd); }
    static Key key(const struct stat& info) { return {info.st_dev, info.st_ino}; }
    void pin(const Key id, int fd) {
        if (pins.contains(id)) return;
        const int held = ::dup(fd);
        if (held < 0) throw std::runtime_error("model inode pin failed");
        pins.emplace(id, held);
    }
    void capture_file(const Key id, int fd, const struct stat& info) {
        auto& node = durable[id]; node.directory = false; node.mode = info.st_mode & 0777;
        node.bytes.resize(static_cast<std::size_t>(info.st_size));
        int reader = fd;
        int opened = -1;
        if ((::fcntl(fd, F_GETFL) & O_ACCMODE) == O_WRONLY) {
            opened = ::open(path_for(id).c_str(), O_RDONLY | O_CLOEXEC);
            if (opened < 0) throw std::runtime_error("model read handle open failed");
            reader = opened;
        }
        std::size_t done{};
        while (done < node.bytes.size()) {
            const auto count = ::pread(reader, node.bytes.data() + done, node.bytes.size() - done,
                                       static_cast<off_t>(done));
            if (count <= 0) { if (opened >= 0) ::close(opened); throw std::runtime_error("model inode read failed"); }
            done += static_cast<std::size_t>(count);
        }
        pin(id, reader);
        if (opened >= 0) ::close(opened);
    }
    void capture_directory(const Key id, int fd, const struct stat& info) {
        auto& node = durable[id]; node.directory = true; node.mode = info.st_mode & 0777;
        node.names.clear();
        const auto path = path_for(id);
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            struct stat child{};
            if (::lstat(entry.path().c_str(), &child) != 0) throw std::runtime_error("model child stat failed");
            node.names.emplace(entry.path().filename().string(), key(child));
            ensure_live(entry.path(), child);
        }
        pin(id, fd);
    }
    void seed(const std::filesystem::path& path) {
        struct stat info{};
        if (::lstat(path.c_str(), &info) != 0) throw std::runtime_error("model seed stat failed");
        const int fd = ::open(path.c_str(), S_ISDIR(info.st_mode) ? O_RDONLY : O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("model seed open failed");
        const Key id = key(info);
        try {
            if (S_ISDIR(info.st_mode)) {
                auto& node = durable[id]; node.directory = true; node.mode = info.st_mode & 0777;
                pin(id, fd);
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    struct stat child{};
                    if (::lstat(entry.path().c_str(), &child) != 0) throw std::runtime_error("model seed child failed");
                    node.names.emplace(entry.path().filename().string(), key(child)); seed(entry.path());
                }
            } else capture_file(id, fd, info);
        } catch (...) { ::close(fd); throw; }
        ::close(fd);
    }
    void ensure_live(const std::filesystem::path& path, const struct stat& info) {
        const Key id = key(info);
        if (durable.contains(id)) return;
        auto& node = durable[id]; node.directory = S_ISDIR(info.st_mode); node.mode = info.st_mode & 0777;
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("model live inode open failed");
        pin(id, fd); ::close(fd);
    }
    std::filesystem::path path_for(const Key wanted) const {
        struct stat info{};
        if (::stat(root.c_str(), &info) == 0 && key(info) == wanted) return root;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (::lstat(entry.path().c_str(), &info) == 0 && key(info) == wanted) return entry.path();
        }
        throw std::runtime_error("model live inode has no name");
    }
    void sync_fd(int fd) {
        struct stat info{};
        if (::fstat(fd, &info) != 0) throw std::runtime_error("model sync fstat failed");
        const Key id = key(info);
        if (S_ISDIR(info.st_mode)) capture_directory(id, fd, info);
        else capture_file(id, fd, info);
    }
    void eager_name(int directory, const char* name) {
        struct stat parent{}, child{};
        if (::fstat(directory, &parent) != 0 || ::fstatat(directory, name, &child, AT_SYMLINK_NOFOLLOW) != 0)
            throw std::runtime_error("model eager name stat failed");
        const Key parent_id = key(parent), child_id = key(child);
        ensure_live(path_for(parent_id) / name, child);
        durable[parent_id].names[std::string(name)] = child_id;
    }
    void eager_rename(int directory, const char* old_name, const char* new_name) {
        eager_name(directory, new_name);
        struct stat parent{};
        if (::fstat(directory, &parent) != 0)
            throw std::runtime_error("model rename parent stat failed");
        durable[key(parent)].names.erase(old_name);
    }
    bool enter() {
        if (pthread_equal(owner, pthread_self())) { bad = true; wrong_thread = true; errno = EIO; return false; }
        std::lock_guard guard(lock);
        if (!worker_set) { worker = pthread_self(); worker_set = true; }
        else if (!pthread_equal(worker, pthread_self())) { bad = true; wrong_thread = true; errno = EIO; return false; }
        if (frozen.load()) { errno = EIO; return false; }
        return true;
    }
    bool event(const char* what) {
        std::lock_guard guard(lock); trace.emplace_back(what);
        const int ordinal = ++events;
        if (ordinal != cut) return true;
        frozen = true; errno = EIO; return false;
    }
    void materialize(const Key id, const std::filesystem::path& path) const {
        const auto& node = durable.at(id);
        if (node.directory) {
            std::filesystem::create_directory(path);
            for (const auto& [name, child] : node.names) materialize(child, path / name);
        } else {
            std::ofstream out(path, std::ios::binary);
            out.write(node.bytes.data(), static_cast<std::streamsize>(node.bytes.size()));
            if (!out) throw std::runtime_error("model materialize failed");
        }
        if (::chmod(path.c_str(), node.mode) != 0) throw std::runtime_error("model chmod failed");
    }
};
std::atomic<Model*> active{};

template<class Action> auto guarded(Action&& action, decltype(action()) failure) noexcept {
    try { return action(); }
    catch (...) { if (auto* model = active.load()) { model->bad = true; model->hook_exception = true; } errno = EIO; return failure; }
}
bool before(Model*& model) noexcept {
    model = active.load(std::memory_order_acquire);
    return !model || model->enter();
}
}

extern "C" ssize_t compaction_model_write(int fd, const void* bytes, size_t size) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] { const auto n = ::write(fd, bytes, size); if (model && n >= 0 && !model->event("write")) return ssize_t{-1}; return n; }, ssize_t{-1});
}
extern "C" ssize_t compaction_model_pwrite(int fd, const void* bytes, size_t size, off_t at) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] { const auto n = ::pwrite(fd, bytes, size, at); if (model && n >= 0 && !model->event("pwrite")) return ssize_t{-1}; return n; }, ssize_t{-1});
}
extern "C" int compaction_model_fsync(int fd) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] { const int r = ::fsync(fd); if (model && r == 0) { model->sync_fd(fd); if (!model->event("fsync")) return -1; } return r; }, -1);
}
extern "C" int compaction_model_fdatasync(int fd) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] {
#if defined(__APPLE__)
        const int r = ::fsync(fd);
#else
        const int r = ::fdatasync(fd);
#endif
        if (model && r == 0) { model->sync_fd(fd); if (!model->event("fdatasync")) return -1; } return r;
    }, -1);
}
extern "C" int compaction_model_ftruncate(int fd, off_t length) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] { const int r = ::ftruncate(fd, length); if (model && r == 0 && !model->event("ftruncate")) return -1; return r; }, -1);
}
static int model_open_common(int directory, const char* path, int flags, mode_t mode, bool relative) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] {
        const int fd = relative ? ::openat(directory, path, flags, mode) : ::open(path, flags, mode);
        if (model && fd >= 0 && (flags & O_CREAT)) {
            struct stat info{}, parent_info{};
            if (::fstat(fd, &info) != 0) throw std::runtime_error("created inode stat failed");
            auto full_path = std::filesystem::path(path);
            if (relative) {
                if (::fstat(directory, &parent_info) != 0) throw std::runtime_error("creation parent stat failed");
                full_path = model->path_for(Model::key(parent_info)) / path;
            }
            model->ensure_live(full_path, info);
            if (model->names == Names::eager_all) {
                const int parent = relative ? directory : ::open(full_path.parent_path().c_str(), O_RDONLY);
                if (parent < 0) throw std::runtime_error("creation parent open failed");
                model->eager_name(parent, full_path.filename().c_str());
                if (!relative) ::close(parent);
            }
            if (!model->event("create")) { ::close(fd); errno = EIO; return -1; }
        }
        return fd;
    }, -1);
}
extern "C" int compaction_model_open(const char* path, int flags, ...) noexcept {
    mode_t mode{}; if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return model_open_common(AT_FDCWD, path, flags, mode, false);
}
extern "C" int compaction_model_openat(int dir, const char* path, int flags, ...) noexcept {
    mode_t mode{}; if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    return model_open_common(dir, path, flags, mode, true);
}
extern "C" int compaction_model_close(int fd) noexcept { return ::close(fd); }
static int model_rename_done(int result, int directory, const char* old_name, const char* name, const char* label) noexcept {
    using namespace compaction_disk_model;
    return guarded([&] {
        auto* model = active.load();
        if (model && result == 0) {
            const bool wal = std::string_view(name) == "v2-wal-blocks.bin";
            if (model->names == Names::eager_all || (model->names == Names::eager_wal_replace && wal))
                model->eager_rename(directory, old_name, name);
            if (!model->event(label)) return -1;
        }
        return result;
    }, -1);
}
extern "C" int compaction_model_rename(const char* from, const char* to) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    const int result = ::rename(from, to);
    if (!model || result != 0) return result;
    return guarded([&] {
        if (std::filesystem::path(from).parent_path() != std::filesystem::path(to).parent_path())
            throw std::runtime_error("cross-directory rename outside model scope");
        const bool wal = std::filesystem::path(to).filename() == "v2-wal-blocks.bin";
        if (model->names == Names::eager_all || (model->names == Names::eager_wal_replace && wal)) {
            const auto parent = std::filesystem::path(to).parent_path();
            const int dir = ::open(parent.c_str(), O_RDONLY); if (dir < 0) throw std::runtime_error("rename parent open failed");
            model->eager_rename(dir, std::filesystem::path(from).filename().c_str(), std::filesystem::path(to).filename().c_str()); ::close(dir);
        }
        return model->event(wal ? "rename-wal" : "rename-snapshot") ? 0 : -1;
    }, -1);
}
extern "C" int compaction_model_renameat(int from, const char* old_name, int to, const char* new_name) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    if (model && from != to) { model->bad = true; errno = EIO; return -1; }
    return model_rename_done(::renameat(from, old_name, to, new_name), to, old_name, new_name,
                             std::string_view(new_name).find("catalog") != std::string_view::npos ? "rename-catalog" : "renameat");
}
extern "C" int compaction_model_mkdirat(int dir, const char* name, mode_t mode) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    return guarded([&] { const int r = ::mkdirat(dir, name, mode); if (model && r == 0) { if (model->names == Names::eager_all) model->eager_name(dir, name); if (!model->event("mkdirat")) return -1; } return r; }, -1);
}
#if defined(__APPLE__)
extern "C" int compaction_model_renameatx_np(int from, const char* old_name, int to, const char* new_name, unsigned flags) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    if (model && from != to) { model->bad = true; errno = EIO; return -1; }
    return model_rename_done(::renameatx_np(from, old_name, to, new_name, flags), to, old_name, new_name, "rename-exclusive");
}
#else
extern "C" long compaction_model_syscall(long number, ...) noexcept {
    using namespace compaction_disk_model; Model* model{}; if (!before(model)) return -1;
    va_list ap; va_start(ap, number); const int from = va_arg(ap, int); const char* old_name = va_arg(ap, const char*);
    const int to = va_arg(ap, int); const char* new_name = va_arg(ap, const char*); const unsigned flags = va_arg(ap, unsigned); va_end(ap);
    if (number != SYS_renameat2 || (model && from != to)) { if (model) model->bad = true; errno = ENOSYS; return -1; }
    return model_rename_done(static_cast<int>(::syscall(number, from, old_name, to, new_name, flags)), to, old_name, new_name, "rename-exclusive");
}
#endif

namespace {
using namespace velocita::v2;
using compaction_disk_model::Model;
using compaction_disk_model::Names;

struct TempRoot {
    std::filesystem::path path;
    TempRoot() { char value[] = "/tmp/v2-composed-model-XXXXXX"; const char* made = ::mkdtemp(value); require(made, "mkdtemp failed"); path = made; }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct TwoVoters {
    std::filesystem::path root;
    std::vector<std::unique_ptr<ReplicatedKernel>> nodes;
    explicit TwoVoters(std::filesystem::path value) : root(std::move(value)) {
        std::filesystem::create_directories(root);
        for (int id : {1, 2}) { auto identity = identity_test::identity(id, {1, 2}); identity.deployment_mode = DeploymentMode::laboratory;
            nodes.push_back(std::make_unique<ReplicatedKernel>(VerifiedIdentityStartup::bootstrap(root / std::to_string(id), identity, catalog_test::standard_catalog()))); }
    }
    void route() { std::vector<PeerMessage> messages; for (auto& node : nodes) while (auto message = identity_test::take_outbound_message(*node)) messages.push_back(std::move(*message));
        for (const auto& message : messages) identity_test::deliver_peer(*nodes[message.recipient_id - 1], message, nodes[message.sender_id - 1]->static_identity()); }
    ReplicatedKernel& leader() { for (int round = 0; round < 2000; ++round) { for (auto& n : nodes) n->flush_durability(); for (auto& n : nodes) n->tick(10); route(); for (auto& n : nodes) if (n->is_leader()) return *n; } throw std::runtime_error("election failed"); }
    void commit(std::uint64_t sequence) { auto& elected = leader(); const auto result = elected.propose(encoded(command(sequence, AdvanceKillEpoch{KillEpoch{sequence}}))); require(result.status == ProposeStatus::admitted, "proposal refused");
        for (int round = 0; round < 400; ++round) { for (auto& n : nodes) n->flush_durability(); for (auto& n : nodes) n->tick(10); route(); if (elected.status().applied_index >= result.index) return; } throw std::runtime_error("commit failed"); }
};

void verify_archive(const std::filesystem::path& root, const VerifiedStaticIdentity& identity,
                    const std::vector<velocita::WalRecord>& expected, std::uint64_t through) {
    const auto catalog_bytes = read_bytes(root / "v2-archive/v2-archive-catalog.bin");
    std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows> rows{}; ArchiveCatalogHeader header; std::size_t count{};
    require(decode_archive_catalog(catalog_bytes, identity, header, rows, count) == ArchiveCatalogError::none && count > 0 && rows[count - 1].last_index == through, "archive catalog lacks exact committed prefix");
    std::size_t seen{};
    for (std::size_t row = 0; row < count; ++row) {
        const auto segment = read_bytes(root / "v2-archive" / ("s-" + to_hex(rows[row].segment_digest) + ".bin"));
        struct Oracle { const std::vector<velocita::WalRecord>* expected; std::size_t* seen; } oracle{&expected, &seen}; ArchiveSegmentScratch scratch; ArchiveSegmentMetadata metadata;
        require(decode_archive_segment(segment, identity, [](const ArchiveRecordView& record, void* raw) noexcept { auto& value = *static_cast<Oracle*>(raw); if (*value.seen >= value.expected->size()) return false; const auto& want = (*value.expected)[(*value.seen)++]; return record.index == want.index && record.term == want.term && record.entry_id == want.entry_id && record.entry_type == want.entry_type && std::ranges::equal(record.payload, want.payload); }, &oracle, scratch, metadata).error == ArchiveSegmentError::none, "archive segment changed exact history");
    }
    require(seen == through, "archive lost or invented committed records");
}

std::map<std::uint64_t, velocita::WalRecord> durable_records(
    const std::filesystem::path& root, const VerifiedStaticIdentity& identity) {
    std::map<std::uint64_t, velocita::WalRecord> result;
    for (const auto& record : identity_test::read_wal(root)) result.emplace(record.index, record);
    const auto catalog_path = root / "v2-archive/v2-archive-catalog.bin";
    if (!std::filesystem::exists(catalog_path)) return result;
    std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows> rows{}; ArchiveCatalogHeader header; std::size_t count{};
    require(decode_archive_catalog(read_bytes(catalog_path), identity, header, rows, count) ==
                ArchiveCatalogError::none, "accepted recovery has invalid archive catalog");
    for (std::size_t row = 0; row < count; ++row) {
        const auto segment = read_bytes(root / "v2-archive" / ("s-" + to_hex(rows[row].segment_digest) + ".bin"));
        struct Sink { std::map<std::uint64_t, velocita::WalRecord>* records; } sink{&result}; ArchiveSegmentScratch scratch; ArchiveSegmentMetadata metadata;
        require(decode_archive_segment(segment, identity, [](const ArchiveRecordView& value, void* raw) noexcept {
            auto& out = *static_cast<Sink*>(raw)->records;
            velocita::WalRecord record{value.index, value.term, value.entry_id, value.entry_type,
                                       std::vector<std::byte>(value.payload.begin(), value.payload.end())};
            const auto [at, inserted] = out.emplace(value.index, record);
            return inserted || (at->second.term == record.term && at->second.entry_id == record.entry_id &&
                at->second.entry_type == record.entry_type && at->second.payload == record.payload);
        }, &sink, scratch, metadata).error == ArchiveSegmentError::none, "accepted archive is invalid");
    }
    return result;
}

bool same_record(const velocita::WalRecord& a, const velocita::WalRecord& b) {
    return a.index == b.index && a.term == b.term && a.entry_id == b.entry_id &&
        a.entry_type == b.entry_type && a.payload == b.payload;
}

DirectoryImage complete_image(const std::filesystem::path& root) {
    auto image = directory_image(root);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_directory())
            image.emplace(entry.path().lexically_relative(root).generic_string() + "/", std::vector<std::byte>{});
    return image;
}

[[maybe_unused]] int exercise(Names schedule, int cut, bool replacement) {
    TempRoot live, recovered;
    const auto recovery_root = recovered.path / "node";
    TwoVoters fixture(live.path); for (std::uint64_t i = 1; i <= 3; ++i) fixture.commit(i);
    auto& leader = fixture.leader(); const int leader_id = leader.status().node_id;
    std::vector<velocita::WalRecord> expected_history = identity_test::read_wal(live.path / std::to_string(leader_id));
    if (replacement) { require(identity_test::complete_compaction(leader) > 0, "seed compaction failed"); fixture.commit(4); fixture.commit(5); fixture.commit(6); }
    const std::uint64_t boundary = replacement ? 6 : 3, suffix_index = boundary + 1;
    const auto suffix = leader.propose(encoded(command(suffix_index, AdvanceKillEpoch{KillEpoch{suffix_index}})));
    require(suffix.status == ProposeStatus::admitted, "suffix refused"); leader.flush_durability();
    require(leader.status().commit_index == static_cast<std::int64_t>(boundary) && leader.durable_index() >= suffix_index, "fixture lacks durable uncommitted suffix");
    const auto before_wal = identity_test::read_wal(live.path / std::to_string(leader_id));
    if (replacement) for (const auto& record : before_wal) if (record.index <= boundary) expected_history.push_back(record);
    require(expected_history.size() == boundary, "fixture did not retain independently captured complete history");
    for (std::size_t i = 0; i < expected_history.size(); ++i)
        require(expected_history[i].index == i + 1 &&
                    expected_history[i].payload == encoded(command(i + 1, AdvanceKillEpoch{KillEpoch{i + 1}})),
                "seed history differs from independently intended command");
    require(before_wal.size() == 4, "fixture must retain three commands plus exactly one suffix");
    for (std::size_t i = 0; i < before_wal.size(); ++i) {
        const auto wanted = (replacement ? 4 : 1) + i;
        require(before_wal[i].index == wanted &&
                    before_wal[i].payload == encoded(command(wanted, AdvanceKillEpoch{KillEpoch{wanted}})),
                "pre-fault WAL differs from independently intended history");
    }
    const auto expected_suffix = before_wal.back(); const auto expected_state = leader.status().state_digest;
    Model model(live.path / std::to_string(leader_id), schedule); model.cut = cut;
    struct Lifetime {
        TwoVoters& fixture;
        ~Lifetime() {
            fixture.nodes.clear();
            compaction_disk_model::active.store(nullptr, std::memory_order_release);
        }
    } lifetime{fixture};
    compaction_disk_model::active.store(&model, std::memory_order_release);
    bool completed{}, failed{}; std::uint64_t returned_cut{}; returned_cut = leader.compact();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !completed && !failed) {
        try { const auto value = leader.compact(); if (value) returned_cut = value; completed = returned_cut == boundary && !leader.compaction_pending(); }
        catch (...) { failed = true; }
        std::this_thread::yield();
    }
    fixture.nodes.clear();
    compaction_disk_model::active.store(nullptr, std::memory_order_release);
    require(!model.bad.load(), model.wrong_thread.load() ? "model intercepted a non-target thread" : "model hook caught an internal exception");
    require(cut < 0 ? completed && !failed && returned_cut == boundary :
                model.frozen.load() && failed && model.events.load() == cut,
            "modeled boundary was missed or transaction did not fail closed");
    if (cut < 0) {
        const auto saw = [&](std::string_view value) { return std::ranges::find(model.trace, value) != model.trace.end(); };
        if (!saw("rename-catalog") || !saw("rename-snapshot") || !saw("rename-wal")) {
            for (const auto& event : model.trace) std::cerr << event << ' ';
            std::cerr << '\n';
        }
        require(saw("rename-catalog") && saw("rename-snapshot") && saw("rename-wal") &&
                    (saw("fsync") || saw("fdatasync")),
                "baseline did not traverse actual archive, snapshot and WAL publication");
    }
    model.materialize(model.root_key, recovery_root);
    const auto image_before = complete_image(recovery_root);
    // Refusal is not permission to violate publication ordering. Inspect the
    // durable image before recovery can re-sync anything.
    const auto image_wal = identity_test::read_wal(recovery_root);
    const bool old_wal = std::ranges::equal(image_wal, before_wal, same_record);
    const bool new_wal = image_wal.size() == 1 && same_record(image_wal[0], expected_suffix);
    require(old_wal || new_wal, "durable WAL image lost or changed exact prefix/suffix");
    if (new_wal) {
        const auto snapshot = identity_test::snapshot_store(recovery_root).load();
        require(snapshot && snapshot->last_included_index == boundary &&
                    snapshot->state_digest == expected_state,
                "new WAL became durable before the matching snapshot namespace");
        auto expected_identity = identity_test::identity(leader_id, {1, 2});
        expected_identity.deployment_mode = DeploymentMode::laboratory;
        verify_archive(recovery_root, identity_test::verified_identity(expected_identity),
                       expected_history, boundary);
    }
    std::unique_ptr<ReplicatedKernel> restart;
    auto identity = identity_test::identity(leader_id, {1, 2}); identity.deployment_mode = DeploymentMode::laboratory;
    try {
        restart = std::make_unique<ReplicatedKernel>(VerifiedIdentityStartup::recover(recovery_root, identity, catalog_test::standard_catalog()));
    } catch (const std::exception& error) {
        if (cut < 0) throw std::runtime_error(
            std::string("successful transaction has no restartable durable image: ") + error.what());
    }
    const bool admitted = bool(restart);
    if (restart) {
        require(restart->status().applied_index == static_cast<std::int64_t>(boundary) && restart->status().state_digest == expected_state && restart->state().kill_epoch == KillEpoch{boundary}, "restart changed committed state");
        const auto records = durable_records(recovery_root, restart->static_identity());
        require(records.size() == suffix_index, "accepted image invented or omitted history");
        for (const auto& want : expected_history) {
            const auto at = records.find(want.index);
            require(at != records.end() && at->second.term == want.term && at->second.entry_id == want.entry_id &&
                        at->second.entry_type == want.entry_type && at->second.payload == want.payload,
                    "accepted image lost or changed committed history");
        }
        const auto suffix_at = records.find(suffix_index);
        require(suffix_at != records.end() && suffix_at->second.term == expected_suffix.term &&
                    suffix_at->second.entry_id == expected_suffix.entry_id && suffix_at->second.entry_type == expected_suffix.entry_type &&
                    suffix_at->second.payload == expected_suffix.payload, "accepted image lost or changed exact suffix");
        if (cut < 0) {
        const auto wal = identity_test::read_wal(recovery_root);
        require(wal.size() == 1 && wal[0].index == suffix_index && wal[0].term == expected_suffix.term && wal[0].entry_id == expected_suffix.entry_id && wal[0].entry_type == expected_suffix.entry_type && wal[0].payload == expected_suffix.payload, "restart lacks exact complete suffix or retained reclaimed prefix");
        verify_archive(recovery_root, restart->static_identity(), expected_history, boundary);
        const auto stored = identity_test::snapshot_store(recovery_root).load();
        require(stored && stored->last_included_index == boundary && stored->state.kill_epoch == KillEpoch{boundary}, "restart did not use exact snapshot cut");
        }
        restart.reset();
    }
    require(complete_image(recovery_root) == image_before, "restart mutated modeled crash evidence");
    if (cut < 0) require(admitted, "successful baseline refused restart");
    return model.events.load();
}
}

#if !defined(V2_COMPACTION_MODEL_FIXTURE_ONLY)
int main() {
    try {
        int cuts{};
        for (const bool replacement : {false, true}) for (const Names names : {Names::explicit_sync, Names::eager_all, Names::eager_wal_replace}) {
            const int count = exercise(names, -1, replacement);
            for (int cut = 1; cut <= count; ++cut) { (void)exercise(names, cut, replacement); ++cuts; }
            std::cout << "composed persistence model replacement=" << replacement << " schedule=" << static_cast<int>(names) << " cuts=" << count << '\n';
        }
        std::cout << "composed archive/snapshot/WAL persistence model passed " << cuts << " cuts\n";
        return 0;
    } catch (const std::exception& error) {
        compaction_disk_model::active.store(nullptr); std::cerr << "composed persistence model: " << error.what() << '\n'; return 1;
    }
}
#endif
