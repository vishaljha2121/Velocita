#include "velocita/v2/https_read.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace velocita::v2::transport {
namespace {
struct Runtime {
    bool ready{curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK};
    ~Runtime() { if (ready) curl_global_cleanup(); }
};
bool supported() {
    static const Runtime runtime;
    if (!runtime.ready) return false;
    const auto* info = curl_version_info(CURLVERSION_NOW);
    return info && info->version_num >= 0x075500 && (info->features & CURL_VERSION_SSL) &&
           (info->features & CURL_VERSION_ASYNCHDNS);
}
bool safe_origin(std::string_view origin) {
    if (!origin.starts_with("https://")) return false;
    origin.remove_prefix(8);
    return !origin.empty() && origin.size() <= 260 &&
           std::all_of(origin.begin(), origin.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':';
           });
}
bool safe_path(std::string_view path) {
    if (path == "/v1/gateway/balances" || path == "/v1/gateway/assets") return true;
    constexpr std::string_view prefix = "/v1/gateway/orders/";
    if (!path.starts_with(prefix)) return false;
    path.remove_prefix(prefix.size());
    return !path.empty() && path.size() <= 96 && std::all_of(path.begin(), path.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}
struct Sink {
    std::span<char> body;
    std::size_t used{}, headers{};
    bool oversized{};
};
std::size_t write_body(char* data, std::size_t size, std::size_t count, void* context) noexcept {
    auto& sink = *static_cast<Sink*>(context);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) { sink.oversized = true; return 0; }
    const auto n = size * count;
    if (n > sink.body.size() - sink.used) { sink.oversized = true; return 0; }
    if (n) std::memcpy(sink.body.data() + sink.used, data, n);
    sink.used += n;
    return n;
}
std::size_t write_header(char*, std::size_t size, std::size_t count, void* context) noexcept {
    auto& sink = *static_cast<Sink*>(context);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) { sink.oversized = true; return 0; }
    const auto n = size * count;
    if (n > 8192 - sink.headers) { sink.oversized = true; return 0; }
    sink.headers += n;
    return n;
}
struct Headers {
    curl_slist* value{};
    ~Headers() { curl_slist_free_all(value); }
    bool add(const char* text) {
        auto* next = curl_slist_append(value, text);
        if (!next) return false;
        value = next; return true;
    }
};
}
HttpsReader::HttpsReader(HttpsReadConfig config) : config_(std::move(config)) {
    if (safe_origin(config_.origin) && config_.timeout_ms > 0 && config_.timeout_ms <= 30'000 && supported())
        handle_ = curl_easy_init();
}
HttpsReader::~HttpsReader() { if (handle_) curl_easy_cleanup(static_cast<CURL*>(handle_)); }
HttpsReadResult HttpsReader::get(std::string_view path, std::string_view bearer, std::span<char> body) {
    if (!safe_path(path) || bearer.size() > 8192 || body.empty() || body.size() > (1U << 20) ||
        !std::all_of(bearer.begin(), bearer.end(), [](unsigned char c) { return c > 32 && c < 127; }))
        return {HttpsReadStatus::refused};
    if (!handle_) return {};
    auto* handle = static_cast<CURL*>(handle_);
    curl_easy_reset(handle);
    Sink sink{body};
    const auto url = config_.origin + std::string(path);
    Headers headers;
    if (!headers.add("Accept: application/json")) return {};
    if (!bearer.empty() && !headers.add(("Authorization: Bearer " + std::string(bearer)).c_str())) return {};
    bool okay = true;
    const auto set = [&](CURLoption key, auto value) { okay &= curl_easy_setopt(handle, key, value) == CURLE_OK; };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_HTTPGET, 1L);
    set(CURLOPT_USERAGENT, "velocita-contract-read/1");
    set(CURLOPT_HTTPHEADER, headers.value);
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_PROXY, "");
    set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    if (!config_.ca_bundle.empty()) set(CURLOPT_CAINFO, config_.ca_bundle.c_str());
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_TIMEOUT_MS, config_.timeout_ms);
    set(CURLOPT_CONNECTTIMEOUT_MS, config_.timeout_ms);
    set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    set(CURLOPT_WRITEFUNCTION, &write_body); set(CURLOPT_WRITEDATA, &sink);
    set(CURLOPT_HEADERFUNCTION, &write_header); set(CURLOPT_HEADERDATA, &sink);
    if (!okay) return {};
    const auto code = curl_easy_perform(handle);
    long http = 0;
    if (code == CURLE_OK && curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http) == CURLE_OK &&
        http >= 200 && http <= 599 && (http < 300 || http >= 400))
        return {HttpsReadStatus::complete, static_cast<unsigned>(http), sink.used};
    std::fill(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(sink.used), '\0');
    if (sink.oversized) return {HttpsReadStatus::oversized};
    if (code == CURLE_OPERATION_TIMEDOUT) return {HttpsReadStatus::timeout};
    if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR || code == CURLE_SSL_CACERT_BADFILE)
        return {HttpsReadStatus::tls_failure};
    if (code == CURLE_OK && http >= 300 && http < 400) return {HttpsReadStatus::refused, static_cast<unsigned>(http)};
    return {HttpsReadStatus::transport_failure};
}
}
