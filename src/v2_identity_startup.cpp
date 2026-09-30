#include "velocita/v2/identity_startup.hpp"
#include "velocita/v2/archive_store.hpp"
#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/generation_base_files.hpp"
#include "velocita/v2/snapshot_store.hpp"

#include <array>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {
using Code = IdentityStartupErrorCode;
[[noreturn]] void refuse(Code code, const char* why) { throw IdentityStartupError(code, why); }

struct Fd {
    int value{-1};
    explicit Fd(int fd) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Fd(const Fd&) = delete;
    int release() noexcept { return std::exchange(value, -1); }
};

void sync(int fd) {
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    if (result < 0) refuse(Code::io_failure, "identity persistence sync failed");
}

VerifiedStaticIdentity validate_expected(const StaticIdentity& expected,
                                         const VerifiedExecutionCatalog& catalog) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(expected, bytes);
    VerifiedStaticIdentity identity;
    if (encoded.error != StaticIdentityCodecError::none ||
        VerifiedStaticIdentity::decode(std::span{bytes}.first(encoded.bytes_written), identity) !=
            StaticIdentityCodecError::none)
        refuse(Code::invalid_identity, "invalid requested static identity");
    if (catalog.row_count() == 0 || catalog.root() != expected.catalog_root)
        refuse(Code::catalog_mismatch, "required immutable catalog missing or mismatched");
    return identity;
}

std::filesystem::path normalized(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    if (absolute.filename().empty() || absolute.filename() == "." || absolute.filename() == "..")
        refuse(Code::invalid_file, "deployment directory must name a leaf");
    return absolute;
}

Fd open_parent(const std::filesystem::path& directory, bool bootstrap) {
    Fd parent(::open(directory.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (parent.value < 0) {
        if (!bootstrap && errno == ENOENT) refuse(Code::directory_missing, "recovery parent missing");
        refuse(Code::io_failure, "deployment parent must already exist and be accessible");
    }
    return parent;
}

Fd open_owner(int parent, const std::filesystem::path& leaf) {
    Fd directory(::openat(parent, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (directory.value < 0) {
        if (errno == ENOENT) refuse(Code::directory_missing, "recovery directory missing");
        if (errno == ELOOP || errno == ENOTDIR) refuse(Code::invalid_file, "deployment leaf is not a real directory");
        refuse(Code::io_failure, "deployment directory open failed");
    }
    struct stat info{};
    if (::fstat(directory.value, &info) != 0) refuse(Code::io_failure, "deployment directory stat failed");
    if (info.st_uid != ::geteuid() || (info.st_mode & 0022) != 0)
        refuse(Code::invalid_file, "deployment directory must be owned and not writable by other users");
    int result;
    do { result = ::flock(directory.value, LOCK_EX | LOCK_NB); } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            refuse(Code::directory_owned, "deployment directory already owned");
        refuse(Code::io_failure, "deployment directory lock unsupported or failed");
    }
    return directory;
}

void verify_name(int parent, const std::filesystem::path& leaf, int directory) {
    struct stat opened{}, named{};
    if (::fstat(directory, &opened) != 0 ||
        ::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(named.st_mode) || opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
        refuse(Code::invalid_file, "deployment directory name changed during startup");
}

constexpr const char* metadata_name = "v2-metadata.bin";
constexpr const char* metadata_temporary = "v2-metadata.bin.tmp";
constexpr const char* selector_name = "v2-active-generation.bin";
constexpr const char* selector_temp = "v2-active-generation.bin.tmp";
constexpr const char* generations_name = "v2-generations";

void check_root_contents(int directory, bool must_be_empty) {
    Fd scan(::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (scan.value < 0) refuse(Code::io_failure, "deployment inventory open failed");
    DIR* raw = ::fdopendir(scan.value);
    if (!raw) refuse(Code::io_failure, "deployment inventory failed");
    (void)scan.release();
    std::unique_ptr<DIR, decltype(&::closedir)> entries(raw, ::closedir);
    while (true) {
        errno = 0;
        const auto* entry = ::readdir(entries.get());
        if (!entry) {
            if (errno != 0) refuse(Code::io_failure, "deployment inventory read failed");
            break;
        }
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        if (must_be_empty || (name != kStaticIdentityFileName && name != selector_name &&
                             name != selector_temp && name != generations_name && name != "v2-archive"))
            refuse(Code::unexpected_contents, "startup refuses nonempty bootstrap or legacy root contents");
    }
}

VerifiedStaticIdentity read_expected(int directory, const char* name,
                                     const StaticIdentity& expected, bool resync) {
    Fd input(::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (input.value < 0) refuse(Code::invalid_file, "identity file missing or inaccessible");
    struct stat info{};
    if (::fstat(input.value, &info) != 0) refuse(Code::io_failure, "identity file stat failed");
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0 ||
        info.st_size > static_cast<off_t>(kMaxStaticIdentityBytes))
        refuse(Code::invalid_file, "identity must be a bounded single-link regular file");
    std::array<std::byte, kMaxStaticIdentityBytes + 1> bytes{};
    std::size_t count{};
    while (count < bytes.size()) {
        const auto read = ::read(input.value, bytes.data() + count, bytes.size() - count);
        if (read < 0) {
            if (errno == EINTR) continue;
            refuse(Code::io_failure, "identity read failed");
        }
        if (read == 0) break;
        count += static_cast<std::size_t>(read);
    }
    VerifiedStaticIdentity identity;
    if (VerifiedStaticIdentity::decode(std::span{bytes}.first(count), identity) !=
        StaticIdentityCodecError::none)
        refuse(Code::invalid_file, "identity frame invalid or unsupported");
    if (identity.value() != expected) refuse(Code::identity_mismatch, "stored identity differs from requested lineage or voter");
    if (resync) sync(input.value);
    return identity;
}

void write_all(int fd, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto count = ::write(fd, bytes.data(), bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) refuse(Code::io_failure, "identity write made no progress");
        bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
}

void write_temporary(int directory, const StaticIdentity& value, IdentityStartupCheckpoints checkpoints) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(value, bytes);
    if (encoded.error != StaticIdentityCodecError::none)
        refuse(Code::invalid_identity, "identity encoding failed");
    Fd output(::openat(directory, kStaticIdentityTemporaryName,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (output.value < 0) refuse(Code::io_failure, "exclusive temporary identity create failed");
    checkpoints(IdentityStartupCheckpoint::identity_created);
    const auto frame = std::span{bytes}.first(encoded.bytes_written);
    if (checkpoints.observe) {
        write_all(output.value, frame.first(frame.size() / 2));
        checkpoints(IdentityStartupCheckpoint::partial_write);
        write_all(output.value, frame.subspan(frame.size() / 2));
    } else write_all(output.value, frame);
    checkpoints(IdentityStartupCheckpoint::identity_written);
    sync(output.value);
    checkpoints(IdentityStartupCheckpoint::file_synced);
    if (::close(output.release()) != 0) refuse(Code::io_failure, "identity close failed");
}

void require_zero(const PersistentMetadata& metadata) {
    if (metadata.term != 0 || metadata.voted_for != -1 || metadata.commit_index != 0)
        refuse(Code::unexpected_contents, "bootstrap refuses participating voting memory");
}

void write_initial_metadata(int directory, const VerifiedStaticIdentity& identity,
                            IdentityStartupCheckpoints checkpoints) {
    std::array<std::byte, kBoundMetadataBytes> bytes{};
    BoundFrameScratch scratch{};
    const auto encoded = encode_bound_metadata(PersistentMetadata{}, identity, bytes, scratch);
    if (encoded.error != BoundFrameError::none)
        refuse(Code::invalid_identity, "initial metadata encoding failed");
    Fd output(::openat(directory, metadata_temporary,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (output.value < 0) refuse(Code::io_failure, "exclusive initial metadata create failed");
    checkpoints(IdentityStartupCheckpoint::metadata_created);
    if (checkpoints.observe) {
        write_all(output.value, std::span{bytes}.first(bytes.size() / 2));
        checkpoints(IdentityStartupCheckpoint::metadata_partial_write);
        write_all(output.value, std::span{bytes}.subspan(bytes.size() / 2));
    } else write_all(output.value, bytes);
    checkpoints(IdentityStartupCheckpoint::metadata_written);
    sync(output.value);
    checkpoints(IdentityStartupCheckpoint::metadata_synced);
    if (::close(output.release()) != 0) refuse(Code::io_failure, "initial metadata close failed");
}

void validate_recovery_metadata(int directory, const VerifiedStaticIdentity& identity,
                                bool resync) {
    (void)read_bound_metadata_at(directory, metadata_name, identity, resync);
    struct stat info{};
    if (::fstatat(directory, metadata_temporary, &info, AT_SYMLINK_NOFOLLOW) == 0) {
        // A temp is evidence of an interrupted replacement, never a newer vote
        // to adopt. Refuse unrecognizable/foreign bytes rather than erase them.
        (void)read_bound_metadata_at(directory, metadata_temporary, identity, false);
    } else if (errno != ENOENT) refuse(Code::io_failure, "metadata temporary stat failed");
}

std::string generation_name(std::uint64_t generation) {
    std::string name(18, '0');
    name[0] = 'g'; name[1] = '-';
    constexpr std::string_view digits = "0123456789abcdef";
    for (unsigned i = 0; i != 16; ++i) name[2 + i] = digits[(generation >> (4*(15-i))) & 15];
    return name;
}

SelectedGenerationBase inspect_base(int root, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, const LocalGenerationSelector* candidate = nullptr) {
    auto scratch = std::make_unique<GenerationBaseScratch>();
    std::array<std::byte, kMaxBoundSnapshotBytes> bytes{};
    SelectedGenerationBase selected;
    const auto error = candidate ?
        read_generation_base_at(root, *candidate, identity, catalog, bytes, *scratch, selected) :
        read_selected_generation_base_at(root, identity, catalog, bytes, *scratch, selected);
    if (error != GenerationBaseFileError::none)
        refuse(Code::invalid_file, "generation selector or immutable base is missing, foreign or invalid");
    return selected;
}

void require_absent(int directory, const char* name) {
    struct stat info{};
    if (::fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0)
        refuse(Code::unexpected_contents, "fresh publication refuses an existing history or selector entry");
    if (errno != ENOENT) refuse(Code::io_failure, "fresh generation inventory failed");
}

void verify_genesis(int root, int runtime, const VerifiedStaticIdentity& identity,
                    const VerifiedExecutionCatalog& catalog, const LocalGenerationSelector& selection) {
    const auto base = inspect_base(root, identity, catalog, &selection);
    if (base.base.kind != GenerationBaseKind::genesis)
        refuse(Code::invalid_file, "fresh generation must have a genesis base");
    require_zero(read_bound_metadata_at(runtime, metadata_name, identity));
    for (const auto* name : {"v2-wal-blocks.bin", "v2-wal.bin", "v2-snapshot.bin"})
        require_absent(runtime, name);
}

void validate_runtime_floor(const std::filesystem::path& runtime,
    const VerifiedStaticIdentity& identity, const VerifiedExecutionCatalog& catalog,
    const GenerationBase& base) {
    if (base.kind != GenerationBaseKind::snapshot) return;
    const auto snapshot = SnapshotStore(runtime, identity).load();
    if (!snapshot || snapshot->last_included_index < base.index ||
        snapshot->last_included_term < base.term ||
        (snapshot->last_included_index == base.index &&
         (snapshot->last_included_term != base.term || snapshot->state_digest != base.state_digest)))
        refuse(Code::invalid_file, "runtime snapshot is missing, corrupt or below the immutable base floor");
    if (!execution_catalog_bindings_valid(snapshot->state, catalog))
        refuse(Code::catalog_mismatch, "runtime snapshot catalog dependency unavailable");
    if (BoundMetadataStore(runtime, identity).load().term < snapshot->last_included_term)
        refuse(Code::invalid_file, "hard state term is behind runtime snapshot");
}

void sync_file(int directory, const char* name, bool optional = false) {
    Fd input(::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (input.value < 0) {
        if (optional && errno == ENOENT) return;
        refuse(Code::io_failure, "generation persistence file open failed");
    }
    struct stat info{};
    if (::fstat(input.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != ::geteuid() || (info.st_mode & 0022) != 0)
        refuse(Code::invalid_file, "generation persistence file is not an owned regular file");
    sync(input.value);
    if (::close(input.release()) != 0) refuse(Code::io_failure, "generation persistence close failed");
}

void write_seal(int runtime, std::span<const std::byte> bytes) {
    Fd output(::openat(runtime, "v2-base-seal.bin", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (output.value < 0) refuse(Code::io_failure, "exclusive base seal creation failed");
    write_all(output.value, bytes);
    sync(output.value);
    if (::close(output.release()) != 0) refuse(Code::io_failure, "base seal close failed");
}

void write_selector(int root, const LocalGenerationSelector& selection,
    const VerifiedStaticIdentity& identity, IdentityStartupCheckpoints checkpoints) {
    std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
    if (encode_local_generation_selector(selection, identity, bytes).error != LocalGenerationError::none)
        refuse(Code::invalid_identity, "initial selector encoding failed");
    Fd output(::openat(root, selector_temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (output.value < 0) refuse(Code::io_failure, "exclusive temporary selector creation failed");
    checkpoints(IdentityStartupCheckpoint::selector_created);
    write_all(output.value, std::span(bytes).first(bytes.size() / 2));
    checkpoints(IdentityStartupCheckpoint::selector_partial_write);
    write_all(output.value, std::span(bytes).subspan(bytes.size() / 2));
    checkpoints(IdentityStartupCheckpoint::selector_written);
    sync(output.value);
    checkpoints(IdentityStartupCheckpoint::selector_synced);
    if (::close(output.release()) != 0) refuse(Code::io_failure, "selector close failed");
}
} // namespace

VerifiedIdentityStartup::VerifiedIdentityStartup(std::filesystem::path root, std::filesystem::path directory,
    VerifiedStaticIdentity identity, VerifiedExecutionCatalog catalog, LocalGenerationSelector selection)
    : root_(std::move(root)), directory_(std::move(directory)), identity_(std::move(identity)),
      catalog_(std::move(catalog)), selection_(selection) {}
VerifiedIdentityStartup::~VerifiedIdentityStartup() {
    if (directory_fd_ >= 0) ::close(directory_fd_);
    if (generations_fd_ >= 0) ::close(generations_fd_);
    if (root_fd_ >= 0) ::close(root_fd_);
}
VerifiedIdentityStartup::VerifiedIdentityStartup(VerifiedIdentityStartup&& other) noexcept
    : root_fd_(std::exchange(other.root_fd_, -1)),
      generations_fd_(std::exchange(other.generations_fd_, -1)),
      directory_fd_(std::exchange(other.directory_fd_, -1)), root_(std::move(other.root_)),
      directory_(std::move(other.directory_)), identity_(other.identity_), catalog_(other.catalog_),
      selection_(other.selection_) {}
SelectorTemporaryStatus VerifiedIdentityStartup::selector_temporary_status() const {
    if (!owns_directory()) refuse(Code::invalid_identity, "startup token no longer owns a deployment directory");
    SelectorTemporaryStatus status{};
    if (inspect_selector_temporary_at(root_fd_, identity_, status) != GenerationBaseFileError::none)
        refuse(Code::io_failure, "temporary selector inspection failed");
    return status;
}
void VerifiedIdentityStartup::validate_ownership() const {
    if (!owns_directory() || !identity_.valid())
        refuse(Code::invalid_identity, "startup token no longer owns a deployment directory");
    auto parent = open_parent(root_, false);
    verify_name(parent.value, root_.filename(), root_fd_);
    verify_name(root_fd_, generations_name, generations_fd_);
    verify_name(generations_fd_, directory_.filename(), directory_fd_);
    check_root_contents(root_fd_, false);
    // The named child is allowed only with its complete dependency/object
    // validation. A directory-name allowlist alone is not archive ownership.
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::array<std::byte, kMaxExecutionCatalogBytes> catalog_bytes{};
    const auto encoded_identity = encode_static_identity(identity_.value(), identity_bytes);
    const auto encoded_catalog = catalog_.encode(catalog_bytes);
    bool archive_present{};
    if (encoded_identity.error != StaticIdentityCodecError::none ||
        encoded_catalog.error != ExecutionCatalogError::none ||
        ArchiveStore::verify_existing(root_, identity_,
            std::span(identity_bytes).first(encoded_identity.bytes_written),
            std::span(catalog_bytes).first(encoded_catalog.bytes_written), catalog_, {}, archive_present) !=
                ArchiveStoreError::none)
        refuse(Code::invalid_file, "archive owner or historical coverage validation failed");
    (void)selector_temporary_status();
    const auto selected = inspect_base(root_fd_, identity_, catalog_);
    if (selected.selector != selection_)
        refuse(Code::invalid_file, "startup token names a superseded storage generation");
    validate_recovery_metadata(directory_fd_, identity_, false);
    validate_runtime_floor(directory_, identity_, catalog_, selected.base);
}
VerifiedIdentityStartup VerifiedIdentityStartup::bootstrap(const std::filesystem::path& directory,
    const StaticIdentity& expected, const VerifiedExecutionCatalog& catalog,
    IdentityStartupCheckpoints checkpoints) {
    const auto identity = validate_expected(expected, catalog);
    const auto path = normalized(directory);
    auto parent = open_parent(path, true);
    if (::mkdirat(parent.value, path.filename().c_str(), 0700) != 0 && errno != EEXIST)
        refuse(Code::io_failure, "deployment directory creation failed");
    auto owner = open_owner(parent.value, path.filename());
    verify_name(parent.value, path.filename(), owner.value);
    check_root_contents(owner.value, true);
    sync(parent.value);
    checkpoints(IdentityStartupCheckpoint::parent_synced);
    if (::mkdirat(owner.value, generations_name, 0700) != 0)
        refuse(Code::io_failure, "exclusive generations directory creation failed");
    auto generations = open_owner(owner.value, generations_name);
    const auto name = generation_name(1);
    if (::mkdirat(generations.value, name.c_str(), 0700) != 0)
        refuse(Code::io_failure, "exclusive generation creation failed");
    auto runtime = open_owner(generations.value, name);
    checkpoints(IdentityStartupCheckpoint::generation_created);
    if (::mkdirat(runtime.value, "base", 0700) != 0)
        refuse(Code::io_failure, "exclusive immutable base creation failed");
    auto base = open_owner(runtime.value, "base");
    checkpoints(IdentityStartupCheckpoint::base_created);
    write_initial_metadata(runtime.value, identity, checkpoints);
    if (::renameat(runtime.value, metadata_temporary, runtime.value, metadata_name) != 0)
        refuse(Code::io_failure, "initial metadata rename failed");
    checkpoints(IdentityStartupCheckpoint::metadata_renamed);
    sync(runtime.value);
    checkpoints(IdentityStartupCheckpoint::metadata_directory_synced);
    write_temporary(owner.value, expected, checkpoints);
    if (::renameat(owner.value, kStaticIdentityTemporaryName, owner.value, kStaticIdentityFileName) != 0)
        refuse(Code::io_failure, "identity publication rename failed");
    checkpoints(IdentityStartupCheckpoint::identity_renamed);
    sync(owner.value);
    checkpoints(IdentityStartupCheckpoint::directory_synced);
    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    auto scratch = std::make_unique<GenerationBaseScratch>();
    const auto encoded = encode_generation_base(GenerationBaseKind::genesis, 1, identity, catalog, {}, seal, *scratch);
    if (encoded.error != GenerationBaseError::none) refuse(Code::invalid_identity, "genesis base encoding failed");
    // The seal is last: every dependency is durable before its name can certify a base.
    sync(base.value);
    sync(runtime.value);
    sync(generations.value);
    sync(owner.value);
    write_seal(runtime.value, seal);
    sync(runtime.value);
    sync(generations.value);
    checkpoints(IdentityStartupCheckpoint::base_sealed);
    const LocalGenerationSelector selection{1, 1, 0, encoded.base.digest};
    verify_genesis(owner.value, runtime.value, identity, catalog, selection);
    write_selector(owner.value, selection, identity, checkpoints);
    verify_name(parent.value, path.filename(), owner.value);
    verify_name(owner.value, generations_name, generations.value);
    verify_name(generations.value, name, runtime.value);
    verify_genesis(owner.value, runtime.value, identity, catalog, selection);
    require_absent(owner.value, selector_name);
    if (::renameat(owner.value, selector_temp, owner.value, selector_name) != 0)
        refuse(Code::io_failure, "initial selector publication failed");
    checkpoints(IdentityStartupCheckpoint::selector_renamed);
    sync(owner.value);
    checkpoints(IdentityStartupCheckpoint::selector_directory_synced);
    VerifiedIdentityStartup result(path, path / generations_name / name, identity, catalog, selection);
    result.root_fd_ = owner.release();
    result.generations_fd_ = generations.release();
    result.directory_fd_ = runtime.release();
    result.validate_ownership();
    checkpoints(IdentityStartupCheckpoint::selection_verified);
    return result;
}
VerifiedIdentityStartup VerifiedIdentityStartup::recover(const std::filesystem::path& directory,
    const StaticIdentity& expected, const VerifiedExecutionCatalog& catalog,
    IdentityStartupCheckpoints checkpoints) {
    (void)validate_expected(expected, catalog);
    const auto path = normalized(directory);
    auto parent = open_parent(path, false);
    auto owner = open_owner(parent.value, path.filename());
    verify_name(parent.value, path.filename(), owner.value);
    check_root_contents(owner.value, false);
    const auto identity = read_expected(owner.value, kStaticIdentityFileName, expected, false);
    const auto selected = inspect_base(owner.value, identity, catalog);
    auto generations = open_owner(owner.value, generations_name);
    const auto name = generation_name(selected.selector.generation);
    auto runtime = open_owner(generations.value, name);
    const auto runtime_path = path / generations_name / name;
    validate_recovery_metadata(runtime.value, identity, false);
    validate_runtime_floor(runtime_path, identity, catalog, selected.base);
    // Re-establish durability after an interrupted rename; never promote a temp.
    (void)read_bound_metadata_at(runtime.value, metadata_name, identity, true);
    sync_file(runtime.value, "v2-wal-blocks.bin", true);
    sync_file(runtime.value, "v2-snapshot.bin", true);
    auto base = open_owner(runtime.value, "base");
    if (selected.base.kind == GenerationBaseKind::snapshot) sync_file(base.value, "v2-snapshot.bin");
    sync(base.value);
    sync_file(runtime.value, "v2-base-seal.bin");
    sync(runtime.value);
    sync(generations.value);
    (void)read_expected(owner.value, kStaticIdentityFileName, expected, true);
    sync_file(owner.value, selector_name);
    checkpoints(IdentityStartupCheckpoint::file_synced);
    sync(owner.value);
    sync(parent.value);
    checkpoints(IdentityStartupCheckpoint::directory_synced);
    verify_name(parent.value, path.filename(), owner.value);
    VerifiedIdentityStartup result(path, runtime_path, identity, catalog, selected.selector);
    result.root_fd_ = owner.release();
    result.generations_fd_ = generations.release();
    result.directory_fd_ = runtime.release();
    result.validate_ownership();
    checkpoints(IdentityStartupCheckpoint::selection_verified);
    return result;
}
} // namespace velocita::v2
