// A minimal RFC 6455 WebSocket client over TCP or TLS (OpenSSL), with read timeouts.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace velocita::tape {

struct Url {
    bool tls{};
    std::string host;
    std::uint16_t port{};
    std::string path;
    [[nodiscard]] static Url parse(const std::string& text);
};

enum class ReadStatus : std::uint8_t { message = 0, timeout = 1, closed = 2, error = 3 };

struct ReadResult {
    ReadStatus status{ReadStatus::timeout};
    std::string message;
};

class WebSocket {
public:
    WebSocket(const Url& url, std::chrono::milliseconds read_timeout, const std::string& ca_file = {});
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    void send_text(const std::string& text);
    [[nodiscard]] ReadResult read();
    void close() noexcept;

private:
    struct Io;
    bool fill();
    std::size_t available() const noexcept { return buffer_.size() - at_; }
    bool need(std::size_t n);
    void send_frame(std::uint8_t opcode, const std::string& payload);
    void write_all(const void* data, std::size_t size);
    std::unique_ptr<Io> io_;
    std::vector<char> buffer_;
    std::size_t at_{};
    bool timed_out_{};
    bool closed_{};
};

}
