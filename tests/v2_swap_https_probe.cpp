#include "velocita/v2/swap_https.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace velocita::v2::transport;
namespace {
const char* status(SwapHttpsStatus s) {
    switch (s) {
        case SwapHttpsStatus::complete: return "complete";
        case SwapHttpsStatus::local_refusal: return "local_refusal";
        case SwapHttpsStatus::unavailable: return "unavailable";
        case SwapHttpsStatus::tls_failure: return "tls_failure";
        case SwapHttpsStatus::timeout: return "timeout";
        case SwapHttpsStatus::oversized: return "oversized";
        case SwapHttpsStatus::redirect: return "redirect";
        case SwapHttpsStatus::transport_failure: return "transport_failure";
    }
    return "unknown";
}
const char* exposure(SwapHttpsExposure e) {
    switch (e) {
        case SwapHttpsExposure::not_attempted: return "not_attempted";
        case SwapHttpsExposure::possibly_sent: return "possibly_sent";
        case SwapHttpsExposure::response_received: return "response_received";
    }
    return "unknown";
}
void quoted(std::string_view value) {
    std::cout << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') std::cout << '\\';
        if (c < 32 || c > 126) std::cout << '?'; else std::cout << static_cast<char>(c);
    }
    std::cout << '"';
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--fingerprint") {
        const auto f = SwapHttpsClient::fingerprint();
        std::cout << "{\"curl_headers\":"; quoted(f.curl_headers);
        std::cout << ",\"curl_runtime\":"; quoted(f.curl_runtime);
        std::cout << ",\"tls_backend\":"; quoted(f.tls_backend);
        std::cout << ",\"curl_host\":"; quoted(f.curl_host);
        std::cout << ",\"ssl\":" << (f.ssl ? "true" : "false")
                  << ",\"async_dns\":" << (f.async_dns ? "true" : "false") << "}\n";
        return 0;
    }
    if (argc != 8) return 2;
    const std::string_view origin = argv[1], mode = argv[7];
    if (!origin.starts_with("https://localhost:") && !origin.starts_with("https://127.0.0.1:")) {
        std::cout << "local_refusal not_attempted 0 0\n"; return 0;
    }
    auto method = SwapHttpsMethod::get;
    if (std::string_view(argv[3]) == "POST") method = SwapHttpsMethod::post;
    else if (std::string_view(argv[3]) != "GET") method = static_cast<SwapHttpsMethod>(255);
    SwapHttpsConfig config;
    config.origin = origin;
    config.ca_bundle = argv[2];
    config.timeout_ms = std::strtol(argv[5], nullptr, 10);
    config.max_response_bytes = std::strtoull(argv[6], nullptr, 10);
    config.routes[0] = {SwapHttpsMethod::get,"/v1/gateway/orders/fixture"};
    config.routes[1] = {SwapHttpsMethod::get,"/v1/gateway/assets"};
    config.routes[2] = {SwapHttpsMethod::get,"/v1/gateway/balances"};
    config.routes[3] = {SwapHttpsMethod::post,"/v1/gateway/orders"};
    config.route_count = 4;
    if (mode == "invalid-origin") config.origin = "https://localhost:123@escape.invalid";
    if (mode == "invalid-route") config.routes[0].path = "/v1/../escape";
    if (mode == "empty-routes") config.route_count = 0;
    if (mode == "too-many-routes") config.route_count = kSwapHttpsMaxRoutes + 1;
    if (mode == "duplicate-route") config.routes[1] = config.routes[0];
    std::string body = method == SwapHttpsMethod::post ? "{\"fixture\":true}" : "";
    if (mode == "large-request") body.assign(kSwapHttpsRequestCapacity + 1, 'x');
    if (mode == "get-with-body") body = "{}";
    if (mode == "empty-post") body.clear();
    SwapHttpsResult result;
    {
        SwapHttpsClient client(std::move(config));
        result = client.exchange({method,argv[4],mode == "bad-token" ? "bad\r\ntoken" : "fixture-token",body});
        if (mode == "owned") {
            auto other = client.exchange({SwapHttpsMethod::get,"/v1/gateway/assets","fixture-token",{}});
            other.body.fill('z');
        }
    }
    if (mode == "owned") {
        if (result.body_view() != "{\"ok\":true}") return 4;
        std::cout << "owned_response_preserved\n"; return 0;
    }
    if (result.status != SwapHttpsStatus::complete &&
        (result.body_size != 0 || std::any_of(result.body.begin(), result.body.end(), [](char c) { return c != 0; }))) return 3;
    std::cout << status(result.status) << ' ' << exposure(result.exposure) << ' '
              << result.http_status << ' ' << result.body_size << '\n';
}
