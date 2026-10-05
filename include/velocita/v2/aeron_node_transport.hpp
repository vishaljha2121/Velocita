// Aeron UDP transports for a voter and for a client of the cluster.
#pragma once

#include "velocita/aeron_runtime.hpp"
#include "velocita/v2/node.hpp"
#include "velocita/v2/node_client.hpp"
#include "velocita/v2/node_config.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace velocita::v2 {

inline constexpr std::int32_t kPeerStreamId = 3001;
inline constexpr std::int32_t kIngressStreamId = 3002;
inline constexpr std::int32_t kResponseStreamId = 3003;

[[nodiscard]] std::string udp_channel(const Endpoint& endpoint);

class AeronNodeTransport final : public NodeTransport {
public:
    static constexpr std::size_t kInboxFrames = 256;
    static constexpr int kPollFragments = 64;

    AeronNodeTransport(AeronRuntime& runtime, const NodeConfig& config);
    ~AeronNodeTransport() override;

    std::optional<std::size_t> receive(Channel channel, std::span<std::byte> buffer) override;
    bool offer_peer(std::int32_t voter_id, std::span<const std::byte> envelope) override;
    bool offer_response(std::int32_t caller_key_id, std::span<const std::byte> envelope) override;

    [[nodiscard]] std::uint64_t dropped_frames() const noexcept { return dropped_; }

private:
    struct Inbound;
    std::unique_ptr<Inbound> peer_;
    std::unique_ptr<Inbound> ingress_;
    std::map<std::int32_t, std::shared_ptr<aeron::Publication>> peers_;
    std::map<std::int32_t, std::shared_ptr<aeron::Publication>> responses_;
    std::uint64_t dropped_{};
};

class AeronClientTransport final : public ClientTransport {
public:
    AeronClientTransport(AeronRuntime& runtime, const std::map<std::int32_t, Endpoint>& voters,
                         const Endpoint& response_endpoint);
    ~AeronClientTransport() override;

    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override;
    std::optional<std::size_t> receive_response(std::span<std::byte> buffer) override;

    [[nodiscard]] std::uint64_t dropped_frames() const noexcept { return dropped_; }

private:
    struct Inbound;
    std::unique_ptr<Inbound> responses_;
    std::map<std::int32_t, std::shared_ptr<aeron::Publication>> ingress_;
    std::uint64_t dropped_{};
};

}
