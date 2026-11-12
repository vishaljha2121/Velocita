// Aeron UDP link for tape processes: one inbound endpoint, one publication per destination.
#pragma once

#include "velocita/aeron_runtime.hpp"
#include "velocita/tape/auth.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aeron {
class FragmentAssembler;
}

namespace velocita::tape {

struct Endpoint {
    std::string host;
    std::uint16_t port{};
    [[nodiscard]] std::string channel() const;
    [[nodiscard]] static Endpoint parse(const std::string& text);
};

inline constexpr std::size_t kMtu = 1216;

[[nodiscard]] std::string aeron_metrics(const AeronRuntime& runtime, const std::string& labels);

class AeronLink {
public:
    AeronLink(AeronRuntime& runtime, const Endpoint& listen, const std::map<Party, Endpoint>& destinations);
    ~AeronLink();
    bool send(const Party& to, std::span<const std::byte> frame);
    std::size_t poll(std::deque<std::vector<std::byte>>& out, std::size_t limit = 64);
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

private:
    std::shared_ptr<aeron::Subscription> subscription_;
    std::map<Party, std::shared_ptr<aeron::Publication>> publications_;
    std::unique_ptr<aeron::FragmentAssembler> assembler_;
    std::deque<std::vector<std::byte>>* sink_{};
    std::uint64_t dropped_{};
};

}
