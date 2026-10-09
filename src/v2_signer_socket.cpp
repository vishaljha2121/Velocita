#include "velocita/v2/signer_service.hpp"

#include "velocita/v2/digest.hpp"
#include "velocita/v2/wire.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
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
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

std::optional<std::size_t> read_frame(const int fd, const std::span<std::byte> out, const int timeout_ms) noexcept {
    const auto deadline = now_ms() + timeout_ms;
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

FloorError FloorFile::open(const std::filesystem::path& file, FloorFile& destination) noexcept {
    try {
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
        return FloorError::none;
    } catch (...) {
        return FloorError::io_failure;
    }
}

bool FloorFile::persist(void* context, const std::uint64_t floor) noexcept {
    try {
        auto& self = *static_cast<FloorFile*>(context);
        if (floor <= self.floor_) return false;
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
        ::unlink(path_.c_str());
    }
}

bool SignerServer::listen(const std::filesystem::path& socket, const std::uint32_t allowed_uid) noexcept {
    sockaddr_un address{};
    if (fd_ >= 0 || !socket_address(socket, address)) return false;
    struct stat existing {};
    if (::lstat(socket.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || ::unlink(socket.c_str()) != 0) return false;
    } else if (errno != ENOENT) {
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const auto previous = ::umask(0117);
    const bool bound = ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    ::umask(previous);
    if (!bound || ::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || ::listen(fd, 4) != 0) {
        ::close(fd);
        if (bound) ::unlink(socket.c_str());
        return false;
    }
    fd_ = fd;
    path_ = socket;
    allowed_uid_ = allowed_uid;
    return true;
}

bool SignerServer::serve_once(SignerCore& core, const int timeout_ms) {
    if (fd_ < 0) return false;
    pollfd waiting{fd_, POLLIN, 0};
    const int ready = ::poll(&waiting, 1, timeout_ms);
    if (ready <= 0) return false;
    const Fd connection(::accept(fd_, nullptr, nullptr));
    if (connection.get() < 0) {
        ++counters_.io_failures;
        return false;
    }
    const auto uid = peer_uid(connection.get());
    if (!uid || *uid != allowed_uid_) {
        ++counters_.peer_refusals;
        return false;
    }
    std::vector<std::byte> frame(kMaxRequestBytes);
    const auto length = read_frame(connection.get(), frame, 2'000);
    OwnedSigningRequest request;
    if (!length || decode_request(std::span(frame).first(*length), request) != WireError::none) {
        ++counters_.malformed;
        return false;
    }
    const auto result = core.sign(request.request);
    std::array<std::byte, kMaxResponseBytes> reply{};
    const auto encoded = encode_response(result, reply);
    if (encoded.error != WireError::none || !write_all(connection.get(), std::span(reply).first(encoded.bytes_written))) {
        ++counters_.io_failures;
        return false;
    }
    ++counters_.served;
    return true;
}

SocketSigner::SocketSigner(std::filesystem::path socket, const int timeout_ms) noexcept
    : socket_(std::move(socket)), timeout_ms_(timeout_ms) {}

SignerResult SocketSigner::sign(const SigningRequest& request) {
    sockaddr_un address{};
    std::vector<std::byte> frame(kMaxRequestBytes);
    const auto encoded = encode_request(request, frame);
    if (encoded.error != WireError::none || !socket_address(socket_, address)) return {};
    const Fd connection(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (connection.get() < 0 || ::fcntl(connection.get(), F_SETFD, FD_CLOEXEC) != 0 ||
        ::connect(connection.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        !write_all(connection.get(), std::span(frame).first(encoded.bytes_written))) {
        return {};
    }
    std::array<std::byte, kMaxResponseBytes> reply{};
    const auto length = read_frame(connection.get(), reply, timeout_ms_);
    SignerResult result;
    if (!length || decode_response(std::span(reply).first(*length), result) != WireError::none) return {};
    if (result.verdict == SignerVerdict::signed_ok && result.signature_count != request.payload_count) return {};
    return result;
}

}
