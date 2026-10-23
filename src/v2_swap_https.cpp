#include "velocita/v2/swap_https.hpp"

#if !defined(VELOCITA_SWAP_HTTPS_TEST_ONLY) && !defined(VELOCITA_SWAP_RUNTIME_WIRING)
#error "Swap HTTPS transport requires explicit test-only opt-in"
#endif

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
bool ready() {
    static const Runtime runtime;
    const auto* info = runtime.ready ? curl_version_info(CURLVERSION_NOW) : nullptr;
    return info && info->version_num >= 0x075500 && (info->features & CURL_VERSION_SSL) &&
           (info->features & CURL_VERSION_ASYNCHDNS);
}
bool alnum(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
bool safe_origin(std::string_view origin) noexcept {
    if (!origin.starts_with("https://") || origin.size() > 280) return false;
    auto host = origin.substr(8);
    const auto colon = host.find(':');
    if (colon != std::string_view::npos) {
        const auto port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        unsigned value{};
        for (const unsigned char c : port) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + c - '0';
        }
        if (value == 0 || value > 65535) return false;
        host = host.substr(0, colon);
    }
    if (host.empty() || host.size() > 253 || host.back() == '.') return false;
    while (!host.empty()) {
        const auto end = host.find('.');
        const auto label = host.substr(0, end);
        if (label.empty() || label.size() > 63 || !alnum(label.front()) || !alnum(label.back()) ||
            !std::all_of(label.begin(), label.end(), [](unsigned char c) { return alnum(c) || c == '-'; })) return false;
        if (end == std::string_view::npos) break;
        host.remove_prefix(end + 1);
    }
    return true;
}
bool safe_path(std::string_view path) noexcept {
    if (path == "/") return true;
    return path.size() >= 2 && path.size() <= 256 && path.front() == '/' && path.back() != '/' &&
        path.find("//") == std::string_view::npos &&
        std::all_of(path.begin(), path.end(), [](unsigned char c) { return alnum(c) || c == '/' || c == '-' || c == '_'; });
}
bool method_valid(SwapHttpsMethod method) noexcept {
    return method == SwapHttpsMethod::get || method == SwapHttpsMethod::post;
}
struct List {
    curl_slist* value{};
    ~List() { curl_slist_free_all(value); }
    bool add(const char* text) {
        auto* next = curl_slist_append(value, text);
        if (!next) return false;
        value = next; return true;
    }
};
struct Handle {
    CURL* value{curl_easy_init()};
    ~Handle() { if (value) curl_easy_cleanup(value); }
};
struct Sink {
    SwapHttpsResult& result;
    std::size_t limit{}, headers{};
    bool oversized{};
};
std::size_t body_write(char* data, std::size_t size, std::size_t count, void* context) noexcept {
    auto& s = *static_cast<Sink*>(context);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) { s.oversized = true; return 0; }
    const auto n = size * count;
    if (n > s.limit - s.result.body_size) { s.oversized = true; return 0; }
    if (n) std::memcpy(s.result.body.data() + s.result.body_size, data, n);
    s.result.body_size += n;
    return n;
}
std::size_t header_write(char*, std::size_t size, std::size_t count, void* context) noexcept {
    auto& s = *static_cast<Sink*>(context);
    if (size && count > std::numeric_limits<std::size_t>::max() / size) { s.oversized = true; return 0; }
    const auto n = size * count;
    if (n > 8192 - s.headers) { s.oversized = true; return 0; }
    s.headers += n;
    return n;
}
}

SwapHttpsClient::SwapHttpsClient(SwapHttpsConfig config) : config_(std::move(config)) {
    valid_ = safe_origin(config_.origin) && config_.ca_bundle.size() <= 4096 &&
        config_.ca_bundle.find('\0') == std::string::npos && config_.timeout_ms > 0 &&
        config_.timeout_ms <= 30000 && config_.max_response_bytes > 0 &&
        config_.max_response_bytes <= kSwapHttpsBodyCapacity && config_.route_count > 0 &&
        config_.route_count <= config_.routes.size();
    if (!valid_) return;
    for (std::size_t i = 0; i < config_.route_count; ++i) {
        if (!method_valid(config_.routes[i].method) || !safe_path(config_.routes[i].path)) { valid_ = false; return; }
        for (std::size_t j = 0; j < i; ++j)
            if (config_.routes[i].method == config_.routes[j].method && config_.routes[i].path == config_.routes[j].path)
                { valid_ = false; return; }
    }
}

SwapHttpsResult SwapHttpsClient::exchange(const SwapHttpsRequest& request) {
    SwapHttpsResult result;
    const bool matched = valid_ && std::any_of(config_.routes.begin(), config_.routes.begin() + config_.route_count,
        [&](const auto& route) { return route.method == request.method && route.path == request.path; });
    if (!matched || !safe_path(request.path) || !method_valid(request.method) || request.bearer.size() > 8192 ||
        !std::all_of(request.bearer.begin(), request.bearer.end(), [](unsigned char c) { return c > 32 && c < 127; }) ||
        request.body.size() > kSwapHttpsRequestCapacity ||
        (request.method == SwapHttpsMethod::get && !request.body.empty()) ||
        (request.method == SwapHttpsMethod::post && request.body.empty())) {
        result.status = SwapHttpsStatus::local_refusal; return result;
    }
    if (!ready()) return result;
    const auto url = config_.origin + std::string(request.path);
    List headers, resolve;
    if (!headers.add("Accept: application/json") || !headers.add("Content-Type: application/json") ||
        !headers.add("Expect:") || !headers.add("Connection: close")) return result;
    if (!request.bearer.empty() && !headers.add(("Authorization: Bearer " + std::string(request.bearer)).c_str())) return result;
    if (config_.origin.starts_with("https://localhost:")) {
        const auto entry = config_.origin.substr(8) + ":127.0.0.1";
        if (!resolve.add(entry.c_str())) return result;
    }
    Handle handle;
    if (!handle.value) return result;
    Sink sink{result,config_.max_response_bytes};
    bool okay = true;
    const auto set = [&](CURLoption option, auto value) { okay &= curl_easy_setopt(handle.value, option, value) == CURLE_OK; };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_USERAGENT, "velocita-swap-fixture/1");
    set(CURLOPT_HTTPHEADER, headers.value);
    if (resolve.value) set(CURLOPT_RESOLVE, resolve.value);
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_MAXREDIRS, 0L);
    set(CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_NONE));
    set(CURLOPT_UNRESTRICTED_AUTH, 0L);
    set(CURLOPT_PROXY, "");
    set(CURLOPT_NOPROXY, "*");
    set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    if (!config_.ca_bundle.empty()) set(CURLOPT_CAINFO, config_.ca_bundle.c_str());
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_TIMEOUT_MS, config_.timeout_ms);
    set(CURLOPT_CONNECTTIMEOUT_MS, config_.timeout_ms);
    set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    set(CURLOPT_FRESH_CONNECT, 1L);
    set(CURLOPT_FORBID_REUSE, 1L);
    set(CURLOPT_WRITEFUNCTION, &body_write); set(CURLOPT_WRITEDATA, &sink);
    set(CURLOPT_HEADERFUNCTION, &header_write); set(CURLOPT_HEADERDATA, &sink);
    if (request.method == SwapHttpsMethod::get) set(CURLOPT_HTTPGET, 1L);
    else {
        set(CURLOPT_POST, 1L);
        set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
        set(CURLOPT_POSTFIELDS, request.body.data());
    }
    if (!okay) return result;
    result.exposure = SwapHttpsExposure::possibly_sent;
    const auto code = curl_easy_perform(handle.value);
    long http{};
    if (code == CURLE_OK && curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE, &http) == CURLE_OK &&
        http >= 200 && http <= 599) {
        result.http_status = static_cast<unsigned>(http);
        result.exposure = SwapHttpsExposure::response_received;
        if (http < 300 || http >= 400) { result.status = SwapHttpsStatus::complete; return result; }
        result.status = SwapHttpsStatus::redirect;
    } else if (sink.oversized) result.status = SwapHttpsStatus::oversized;
    else if (code == CURLE_OPERATION_TIMEDOUT) result.status = SwapHttpsStatus::timeout;
    else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
             code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CERTPROBLEM)
        result.status = SwapHttpsStatus::tls_failure;
    else result.status = SwapHttpsStatus::transport_failure;
    result.body.fill(0);
    result.body_size = 0;
    return result;
}

SwapHttpsFingerprint SwapHttpsClient::fingerprint() {
    SwapHttpsFingerprint result;
    result.curl_headers = LIBCURL_VERSION;
    if (!ready()) return result;
    const auto* info = curl_version_info(CURLVERSION_NOW);
    result.curl_runtime = info->version ? info->version : "";
    result.tls_backend = info->ssl_version ? info->ssl_version : "";
    result.curl_host = info->host ? info->host : "";
    result.ssl = (info->features & CURL_VERSION_SSL) != 0;
    result.async_dns = (info->features & CURL_VERSION_ASYNCHDNS) != 0;
    return result;
}
}
