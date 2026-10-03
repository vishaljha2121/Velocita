#include "velocita/v2/archive_store.hpp"

#include "velocita/v2/archive_file.hpp"
#include "velocita/v2/wal_arena.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace velocita::v2 {
namespace {
constexpr char archive_name[] = "v2-archive";
constexpr char identity_name[] = "v2-identity.bin";
constexpr char execution_name[] = "v2-execution-catalog.bin";
constexpr char catalog_name[] = "v2-archive-catalog.bin";
constexpr char catalog_temp[] = "v2-archive-catalog.bin.tmp";
constexpr char segment_temp[] = "v2-archive-segment.bin.tmp";

bool sync_fd(int fd) noexcept {
  while (::fsync(fd) != 0) {
    if (errno != EINTR)
      return false;
  }
  return true;
}
// POSIX permits close(EINTR) to have already released the descriptor. Retrying
// can close an unrelated descriptor after reuse, so close is attempted once.
bool close_fd(int fd) noexcept { return ::close(fd) == 0; }
struct File {
  int fd{-1};
  explicit File(int value) noexcept : fd(value) {}
  ~File() {
    if (fd >= 0)
      (void)::close(fd);
  }
  File(const File &) = delete;
  File &operator=(const File &) = delete;
  bool close() noexcept {
    const int value = fd;
    fd = -1;
    return close_fd(value);
  }
};

struct DirectoryListing {
  DIR *directory{};
  explicit DirectoryListing(int fd) noexcept : directory(::fdopendir(fd)) {
    if (directory == nullptr && fd >= 0)
      (void)::close(fd);
  }
  ~DirectoryListing() {
    if (directory != nullptr)
      (void)::closedir(directory);
  }
  DirectoryListing(const DirectoryListing &) = delete;
  DirectoryListing &operator=(const DirectoryListing &) = delete;
};
bool write_all(int fd, std::span<const std::byte> bytes) noexcept {
  while (!bytes.empty()) {
    const auto n = ::write(fd, bytes.data(), bytes.size());
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    bytes = bytes.subspan(static_cast<std::size_t>(n));
  }
  return true;
}
bool good_file_stat(const struct stat &s, std::size_t exact = 0) noexcept {
  return S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == ::geteuid() &&
           (s.st_mode & 07777) == 0600 &&
         (!exact || static_cast<std::uintmax_t>(s.st_size) == exact);
}
bool read_exact_file(int dir, const char *name, std::span<std::byte> out,
                     std::size_t &length, std::size_t maximum) noexcept {
  int fd;
  do {
    fd = ::openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  File file(fd);
  if (file.fd < 0)
    return false;
  struct stat s{};
  if (::fstat(file.fd, &s) != 0 || !good_file_stat(s) || s.st_size < 0 ||
      static_cast<std::uintmax_t>(s.st_size) > maximum ||
      static_cast<std::uintmax_t>(s.st_size) > out.size()) {
    return false;
  }
  length = static_cast<std::size_t>(s.st_size);
  std::size_t p = 0;
  while (p < length) {
    auto n = ::read(file.fd, out.data() + p, length - p);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      return false;
    }
    p += static_cast<std::size_t>(n);
  }
  std::byte extra{};
  ssize_t n;
  do {
    n = ::read(file.fd, &extra, 1);
  } while (n < 0 && errno == EINTR);
  return n == 0 && file.close();
}
bool publish_fixed(int dir, const char *name,
                   std::span<const std::byte> bytes) noexcept {
  File file(::openat(
      dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  return file.fd >= 0 && write_all(file.fd, bytes) && sync_fd(file.fd) &&
         file.close();
}
std::string segment_name_for(const Digest256 &digest) {
  return "s-" + to_hex(digest) + ".bin";
}
bool no_replace(int dir, const char *from, const char *to) noexcept {
#if defined(__APPLE__)
  return ::renameatx_np(dir, from, dir, to, RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2)
  return ::syscall(SYS_renameat2, dir, from, dir, to, RENAME_NOREPLACE) == 0;
#else
  errno = ENOTSUP;
  return false;
#endif
}
bool expected_segment_name(std::string_view name, const Digest256 &digest) {
  return name == segment_name_for(digest);
}
bool terminal_digest(std::span<const std::byte> bytes, Digest256 &out) {
  if (bytes.size() < 64)
    return false;
  std::copy_n(bytes.end() - 64, 32, out.bytes.begin());
  return true;
}
bool owned_segment_name(int directory, const std::string &name) noexcept {
  struct stat info{};
  return ::fstatat(directory, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 &&
         S_ISREG(info.st_mode) && info.st_nlink == 1 &&
         info.st_uid == ::geteuid() && (info.st_mode & 07777) == 0600;
}
bool owned_segment_of_length(int directory, const std::string &name,
                             std::uint32_t length) noexcept {
  struct stat info{};
  return ::fstatat(directory, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 &&
         good_file_stat(info, length);
}
bool resync_name(int directory, const std::string &name) noexcept {
  int fd;
  do {
    fd = ::openat(directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  File file(fd);
  if (file.fd < 0)
    return false;
  return sync_fd(file.fd) && file.close();
}
struct SourceSlice {
  const ArchiveRecordSource *source{};
  std::uint32_t offset{};
  static bool read(void *context, std::uint32_t ordinal,
                   ArchiveRecordView &destination) noexcept {
    auto &self = *static_cast<SourceSlice *>(context);
    return self.source->record_at(self.source->context, self.offset + ordinal,
                                  destination);
  }
};
struct EncodedCandidate {
  std::unique_ptr<std::array<std::byte, kMaxArchiveSegmentBytes>> bytes;
  std::size_t length{};
  ArchiveSegmentMetadata metadata{};
  Digest256 terminal{};
};
struct WalBridgeConflict final {};
} // namespace

struct ArchiveStore::State {
  int root{-1}, directory{-1};
  VerifiedStaticIdentity identity{};
  ArchiveStoreConfig config{};
  ArchiveCatalogHeader header{};
  std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows> rows{};
  std::size_t count{};
  Digest256 catalog_digest{};
  std::uint64_t logical_bytes{};
  // Next catalog row the rotating scrub rereads (verify_incremental).
  std::size_t scrub_next{};
  std::vector<std::byte> identity_bytes;
  std::vector<std::byte> execution_bytes;
  bool failed{};
  ~State() {
    if (directory >= 0) {
      (void)::flock(directory, LOCK_UN);
      (void)close_fd(directory);
    }
    if (root >= 0)
      (void)close_fd(root);
  }
};

ArchiveStore::ArchiveStore() noexcept = default;
ArchiveStore::ArchiveStore(std::unique_ptr<State> s) noexcept
    : state_(std::move(s)) {}
ArchiveStore::~ArchiveStore() = default;
ArchiveStore::ArchiveStore(ArchiveStore &&) noexcept = default;
ArchiveStore &ArchiveStore::operator=(ArchiveStore &&) noexcept = default;
ArchiveCoverageCapability::ArchiveCoverageCapability(
    ArchiveCoverageCapability &&other) noexcept
    : valid_(other.valid_), storage_generation_(other.storage_generation_),
      catalog_generation_(other.catalog_generation_),
      last_index_(other.last_index_), last_term_(other.last_term_),
      manifest_digest_(other.manifest_digest_),
      catalog_digest_(other.catalog_digest_),
      terminal_record_digest_(other.terminal_record_digest_) {
  other.valid_ = false;
}
ArchiveCoverageCapability &ArchiveCoverageCapability::operator=(
    ArchiveCoverageCapability &&other) noexcept {
  if (this != &other) {
    valid_ = other.valid_;
    storage_generation_ = other.storage_generation_;
    catalog_generation_ = other.catalog_generation_;
    last_index_ = other.last_index_;
    last_term_ = other.last_term_;
    manifest_digest_ = other.manifest_digest_;
    catalog_digest_ = other.catalog_digest_;
    terminal_record_digest_ = other.terminal_record_digest_;
    other.valid_ = false;
  }
  return *this;
}

ArchiveStoreError ArchiveStore::verify_existing(
    const std::filesystem::path &path, const VerifiedStaticIdentity &identity,
    std::span<const std::byte> identity_bytes,
    std::span<const std::byte> execution_bytes,
    const VerifiedExecutionCatalog &execution, const ArchiveStoreConfig &config,
    bool &present) noexcept {
  try {
    present = false;
    File root(
        ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (root.fd < 0)
      return ArchiveStoreError::ownership_failed;
    struct stat info{};
    const int found =
        ::fstatat(root.fd, archive_name, &info, AT_SYMLINK_NOFOLLOW);
    const int saved = errno;
    if (!root.close())
      return ArchiveStoreError::ownership_failed;
    if (found != 0) {
      if (saved == ENOENT)
        return ArchiveStoreError::none;
      return ArchiveStoreError::ownership_failed;
    }
    if (!S_ISDIR(info.st_mode))
      return ArchiveStoreError::ownership_failed;
    ArchiveStore temporary;
    const auto error = open(path, identity, identity_bytes, execution_bytes,
                            execution, config, temporary);
    if (error == ArchiveStoreError::none)
      present = true;
    return error;
  } catch (...) {
    present = false;
    return ArchiveStoreError::ownership_failed;
  }
}

ArchiveStoreError ArchiveStore::open(const std::filesystem::path &path,
                                     const VerifiedStaticIdentity &identity,
                                     std::span<const std::byte> identity_bytes,
                                     std::span<const std::byte> execution_bytes,
                                     const VerifiedExecutionCatalog &execution,
                                     const ArchiveStoreConfig &config,
                                     ArchiveStore &destination) noexcept {
  try {
    if (destination.state_ || config.object_limit == 0 ||
        config.object_limit > kMaxArchiveCatalogRows ||
        config.logical_byte_limit == 0 || identity_bytes.empty() ||
        execution_bytes.empty())
      return ArchiveStoreError::invalid_argument;
    VerifiedStaticIdentity check_identity;
    VerifiedExecutionCatalog check_execution;
    if (VerifiedStaticIdentity::decode(identity_bytes, check_identity) !=
            StaticIdentityCodecError::none ||
        check_identity.value() != identity.value() || !identity.valid() ||
        identity.value().catalog_root != execution.root() ||
        VerifiedExecutionCatalog::decode(execution_bytes, execution.root(),
                                         check_execution) !=
            ExecutionCatalogError::none ||
        check_execution.root() != execution.root())
      return ArchiveStoreError::dependency_failed;
    auto s = std::make_unique<State>();
    s->identity = identity;
    s->config = config;
    s->identity_bytes.assign(identity_bytes.begin(), identity_bytes.end());
    s->execution_bytes.assign(execution_bytes.begin(), execution_bytes.end());
    s->root =
        ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (s->root < 0)
      return ArchiveStoreError::ownership_failed;
    struct stat rs{};
    if (::fstat(s->root, &rs) != 0 || !S_ISDIR(rs.st_mode) ||
        rs.st_uid != ::geteuid())
      return ArchiveStoreError::ownership_failed;
    bool created = false;
    if (::mkdirat(s->root, archive_name, 0700) == 0)
      created = true;
    else if (errno != EEXIST)
      return ArchiveStoreError::ownership_failed;
    s->directory = ::openat(s->root, archive_name,
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat ds{};
    if (s->directory < 0 || ::fstat(s->directory, &ds) != 0 ||
        !S_ISDIR(ds.st_mode) || ds.st_uid != ::geteuid() ||
        (ds.st_mode & 0777) != 0700)
      return ArchiveStoreError::ownership_failed;
    if (::flock(s->directory, LOCK_EX | LOCK_NB) != 0)
      return ArchiveStoreError::lock_failed;
    if (created) {
      if (!publish_fixed(s->directory, identity_name, identity_bytes) ||
          !publish_fixed(s->directory, execution_name, execution_bytes) ||
          !sync_fd(s->directory) || !sync_fd(s->root))
        return ArchiveStoreError::publication_failed;
    }
    std::array<std::byte, kMaxExecutionCatalogBytes> dep{};
    std::size_t n{};
    if (!read_exact_file(s->directory, identity_name, dep, n,
                         kMaxStaticIdentityBytes) ||
        n != identity_bytes.size() ||
        !std::equal(dep.begin(), dep.begin() + n, identity_bytes.begin()))
      return ArchiveStoreError::dependency_failed;
    if (!read_exact_file(s->directory, execution_name, dep, n,
                         kMaxExecutionCatalogBytes) ||
        n != execution_bytes.size() ||
        !std::equal(dep.begin(), dep.begin() + n, execution_bytes.begin()))
      return ArchiveStoreError::dependency_failed;

    auto catalog_owner =
        std::make_unique<std::array<std::byte, kMaxArchiveCatalogBytes>>();
    auto &catalog = *catalog_owner;
    std::size_t catalog_len{};
    const bool has_catalog =
        read_exact_file(s->directory, catalog_name, catalog, catalog_len,
                        kMaxArchiveCatalogBytes);
    struct stat cs{};
    if (!has_catalog &&
        (::fstatat(s->directory, catalog_name, &cs, AT_SYMLINK_NOFOLLOW) == 0 ||
         errno != ENOENT))
      return ArchiveStoreError::catalog_failed;
    if (has_catalog) {
      if (decode_archive_catalog(std::span{catalog}.first(catalog_len),
                                 identity, s->header, s->rows,
                                 s->count) != ArchiveCatalogError::none ||
          s->count > config.object_limit)
        return ArchiveStoreError::catalog_failed;
      std::copy_n(catalog.begin() + catalog_len - 32, 32,
                  s->catalog_digest.bytes.begin());
    }
    std::vector<std::string> names;
    DirectoryListing listing(::dup(s->directory));
    if (listing.directory == nullptr)
      return ArchiveStoreError::ownership_failed;
    errno = 0;
    while (auto *e = ::readdir(listing.directory)) {
      std::string name = e->d_name;
      if (name == "." || name == "..")
        continue;
      if (names.size() >= kMaxArchiveDirectoryEntries) {
        return ArchiveStoreError::unexpected_entry;
      }
      names.push_back(std::move(name));
    }
    if (errno != 0)
      return ArchiveStoreError::ownership_failed;
    for (const auto &name : names) {
      bool known = name == identity_name || name == execution_name ||
                   (has_catalog && name == catalog_name);
      if (!known) {
        for (std::size_t i = 0; i < s->count && !known; ++i)
          known = expected_segment_name(name, s->rows[i].segment_digest);
      }
      if (!known)
        return ArchiveStoreError::unexpected_entry;
    }
    auto buffer =
        std::make_unique<std::array<std::byte, kMaxArchiveSegmentBytes>>();
    ArchiveSegmentScratch scratch{};
    for (std::size_t i = 0; i < s->count; ++i) {
      const auto name = segment_name_for(s->rows[i].segment_digest);
      ArchiveSegmentMetadata m{};
      if (!owned_segment_name(s->directory, name))
        return ArchiveStoreError::segment_failed;
      auto rr = read_archive_segment_at(s->directory, name, identity, *buffer,
                                        scratch, m);
      if (rr.error != ArchiveFileError::none ||
          m.first_index != s->rows[i].first_index ||
          m.last_index != s->rows[i].last_index ||
          m.predecessor_term != s->rows[i].predecessor_term ||
          m.last_term != s->rows[i].last_term ||
          m.record_count != s->rows[i].record_count ||
          m.total_bytes != s->rows[i].segment_file_length ||
          m.segment_digest != s->rows[i].segment_digest)
        return ArchiveStoreError::segment_failed;
      Digest256 td{};
      if (!terminal_digest(std::span{*buffer}.first(rr.bytes_read), td) ||
          td != s->rows[i].terminal_record_digest ||
          !owned_segment_name(s->directory, name) ||
          !resync_name(s->directory, name))
        return ArchiveStoreError::segment_failed;
      s->logical_bytes += m.total_bytes;
    }
    if (s->logical_bytes > config.logical_byte_limit)
      return ArchiveStoreError::capacity_exceeded;
    if (!resync_name(s->directory, identity_name) ||
        !resync_name(s->directory, execution_name) ||
        (has_catalog && !resync_name(s->directory, catalog_name)) ||
        !sync_fd(s->directory))
      return ArchiveStoreError::publication_failed;
    destination = ArchiveStore(std::move(s));
    return ArchiveStoreError::none;
  } catch (...) {
    return ArchiveStoreError::publication_failed;
  }
}

// Everything about the archive that can be checked without reading segment
// contents: the directory is still ours, the dependency files and the catalog
// on disk equal what this store holds, the directory holds exactly the
// catalog's files, and every catalogued segment is an owned regular file of the
// catalogued length. Its cost is one stat per segment, not one read and fsync.
ArchiveStoreError ArchiveStore::verify_layout() noexcept {
  try {
    if (!state_) return ArchiveStoreError::invalid_argument;
    auto& s = *state_;
    struct stat named{}, held{};
    if (::fstatat(s.root, archive_name, &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        ::fstat(s.directory, &held) != 0 || !S_ISDIR(named.st_mode) ||
        named.st_dev != held.st_dev || named.st_ino != held.st_ino ||
        held.st_uid != ::geteuid() || (held.st_mode & 07777) != 0700)
      return ArchiveStoreError::ownership_failed;
    std::array<std::byte, kMaxExecutionCatalogBytes> dependency{};
    std::size_t length{};
    if (!read_exact_file(s.directory, identity_name, dependency, length, kMaxStaticIdentityBytes) ||
        !std::ranges::equal(std::span(dependency).first(length), s.identity_bytes) ||
        !read_exact_file(s.directory, execution_name, dependency, length, kMaxExecutionCatalogBytes) ||
        !std::ranges::equal(std::span(dependency).first(length), s.execution_bytes))
      return ArchiveStoreError::dependency_failed;
    auto bytes = std::make_unique<std::array<std::byte, kMaxArchiveCatalogBytes>>();
    if (s.header.generation) {
      auto rows = std::make_unique<std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows>>();
      ArchiveCatalogHeader header;
      std::size_t count{};
      if (!read_exact_file(s.directory, catalog_name, *bytes, length, kMaxArchiveCatalogBytes) ||
          decode_archive_catalog(std::span(*bytes).first(length), s.identity, header, *rows, count) !=
              ArchiveCatalogError::none || header != s.header || count != s.count ||
          !std::equal(rows->begin(), rows->begin() + count, s.rows.begin()) ||
          !std::equal(bytes->begin() + length - 32, bytes->begin() + length, s.catalog_digest.bytes.begin()))
        return ArchiveStoreError::catalog_failed;
    }
    std::vector<std::string> allowed{identity_name, execution_name};
    if (s.header.generation) allowed.emplace_back(catalog_name);
    for (std::size_t i = 0; i < s.count; ++i) allowed.push_back(segment_name_for(s.rows[i].segment_digest));
    std::sort(allowed.begin(), allowed.end());
    DirectoryListing listing(::openat(s.directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!listing.directory) return ArchiveStoreError::ownership_failed;
    std::size_t names{};
    for (;;) {
      errno = 0;
      const auto* entry = ::readdir(listing.directory);
      if (!entry) { if (errno) return ArchiveStoreError::ownership_failed; break; }
      const std::string_view name{entry->d_name};
      if (name == "." || name == "..") continue;
      if (++names >= kMaxArchiveDirectoryEntries || !std::binary_search(allowed.begin(), allowed.end(), name))
        return ArchiveStoreError::unexpected_entry;
    }
    if (names != allowed.size()) return ArchiveStoreError::unexpected_entry;
    for (std::size_t i = 0; i < s.count; ++i)
      if (!owned_segment_of_length(s.directory, segment_name_for(s.rows[i].segment_digest),
                                   s.rows[i].segment_file_length))
        return ArchiveStoreError::segment_failed;
    if (!resync_name(s.directory, identity_name) || !resync_name(s.directory, execution_name) ||
        (s.header.generation && !resync_name(s.directory, catalog_name)) ||
        !sync_fd(s.directory) || !sync_fd(s.root)) return ArchiveStoreError::publication_failed;
    return ArchiveStoreError::none;
  } catch (...) { return ArchiveStoreError::publication_failed; }
}

// Rereads COUNT catalogued segments starting at row FIRST (wrapping), checks
// each against its catalog row and digests, and resyncs it.
ArchiveStoreError ArchiveStore::verify_segments(const std::size_t first, const std::size_t count) noexcept {
  try {
    if (!state_) return ArchiveStoreError::invalid_argument;
    auto& s = *state_;
    if (count > s.count) return ArchiveStoreError::invalid_argument;
    auto segment = std::make_unique<std::array<std::byte, kMaxArchiveSegmentBytes>>();
    ArchiveSegmentScratch scratch;
    for (std::size_t n = 0; n < count; ++n) {
      const auto& row = s.rows[(first + n) % s.count];
      const auto name = segment_name_for(row.segment_digest);
      ArchiveSegmentMetadata metadata;
      if (!owned_segment_name(s.directory, name)) return ArchiveStoreError::segment_failed;
      const auto read = read_archive_segment_at(s.directory, name, s.identity, *segment, scratch, metadata);
      Digest256 terminal;
      if (read.error != ArchiveFileError::none || metadata.first_index != row.first_index ||
          metadata.last_index != row.last_index || metadata.predecessor_term != row.predecessor_term ||
          metadata.last_term != row.last_term || metadata.record_count != row.record_count ||
          metadata.total_bytes != row.segment_file_length || metadata.segment_digest != row.segment_digest ||
          !terminal_digest(std::span(*segment).first(read.bytes_read), terminal) ||
          terminal != row.terminal_record_digest || !owned_segment_name(s.directory, name) ||
          !resync_name(s.directory, name)) return ArchiveStoreError::segment_failed;
    }
    return ArchiveStoreError::none;
  } catch (...) { return ArchiveStoreError::publication_failed; }
}

// The whole archive: layout, then every segment's bytes. Linear in the bytes
// archived, so it runs where a voter is not serving consensus (history
// recovery), never on the compaction path.
ArchiveStoreError ArchiveStore::verify_current() noexcept {
  if (const auto layout = verify_layout(); layout != ArchiveStoreError::none) return layout;
  return verify_segments(0, state_->count);
}

// The compaction path's check. Its cost does not grow with the archive: the
// layout, plus a rotating scrub that rereads kArchiveScrubSegmentsPerVerify
// segments per call, so every segment is still reread once every
// ceil(count / kArchiveScrubSegmentsPerVerify) calls (ADR 0014, 2026-10-03).
ArchiveStoreError ArchiveStore::verify_incremental() noexcept {
  if (const auto layout = verify_layout(); layout != ArchiveStoreError::none) return layout;
  auto& s = *state_;
  if (s.count == 0) return ArchiveStoreError::none;
  const auto count = std::min(kArchiveScrubSegmentsPerVerify, s.count);
  const auto first = s.scrub_next % s.count;
  if (const auto scrubbed = verify_segments(first, count); scrubbed != ArchiveStoreError::none) return scrubbed;
  s.scrub_next = (first + count) % s.count;
  return ArchiveStoreError::none;
}

ArchivePublishResult
ArchiveStore::publish(const ArchivePublicationContext &c,
                      const ArchiveRecordSource &source) noexcept {
  ArchivePublishResult result;
  try {
    if (!state_ || state_->failed || !source.record_at || source.count == 0 ||
        source.count > kMaxArchiveRecords) {
      result.error = ArchiveStoreError::invalid_argument;
      return result;
    }
    auto &s = *state_;
    if (const auto checked = verify_incremental(); checked != ArchiveStoreError::none) {
      s.failed = true;
      result.error = checked;
      return result;
    }
    if (s.header.generation == UINT64_MAX ||
        c.catalog_generation !=
            (s.header.generation ? s.header.generation + 1 : 1) ||
        c.storage_generation == 0 || c.storage_generation < s.header.publication_storage_generation ||
        c.remaining_pending_records > kMaxArchiveRecords) {
      result.error = ArchiveStoreError::stale_generation;
      return result;
    }
    if (s.count >= s.config.object_limit) {
      result.error = ArchiveStoreError::capacity_exceeded;
      return result;
    }
    std::array<std::uint32_t, 4> starts{}, counts{};
    std::size_t objects = 1;
    std::size_t estimated = 128;
    ArchiveRecordView view{};
    for (std::uint32_t i = 0; i < source.count; ++i) {
      if (!source.record_at(source.context, i, view) ||
          view.payload.size() > kMaxArchiveRecordBytes -
                                    kArchiveRecordHeaderBytes -
                                    kArchiveDigestBytes) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
      const auto add =
          kArchiveRecordHeaderBytes + view.payload.size() + kArchiveDigestBytes;
      if (estimated + add > kMaxArchiveSegmentBytes) {
        if (objects == 4) {
          result.error = ArchiveStoreError::capacity_exceeded;
          return result;
        }
        starts[objects] = i;
        ++objects;
        estimated = 128;
      }
      estimated += add;
      ++counts[objects - 1];
    }
    if (s.count + objects > s.config.object_limit) {
      result.error = ArchiveStoreError::capacity_exceeded;
      return result;
    }
    std::array<EncodedCandidate, 4> candidates{};
    ArchiveSegmentScratch scratch{};
    std::uint64_t total_bytes{};
    std::int64_t predecessor = c.predecessor_term;
    for (std::size_t i = 0; i < objects; ++i) {
      auto &x = candidates[i];
      x.bytes =
          std::make_unique<std::array<std::byte, kMaxArchiveSegmentBytes>>();
      SourceSlice slice{&source, starts[i]};
      ArchiveRecordSource part{&slice, counts[i], &SourceSlice::read};
      auto encoded = encode_archive_segment(part, predecessor, s.identity,
                                            *x.bytes, scratch);
      if (encoded.error != ArchiveSegmentError::none) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
      x.length = encoded.bytes_written;
      if (decode_archive_segment(std::span{*x.bytes}.first(x.length),
                                 s.identity, nullptr, nullptr, scratch,
                                 x.metadata)
                  .error != ArchiveSegmentError::none ||
          !terminal_digest(std::span{*x.bytes}.first(x.length), x.terminal)) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
      predecessor = x.metadata.last_term;
      total_bytes += x.length;
    }
    auto rows =
        std::make_unique<std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows>>(
            s.rows);
    const std::uint64_t expected_first =
        s.count ? s.rows[s.count - 1].last_index + 1 : 1;
    const auto &first = candidates[0].metadata;
    const auto &last = candidates[objects - 1].metadata;
    const std::uint64_t reserved_objects =
        (c.remaining_pending_records + 2262U) / 2263U;
    if (first.first_index != expected_first ||
        first.predecessor_term !=
            (s.count ? s.rows[s.count - 1].last_term : 0) ||
        last.last_index > c.committed_index ||
        last.last_index > c.durable_index || c.current_term < last.last_term ||
        total_bytes > UINT32_MAX ||
        objects + reserved_objects > s.config.object_limit - s.count ||
        !can_publish(static_cast<std::uint32_t>(total_bytes),
                     c.remaining_pending_records, true)) {
      result.error = ArchiveStoreError::capacity_exceeded;
      return result;
    }
    for (std::size_t i = 1; i < objects; ++i)
      if (candidates[i].metadata.first_index !=
              candidates[i - 1].metadata.last_index + 1 ||
          candidates[i].metadata.predecessor_term !=
              candidates[i - 1].metadata.last_term) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
    ArchiveCatalogHeader header{c.catalog_generation, c.storage_generation,
                                c.committed_index, c.durable_index,
                                c.current_term};
    auto catalog =
        std::make_unique<std::array<std::byte, kMaxArchiveCatalogBytes>>();
    for (std::size_t i = 0; i < objects; ++i) {
      const auto &m = candidates[i].metadata;
      (*rows)[s.count + i] = {m.first_index,
                              m.last_index,
                              m.predecessor_term,
                              m.last_term,
                              m.record_count,
                              static_cast<std::uint32_t>(candidates[i].length),
                              c.storage_generation,
                              m.segment_digest,
                              candidates[i].terminal};
    }
    auto ce = encode_archive_catalog(header,
                                     std::span{*rows}.first(s.count + objects),
                                     s.identity, *catalog);
    if (ce.error != ArchiveCatalogError::none) {
      result.error = ArchiveStoreError::catalog_failed;
      return result;
    }
    s.failed = true;
    for (std::size_t i = 0; i < objects; ++i) {
      auto &x = candidates[i];
      File temporary(
          ::openat(s.directory, segment_temp,
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
      if (temporary.fd < 0 ||
          !write_all(temporary.fd, std::span{*x.bytes}.first(x.length)) ||
          !sync_fd(temporary.fd) || !temporary.close()) {
        result.error = ArchiveStoreError::publication_failed;
        return result;
      }
      ArchiveSegmentMetadata staged;
      if (!owned_segment_name(s.directory, segment_temp) ||
          read_archive_segment_at(s.directory, segment_temp, s.identity, *x.bytes,
                                  scratch, staged).error != ArchiveFileError::none || staged != x.metadata) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
      const auto final = segment_name_for(x.metadata.segment_digest);
      if (!no_replace(s.directory, segment_temp, final.c_str())) {
        if (errno != EEXIST) {
          result.error = ArchiveStoreError::publication_failed;
          return result;
        }
        ArchiveSegmentMetadata existing{};
        if (!owned_segment_name(s.directory, final) ||
            read_archive_segment_at(s.directory, final, s.identity, *x.bytes,
                                    scratch, existing)
                    .error != ArchiveFileError::none ||
            existing != x.metadata) {
          result.error = ArchiveStoreError::conflict;
          return result;
        }
        (void)::unlinkat(s.directory, segment_temp, 0);
      }
      if (!sync_fd(s.directory)) {
        result.error = ArchiveStoreError::publication_failed;
        return result;
      }
      ArchiveSegmentMetadata verified{};
      if (!owned_segment_name(s.directory, final) ||
          read_archive_segment_at(s.directory, final, s.identity, *x.bytes,
                                  scratch, verified)
                  .error != ArchiveFileError::none ||
          verified != x.metadata || !owned_segment_name(s.directory, final) ||
          !resync_name(s.directory, final)) {
        result.error = ArchiveStoreError::segment_failed;
        return result;
      }
    }
    File temporary(
        ::openat(s.directory, catalog_temp,
                 O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (temporary.fd < 0 ||
        !write_all(temporary.fd, std::span{*catalog}.first(ce.bytes_written)) ||
        !sync_fd(temporary.fd) || !temporary.close() ||
        ::renameat(s.directory, catalog_temp, s.directory, catalog_name) != 0 ||
        !sync_fd(s.directory)) {
      result.error = ArchiveStoreError::publication_failed;
      return result;
    }
    auto verified = std::make_unique<
        std::array<ArchiveCatalogRow, kMaxArchiveCatalogRows>>();
    ArchiveCatalogHeader vh{};
    std::size_t vc{}, len{};
    if (!read_exact_file(s.directory, catalog_name, *catalog, len,
                         kMaxArchiveCatalogBytes) ||
        decode_archive_catalog(std::span{*catalog}.first(len), s.identity, vh,
                               *verified, vc) != ArchiveCatalogError::none ||
        vh != header || vc != s.count + objects ||
        !std::equal(verified->begin(), verified->begin() + vc, rows->begin())) {
      result.error = ArchiveStoreError::catalog_failed;
      return result;
    }
    s.rows = *rows;
    s.count = vc;
    s.header = header;
    s.catalog_digest = ce.catalog_digest;
    s.logical_bytes += total_bytes;
    // Each new segment was reread and resynced above; the layout check
    // confirms the published catalog and directory around them.
    if (const auto checked = verify_layout(); checked != ArchiveStoreError::none) {
      result.error = checked;
      return result;
    }
    s.failed = false;
    result.capability.valid_ = true;
    result.capability.storage_generation_ = c.storage_generation;
    result.capability.catalog_generation_ = c.catalog_generation;
    result.capability.last_index_ = last.last_index;
    result.capability.last_term_ = last.last_term;
    result.capability.manifest_digest_ = s.identity.manifest_digest();
    result.capability.catalog_digest_ = ce.catalog_digest;
    result.capability.terminal_record_digest_ =
        candidates[objects - 1].terminal;
    return result;
  } catch (...) {
    if (state_) state_->failed = true;
    result.error = ArchiveStoreError::publication_failed;
    return result;
  }
}

ArchiveWalBridgeResult ArchiveStore::publish_wal_bridge(
    const std::filesystem::path &runtime,
    const ArchivePublicationContext &captured, const std::uint64_t snapshot_cut,
    const std::uint64_t wal_cap) noexcept {
  ArchiveWalBridgeResult result;
  try {
    constexpr std::uint64_t max_wal_cap = 1ULL << 30;
    constexpr std::uint64_t records_per_reserved_object = 2263;
    if (!state_ || state_->failed || runtime.empty() || snapshot_cut == 0 ||
        wal_cap == 0 || wal_cap > max_wal_cap ||
        captured.storage_generation == 0 ||
        captured.committed_index < snapshot_cut ||
        captured.durable_index < snapshot_cut ||
        captured.committed_index > captured.durable_index ||
        captured.remaining_pending_records != captured.durable_index - snapshot_cut ||
        captured.remaining_pending_records > kMaxArchiveRecords)
      return {ArchiveStoreError::invalid_argument};
    auto &s = *state_;
    if (const auto checked = verify_current(); checked != ArchiveStoreError::none)
      return {checked};
    const auto covered = covered_through();
    if (covered >= snapshot_cut ||
        captured.predecessor_term != last_term() ||
        captured.catalog_generation !=
            (s.header.generation ? s.header.generation + 1 : 1) ||
        captured.storage_generation < s.header.publication_storage_generation)
      return {ArchiveStoreError::stale_generation};
    struct stat wal_info{};
    const auto wal_path = runtime / "v2-wal-blocks.bin";
    if (::lstat(wal_path.c_str(), &wal_info) != 0 ||
        !S_ISREG(wal_info.st_mode) || wal_info.st_nlink != 1 ||
        wal_info.st_uid != ::geteuid() || wal_info.st_size < 0 ||
        static_cast<std::uint64_t>(wal_info.st_size) > wal_cap)
      return {ArchiveStoreError::capacity_exceeded};

    std::uint64_t expected{};
    std::int64_t previous_term{};
    bool saw_record{};
    bool saw_first_uncovered{};
    std::uint64_t bridge_records{};
    std::uint64_t bridge_bytes{};
    std::uint64_t objects{};
    std::uint64_t current_bytes = 128;
    std::uint64_t current_records{};
    WalSegment::visit_existing_records(runtime, s.identity, [&](const WalRecord &record) {
      if ((!saw_record && (record.index == 0 || record.index > covered + 1)) ||
          (saw_record && record.index != expected) ||
          record.index > captured.durable_index ||
          (saw_record && record.term < previous_term) ||
          record.term > captured.current_term ||
          (record.index == covered && record.term != last_term()) ||
          (record.index == covered + 1 && record.term < last_term()))
        throw WalBridgeConflict{};
      if (record.index == covered + 1) saw_first_uncovered = true;
      if (record.index > covered && record.index <= snapshot_cut) {
        const std::uint64_t add = kArchiveRecordHeaderBytes + record.payload.size() +
                                  kArchiveDigestBytes;
        if (add + 128 > kMaxArchiveSegmentBytes)
          throw std::runtime_error("WAL bridge record exceeds archive object");
        if (objects == 0) objects = 1;
        if (current_records == kMaxArchiveRecords ||
            current_bytes + add > kMaxArchiveSegmentBytes) {
          bridge_bytes += current_bytes;
          ++objects;
          current_bytes = 128;
          current_records = 0;
        }
        current_bytes += add;
        ++current_records;
        ++bridge_records;
      }
      expected = record.index + 1;
      previous_term = record.term;
      saw_record = true;
    });
    if (objects) bridge_bytes += current_bytes;
    if (!saw_first_uncovered || expected != captured.durable_index + 1 ||
        bridge_records == 0 ||
        bridge_records != snapshot_cut - covered)
      return {ArchiveStoreError::conflict};
    const std::uint64_t reserved_objects =
        (captured.remaining_pending_records + records_per_reserved_object - 1) /
        records_per_reserved_object;
    const std::uint64_t batch_count = objects;
    if (objects > s.config.object_limit ||
        s.count > s.config.object_limit - objects ||
        reserved_objects > s.config.object_limit - s.count - objects ||
        bridge_bytes > s.config.logical_byte_limit ||
        s.logical_bytes > s.config.logical_byte_limit - bridge_bytes ||
        reserved_objects * kMaxArchiveSegmentBytes >
            s.config.logical_byte_limit - s.logical_bytes - bridge_bytes ||
        batch_count > UINT64_MAX - s.header.generation)
      return {ArchiveStoreError::capacity_exceeded};

    std::vector<WalRecord> batch;
    batch.reserve(kMaxArchiveRecords);
    std::uint64_t batch_segment_bytes = 128;
    const auto publish_batch = [&]() -> ArchiveStoreError {
      if (batch.empty()) return ArchiveStoreError::none;
      struct VectorSource {
        const std::vector<WalRecord> *records{};
        static bool read(void *raw, std::uint32_t ordinal,
                         ArchiveRecordView &out) noexcept {
          const auto &self = *static_cast<VectorSource *>(raw);
          if (ordinal >= self.records->size()) return false;
          const auto &record = (*self.records)[ordinal];
          out = {record.index, record.term, record.entry_id, record.entry_type,
                 record.payload};
          return true;
        }
      } source{&batch};
      ArchivePublicationContext next = captured;
      next.catalog_generation = s.header.generation + 1;
      next.predecessor_term = last_term();
      ArchiveRecordSource records{&source, static_cast<std::uint32_t>(batch.size()),
                                  &VectorSource::read};
      auto published = publish(next, records);
      if (published.error != ArchiveStoreError::none) return published.error;
      batch.clear();
      batch_segment_bytes = 128;
      return ArchiveStoreError::none;
    };
    std::uint64_t replay_expected{};
    std::int64_t replay_term{};
    bool replay_saw_record{};
    bool replay_saw_first_uncovered{};
    const auto initial_count = s.count;
    const auto initial_logical_bytes = s.logical_bytes;
    ArchiveStoreError publication_error{ArchiveStoreError::none};
    WalSegment::visit_existing_records(runtime, s.identity, [&](const WalRecord &record) {
      if (publication_error != ArchiveStoreError::none) return;
      if ((!replay_saw_record &&
           (record.index == 0 || record.index > covered + 1)) ||
          (replay_saw_record && record.index != replay_expected) ||
          record.index > captured.durable_index ||
          (replay_saw_record && record.term < replay_term) ||
          record.term > captured.current_term ||
          (record.index == covered && record.term != last_term()) ||
          (record.index == covered + 1 && record.term < last_term())) {
        publication_error = ArchiveStoreError::conflict;
        return;
      }
      if (record.index == covered + 1) replay_saw_first_uncovered = true;
      replay_expected = record.index + 1;
      replay_term = record.term;
      replay_saw_record = true;
      if (record.index <= covered || record.index > snapshot_cut) return;
      const std::uint64_t add = kArchiveRecordHeaderBytes + record.payload.size() +
                                kArchiveDigestBytes;
      const bool new_object = !batch.empty() &&
          batch_segment_bytes + add > kMaxArchiveSegmentBytes;
      if (new_object) publication_error = publish_batch();
      if (publication_error != ArchiveStoreError::none) return;
      if (batch.empty()) {
        batch_segment_bytes = 128;
      }
      if (batch.size() == kMaxArchiveRecords) {
        publication_error = publish_batch();
        if (publication_error != ArchiveStoreError::none) return;
        batch_segment_bytes = 128;
      }
      batch.push_back(record);
      batch_segment_bytes += add;
    });
    if (publication_error == ArchiveStoreError::none &&
        (!replay_saw_first_uncovered ||
         replay_expected != captured.durable_index + 1))
      publication_error = ArchiveStoreError::conflict;
    if (publication_error == ArchiveStoreError::none)
      publication_error = publish_batch();
    if (publication_error != ArchiveStoreError::none)
      return {publication_error, covered_through(), last_term(),
              static_cast<std::uint32_t>(state_->count - initial_count),
              state_->logical_bytes - initial_logical_bytes};
    if (covered_through() != snapshot_cut ||
        state_->count - initial_count != objects ||
        state_->logical_bytes - initial_logical_bytes != bridge_bytes)
      return {ArchiveStoreError::conflict, covered_through(), last_term(),
              static_cast<std::uint32_t>(state_->count - initial_count),
              state_->logical_bytes - initial_logical_bytes};
    result.covered_through = covered_through();
    result.last_term = last_term();
    result.published_objects = static_cast<std::uint32_t>(objects);
    result.published_bytes = bridge_bytes;
    return result;
  } catch (const WalBridgeConflict &) {
    result.error = ArchiveStoreError::conflict;
    result.covered_through = covered_through();
    result.last_term = last_term();
    return result;
  } catch (...) {
    result.error = ArchiveStoreError::publication_failed;
    result.covered_through = covered_through();
    result.last_term = last_term();
    return result;
  }
}

bool ArchiveStore::consume_for_reclamation(ArchiveCoverageCapability &&c,
                                           std::uint64_t storage,
                                           std::uint64_t cut) noexcept {
  const bool valid =
      state_ && !state_->failed &&
      state_->identity.value().deployment_mode == DeploymentMode::laboratory &&
      c.valid_ && c.storage_generation_ == storage &&
      c.catalog_generation_ == state_->header.generation &&
      c.catalog_digest_ == state_->catalog_digest &&
      c.manifest_digest_ == state_->identity.manifest_digest() && cut > 0 &&
      cut <= c.last_index_ && state_->count &&
      c.last_index_ == state_->rows[state_->count - 1].last_index &&
      c.last_term_ == state_->rows[state_->count - 1].last_term &&
      c.terminal_record_digest_ ==
          state_->rows[state_->count - 1].terminal_record_digest;
  c.valid_ = false;
  return valid;
}

bool ArchiveStore::consume_for_reclamation(
    ArchiveCoverageCapability &&local,
    ArchiveDestinationEvidence &&independent,
    const ArchiveDestinationRequest &expected,
    const std::uint64_t storage, const std::uint64_t cut) noexcept {
  const bool independent_valid = independent.consume(expected);
  const bool valid =
      independent_valid && state_ && !state_->failed &&
      state_->identity.value().deployment_mode == DeploymentMode::two_host_pilot &&
      expected.local_storage_generation == storage &&
      expected.publisher_id == state_->identity.value().local_node_id &&
      expected.publisher_manifest == state_->identity.manifest_digest() &&
      expected.catalog_generation == state_->header.generation &&
      expected.catalog_digest == state_->catalog_digest &&
      expected.publication_storage_generation ==
          state_->header.publication_storage_generation &&
      expected.first_index == 1 && expected.last_index >= cut;
  if (!valid) {
    local.valid_ = false;
    return false;
  }
  const auto mode = state_->identity.value().deployment_mode;
  // Validate the local capability under the same conditions as the laboratory
  // path, except for the explicitly independent deployment evidence above.
  const bool local_valid =
      local.valid_ && mode == DeploymentMode::two_host_pilot &&
      local.storage_generation_ == storage &&
      local.catalog_generation_ == state_->header.generation &&
      local.catalog_digest_ == state_->catalog_digest &&
      local.manifest_digest_ == state_->identity.manifest_digest() && cut > 0 &&
      cut <= local.last_index_ && state_->count &&
      local.last_index_ == state_->rows[state_->count - 1].last_index &&
      local.last_term_ == state_->rows[state_->count - 1].last_term &&
      local.terminal_record_digest_ ==
          state_->rows[state_->count - 1].terminal_record_digest;
  local.valid_ = false;
  return local_valid;
}

ArchiveDestinationRequest ArchiveStore::make_destination_request(
    const std::uint64_t local_storage_generation,
    const std::uint64_t attempt,
    const Digest256 &challenge,
    const Digest256 &destination_identity) const noexcept {
  ArchiveDestinationRequest request;
  if (!state_ || state_->failed || !state_->count ||
      local_storage_generation == 0 || attempt == 0 ||
      std::ranges::all_of(challenge.bytes,
                          [](const std::byte byte) { return byte == std::byte{}; }) ||
      std::ranges::all_of(destination_identity.bytes,
                          [](const std::byte byte) { return byte == std::byte{}; }))
    return request;
  request.publisher_id = state_->identity.value().local_node_id;
  request.publisher_manifest = state_->identity.manifest_digest();
  request.catalog_generation = state_->header.generation;
  request.catalog_digest = state_->catalog_digest;
  request.first_index = state_->rows[0].first_index;
  request.last_index = state_->rows[state_->count - 1].last_index;
  request.publication_storage_generation =
      state_->header.publication_storage_generation;
  request.local_storage_generation = local_storage_generation;
  request.attempt = attempt;
  request.challenge = challenge;
  request.destination_identity = destination_identity;
  return request;
}
ArchiveCoverageCapability
ArchiveStore::coverage_for_reclamation(std::uint64_t storage,
                                       std::uint64_t cut) const noexcept {
  ArchiveCoverageCapability c;
  if (!state_ || state_->failed || !state_->count ||
      storage < state_->header.publication_storage_generation || cut == 0 ||
      cut > state_->rows[state_->count - 1].last_index)
    return c;
  c.valid_ = true;
  c.storage_generation_ = storage;
  c.catalog_generation_ = state_->header.generation;
  c.last_index_ = state_->rows[state_->count - 1].last_index;
  c.last_term_ = state_->rows[state_->count - 1].last_term;
  c.manifest_digest_ = state_->identity.manifest_digest();
  c.catalog_digest_ = state_->catalog_digest;
  c.terminal_record_digest_ =
      state_->rows[state_->count - 1].terminal_record_digest;
  return c;
}
bool ArchiveStore::can_admit(std::uint32_t pending,
                             bool protected_lane) const noexcept {
  return can_publish(0, pending, protected_lane);
}
bool ArchiveStore::can_publish(std::uint32_t candidate, std::uint32_t remaining,
                               bool protected_lane) const noexcept {
  if (!state_ || state_->failed || state_->header.generation == UINT64_MAX ||
      candidate > 4ULL * kMaxArchiveSegmentBytes ||
      remaining > kMaxArchiveRecords)
    return false;
  constexpr std::uint64_t records_per_object = 2263;
  const std::uint64_t reserved_objects =
      (remaining + records_per_object - 1) / records_per_object;
  const std::uint64_t candidate_objects =
      (static_cast<std::uint64_t>(candidate) + kMaxArchiveSegmentBytes - 1) /
      kMaxArchiveSegmentBytes;
  const std::uint64_t byte_cap =
      protected_lane
          ? state_->config.logical_byte_limit
          : (state_->config.logical_byte_limit >= 16ULL * 1024 * 1024
                 ? state_->config.logical_byte_limit - 16ULL * 1024 * 1024
                 : 0);
  const std::uint64_t object_cap =
      protected_lane
          ? state_->config.object_limit
          : (state_->config.object_limit >= 4 ? state_->config.object_limit - 4
                                              : 0);
  const std::uint64_t reserved_bytes =
      reserved_objects * kMaxArchiveSegmentBytes;
  return candidate <= byte_cap && reserved_bytes <= byte_cap - candidate &&
         state_->logical_bytes <= byte_cap - candidate - reserved_bytes &&
         candidate_objects <= object_cap &&
         reserved_objects <= object_cap - candidate_objects &&
         state_->count <= object_cap - candidate_objects - reserved_objects;
}
std::uint64_t ArchiveStore::covered_through() const noexcept {
  return state_ && state_->count ? state_->rows[state_->count - 1].last_index
                                 : 0;
}
std::int64_t ArchiveStore::last_term() const noexcept {
  return state_ && state_->count ? state_->rows[state_->count - 1].last_term
                                 : 0;
}
std::uint64_t ArchiveStore::catalog_generation() const noexcept {
  return state_ ? state_->header.generation : 0;
}
std::uint64_t ArchiveStore::publication_storage_generation() const noexcept {
  return state_ ? state_->header.publication_storage_generation : 0;
}
const Digest256 &ArchiveStore::catalog_digest() const noexcept {
  static const Digest256 empty{};
  return state_ ? state_->catalog_digest : empty;
}
} // namespace velocita::v2
