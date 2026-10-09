#include "velocita/v2/signer_service.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/wire.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace velocita::v2::signer {
namespace {

constexpr std::array<std::byte, 4> kFloorMagic{std::byte{'V'}, std::byte{'S'}, std::byte{'F'}, std::byte{'1'}};
constexpr std::size_t kFloorBytes = 16;

class Fd final {
public:
    explicit Fd(const int value) noexcept : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    [[nodiscard]] int get() const noexcept { return value_; }
    [[nodiscard]] int release() noexcept { const int result = value_; value_ = -1; return result; }
    [[nodiscard]] bool close() noexcept {
        const int value = value_;
        value_ = -1;
        return ::close(value) == 0;
    }

private:
    int value_;
};

bool sync(const int fd) noexcept {
    int result;
    do { result = ::fsync(fd); } while (result < 0 && errno == EINTR);
    return result == 0;
}

bool write_all(const int fd, std::span<const std::byte> bytes) noexcept {
    while (!bytes.empty()) {
        const auto written = ::write(fd, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

std::int64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool wait_ready(const int fd, const short events, const std::int64_t deadline) noexcept {
    for (;;) {
        const auto left = deadline - now_ms();
        if (left <= 0) return false;
        pollfd waiting{fd, events, 0};
        const int result = ::poll(&waiting, 1, static_cast<int>(std::min<std::int64_t>(left, 1'000)));
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return false;
        if (result > 0) return (waiting.revents & events) != 0;
    }
}

bool prepare_socket(const int fd) noexcept {
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || ::fcntl(fd, F_SETFL, O_NONBLOCK) != 0) return false;
#if defined(SO_NOSIGPIPE)
    const int one = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0) return false;
#endif
    return true;
}

bool write_socket(const int fd, std::span<const std::byte> bytes, const std::int64_t deadline) noexcept {
    while (!bytes.empty()) {
        if (!wait_ready(fd, POLLOUT, deadline)) return false;
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto written = ::send(fd, bytes.data(), bytes.size(), flags);
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (written <= 0) return false;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
}

bool connect_socket(const int fd, const sockaddr_un& address, const std::int64_t deadline) noexcept {
    if (now_ms() >= deadline) return false;
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) return true;
    if (errno != EINPROGRESS || !wait_ready(fd, POLLOUT, deadline)) return false;
    int error = 0;
    socklen_t length = sizeof(error);
    return ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
}

bool read_exact(const int fd, const std::span<std::byte> out, const std::int64_t deadline) noexcept {
    std::size_t done = 0;
    while (done < out.size()) {
        const auto left = deadline - now_ms();
        if (left <= 0) return false;
        pollfd waiting{fd, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(std::min<std::int64_t>(left, 1'000)));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return false;
        if (ready == 0) continue;
        const auto got = ::read(fd, out.data() + done, out.size() - done);
        if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

std::optional<std::size_t> read_frame(const int fd, const std::span<std::byte> out, const std::int64_t deadline) noexcept {
    if (out.size() < 8 || !read_exact(fd, out.first(8), deadline)) return std::nullopt;
    const auto total = frame_length(out.first(8));
    if (!total || *total > out.size() || !read_exact(fd, out.subspan(8, *total - 8), deadline)) return std::nullopt;
    return total;
}

std::array<std::byte, kFloorBytes> encode_floor(const std::uint64_t floor) noexcept {
    std::array<std::byte, kFloorBytes> bytes{};
    std::copy(kFloorMagic.begin(), kFloorMagic.end(), bytes.begin());
    for (std::size_t i = 0; i < 8; ++i) bytes[4 + i] = static_cast<std::byte>((floor >> (8 * i)) & 0xff);
    const auto crc = crc32_checksum(std::span(bytes).first(12));
    for (std::size_t i = 0; i < 4; ++i) bytes[12 + i] = static_cast<std::byte>((crc >> (8 * i)) & 0xff);
    return bytes;
}

int open_parent(const std::filesystem::path& file) noexcept {
    const auto parent = file.parent_path().empty() ? std::filesystem::path(".") : file.parent_path();
    return ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

int acquire_lock(const std::filesystem::path& file) {
    Fd lock(::open((file.string() + ".lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
    struct stat info {};
    if (lock.get() < 0 || ::fstat(lock.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != ::geteuid() || info.st_nlink != 1 || (info.st_mode & 0077) != 0) return -1;
    if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) return -1;
    return lock.release();
}

std::optional<std::uint32_t> peer_uid(const int fd) noexcept {
#if defined(__linux__)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0 || length != sizeof(credentials))
        return std::nullopt;
    return static_cast<std::uint32_t>(credentials.uid);
#else
    uid_t user{};
    gid_t group{};
    if (::getpeereid(fd, &user, &group) != 0) return std::nullopt;
    return static_cast<std::uint32_t>(user);
#endif
}

bool socket_address(const std::filesystem::path& path, sockaddr_un& address) noexcept {
    const auto& text = path.native();
    if (text.empty() || text.size() >= sizeof(address.sun_path)) return false;
    address = {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
    return true;
}

sim::Tick monotonic_now(void*) noexcept { return static_cast<sim::Tick>(now_ms()); }

}

SignerClock monotonic_clock() noexcept { return {&monotonic_now, nullptr}; }

FloorError FloorFile::create(const std::filesystem::path& file) noexcept {
    try {
        const Fd directory(open_parent(file));
        if (directory.get() < 0) return FloorError::io_failure;
        const auto name = file.filename().string();
        Fd output(::openat(directory.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (output.get() < 0) return errno == EEXIST ? FloorError::already_exists : FloorError::io_failure;
        const auto bytes = encode_floor(0);
        if (!write_all(output.get(), bytes) || !sync(output.get()) || !output.close() || !sync(directory.get()))
            return FloorError::io_failure;
        return FloorError::none;
    } catch (...) {
        return FloorError::io_failure;
    }
}

FloorFile::~FloorFile() {
    if (lock_fd_ >= 0) ::close(lock_fd_);
}

FloorError FloorFile::open(const std::filesystem::path& file, FloorFile& destination) noexcept {
    try {
        if (destination.lock_fd_ >= 0) return FloorError::busy;
        Fd lock(acquire_lock(file));
        if (lock.get() < 0) return FloorError::busy;
        const Fd input(::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
        if (input.get() < 0) return errno == ENOENT ? FloorError::missing : FloorError::io_failure;
        struct stat info {};
        if (::fstat(input.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != kFloorBytes)
            return FloorError::invalid_file;
        std::array<std::byte, kFloorBytes> bytes{};
        std::size_t length = 0;
        while (length < bytes.size()) {
            const auto got = ::read(input.get(), bytes.data() + length, bytes.size() - length);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return FloorError::io_failure;
            length += static_cast<std::size_t>(got);
        }
        std::uint64_t floor = 0;
        for (std::size_t i = 0; i < 8; ++i) floor |= std::to_integer<std::uint64_t>(bytes[4 + i]) << (8 * i);
        if (bytes != encode_floor(floor)) return FloorError::invalid_file;
        destination.file_ = file;
        destination.floor_ = floor;
        destination.lock_fd_ = lock.release();
        return FloorError::none;
    } catch (...) {
        return FloorError::io_failure;
    }
}

bool FloorFile::persist(void* context, const std::uint64_t floor) noexcept {
    try {
        auto& self = *static_cast<FloorFile*>(context);
        if (self.lock_fd_ < 0 || floor <= self.floor_) return false;
        const Fd directory(open_parent(self.file_));
        if (directory.get() < 0) return false;
        const auto name = self.file_.filename().string();
        const auto temporary = name + ".tmp";
        Fd output(::openat(directory.get(), temporary.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600));
        const auto bytes = encode_floor(floor);
        if (output.get() < 0 || !write_all(output.get(), bytes) || !sync(output.get()) || !output.close() ||
            ::renameat(directory.get(), temporary.c_str(), directory.get(), name.c_str()) != 0 ||
            !sync(directory.get())) {
            return false;
        }
        self.floor_ = floor;
        return true;
    } catch (...) {
        return false;
    }
}

SequenceStore FloorFile::store() noexcept { return {&FloorFile::persist, this}; }

AuditFile::~AuditFile() {
    if (fd_ >= 0) ::close(fd_);
}

bool AuditFile::open(const std::filesystem::path& file) noexcept {
    if (fd_ >= 0) return false;
    fd_ = ::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat info {};
    if (fd_ >= 0 && (::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode))) {
        ::close(fd_);
        fd_ = -1;
    }
    return fd_ >= 0;
}

std::string AuditFile::line(const AuditRecord& record) {
    return "sequence=" + std::to_string(record.request_sequence) + " effect=" +
           std::to_string(record.effect_id.value()) + " verdict=" +
           std::to_string(static_cast<int>(record.verdict)) + " key_version=" + std::to_string(record.key_version) +
           " policy_version=" + std::to_string(record.policy_version.value()) + " signature_id=" +
           std::to_string(record.signature_id) + " payload_sha256=" + to_hex(record.payload_digest) + "\n";
}

bool AuditFile::append(void* context, const AuditRecord& record) noexcept {
    try {
        const auto& self = *static_cast<AuditFile*>(context);
        const auto text = line(record);
        return self.fd_ >= 0 && write_all(self.fd_, std::as_bytes(std::span(text.data(), text.size()))) &&
               sync(self.fd_);
    } catch (...) {
        return false;
    }
}

AuditSink AuditFile::sink() noexcept { return {&AuditFile::append, this}; }

SignerServer::~SignerServer() {
    if (fd_ >= 0) {
        ::close(fd_);
        struct stat current {};
        if (::lstat(path_.c_str(), &current) == 0 &&
            static_cast<std::uint64_t>(current.st_dev) == socket_device_ &&
            static_cast<std::uint64_t>(current.st_ino) == socket_inode_)
            ::unlink(path_.c_str());
    }
    if (lock_fd_ >= 0) ::close(lock_fd_);
}

bool SignerServer::listen(const std::filesystem::path& socket, const std::uint32_t allowed_uid) noexcept {
    try {
        sockaddr_un address{};
        if (fd_ >= 0 || !socket_address(socket, address)) return false;
        auto owned_path = socket;
        Fd lock(acquire_lock(socket));
        if (lock.get() < 0) return false;
        struct stat existing {};
        if (::lstat(socket.c_str(), &existing) == 0) {
            if (!S_ISSOCK(existing.st_mode)) return false;
            const Fd probe(::socket(AF_UNIX, SOCK_STREAM, 0));
            if (probe.get() < 0 || ::fcntl(probe.get(), F_SETFL, O_NONBLOCK) != 0) return false;
            if (::connect(probe.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 ||
                errno != ECONNREFUSED || ::unlink(socket.c_str()) != 0) return false;
        } else if (errno != ENOENT) {
            return false;
        }
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;
        const auto previous = ::umask(0117);
        const bool bound = ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
        ::umask(previous);
        struct stat info {};
        if (!bound || !prepare_socket(fd) || ::listen(fd, 4) != 0 ||
            ::lstat(socket.c_str(), &info) != 0) {
            ::close(fd);
            if (bound) ::unlink(socket.c_str());
            return false;
        }
        fd_ = fd;
        path_ = std::move(owned_path);
        lock_fd_ = lock.release();
        socket_device_ = static_cast<std::uint64_t>(info.st_dev);
        socket_inode_ = static_cast<std::uint64_t>(info.st_ino);
        allowed_uid_ = allowed_uid;
        return true;
    } catch (...) {
        return false;
    }
}

bool SignerServer::serve_once(SignerCore& core, const int timeout_ms) {
    if (fd_ < 0) return false;
    pollfd waiting{fd_, POLLIN, 0};
    const int ready = ::poll(&waiting, 1, timeout_ms);
    if (ready <= 0) return false;
    const Fd connection(::accept(fd_, nullptr, nullptr));
    if (connection.get() < 0 || !prepare_socket(connection.get())) {
        ++counters_.io_failures;
        return false;
    }
    const auto uid = peer_uid(connection.get());
    if (!uid || *uid != allowed_uid_) {
        ++counters_.peer_refusals;
        return false;
    }
    std::vector<std::byte> frame(kMaxRequestBytes);
    const auto deadline = now_ms() + 2'000;
    const auto length = read_frame(connection.get(), frame, deadline);
    OwnedSigningRequest request;
    if (!length || decode_request(std::span(frame).first(*length), request) != WireError::none) {
        ++counters_.malformed;
        return false;
    }
    const auto result = core.sign(request.request);
    std::array<std::byte, kMaxResponseBytes> reply{};
    const auto encoded = encode_response(result, reply);
    if (encoded.error != WireError::none || !write_socket(connection.get(), std::span(reply).first(encoded.bytes_written), deadline)) {
        ++counters_.io_failures;
        return false;
    }
    ++counters_.served;
    return true;
}

SocketSigner::SocketSigner(std::filesystem::path socket, const int timeout_ms) noexcept
    : socket_(std::move(socket)), timeout_ms_(timeout_ms) {}

SignerResult SocketSigner::sign(const SigningRequest& request) {
    if (timeout_ms_ <= 0) return {};
    const auto deadline = now_ms() + timeout_ms_;
    sockaddr_un address{};
    std::vector<std::byte> frame(kMaxRequestBytes);
    const auto encoded = encode_request(request, frame);
    if (encoded.error != WireError::none || !socket_address(socket_, address)) return {};
    const Fd connection(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (connection.get() < 0 || !prepare_socket(connection.get()) ||
        !connect_socket(connection.get(), address, deadline) ||
        !write_socket(connection.get(), std::span(frame).first(encoded.bytes_written), deadline)) {
        return {};
    }
    std::array<std::byte, kMaxResponseBytes> reply{};
    const auto length = read_frame(connection.get(), reply, deadline);
    SignerResult result;
    if (!length || decode_response(std::span(reply).first(*length), result) != WireError::none) return {};
    if (result.verdict == SignerVerdict::signed_ok && result.signature_count != request.payload_count) return {};
    return result;
}

}
