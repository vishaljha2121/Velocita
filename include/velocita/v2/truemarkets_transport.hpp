// True Markets calls over HTTP with the MCP envelope; the only stream provided connects to the loopback address.
#pragma once

#include "velocita/v2/truemarkets_venue.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace velocita::v2::truemarkets::transport {

inline constexpr std::size_t kMaxHttpHeaderBytes = 8'192;
inline constexpr std::size_t kMaxHttpBodyBytes = 2 * kMaxTmResponseBytes;
inline constexpr std::size_t kMaxHttpRequestBytes = kMaxTmRequestBytes + 1'024;

enum class HttpMethod : std::uint8_t { get = 1, post };

struct HttpRequest {
    HttpMethod method{HttpMethod::get};
    std::string_view host{};
    std::string_view path{};
    std::string_view bearer{};
    std::string_view body{};
};

struct HttpEncodeResult {
    bool ok{};
    std::size_t bytes_written{};
};

[[nodiscard]] HttpEncodeResult build_http_request(const HttpRequest& request, std::span<char> out) noexcept;

enum class HttpParse : std::uint8_t { incomplete = 1, complete, malformed, too_large };

struct HttpResponse {
    HttpParse state{HttpParse::incomplete};
    unsigned status{};
    std::string body;
};

[[nodiscard]] HttpResponse parse_http_response(std::string_view bytes, bool stream_closed);

enum class EnvelopeVerdict : std::uint8_t { completed = 1, tool_error, malformed };

struct EnvelopeResult {
    EnvelopeVerdict verdict{EnvelopeVerdict::malformed};
    std::size_t length{};
};

[[nodiscard]] EnvelopeResult unwrap_mcp_result(std::string_view http_body, std::uint64_t rpc_id,
                                               std::span<char> out) noexcept;

[[nodiscard]] std::optional<std::uint64_t> rpc_id_of(std::string_view request) noexcept;

struct ByteStream {
    bool (*connect_fn)(void* context, Tick deadline) noexcept {};
    bool (*write_fn)(void* context, std::span<const char> bytes, Tick deadline) noexcept {};
    std::ptrdiff_t (*read_fn)(void* context, std::span<char> out, Tick deadline) noexcept {};
    void (*close_fn)(void* context) noexcept {};
    void* context{};
};

class LoopbackStream final {
public:
    explicit LoopbackStream(std::uint16_t port) noexcept : port_(port) {}
    ~LoopbackStream();
    LoopbackStream(const LoopbackStream&) = delete;
    LoopbackStream& operator=(const LoopbackStream&) = delete;
    [[nodiscard]] ByteStream stream() noexcept;

private:
    static bool connect(void* context, Tick deadline) noexcept;
    static bool write(void* context, std::span<const char> bytes, Tick deadline) noexcept;
    static std::ptrdiff_t read(void* context, std::span<char> out, Tick deadline) noexcept;
    static void close(void* context) noexcept;
    std::uint16_t port_;
    int fd_{-1};
};

[[nodiscard]] Tick monotonic_ms() noexcept;

struct HttpApiConfig {
    std::string host;
    std::string mcp_path;
    std::string bearer;
    Tick timeout{2'000};
};

struct HttpApiCounters {
    std::uint64_t calls{};
    std::uint64_t transport_failures{};
    std::uint64_t malformed_replies{};
    std::uint64_t rate_limited{};
    std::uint64_t http_errors{};
};

class HttpApi final {
public:
    HttpApi(HttpApiConfig config, ByteStream stream) noexcept;
    [[nodiscard]] TrueMarketsApi handle() noexcept { return {&HttpApi::dispatch, this}; }
    [[nodiscard]] ApiReply call(ApiCall kind, std::string_view request, std::span<char> response);
    [[nodiscard]] const HttpApiCounters& counters() const noexcept { return counters_; }

private:
    static ApiReply dispatch(void* self, ApiCall kind, std::string_view request, std::span<char> response, Tick) {
        return static_cast<HttpApi*>(self)->call(kind, request, response);
    }
    HttpApiConfig config_;
    ByteStream stream_;
    HttpApiCounters counters_{};
};

}
