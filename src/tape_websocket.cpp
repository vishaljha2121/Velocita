#include "velocita/tape/websocket.hpp"

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace velocita::tape {

struct WebSocket::Io {
    int fd{-1};
    SSL_CTX* ctx{};
    SSL* ssl{};
    ~Io() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ctx) SSL_CTX_free(ctx);
        if (fd >= 0) ::close(fd);
    }
};

namespace {

constexpr std::size_t kMaxMessage = 1 << 20;
const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string base64(const unsigned char* data, std::size_t size) {
    std::string out(4 * ((size + 2) / 3) + 1, '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(size));
    out.resize(static_cast<std::size_t>(n));
    return out;
}

std::string accept_for(const std::string& key) {
    const auto text = key + kGuid;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_Digest(text.data(), text.size(), digest, &length, EVP_sha1(), nullptr);
    return base64(digest, length);
}

void random_bytes(void* out, std::size_t n) {
    if (::getentropy(out, n) != 0) throw std::runtime_error("entropy unavailable");
}

}

Url Url::parse(const std::string& text) {
    Url u;
    std::string rest;
    if (text.rfind("wss://", 0) == 0) {
        u.tls = true;
        rest = text.substr(6);
        u.port = 443;
    } else if (text.rfind("ws://", 0) == 0) {
        rest = text.substr(5);
        u.port = 80;
    } else {
        throw std::runtime_error("url must start with ws:// or wss://");
    }
    const auto slash = rest.find('/');
    auto hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        u.port = static_cast<std::uint16_t>(std::stoul(hostport.substr(colon + 1)));
        hostport = hostport.substr(0, colon);
    }
    u.host = hostport;
    if (u.host.empty()) throw std::runtime_error("url has no host");
    return u;
}

WebSocket::WebSocket(const Url& url, std::chrono::milliseconds read_timeout, const std::string& ca_file)
    : io_(std::make_unique<Io>()) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (::getaddrinfo(url.host.c_str(), std::to_string(url.port).c_str(), &hints, &found) != 0 || !found)
        throw std::runtime_error("cannot resolve " + url.host);
    for (auto* a = found; a; a = a->ai_next) {
        const int fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        timeval connect_timeout{10, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connect_timeout, sizeof(connect_timeout));
        if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
            io_->fd = fd;
            break;
        }
        ::close(fd);
    }
    ::freeaddrinfo(found);
    if (io_->fd < 0) throw std::runtime_error("cannot connect to " + url.host);
    const int one = 1;
    ::setsockopt(io_->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    timeval tv{static_cast<time_t>(read_timeout.count() / 1000), static_cast<suseconds_t>((read_timeout.count() % 1000) * 1000)};
    ::setsockopt(io_->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (url.tls) {
        io_->ctx = SSL_CTX_new(TLS_client_method());
        if (!io_->ctx) throw std::runtime_error("TLS context failed");
        SSL_CTX_set_min_proto_version(io_->ctx, TLS1_2_VERSION);
        SSL_CTX_set_verify(io_->ctx, SSL_VERIFY_PEER, nullptr);
        if (!ca_file.empty() ? SSL_CTX_load_verify_locations(io_->ctx, ca_file.c_str(), nullptr) != 1
                             : SSL_CTX_set_default_verify_paths(io_->ctx) != 1)
            throw std::runtime_error("cannot load TLS trust store");
        io_->ssl = SSL_new(io_->ctx);
        SSL_set_fd(io_->ssl, io_->fd);
        SSL_set_tlsext_host_name(io_->ssl, url.host.c_str());
        SSL_set1_host(io_->ssl, url.host.c_str());
        if (SSL_connect(io_->ssl) != 1) throw std::runtime_error("TLS handshake failed for " + url.host);
    }
    unsigned char nonce[16];
    random_bytes(nonce, sizeof(nonce));
    const auto key = base64(nonce, sizeof(nonce));
    const auto host = url.host + ((url.tls && url.port == 443) || (!url.tls && url.port == 80) ? "" : ":" + std::to_string(url.port));
    const std::string request = "GET " + url.path + " HTTP/1.1\r\nHost: " + host +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                                "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: velocita-tape-recorder/1\r\n\r\n";
    write_all(request.data(), request.size());
    std::string head;
    while (head.find("\r\n\r\n") == std::string::npos) {
        if (available() == 0 && !fill()) throw std::runtime_error("WebSocket upgrade got no answer");
        head.push_back(buffer_[at_++]);
        if (head.size() > 16384) throw std::runtime_error("WebSocket upgrade answer too long");
    }
    if (head.rfind("HTTP/1.1 101", 0) != 0) throw std::runtime_error("WebSocket upgrade refused: " + head.substr(0, head.find('\r')));
    std::string lower = head;
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const auto pos = lower.find("sec-websocket-accept:");
    if (pos == std::string::npos) throw std::runtime_error("WebSocket upgrade has no accept header");
    auto value = head.substr(pos + 21, head.find("\r\n", pos) - pos - 21);
    value.erase(0, value.find_first_not_of(' '));
    if (value != accept_for(key)) throw std::runtime_error("WebSocket accept header does not match");
}

WebSocket::~WebSocket() = default;

void WebSocket::write_all(const void* data, std::size_t size) {
    const auto* p = static_cast<const char*>(data);
    while (size > 0) {
        long n;
        if (io_->ssl) n = SSL_write(io_->ssl, p, static_cast<int>(size));
        else n = ::send(io_->fd, p, size, 0);
        if (n <= 0) {
            if (!io_->ssl && errno == EINTR) continue;
            throw std::runtime_error("WebSocket write failed");
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
}

bool WebSocket::fill() {
    if (at_ > 0 && (at_ == buffer_.size() || at_ > 65536)) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(at_));
        at_ = 0;
    }
    char chunk[16384];
    long n;
    if (io_->ssl) {
        n = SSL_read(io_->ssl, chunk, sizeof(chunk));
        if (n <= 0) {
            const int err = SSL_get_error(io_->ssl, static_cast<int>(n));
            if ((err == SSL_ERROR_WANT_READ || err == SSL_ERROR_SYSCALL) && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                timed_out_ = true;
                return false;
            }
            closed_ = true;
            return false;
        }
    } else {
        n = ::recv(io_->fd, chunk, sizeof(chunk), 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            timed_out_ = true;
            return false;
        }
        if (n <= 0) {
            closed_ = true;
            return false;
        }
    }
    buffer_.insert(buffer_.end(), chunk, chunk + n);
    return true;
}

bool WebSocket::need(std::size_t n) {
    while (available() < n)
        if (!fill()) return false;
    return true;
}

ReadResult WebSocket::read() {
    std::string message;
    bool started = false;
    while (true) {
        timed_out_ = false;
        if (!need(2)) {
            if (timed_out_ && !started) return {ReadStatus::timeout, {}};
            if (timed_out_) continue;
            return {ReadStatus::closed, {}};
        }
        const auto b0 = static_cast<unsigned char>(buffer_[at_]), b1 = static_cast<unsigned char>(buffer_[at_ + 1]);
        const bool fin = b0 & 0x80;
        const auto opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        std::uint64_t length = b1 & 0x7F;
        std::size_t header = 2;
        if (length == 126) header = 4;
        else if (length == 127) header = 10;
        if (masked) header += 4;
        if (!need(header)) {
            if (timed_out_) continue;
            return {ReadStatus::closed, {}};
        }
        if (length == 126) length = (static_cast<std::uint64_t>(static_cast<unsigned char>(buffer_[at_ + 2])) << 8) |
                                    static_cast<unsigned char>(buffer_[at_ + 3]);
        else if (length == 127) {
            length = 0;
            for (int i = 0; i < 8; ++i) length = (length << 8) | static_cast<unsigned char>(buffer_[at_ + 2 + i]);
        }
        if (length > kMaxMessage) return {ReadStatus::error, "frame too large"};
        if (!need(header + length)) {
            if (timed_out_) continue;
            return {ReadStatus::closed, {}};
        }
        std::string payload(buffer_.data() + at_ + header, static_cast<std::size_t>(length));
        if (masked) {
            const char* mask = buffer_.data() + at_ + header - 4;
            for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
        }
        at_ += header + length;
        switch (opcode) {
            case 0x9: send_frame(0xA, payload); continue;
            case 0xA: continue;
            case 0x8: return {ReadStatus::closed, payload};
            case 0x1:
            case 0x2:
                message = std::move(payload);
                started = true;
                break;
            case 0x0:
                if (!started) return {ReadStatus::error, "continuation without a start"};
                message += payload;
                break;
            default: return {ReadStatus::error, "unknown opcode"};
        }
        if (message.size() > kMaxMessage) return {ReadStatus::error, "message too large"};
        if (fin) return {ReadStatus::message, std::move(message)};
    }
}

void WebSocket::send_frame(std::uint8_t opcode, const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    if (payload.size() < 126) {
        frame.push_back(static_cast<char>(0x80 | payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>(payload.size() >> 8));
        frame.push_back(static_cast<char>(payload.size() & 0xFF));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) frame.push_back(static_cast<char>((payload.size() >> (8 * i)) & 0xFF));
    }
    unsigned char mask[4];
    random_bytes(mask, sizeof(mask));
    frame.append(reinterpret_cast<const char*>(mask), 4);
    for (std::size_t i = 0; i < payload.size(); ++i) frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    write_all(frame.data(), frame.size());
}

void WebSocket::send_text(const std::string& text) { send_frame(0x1, text); }

void WebSocket::close() noexcept {
    try {
        send_frame(0x8, {});
    } catch (...) {
    }
}

}
