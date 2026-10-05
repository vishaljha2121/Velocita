// velocita-v2-exporter: serves a voter's health page as Prometheus metrics.
#include "velocita/v2/node_health.hpp"
#include "velocita/v2/node_metrics_text.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::atomic<bool> g_running{true};
void stop(int) { g_running = false; }

struct Options {
    std::filesystem::path health;
    std::string host;
    std::string listen{"127.0.0.1"};
    std::uint16_t port{9410};
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--health") o.health = value;
        else if (key == "--host") o.host = value;
        else if (key == "--listen") o.listen = value;
        else if (key == "--port") o.port = static_cast<std::uint16_t>(std::stoul(value));
        else throw std::runtime_error("unknown argument " + key);
    }
    if (o.health.empty() || o.host.empty() || argc % 2 == 0)
        throw std::runtime_error("usage: velocita-v2-exporter --health FILE --host LABEL [--listen IPV4] [--port N]");
    return o;
}

std::string response(std::string_view status, std::string_view body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
    return out.str();
}

void send_all(int socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
        const auto n = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
        const auto n = ::send(socket, data.data() + sent, data.size() - sent, 0);
#endif
        if (n > 0) sent += static_cast<std::size_t>(n);
        else if (n < 0 && errno == EINTR) continue;
        else return;
    }
}

int listen_socket(const Options& o) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket failed");
    const int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in at{};
    at.sin_family = AF_INET;
    at.sin_port = htons(o.port);
    if (::inet_pton(AF_INET, o.listen.c_str(), &at.sin_addr) != 1) throw std::runtime_error("--listen must be IPv4");
    if (::bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof(at)) != 0 || ::listen(fd, 16) != 0)
        throw std::runtime_error("bind " + o.listen + ":" + std::to_string(o.port) + " failed: " + std::strerror(errno));
    return fd;
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        const int listener = listen_socket(options);
        std::cout << "velocita-v2-exporter listening on " << options.listen << ':' << options.port << std::endl;
        while (g_running) {
            pollfd ready{listener, POLLIN, 0};
            if (::poll(&ready, 1, 500) <= 0 || !(ready.revents & POLLIN)) continue;
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0) continue;
            pollfd request{client, POLLIN, 0};
            if (::poll(&request, 1, 1000) > 0) {
                std::array<char, 2048> buffer{};
                const auto n = ::recv(client, buffer.data(), buffer.size(), 0);
                const std::string_view line(buffer.data(), n > 0 ? static_cast<std::size_t>(n) : 0);
                if (line.starts_with("GET /metrics ")) {
                    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                    send_all(client, response("200 OK", velocita::v2::render_node_metrics(
                        velocita::v2::read_node_health(options.health), options.host, now)));
                } else {
                    send_all(client, response("404 Not Found", "not found\n"));
                }
            }
            ::close(client);
        }
        ::close(listener);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "velocita-v2-exporter failed: " << error.what() << '\n';
        return 1;
    }
}
