#include "velocita/client_session.hpp"

#include "velocita/aeron_runtime.hpp"

#include "FragmentAssembler.h"

#include <span>
#include <stdexcept>
#include <utility>

namespace velocita {

struct AeronClientSession::Impl {
    explicit Impl(ClientSessionOptions supplied, ResponseHandler handler)
        : options(std::move(supplied)),
          response_handler(std::move(handler)),
          aeron(options.aeron_dir),
          responses(aeron.add_subscription(options.response_channel, options.response_stream)),
          assembler([this](aeron::concurrent::AtomicBuffer& buffer, aeron::util::index_t offset,
                           aeron::util::index_t length, aeron::Header&) {
              const auto* begin = reinterpret_cast<const std::byte*>(buffer.buffer() + offset);
              response_handler(decode_response(
                  std::span<const std::byte>(begin, static_cast<std::size_t>(length))));
          }) {
        validate_request_targets(options.request_targets, options.required_members);
        requests.reserve(options.request_targets.size());
        for (const auto& request_target : options.request_targets) {
            requests.push_back(aeron.add_publication(request_target.channel, options.request_stream));
        }
    }

    ClientSessionOptions options;
    ResponseHandler response_handler;
    AeronRuntime aeron;
    std::shared_ptr<aeron::Subscription> responses;
    std::vector<std::shared_ptr<aeron::Publication>> requests;
    aeron::FragmentAssembler assembler;
};

AeronClientSession::AeronClientSession(ClientSessionOptions options,
                                       ResponseHandler response_handler)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(response_handler))) {}

AeronClientSession::~AeronClientSession() = default;

std::int64_t AeronClientSession::offer(const std::size_t channel_index, const Command& command) {
    if (channel_index >= impl_->requests.size()) throw std::out_of_range("request channel index");
    const auto encoded = encode_command_fixed(command);
    return AeronRuntime::offer(*impl_->requests[channel_index], encoded);
}

int AeronClientSession::poll(const int fragment_limit) {
    auto handler = impl_->assembler.handler();
    return impl_->responses->poll(handler, fragment_limit);
}

bool AeronClientSession::is_connected(const std::size_t channel_index) const {
    if (channel_index >= impl_->requests.size()) throw std::out_of_range("request channel index");
    return impl_->requests[channel_index]->isConnected();
}

std::size_t AeronClientSession::channel_count() const noexcept {
    return impl_->requests.size();
}

const std::string& AeronClientSession::channel(const std::size_t channel_index) const {
    if (channel_index >= impl_->options.request_targets.size()) throw std::out_of_range("request channel index");
    return impl_->options.request_targets[channel_index].channel;
}

std::int32_t AeronClientSession::node_id(const std::size_t channel_index) const {
    if (channel_index >= impl_->options.request_targets.size()) throw std::out_of_range("request channel index");
    return impl_->options.request_targets[channel_index].node_id;
}

}
