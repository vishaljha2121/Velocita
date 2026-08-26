#pragma once

#include "velocita/wire.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace velocita {

struct ClientSessionOptions {
    std::string aeron_dir{"/dev/shm/aeron-raft-client"};
    std::vector<std::string> request_channels;
    std::string response_channel{"aeron:udp?endpoint=0.0.0.0:40101"};
    std::int32_t request_stream{1001};
    std::int32_t response_stream{1002};
};

class AeronClientSession {
public:
    using ResponseHandler = std::function<void(const Response&)>;

    AeronClientSession(ClientSessionOptions options, ResponseHandler response_handler);
    ~AeronClientSession();
    AeronClientSession(const AeronClientSession&) = delete;
    AeronClientSession& operator=(const AeronClientSession&) = delete;

    [[nodiscard]] std::int64_t offer(std::size_t channel_index, const Command& command);
    int poll(int fragment_limit = 100);
    [[nodiscard]] bool is_connected(std::size_t channel_index) const;
    [[nodiscard]] std::size_t channel_count() const noexcept;
    [[nodiscard]] const std::string& channel(std::size_t channel_index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace velocita
