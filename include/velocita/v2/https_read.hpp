#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace velocita::v2::transport {

enum class HttpsReadStatus { complete, refused, unavailable, tls_failure, timeout, oversized, transport_failure };
struct HttpsReadResult {
    HttpsReadStatus status{HttpsReadStatus::unavailable};
    unsigned http_status{};
    std::size_t body_size{};
};
struct HttpsReadConfig {
    std::string origin;
    std::string ca_bundle;
    long timeout_ms{2'000};
};
class HttpsReader final {
public:
    explicit HttpsReader(HttpsReadConfig config);
    ~HttpsReader();
    HttpsReader(const HttpsReader&) = delete;
    HttpsReader& operator=(const HttpsReader&) = delete;
    [[nodiscard]] HttpsReadResult get(std::string_view path, std::string_view bearer, std::span<char> body);
private:
    HttpsReadConfig config_;
    void* handle_{};
};
}
