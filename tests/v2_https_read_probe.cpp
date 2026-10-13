#include "velocita/v2/https_read.hpp"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    using namespace velocita::v2::transport;
    HttpsReader reader({argv[1], argv[2], std::strtol(argv[4], nullptr, 10)});
    std::array<char, 1024> body{};
    const auto result = reader.get(argv[3], "fixture-token", body);
    if (result.status != HttpsReadStatus::complete &&
        std::any_of(body.begin(), body.end(), [](char c) { return c != '\0'; })) return 3;
    const char* status = "unknown";
    switch (result.status) {
        case HttpsReadStatus::complete: status = "complete"; break;
        case HttpsReadStatus::refused: status = "refused"; break;
        case HttpsReadStatus::unavailable: status = "unavailable"; break;
        case HttpsReadStatus::tls_failure: status = "tls_failure"; break;
        case HttpsReadStatus::timeout: status = "timeout"; break;
        case HttpsReadStatus::oversized: status = "oversized"; break;
        case HttpsReadStatus::transport_failure: status = "transport_failure"; break;
    }
    std::cout << status << ' ' << result.http_status << ' ' << result.body_size << '\n';
}
