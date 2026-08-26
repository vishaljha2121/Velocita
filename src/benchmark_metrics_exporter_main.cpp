#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::atomic_bool running{true};

void stop(int) {
    running.store(false);
}

struct Options {
    std::string listen_address{"127.0.0.1"};
    std::uint16_t port{9300};
    std::filesystem::path metrics_file{"/var/lib/velocita-benchmark/latest.prom"};
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "benchmark-metrics-exporter [--listen-address IP] [--port PORT] "
                         "[--metrics-file PATH]\n";
            std::exit(0);
        }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--listen-address") options.listen_address = value;
        else if (key == "--port") {
            const auto port = std::stoul(value);
            if (port == 0 || port > 65535) throw std::runtime_error("port must be between 1 and 65535");
            options.port = static_cast<std::uint16_t>(port);
        } else if (key == "--metrics-file") options.metrics_file = value;
        else throw std::runtime_error("unknown option: " + key);
    }
    return options;
}

std::string render_metrics(const std::filesystem::path& path) {
    constexpr std::uintmax_t maximum_size = 1U << 20;
    std::ostringstream output;
    output << "# HELP velocita_benchmark_metrics_file_up Whether the latest benchmark snapshot is readable.\n"
           << "# TYPE velocita_benchmark_metrics_file_up gauge\n";
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_size) {
        output << "velocita_benchmark_metrics_file_up 0\n";
        return output.str();
    }
    std::ifstream input(path);
    if (!input) {
        output << "velocita_benchmark_metrics_file_up 0\n";
        return output.str();
    }
    output << "velocita_benchmark_metrics_file_up 1\n" << input.rdbuf();
    return output.str();
}

std::string http_response(const std::string_view status, const std::string_view content_type,
                          const std::string_view body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n" << body;
    return response.str();
}

void send_all(const int socket, const std::string& response) {
    std::size_t sent{};
    while (sent < response.size()) {
#ifdef MSG_NOSIGNAL
        const auto result = ::send(socket, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
#else
        const auto result = ::send(socket, response.data() + sent, response.size() - sent, 0);
#endif
        if (result > 0) sent += static_cast<std::size_t>(result);
        else if (result < 0 && errno == EINTR) continue;
        else return;
    }
}

int listen_socket(const Options& options) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) throw std::runtime_error("creating exporter socket failed: " + std::string(std::strerror(errno)));
    const int reuse = 1;
    if (::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(listener);
        throw std::runtime_error("configuring exporter socket failed");
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.listen_address.c_str(), &endpoint.sin_addr) != 1) {
        ::close(listener);
        throw std::runtime_error("listen address must be an IPv4 address");
    }
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) < 0 ||
        ::listen(listener, 16) < 0) {
        const auto message = std::string(std::strerror(errno));
        ::close(listener);
        throw std::runtime_error("binding exporter endpoint failed: " + message);
    }
    return listener;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        const int listener = listen_socket(options);
        std::cout << "Benchmark snapshot exporter listening on " << options.listen_address
                  << ':' << options.port << std::endl;
        while (running.load()) {
            pollfd descriptor{listener, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 500);
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0 || (descriptor.revents & POLLIN) == 0) continue;
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0) continue;
            pollfd request_ready{client, POLLIN, 0};
            if (::poll(&request_ready, 1, 1000) > 0) {
                std::array<char, 2048> request{};
                const auto length = ::recv(client, request.data(), request.size(), 0);
                const std::string_view line(request.data(), length > 0 ? static_cast<std::size_t>(length) : 0);
                if (line.starts_with("GET /metrics ")) {
                    const auto body = render_metrics(options.metrics_file);
                    send_all(client, http_response("200 OK", "text/plain; version=0.0.4; charset=utf-8", body));
                } else {
                    constexpr std::string_view body = "not found\n";
                    send_all(client, http_response("404 Not Found", "text/plain; charset=utf-8", body));
                }
            }
            ::close(client);
        }
        ::close(listener);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark-metrics-exporter failed: " << error.what() << '\n';
        return 1;
    }
}
