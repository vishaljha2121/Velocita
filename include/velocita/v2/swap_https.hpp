#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace velocita::v2::transport {

inline constexpr std::size_t kSwapHttpsBodyCapacity = 65536;
inline constexpr std::size_t kSwapHttpsRequestCapacity = 16384;
inline constexpr std::size_t kSwapHttpsMaxRoutes = 8;
enum class SwapHttpsMethod : std::uint8_t { get, post };
enum class SwapHttpsStatus : std::uint8_t {
    complete, local_refusal, unavailable, tls_failure, timeout, oversized, redirect, transport_failure
};
enum class SwapHttpsExposure : std::uint8_t { not_attempted, possibly_sent, response_received };

struct SwapHttpsRoute {
    SwapHttpsMethod method{};
    std::string path;
};
struct SwapHttpsConfig {
    std::string origin;
    std::string ca_bundle;
    long timeout_ms{2000};
    std::size_t max_response_bytes{kSwapHttpsBodyCapacity};
    std::array<SwapHttpsRoute, kSwapHttpsMaxRoutes> routes{};
    std::size_t route_count{};
};
struct SwapHttpsRequest {
    SwapHttpsMethod method{};
    std::string_view path;
    std::string_view bearer;
    std::string_view body;
};
struct SwapHttpsResult {
    SwapHttpsStatus status{SwapHttpsStatus::unavailable};
    SwapHttpsExposure exposure{SwapHttpsExposure::not_attempted};
    unsigned http_status{};
    std::size_t body_size{};
    std::array<char, kSwapHttpsBodyCapacity> body{};
    [[nodiscard]] std::string_view body_view() const noexcept { return {body.data(), body_size}; }
};
struct SwapHttpsFingerprint {
    std::string curl_headers, curl_runtime, tls_backend, curl_host;
    bool ssl{}, async_dns{};
};

class SwapHttpsClient final {
public:
    explicit SwapHttpsClient(SwapHttpsConfig config);
    SwapHttpsClient(const SwapHttpsClient&) = delete;
    SwapHttpsClient& operator=(const SwapHttpsClient&) = delete;
    [[nodiscard]] SwapHttpsResult exchange(const SwapHttpsRequest& request);
    [[nodiscard]] static SwapHttpsFingerprint fingerprint();
private:
    SwapHttpsConfig config_;
    bool valid_{};
};

}
