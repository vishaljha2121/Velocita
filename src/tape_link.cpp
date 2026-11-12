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

std::string aeron_metrics(const AeronRuntime& runtime, const std::string& labels) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"Bytes sent", "bytes_sent"},
        {"Bytes received", "bytes_received"},
        {"Retransmitted bytes", "retransmitted_bytes"},
        {"Retransmits sent", "retransmits_sent"},
        {"NAKs sent", "naks_sent"},
        {"NAKs received", "naks_received"},
        {"Heartbeats sent", "heartbeats_sent"},
        {"Status Messages sent", "status_messages_sent"},
        {"Sender flow control limits", "back_pressure_events"},
        {"Short sends", "short_sends"},
        {"Loss gap fills", "loss_gap_fills"},
        {"Invalid packets", "invalid_packets"},
        {"Retransmit Pool Overflow count", "retransmit_pool_overflows"},
        {"Errors", "errors"},
    };
    std::string out;
    for (const auto& c : runtime.system_counters())
        for (const auto& [prefix, name] : kNames)
            if (c.label.rfind(prefix, 0) == 0) {
                out += "velocita_tape_aeron_" + std::string(name) + "_total{" + labels + "} " + std::to_string(c.value) + "\n";
                break;
            }
    return out;
}

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
