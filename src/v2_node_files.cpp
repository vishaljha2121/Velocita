#include "velocita/v2/node_files.hpp"

#include <array>
#include <string_view>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

// Opens without following a final symlink and reads at most CAPACITY + 1
// bytes, so an oversized file is detected without an unbounded read.
template <std::size_t Capacity>
NodeFileError read_bounded(const std::filesystem::path& file, bool secret,
                           std::array<std::byte, Capacity + 1>& buffer, std::size_t& length) noexcept {
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? NodeFileError::missing : NodeFileError::io_failure;
    struct stat info {};
    NodeFileError error = NodeFileError::none;
    if (::fstat(fd, &info) != 0) error = NodeFileError::io_failure;
    else if (!S_ISREG(info.st_mode)) error = NodeFileError::not_regular;
    else if (secret && info.st_uid != ::geteuid()) error = NodeFileError::wrong_owner;
    else if (secret && (info.st_mode & 077) != 0) error = NodeFileError::insecure_permissions;
    length = 0;
    while (error == NodeFileError::none && length < buffer.size()) {
        const auto got = ::read(fd, buffer.data() + length, buffer.size() - length);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) error = NodeFileError::io_failure;
        if (got <= 0) break;
        length += static_cast<std::size_t>(got);
    }
    ::close(fd);
    return error;
}

}  // namespace

LoadSecretResult load_secret_file(const std::filesystem::path& file) noexcept {
    std::array<std::byte, kAuthSecretBytes + 1> buffer{};
    std::size_t length = 0;
    const auto error = read_bounded<kAuthSecretBytes>(file, true, buffer, length);
    LoadSecretResult result;
    if (error != NodeFileError::none) {
        result.error = error;
    } else if (length != kAuthSecretBytes) {
        result.error = NodeFileError::wrong_size;
    } else {
        result.secret = AuthSecret::from_bytes(std::span(buffer).first(kAuthSecretBytes));
    }
    // Wipe the stack copy; the AuthSecret owns the only surviving copy.
    volatile std::byte* cursor = buffer.data();
    for (std::size_t i = 0; i < buffer.size(); ++i) cursor[i] = std::byte{0};
    return result;
}

LoadSecretSetResult load_secret_set(const std::filesystem::path& file) noexcept {
    try {
        auto current = load_secret_file(file);
        if (current.error != NodeFileError::none) return {current.error, file, std::nullopt};
        SecretSet set{std::move(*current.secret)};
        for (const auto* suffix : {".next", ".previous"}) {
            auto optional_file = file;
            optional_file += suffix;
            auto loaded = load_secret_file(optional_file);
            if (loaded.error == NodeFileError::missing) continue;
            if (loaded.error != NodeFileError::none) return {loaded.error, optional_file, std::nullopt};
            (std::string_view(suffix) == ".next" ? set.next : set.previous) = std::move(*loaded.secret);
        }
        return {NodeFileError::none, {}, std::move(set)};
    } catch (...) {
        return {NodeFileError::io_failure, file, std::nullopt};
    }
}

LoadCatalogResult load_catalog_file(const std::filesystem::path& file, const Digest256& expected_root) noexcept {
    std::array<std::byte, kMaxExecutionCatalogBytes + 1> buffer{};
    std::size_t length = 0;
    LoadCatalogResult result;
    if (const auto error = read_bounded<kMaxExecutionCatalogBytes>(file, false, buffer, length);
        error != NodeFileError::none) {
        result.error = error;
    } else if (length > kMaxExecutionCatalogBytes) {
        result.error = NodeFileError::wrong_size;
    } else if (VerifiedExecutionCatalog::decode(std::span(buffer).first(length), expected_root, result.catalog) !=
               ExecutionCatalogError::none) {
        result.error = NodeFileError::invalid_catalog;
    }
    return result;
}

}  // namespace velocita::v2
