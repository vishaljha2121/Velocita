#include "velocita/v2/session_fence.hpp"

#include "velocita/v2/wire.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {

SessionFence::Decision SessionFence::admit(const std::uint64_t session_epoch,
                                           const std::uint64_t sequence) noexcept {
    if (session_epoch == 0 || sequence == 0) return Decision::invalid_counter;
    if (session_epoch < epoch_) return Decision::stale_epoch;
    if (session_epoch == epoch_ && sequence <= sequence_) return Decision::replayed_sequence;
    epoch_ = session_epoch;
    sequence_ = sequence;
    return Decision::accept;
}

std::optional<std::uint64_t> SessionSequencer::next() noexcept {
    if (epoch_ == 0 || issued_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return ++issued_;
}

namespace {

constexpr std::array<std::byte, 4> kMagic{std::byte{'V'}, std::byte{'L'}, std::byte{'S'}, std::byte{'1'}};

class Fd final {
public:
    explicit Fd(int value) noexcept : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    [[nodiscard]] int get() const noexcept { return value_; }
    [[nodiscard]] bool close() noexcept {
        const int value = value_;
        value_ = -1;
        return ::close(value) == 0;
    }

private:
    int value_;
};

bool sync(int fd) noexcept {
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    return result == 0;
}

bool write_all(int fd, std::span<const std::byte> bytes) noexcept {
    while (!bytes.empty()) {
        const auto written = ::write(fd, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

std::array<std::byte, kSessionEpochFileBytes> encode(std::uint64_t counter) noexcept {
    std::array<std::byte, kSessionEpochFileBytes> bytes{};
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    for (std::size_t i = 0; i < 8; ++i) bytes[4 + i] = std::byte((counter >> (8 * i)) & 0xff);
    const auto crc = crc32_checksum(std::span(bytes).first(12));
    for (std::size_t i = 0; i < 4; ++i) bytes[12 + i] = std::byte((crc >> (8 * i)) & 0xff);
    return bytes;
}

Fd open_parent(const std::filesystem::path& file) noexcept {
    const auto parent = file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
    return Fd(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
}

SessionEpochError publish(int directory, const std::string& name, std::uint64_t counter) noexcept {
    const auto temporary = name + ".tmp";
    Fd output(::openat(directory, temporary.c_str(),
                       O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (output.get() < 0 || ::fchmod(output.get(), 0600) != 0) return SessionEpochError::io_failure;
    const auto bytes = encode(counter);
    if (!write_all(output.get(), bytes) || !sync(output.get()) || !output.close())
        return SessionEpochError::io_failure;
    if (::renameat(directory, temporary.c_str(), directory, name.c_str()) != 0)
        return SessionEpochError::io_failure;
    return sync(directory) ? SessionEpochError::none : SessionEpochError::io_failure;
}

}

SessionEpochResult create_session_epoch_file(const std::filesystem::path& file) noexcept {
    try {
        const Fd directory = open_parent(file);
        if (directory.get() < 0) return {SessionEpochError::io_failure, 0};
        const auto name = file.filename().string();
        Fd output(::openat(directory.get(), name.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (output.get() < 0)
            return {errno == EEXIST ? SessionEpochError::already_exists : SessionEpochError::io_failure, 0};
        const auto bytes = encode(1);
        if (!write_all(output.get(), bytes) || !sync(output.get()) || !output.close() || !sync(directory.get()))
            return {SessionEpochError::io_failure, 0};
        return {SessionEpochError::none, 1};
    } catch (...) {
        return {SessionEpochError::io_failure, 0};
    }
}

SessionEpochResult advance_session_epoch_file(const std::filesystem::path& file) noexcept {
    try {
        const Fd directory = open_parent(file);
        if (directory.get() < 0) return {SessionEpochError::io_failure, 0};
        const auto name = file.filename().string();
        Fd input(::openat(directory.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        if (input.get() < 0)
            return {errno == ENOENT ? SessionEpochError::missing : SessionEpochError::io_failure, 0};
        struct stat info {};
        if (::fstat(input.get(), &info) != 0 || !S_ISREG(info.st_mode)) return {SessionEpochError::invalid_file, 0};

        std::array<std::byte, kSessionEpochFileBytes + 1> bytes{};
        std::size_t length = 0;
        while (length < bytes.size()) {
            const auto got = ::read(input.get(), bytes.data() + length, bytes.size() - length);
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) return {SessionEpochError::io_failure, 0};
            if (got == 0) break;
            length += static_cast<std::size_t>(got);
        }
        if (length != kSessionEpochFileBytes || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
            return {SessionEpochError::invalid_file, 0};
        std::uint64_t counter = 0;
        for (std::size_t i = 0; i < 8; ++i)
            counter |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[4 + i])) << (8 * i);
        std::uint32_t stored_crc = 0;
        for (std::size_t i = 0; i < 4; ++i)
            stored_crc |= static_cast<std::uint32_t>(std::to_integer<unsigned>(bytes[12 + i])) << (8 * i);
        if (stored_crc != crc32_checksum(std::span(bytes).first(12)) || counter == 0)
            return {SessionEpochError::invalid_file, 0};
        if (counter == std::numeric_limits<std::uint64_t>::max()) return {SessionEpochError::exhausted, 0};

        const auto next = counter + 1;
        if (const auto error = publish(directory.get(), name, next); error != SessionEpochError::none)
            return {error, 0};
        return {SessionEpochError::none, next};
    } catch (...) {
        return {SessionEpochError::io_failure, 0};
    }
}

}
