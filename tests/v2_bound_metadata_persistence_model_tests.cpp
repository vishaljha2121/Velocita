#include "velocita/v2/bound_metadata.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace bpm {
struct PowerCut final {};
struct Inode {
  std::vector<std::byte> volatile_bytes, durable_bytes;
};
struct Handle {
  bool directory{};
  int inode{-1};
  std::size_t offset{};
};
struct FileSystem {
  std::map<int, Inode> inodes;
  std::map<std::string, int> volatile_names, durable_names;
  std::map<int, Handle> handles;
  int next_inode{1}, next_fd{10}, event{}, cut_after{-1}, fail_before{-1};
  bool short_first_write{}, short_write_done{}, crashed{};

  void before() {
    if (crashed)
      throw PowerCut{};
    if (event + 1 == fail_before) {
      ++event;
      errno = EIO;
      throw std::system_error(EIO, std::generic_category());
    }
  }
  void after() {
    if (++event == cut_after) {
      crashed = true;
      throw PowerCut{};
    }
  }
  static std::string name(const char *path) {
    return std::filesystem::path(path).filename().string();
  }
  int open_directory() {
    before();
    const int fd = next_fd++;
    handles.emplace(fd, Handle{true});
    after();
    return fd;
  }
  int open_file(const char *path, int flags) {
    before();
    const auto key = name(path);
    auto found = volatile_names.find(key);
    if (found == volatile_names.end()) {
      if (!(flags & O_CREAT)) {
        errno = ENOENT;
        after();
        throw std::system_error(ENOENT, std::generic_category());
      }
      const int inode = next_inode++;
      inodes.emplace(inode, Inode{});
      volatile_names[key] = inode;
      found = volatile_names.find(key);
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
      errno = EEXIST;
      throw std::system_error(EEXIST, std::generic_category());
    }
    const int fd = next_fd++;
    handles.emplace(fd, Handle{false, found->second, 0});
    after();
    return fd;
  }
  ssize_t read(int fd, void *output, std::size_t size) {
    before();
    auto &handle = handles.at(fd);
    const auto &bytes = inodes.at(handle.inode).volatile_bytes;
    const auto count =
        std::min(size, bytes.size() - std::min(handle.offset, bytes.size()));
    if (count != 0)
      std::memcpy(output, bytes.data() + handle.offset, count);
    handle.offset += count;
    after();
    return count;
  }
  ssize_t write(int fd, const void *input, std::size_t size) {
    before();
    auto &bytes = inodes.at(handles.at(fd).inode).volatile_bytes;
    std::size_t count = size;
    if (short_first_write && !short_write_done && size > 1) {
      count = size / 2;
      short_write_done = true;
    }
    const auto *first = static_cast<const std::byte *>(input);
    bytes.insert(bytes.end(), first, first + count);
    after();
    return count;
  }
  int truncate(int fd) {
    before();
    inodes.at(handles.at(fd).inode).volatile_bytes.clear();
    handles.at(fd).offset = 0;
    after();
    return 0;
  }
  int sync(int fd) {
    before();
    const auto handle = handles.at(fd);
    if (handle.directory)
      durable_names = volatile_names;
    else
      inodes.at(handle.inode).durable_bytes =
          inodes.at(handle.inode).volatile_bytes;
    after();
    return 0;
  }
  int close(int fd) {
    before();
    handles.erase(fd);
    after();
    return 0;
  }
  int stat_fd(int fd, struct stat *out) {
    before();
    std::memset(out, 0, sizeof(*out));
    const auto handle = handles.at(fd);
    out->st_mode = handle.directory ? S_IFDIR | 0755 : S_IFREG | 0644;
    out->st_nlink = 1;
    out->st_dev = 1;
    out->st_ino = handle.directory ? 1 : handles.at(fd).inode + 10;
    after();
    return 0;
  }
  int stat_name(const char *raw, struct stat *out) {
    before();
    const auto found = volatile_names.find(name(raw));
    if (found == volatile_names.end()) {
      errno = ENOENT;
      after();
      throw std::system_error(ENOENT, std::generic_category());
    }
    std::memset(out, 0, sizeof(*out));
    out->st_mode = S_IFREG | 0644;
    out->st_nlink = 1;
    out->st_dev = 1;
    out->st_ino = found->second + 10;
    after();
    return 0;
  }
  int rename(const char *from, const char *to) {
    before();
    const auto source = name(from), target = name(to);
    const auto found = volatile_names.find(source);
    if (found == volatile_names.end()) {
      errno = ENOENT;
      after();
      throw std::system_error(ENOENT, std::generic_category());
    }
    const int inode = found->second;
    volatile_names.erase(found);
    volatile_names[target] = inode;
    after();
    return 0;
  }
  std::vector<std::byte> recovered(const std::string &key) const {
    const auto found = durable_names.find(key);
    return found == durable_names.end()
               ? std::vector<std::byte>{}
               : inodes.at(found->second).durable_bytes;
  }
};
thread_local FileSystem *active{};
struct ScopedFileSystem {
  FileSystem *previous{active};
  explicit ScopedFileSystem(FileSystem &fs) { active = &fs; }
  ~ScopedFileSystem() { active = previous; }
};
}

template <class F> int bpm_int(F &&operation) {
  try {
    return operation();
  } catch (const bpm::PowerCut &) {
    throw;
  } catch (const std::system_error &e) {
    errno = e.code().value();
    return -1;
  }
}
int bpm_open(const char *, int flags, ...) {
  return bpm_int([&] {
    return (flags & O_DIRECTORY) ? bpm::active->open_directory() : -1;
  });
}
int bpm_openat(int, const char *path, int flags, ...) {
  return bpm_int([&] { return bpm::active->open_file(path, flags); });
}
ssize_t bpm_read(int fd, void *out, size_t n) {
  try {
    return bpm::active->read(fd, out, n);
  } catch (const bpm::PowerCut &) {
    throw;
  } catch (const std::system_error &error) {
    errno = error.code().value();
    return -1;
  }
}
ssize_t bpm_write(int fd, const void *in, size_t n) {
  try {
    return bpm::active->write(fd, in, n);
  } catch (const bpm::PowerCut &) {
    throw;
  } catch (const std::system_error &error) {
    errno = error.code().value();
    return -1;
  }
}
int bpm_fsync(int fd) {
  return bpm_int([&] { return bpm::active->sync(fd); });
}
int bpm_close(int fd) {
  return bpm_int([&] { return bpm::active->close(fd); });
}
int bpm_fstat(int fd, struct stat *out) {
  return bpm_int([&] { return bpm::active->stat_fd(fd, out); });
}
int bpm_fstatat(int, const char *path, struct stat *out, int) {
  return bpm_int([&] { return bpm::active->stat_name(path, out); });
}
int bpm_ftruncate(int fd, off_t) {
  return bpm_int([&] { return bpm::active->truncate(fd); });
}
int bpm_renameat(int, const char *from, int, const char *to) {
  return bpm_int([&] { return bpm::active->rename(from, to); });
}

#define open bpm_open
#define openat bpm_openat
#define read bpm_read
#define write bpm_write
#define fsync bpm_fsync
#define close bpm_close
#define fstat bpm_fstat
#define fstatat bpm_fstatat
#define ftruncate bpm_ftruncate
#define renameat bpm_renameat
#ifndef BPM_BOUND_METADATA_IMPLEMENTATION
#define BPM_BOUND_METADATA_IMPLEMENTATION "../src/v2_bound_metadata.cpp"
#endif
#include BPM_BOUND_METADATA_IMPLEMENTATION
#undef renameat
#undef ftruncate
#undef fstatat
#undef fstat
#undef close
#undef fsync
#undef write
#undef read
#undef openat
#undef open

namespace {
using namespace velocita;
using namespace velocita::v2;
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool equal(const PersistentMetadata &a, const PersistentMetadata &b) {
  return a.term == b.term && a.voted_for == b.voted_for &&
         a.commit_index == b.commit_index;
}

std::vector<std::byte> hex(const std::string &text) {
  std::vector<std::byte> result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(
        static_cast<std::byte>(std::stoul(text.substr(i, 2), nullptr, 16)));
  return result;
}
VerifiedStaticIdentity fixed_identity() {
  const auto bytes =
      hex("563249447c000000010000000102030405060708090a0b0c0d0e0f10"
          "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
          "0c000c0001000100020001000101030011000000010100000100010011000000"
          "4a5cd70c7355448c05ae9d61053151ab8202198996d3317eecfc6a857d353719");
  VerifiedStaticIdentity result;
  require(VerifiedStaticIdentity::decode(bytes, result) ==
              StaticIdentityCodecError::none,
          "fixed identity invalid");
  return result;
}
void put(std::span<std::byte> out, std::size_t at, std::size_t width,
         std::uint64_t value) {
  for (std::size_t i = 0; i < width; ++i)
    out[at + i] = std::byte((value >> (8 * i)) & 255);
}
std::array<std::byte, kBoundMetadataBytes>
independently_encoded(PersistentMetadata value,
                      const VerifiedStaticIdentity &identity) {
  std::array<std::byte, kBoundMetadataBytes> out{};
  out[0] = std::byte{'V'};
  out[1] = std::byte{'2'};
  out[2] = std::byte{'B'};
  out[3] = std::byte{'F'};
  put(out, 4, 2, 1);
  put(out, 6, 2, 1);
  put(out, 8, 4, kBoundMetadataBytes);
  put(out, 12, 4, 24);
  std::copy(identity.manifest_digest().bytes.begin(),
            identity.manifest_digest().bytes.end(), out.begin() + 16);
  put(out, 48, 4, static_cast<std::uint32_t>(identity.value().local_node_id));
  put(out, 56, 8, static_cast<std::uint64_t>(value.term));
  put(out, 64, 4, static_cast<std::uint32_t>(value.voted_for));
  put(out, 72, 8, static_cast<std::uint64_t>(value.commit_index));
  const std::string domain = "velocita.v2.bound-frame.v1";
  std::array<std::byte, 26 + kBoundFrameHeaderBytes + 24> input{};
  std::transform(domain.begin(), domain.end(), input.begin(),
                 [](char c) { return std::byte(c); });
  std::copy(out.begin(), out.begin() + 80, input.begin() + 26);
  const auto digest = sha256_digest(input);
  std::copy(digest.bytes.begin(), digest.bytes.end(), out.begin() + 80);
  return out;
}

struct Fixture {
  std::filesystem::path root;
  VerifiedStaticIdentity identity{fixed_identity()};
  bpm::FileSystem fs;
  PersistentMetadata old_value{7, 17, 3}, new_value{47, 257, 127};
  explicit Fixture(std::filesystem::path path, bool existing_temp = false)
      : root(std::move(path)) {
    const auto bytes = independently_encoded(old_value, identity);
    const auto golden =
        hex("563242460100010070000000180000007b6a13e548fef5ff55053c1c513155ab40"
            "409fae8a165c5bd36e064d43a2cd7a110000000000000007000000000000001100"
            "0000000000000300000000000000565f4a1d98db2f2789d24bd6f30f69558cbd26"
            "a473c94a2425bb67ec22029913");
    require(golden.size() == bytes.size(), "fixed golden has wrong size");
    const auto mismatch =
        std::mismatch(bytes.begin(), bytes.end(), golden.begin());
    require(
        mismatch.first == bytes.end(),
        "independent bound-frame packer disagrees with fixed golden at byte " +
            std::to_string(mismatch.first - bytes.begin()));
    const int inode = fs.next_inode++;
    fs.inodes.emplace(inode, bpm::Inode{{bytes.begin(), bytes.end()},
                                        {bytes.begin(), bytes.end()}});
    fs.volatile_names[kV2MetadataFileName] = inode;
    fs.durable_names[kV2MetadataFileName] = inode;
    if (existing_temp) {
      const int temp = fs.next_inode++;
      fs.inodes.emplace(temp, bpm::Inode{{bytes.begin(), bytes.end()},
                                         {bytes.begin(), bytes.end()}});
      fs.volatile_names[std::string(kV2MetadataFileName) + ".tmp"] = temp;
      fs.durable_names[std::string(kV2MetadataFileName) + ".tmp"] = temp;
    }
    require(decode_recovered().has_value() &&
                equal(*decode_recovered(), old_value),
            "fixture did not establish bound old tuple");
  }
  std::optional<PersistentMetadata> decode_recovered() const {
    const auto bytes = fs.recovered(kV2MetadataFileName);
    PersistentMetadata value{};
    BoundFrameScratch scratch{};
    if (decode_bound_metadata(bytes, identity, value, scratch) !=
        BoundFrameError::none)
      return std::nullopt;
    return value;
  }
  bool save() {
    bpm::ScopedFileSystem scope(fs);
    try {
      BoundMetadataStore(root, identity).save(new_value);
      return true;
    } catch (const bpm::PowerCut &) {
      return false;
    } catch (const BoundMetadataError &) {
      return false;
    }
  }
  void oracle(bool acknowledged, const std::string &where) const {
    const auto recovered_bytes = fs.recovered(kV2MetadataFileName);
    const auto recovered = decode_recovered();
    require(recovered.has_value(),
            where + ": recovered invalid bound metadata");
    require(equal(*recovered, old_value) || equal(*recovered, new_value),
            where + ": recovered neither authored tuple");
    require(
        !acknowledged || equal(*recovered, new_value),
        where +
            ": acknowledged term/vote/commit did not recover as 47/257/127");
    const auto authored_new = independently_encoded(new_value, identity);
    require(
        !acknowledged ||
            std::equal(authored_new.begin(), authored_new.end(),
                       recovered_bytes.begin(), recovered_bytes.end()),
        where +
            ": acknowledged bytes differ from independently packed new frame");

    bpm::FileSystem rebooted;
    rebooted.volatile_names = fs.durable_names;
    rebooted.durable_names = fs.durable_names;
    for (const auto &[id, inode] : fs.inodes)
      rebooted.inodes.emplace(id, bpm::Inode{inode.durable_bytes, inode.durable_bytes});
    bpm::ScopedFileSystem scope(rebooted);
    const auto loaded = BoundMetadataStore(root, identity).load();
    require(equal(loaded, *recovered),
            where + ": actual load disagrees with recovered authored tuple");
  }
};
std::filesystem::path root(const std::string &label, int n) {
  return std::filesystem::temp_directory_path() /
         ("v2-bound-metadata-model-" + std::to_string(::getpid()) + "-" +
          label + "-" + std::to_string(n));
}

void all_cuts_and_failures() {
  for (bool temp : {false, true}) {
    Fixture trace(root(temp ? "temp-trace" : "trace", 0), temp);
    require(trace.save(), "uncut actual save failed");
    trace.oracle(true, "uncut");
    const int events = trace.fs.event;
    require(events > 10, "actual save exercised too few modeled syscalls");
    std::cout << "metadata-model path="
              << (temp ? "existing-temp" : "absent-temp")
              << " events=" << events << '\n';
    for (int cut = 1; cut <= events; ++cut) {
      Fixture f(root(temp ? "temp-cut" : "cut", cut), temp);
      f.fs.cut_after = cut;
      const bool ack = f.save();
      require(!ack, "cut acknowledged");
      f.oracle(false, "cut " + std::to_string(cut));
    }
    for (int failure = 1; failure <= events; ++failure) {
      Fixture f(root(temp ? "temp-failure" : "failure", failure), temp);
      f.fs.fail_before = failure;
      const bool ack = f.save();
      f.oracle(ack, "failure " + std::to_string(failure));
    }
  }
}
void short_writes() {
  Fixture trace(root("short", 0));
  trace.fs.short_first_write = true;
  const bool ack = trace.save();
  require(ack, "actual write_all did not finish short write");
  trace.oracle(true, "short");
  std::cout << "metadata-model path=short-write events=" << trace.fs.event
            << '\n';
  for (int cut = 1; cut <= trace.fs.event; ++cut) {
    Fixture f(root("short-cut", cut));
    f.fs.short_first_write = true;
    f.fs.cut_after = cut;
    const bool ack2 = f.save();
    require(!ack2, "short-prefix cut acknowledged");
    f.oracle(false, "short cut " + std::to_string(cut));
  }
}
}
int main() {
  try {
    all_cuts_and_failures();
    short_writes();
    std::cout << "bound metadata persistence ordering model passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "bound metadata persistence ordering model failed: "
              << e.what() << '\n';
    return 1;
  }
}
