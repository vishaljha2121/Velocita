#include "velocita/v2/truemarkets_transport.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace velocita::v2::truemarkets::transport {
namespace {

bool token_text(const std::string_view text) noexcept {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](const char c) { return c > 0x20 && c < 0x7f; });
}

class Appender final {
public:
    explicit Appender(const std::span<char> out) noexcept : out_(out) {}
    void put(const std::string_view text) noexcept {
        for (const char c : text) {
            if (at_ < out_.size()) out_[at_] = c;
            ++at_;
        }
    }
    void number(std::size_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0) put(std::string_view(&digits[--count], 1));
    }
    [[nodiscard]] HttpEncodeResult result() const noexcept {
        return at_ <= out_.size() ? HttpEncodeResult{true, at_} : HttpEncodeResult{};
    }

private:
    std::span<char> out_;
    std::size_t at_{};
};

char lower(const char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

bool same_name(const std::string_view left, const std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b) { return lower(a) == b; });
}

std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

std::optional<std::size_t> decimal(const std::string_view text) noexcept {
    if (text.empty() || text.size() > 9) return std::nullopt;
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    return value;
}

std::optional<std::size_t> hexadecimal(const std::string_view text) noexcept {
    if (text.empty() || text.size() > 6) return std::nullopt;
    std::size_t value = 0;
    for (const char c : text) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return std::nullopt;
        value = value * 16 + static_cast<std::size_t>(digit);
    }
    return value;
}

HttpResponse finished(const HttpParse state, const unsigned status = 0, std::string body = {}) {
    HttpResponse response;
    response.state = state;
    response.status = status;
    response.body = std::move(body);
    return response;
}

HttpResponse unfinished(const bool stream_closed) {
    return finished(stream_closed ? HttpParse::malformed : HttpParse::incomplete);
}

HttpResponse parse_chunked(std::string_view rest, const unsigned status, const bool stream_closed) {
    std::string body;
    for (;;) {
        const auto line_end = rest.find("\r\n");
        if (line_end == std::string_view::npos) return unfinished(stream_closed);
        auto size_text = rest.substr(0, line_end);
        if (const auto extension = size_text.find(';'); extension != std::string_view::npos)
            size_text = size_text.substr(0, extension);
        const auto size = hexadecimal(trimmed(size_text));
        if (!size) return finished(HttpParse::malformed);
        rest.remove_prefix(line_end + 2);
        if (*size == 0) {
            if (rest.substr(0, 2) == "\r\n") return finished(HttpParse::complete, status, std::move(body));
            return rest.find("\r\n\r\n") != std::string_view::npos ? finished(HttpParse::complete, status, std::move(body))
                                                                   : unfinished(stream_closed);
        }
        if (body.size() + *size > kMaxHttpBodyBytes) return finished(HttpParse::too_large);
        if (rest.size() < *size + 2) return unfinished(stream_closed);
        if (rest.substr(*size, 2) != "\r\n") return finished(HttpParse::malformed);
        body.append(rest.substr(0, *size));
        rest.remove_prefix(*size + 2);
    }
}

bool append_unescaped(const std::string_view quoted, const std::span<char> out, std::size_t& length) noexcept {
    if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"') return false;
    const auto text = quoted.substr(1, quoted.size() - 2);
    length = 0;
    const auto put = [&](const char c) {
        if (length == out.size()) return false;
        out[length++] = c;
        return true;
    };
    for (std::size_t at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c != '\\') {
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e || !put(c)) return false;
            continue;
        }
        if (++at == text.size()) return false;
        char decoded = 0;
        switch (text[at]) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            case 'u': {
                if (at + 4 >= text.size()) return false;
                const auto code = hexadecimal(text.substr(at + 1, 4));
                if (!code || *code < 0x20 || *code > 0x7e) return false;
                decoded = static_cast<char>(*code);
                at += 4;
                break;
            }
            default: return false;
        }
        if (!put(decoded)) return false;
    }
    return true;
}

EnvelopeResult copy_raw(const std::string_view raw, const std::span<char> out, const EnvelopeVerdict verdict) noexcept {
    if (raw.size() > out.size()) return {};
    std::copy(raw.begin(), raw.end(), out.begin());
    return {verdict, raw.size()};
}

EnvelopeResult unwrap_document(const std::string_view document, const std::uint64_t rpc_id,
                               const std::span<char> out) noexcept {
    if (validate_json_object(document) != JsonError::none) return {};
    const auto version = json_member_string(document, "jsonrpc");
    const auto id = json_member_raw(document, "id");
    if (!version || *version != "2.0" || !id || decimal(*id) != std::optional<std::size_t>{rpc_id}) return {};
    if (const auto error = json_member_raw(document, "error")) return copy_raw(*error, out, EnvelopeVerdict::tool_error);
    const auto result = json_member_raw(document, "result");
    if (!result || validate_json_object(*result) != JsonError::none) return {};
    const auto is_error = json_member_raw(*result, "isError");
    if (is_error && *is_error != "true" && *is_error != "false") return {};
    const auto content = json_member_raw(*result, "content");
    std::array<std::string_view, 4> elements{};
    const auto count = content ? json_array_elements(*content, elements) : std::nullopt;
    if (!count || *count == 0) return {};
    const auto kind = json_member_string(elements[0], "type");
    const auto text = json_member_raw(elements[0], "text");
    std::size_t length = 0;
    if (!kind || *kind != "text" || !text || !append_unescaped(*text, out, length)) return {};
    return {is_error && *is_error == "true" ? EnvelopeVerdict::tool_error : EnvelopeVerdict::completed, length};
}

bool wait_for(const int fd, const short events, const Tick deadline) noexcept {
    for (;;) {
        const auto now = monotonic_ms();
        if (now >= deadline) return false;
        pollfd waiting{fd, events, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(std::min<Tick>(deadline - now, 1'000)));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return false;
        if (ready > 0) return true;
    }
}

}

HttpEncodeResult build_http_request(const HttpRequest& request, const std::span<char> out) noexcept {
    const bool post = request.method == HttpMethod::post;
    if ((!post && request.method != HttpMethod::get) || !token_text(request.host) || !token_text(request.path) ||
        request.path.front() != '/' || (!request.bearer.empty() && !token_text(request.bearer)) ||
        (!post && !request.body.empty()) || request.body.size() > kMaxTmRequestBytes) {
        return {};
    }
    Appender text(out);
    text.put(post ? "POST " : "GET ");
    text.put(request.path);
    text.put(" HTTP/1.1\r\nHost: ");
    text.put(request.host);
    text.put("\r\nUser-Agent: velocita-v2\r\nAccept: application/json, text/event-stream\r\nConnection: close\r\n");
    if (!request.bearer.empty()) {
        text.put("Authorization: Bearer ");
        text.put(request.bearer);
        text.put("\r\n");
    }
    if (post) {
        text.put("Content-Type: application/json\r\nContent-Length: ");
        text.number(request.body.size());
        text.put("\r\n");
    }
    text.put("\r\n");
    text.put(request.body);
    return text.result();
}

HttpResponse parse_http_response(const std::string_view bytes, const bool stream_closed) {
    const auto header_end = bytes.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (bytes.size() > kMaxHttpHeaderBytes) return finished(HttpParse::too_large);
        return unfinished(stream_closed);
    }
    if (header_end > kMaxHttpHeaderBytes) return finished(HttpParse::too_large);
    const auto head = bytes.substr(0, header_end);
    const auto status_end = head.find("\r\n");
    const auto status_line = head.substr(0, status_end);
    if (status_line.size() < 12 || (status_line.substr(0, 9) != "HTTP/1.1 " && status_line.substr(0, 9) != "HTTP/1.0 ") ||
        (status_line.size() > 12 && status_line[12] != ' '))
        return finished(HttpParse::malformed);
    const auto status = decimal(status_line.substr(9, 3));
    if (!status || *status < 200 || *status > 599) return finished(HttpParse::malformed);

    std::optional<std::size_t> content_length;
    bool chunked = false;
    auto lines = status_end == std::string_view::npos ? std::string_view{} : head.substr(status_end + 2);
    while (!lines.empty()) {
        const auto line_end = lines.find("\r\n");
        const auto line = lines.substr(0, line_end);
        lines = line_end == std::string_view::npos ? std::string_view{} : lines.substr(line_end + 2);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return finished(HttpParse::malformed);
        const auto name = line.substr(0, colon);
        const auto value = trimmed(line.substr(colon + 1));
        if (same_name(name, "content-length")) {
            const auto parsed = decimal(value);
            if (!parsed || (content_length && *content_length != *parsed)) return finished(HttpParse::malformed);
            content_length = parsed;
        } else if (same_name(name, "transfer-encoding")) {
            if (!same_name(value, "chunked")) return finished(HttpParse::malformed);
            chunked = true;
        }
    }
    if (chunked && content_length) return finished(HttpParse::malformed);
    const auto rest = bytes.substr(header_end + 4);
    const auto code = static_cast<unsigned>(*status);
    if (code == 204 || code == 304) return finished(HttpParse::complete, code);
    if (chunked) return parse_chunked(rest, code, stream_closed);
    if (content_length) {
        if (*content_length > kMaxHttpBodyBytes) return finished(HttpParse::too_large);
        if (rest.size() < *content_length) return unfinished(stream_closed);
        if (rest.size() > *content_length) return finished(HttpParse::malformed);
        return finished(HttpParse::complete, code, std::string(rest));
    }
    if (rest.size() > kMaxHttpBodyBytes) return finished(HttpParse::too_large);
    return stream_closed ? finished(HttpParse::complete, code, std::string(rest)) : finished(HttpParse::incomplete);
}

EnvelopeResult unwrap_mcp_result(const std::string_view http_body, const std::uint64_t rpc_id,
                                 const std::span<char> out) noexcept {
    const auto body = trimmed(http_body);
    if (!body.empty() && body.front() == '{') return unwrap_document(body, rpc_id, out);
    auto lines = body;
    while (!lines.empty()) {
        const auto line_end = lines.find('\n');
        const auto line = trimmed(lines.substr(0, line_end));
        lines = line_end == std::string_view::npos ? std::string_view{} : lines.substr(line_end + 1);
        if (line.substr(0, 5) != "data:") continue;
        const auto found = unwrap_document(trimmed(line.substr(5)), rpc_id, out);
        if (found.verdict != EnvelopeVerdict::malformed) return found;
    }
    return {};
}

std::optional<std::uint64_t> rpc_id_of(const std::string_view request) noexcept {
    if (validate_json_object(request) != JsonError::none) return std::nullopt;
    const auto id = json_member_raw(request, "id");
    const auto value = id ? decimal(*id) : std::nullopt;
    if (!value) return std::nullopt;
    return static_cast<std::uint64_t>(*value);
}

Tick monotonic_ms() noexcept {
    return static_cast<Tick>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count());
}

LoopbackStream::~LoopbackStream() { close(this); }

ByteStream LoopbackStream::stream() noexcept {
    return {&LoopbackStream::connect, &LoopbackStream::write, &LoopbackStream::read, &LoopbackStream::close, this};
}

bool LoopbackStream::connect(void* context, const Tick deadline) noexcept {
    auto& self = *static_cast<LoopbackStream*>(context);
    close(context);
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(self.port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int flags = ::fcntl(fd, F_GETFL, 0);
    bool connected = flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
    if (connected && ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        int error = 0;
        socklen_t size = sizeof(error);
        connected = errno == EINPROGRESS && wait_for(fd, POLLOUT, deadline) &&
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
    }
    if (!connected) {
        ::close(fd);
        return false;
    }
    self.fd_ = fd;
    return true;
}

bool LoopbackStream::write(void* context, std::span<const char> bytes, const Tick deadline) noexcept {
    const auto& self = *static_cast<LoopbackStream*>(context);
    while (!bytes.empty()) {
        if (self.fd_ < 0 || !wait_for(self.fd_, POLLOUT, deadline)) return false;
#if defined(MSG_NOSIGNAL)
        const auto sent = ::send(self.fd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#else
        const auto sent = ::send(self.fd_, bytes.data(), bytes.size(), 0);
#endif
        if (sent < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (sent <= 0) return false;
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
    return true;
}

std::ptrdiff_t LoopbackStream::read(void* context, const std::span<char> out, const Tick deadline) noexcept {
    const auto& self = *static_cast<LoopbackStream*>(context);
    for (;;) {
        if (self.fd_ < 0 || !wait_for(self.fd_, POLLIN, deadline)) return -1;
        const auto got = ::recv(self.fd_, out.data(), out.size(), 0);
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        return got;
    }
}

void LoopbackStream::close(void* context) noexcept {
    auto& self = *static_cast<LoopbackStream*>(context);
    if (self.fd_ >= 0) ::close(self.fd_);
    self.fd_ = -1;
}

HttpApi::HttpApi(HttpApiConfig config, const ByteStream stream) noexcept
    : config_(std::move(config)), stream_(stream) {}

ApiReply HttpApi::call(const ApiCall kind, const std::string_view request, const std::span<char> response) {
    ++counters_.calls;
    const bool status_read = kind == ApiCall::order_status;
    const auto id = status_read ? std::optional<std::uint64_t>{0} : rpc_id_of(request);
    std::array<char, kMaxHttpRequestBytes> wire{};
    const auto encoded = id ? build_http_request(
                                  status_read
                                      ? HttpRequest{HttpMethod::get, config_.host, request, config_.bearer, {}}
                                      : HttpRequest{HttpMethod::post, config_.host, config_.mcp_path, config_.bearer, request},
                                  wire)
                            : HttpEncodeResult{};
    if (!encoded.ok || !stream_.connect_fn || !stream_.write_fn || !stream_.read_fn || !stream_.close_fn) {
        ++counters_.transport_failures;
        return {};
    }
    const auto deadline = monotonic_ms() + config_.timeout;
    std::string received;
    HttpResponse parsed;
    if (stream_.connect_fn(stream_.context, deadline) &&
        stream_.write_fn(stream_.context, std::span<const char>(wire).first(encoded.bytes_written), deadline)) {
        std::array<char, 4'096> chunk{};
        while (parsed.state == HttpParse::incomplete) {
            const auto got = stream_.read_fn(stream_.context, chunk, deadline);
            if (got < 0) break;
            received.append(chunk.data(), static_cast<std::size_t>(got));
            if (received.size() > kMaxHttpHeaderBytes + kMaxHttpBodyBytes + 1'024) {
                parsed.state = HttpParse::too_large;
                break;
            }
            parsed = parse_http_response(received, got == 0);
        }
        stream_.close_fn(stream_.context);
    } else {
        stream_.close_fn(stream_.context);
        ++counters_.transport_failures;
        return {};
    }
    if (parsed.state != HttpParse::complete) {
        ++(parsed.state == HttpParse::incomplete ? counters_.transport_failures : counters_.malformed_replies);
        return {};
    }
    if (parsed.status == 429) {
        ++counters_.rate_limited;
        return {ApiStatus::rate_limited, 0};
    }
    if (parsed.status < 200 || parsed.status > 299) {
        ++counters_.http_errors;
        const auto length = std::min(parsed.body.size(), response.size());
        std::copy_n(parsed.body.begin(), length, response.begin());
        return {ApiStatus::tool_error, length};
    }
    if (status_read) {
        if (parsed.body.size() > response.size()) {
            ++counters_.malformed_replies;
            return {};
        }
        std::copy(parsed.body.begin(), parsed.body.end(), response.begin());
        return {ApiStatus::completed, parsed.body.size()};
    }
    const auto unwrapped = unwrap_mcp_result(parsed.body, *id, response);
    if (unwrapped.verdict == EnvelopeVerdict::malformed) {
        ++counters_.malformed_replies;
        return {};
    }
    return {unwrapped.verdict == EnvelopeVerdict::completed ? ApiStatus::completed : ApiStatus::tool_error,
            unwrapped.length};
}

}
