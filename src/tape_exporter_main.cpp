// velocita-tape-exporter: serves the tape's metrics files over HTTP for Prometheus. Reads only; links no tape code.
#include <arpa/inet.h>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace {

volatile std::sig_atomic_t running = 1;
void stop(int) { running = 0; }

void reply(int fd, const std::string& status, const std::string& body, const std::string& type) {
    const auto head = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    (void)!::write(fd, head.data(), head.size());
    (void)!::write(fd, body.data(), body.size());
}

}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: velocita-tape-exporter BIND_ADDRESS PORT METRICS_FILE...\n";
        return 64;
    }
    const std::vector<std::string> files(argv + 3, argv + argc);
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(std::stoi(argv[2])));
    if (::inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1 || ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 16) != 0) {
        std::cerr << "velocita-tape-exporter: cannot listen on " << argv[1] << ":" << argv[2] << "\n";
        return 70;
    }
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (running) {
        const int client = ::accept(fd, nullptr, nullptr);
        if (client < 0) continue;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char request[1024];
        const auto n = ::read(client, request, sizeof(request) - 1);
        const std::string line = n > 0 ? std::string(request, static_cast<std::size_t>(n)) : std::string();
        if (line.rfind("GET /metrics", 0) == 0) {
            std::string body;
            for (const auto& file : files) {
                std::ifstream in(file);
                if (in) body.append(std::istreambuf_iterator<char>(in), {});
            }
            if (body.empty()) reply(client, "503 Service Unavailable", "metrics files not written yet\n", "text/plain");
            else reply(client, "200 OK", body, "text/plain; version=0.0.4");
        } else {
            reply(client, "404 Not Found", "only /metrics\n", "text/plain");
        }
        ::close(client);
    }
    ::close(fd);
    return 0;
}
