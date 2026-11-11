#include "velocita/tape/link.hpp"

#include "FragmentAssembler.h"

#include <stdexcept>

namespace velocita::tape {

namespace {
constexpr std::int32_t kStreamId = 7101;
}

std::string Endpoint::channel() const { return "aeron:udp?endpoint=" + host + ":" + std::to_string(port); }

Endpoint Endpoint::parse(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0) throw std::runtime_error("endpoint must be host:port: " + text);
    const auto port = std::stoul(text.substr(colon + 1));
    if (port == 0 || port > 65535) throw std::runtime_error("bad port in " + text);
    return {text.substr(0, colon), static_cast<std::uint16_t>(port)};
}

AeronLink::AeronLink(AeronRuntime& runtime, const Endpoint& listen, const std::map<Party, Endpoint>& destinations) {
    subscription_ = runtime.add_subscription(listen.channel(), kStreamId);
    for (const auto& [party, endpoint] : destinations)
        publications_.emplace(party, runtime.add_publication(endpoint.channel(), kStreamId));
    assembler_ = std::make_unique<aeron::FragmentAssembler>(
        [this](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset, aeron::util::index_t length,
               aeron::Header&) {
            if (!sink_ || static_cast<std::size_t>(length) > kMaxEnvelopeBytes) {
                ++dropped_;
                return;
            }
            const auto* begin = reinterpret_cast<const std::byte*>(buffer.buffer() + offset);
            sink_->emplace_back(begin, begin + length);
        });
}

AeronLink::~AeronLink() = default;

bool AeronLink::send(const Party& to, std::span<const std::byte> frame) {
    const auto it = publications_.find(to);
    if (it == publications_.end()) return false;
    const auto result = AeronRuntime::offer(*it->second, frame);
    if (result == aeron::PUBLICATION_CLOSED || result == aeron::MAX_POSITION_EXCEEDED)
        throw std::runtime_error("Aeron publication became unusable");
    if (result < 0) ++dropped_;
    return result >= 0;
}

std::size_t AeronLink::poll(std::deque<std::vector<std::byte>>& out, std::size_t limit) {
    sink_ = &out;
    const auto before = out.size();
    subscription_->poll(assembler_->handler(), static_cast<int>(limit));
    sink_ = nullptr;
    return out.size() - before;
}

}
