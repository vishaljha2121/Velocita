#include "velocita/v2/generation_base_files.hpp"

#include <array>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {
using Error = GenerationBaseFileError;
struct Fd {
    int value{-1};
    ~Fd() { if (value >= 0) ::close(value); }
    Fd() = default;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    bool close() noexcept { return ::close(std::exchange(value, -1)) == 0; }
};

bool directory_ok(int fd) noexcept {
    struct stat info{};
    return ::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) &&
        info.st_uid == ::geteuid() && (info.st_mode & 0022) == 0;
}

bool name_matches(int parent, const char* name, int fd) noexcept {
    struct stat opened{}, named{};
    return ::fstat(fd, &opened) == 0 &&
        ::fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISDIR(named.st_mode) && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
}

Error open_directory(int parent, const char* name, Fd& out) noexcept {
    do { out.value = ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC); }
    while (out.value < 0 && errno == EINTR);
    if (out.value < 0) return Error::invalid_file;
    if (!directory_ok(out.value) || !name_matches(parent, name, out.value)) return Error::invalid_file;
    return Error::none;
}

Error read_file(int directory, const char* name, std::span<std::byte> buffer,
                std::size_t& length) noexcept {
    Fd input;
    do { input.value = ::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC); }
    while (input.value < 0 && errno == EINTR);
    if (input.value < 0)
        return errno == ELOOP || errno == ENOENT || errno == ENOTDIR ?
            Error::invalid_file : Error::io_failure;
    struct stat before{};
    if (::fstat(input.value, &before) != 0) return Error::io_failure;
    if (!S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_uid != ::geteuid() ||
        (before.st_mode & 0077) != 0 || before.st_size < 0 ||
        static_cast<std::uintmax_t>(before.st_size) > buffer.size()) return Error::invalid_file;
    const auto size = static_cast<std::size_t>(before.st_size);
    std::size_t offset{};
    while (offset != size) {
        const auto got = ::read(input.value, buffer.data() + offset, size - offset);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return Error::io_failure;
        offset += static_cast<std::size_t>(got);
    }
    std::byte extra{};
    ssize_t tail;
    do { tail = ::read(input.value, &extra, 1); } while (tail < 0 && errno == EINTR);
    if (tail < 0) return Error::io_failure;
    if (tail != 0) return Error::invalid_file;
    struct stat after{}, named{};
    if (::fstat(input.value, &after) != 0 ||
        ::fstatat(directory, name, &named, AT_SYMLINK_NOFOLLOW) != 0) return Error::io_failure;
    if (!S_ISREG(named.st_mode) || named.st_dev != before.st_dev || named.st_ino != before.st_ino ||
        after.st_size != before.st_size || after.st_nlink != 1 || after.st_uid != ::geteuid() ||
        (after.st_mode & 0077) != 0) return Error::invalid_file;
    if (!input.close()) return Error::io_failure;
    length = size;
    return Error::none;
}

Error base_contents(int directory, bool snapshot) noexcept {
    Fd scan;
    auto error = open_directory(directory, ".", scan);
    if (error != Error::none) return error;
    DIR* entries = ::fdopendir(scan.value);
    if (!entries) return Error::io_failure;
    scan.value = -1;
    bool found{};
    Error result = Error::none;
    for (;;) {
        errno = 0;
        auto* entry = ::readdir(entries);
        if (!entry) {
            if (errno != 0) result = Error::io_failure;
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (!snapshot || found || name != "v2-snapshot.bin") {
            result = Error::unexpected_contents;
            break;
        }
        found = true;
    }
    if (::closedir(entries) != 0) return Error::io_failure;
    if (result == Error::none && found != snapshot) return Error::invalid_file;
    return result;
}
GenerationBaseFileError read_base(
    int root_fd, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, std::span<std::byte> snapshot_buffer,
    GenerationBaseScratch& scratch, SelectedGenerationBase& destination,
    const LocalGenerationSelector* candidate) noexcept {
    if (root_fd < 0 || !identity.valid() || snapshot_buffer.size() < kMaxBoundSnapshotBytes)
        return Error::invalid_argument;
    if (!directory_ok(root_fd)) return Error::invalid_file;
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::size_t length{};
    auto error = read_file(root_fd, "v2-identity.bin", identity_bytes, length);
    if (error != Error::none) return error;
    VerifiedStaticIdentity stored_identity;
    if (VerifiedStaticIdentity::decode(std::span(identity_bytes).first(length), stored_identity) !=
            StaticIdentityCodecError::none || stored_identity.value() != identity.value())
        return Error::invalid_identity;
    std::array<std::byte, kLocalGenerationSelectorBytes> selector_bytes{};
    SelectedGenerationBase result;
    if (candidate) {
        if (encode_local_generation_selector(*candidate, identity, selector_bytes).error !=
                LocalGenerationError::none) return Error::invalid_selector;
        result.selector = *candidate;
    } else {
        error = read_file(root_fd, "v2-active-generation.bin", selector_bytes, length);
        if (error != Error::none) return error;
        if (decode_local_generation_selector(std::span(selector_bytes).first(length), identity,
                                        result.selector) != LocalGenerationError::none)
            return Error::invalid_selector;
    }
    std::array<char, 19> name{'g', '-'};
    constexpr std::string_view digits = "0123456789abcdef";
    for (unsigned i = 0; i != 16; ++i)
        name[2 + i] = digits[(result.selector.generation >> (4*(15-i))) & 15];
    Fd generations, generation, base;
    error = open_directory(root_fd, "v2-generations", generations);
    if (error != Error::none) return error;
    error = open_directory(generations.value, name.data(), generation);
    if (error != Error::none) return error;
    error = open_directory(generation.value, "base", base);
    if (error != Error::none) return error;
    std::array<std::byte, kGenerationBaseSealBytes> seal{};
    error = read_file(generation.value, "v2-base-seal.bin", seal, length);
    if (error != Error::none) return error;
    if (length != seal.size() || (seal[6] != std::byte{0} && seal[6] != std::byte{1}))
        return Error::invalid_base;
    const bool has_snapshot = seal[6] == std::byte{1};
    error = base_contents(base.value, has_snapshot);
    if (error != Error::none) return error;
    if (has_snapshot) {
        error = read_file(base.value, "v2-snapshot.bin", snapshot_buffer.first(kMaxBoundSnapshotBytes),
                          result.snapshot_bytes);
        if (error != Error::none) return error;
    }
    if (decode_generation_base(seal, result.selector.generation, identity, catalog,
            snapshot_buffer.first(result.snapshot_bytes), scratch, result.base) != GenerationBaseError::none ||
        result.base.digest != result.selector.prepared_base_digest) return Error::invalid_base;
    if (!name_matches(root_fd, "v2-generations", generations.value) ||
        !name_matches(generations.value, name.data(), generation.value) ||
        !name_matches(generation.value, "base", base.value)) return Error::invalid_file;
    if (!base.close() || !generation.close() || !generations.close()) return Error::io_failure;
    destination = result;
    return Error::none;
}
} // namespace

GenerationBaseFileError inspect_selector_temporary_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    SelectorTemporaryStatus& destination) noexcept {
    if (root_fd < 0 || !identity.valid()) return Error::invalid_argument;
    constexpr auto name = "v2-active-generation.bin.tmp";
    struct stat info{};
    if (::fstatat(root_fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) return Error::io_failure;
        destination = SelectorTemporaryStatus::absent;
        return Error::none;
    }
    std::array<std::byte, kLocalGenerationSelectorBytes> bytes{};
    std::size_t length{};
    const auto error = read_file(root_fd, name, bytes, length);
    if (error != Error::none && error != Error::invalid_file) return error;
    LocalGenerationSelector ignored;
    destination = error == Error::none &&
        decode_local_generation_selector(std::span(bytes).first(length), identity, ignored) ==
            LocalGenerationError::none ? SelectorTemporaryStatus::valid : SelectorTemporaryStatus::invalid;
    return Error::none;
}

GenerationBaseFileError read_selected_generation_base_at(
    int root_fd, const VerifiedStaticIdentity& identity,
    const VerifiedExecutionCatalog& catalog, std::span<std::byte> snapshot_buffer,
    GenerationBaseScratch& scratch, SelectedGenerationBase& destination) noexcept {
    return read_base(root_fd, identity, catalog, snapshot_buffer, scratch, destination, nullptr);
}

GenerationBaseFileError read_generation_base_at(
    int root_fd, const LocalGenerationSelector& candidate,
    const VerifiedStaticIdentity& identity, const VerifiedExecutionCatalog& catalog,
    std::span<std::byte> snapshot_buffer, GenerationBaseScratch& scratch,
    SelectedGenerationBase& destination) noexcept {
    return read_base(root_fd, identity, catalog, snapshot_buffer, scratch, destination, &candidate);
}
} // namespace velocita::v2
