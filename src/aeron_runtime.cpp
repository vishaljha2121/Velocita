#include "velocita/aeron_runtime.hpp"

#include "concurrent/AtomicBuffer.h"
#include "util/Exceptions.h"

extern "C" {
#include "util/aeron_error.h"
}

#include <stdexcept>
#include <thread>

namespace velocita {
namespace {

std::runtime_error driver_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + aeron_errmsg());
}

}  // namespace

AeronRuntime::AeronRuntime(std::string directory) {
    if (aeron_driver_context_init(&driver_context_) < 0) {
        throw driver_error("aeron_driver_context_init failed");
    }
    try {
        if (aeron_driver_context_set_dir(driver_context_, directory.c_str()) < 0 ||
            aeron_driver_context_set_dir_delete_on_start(driver_context_, true) < 0 ||
            aeron_driver_context_set_dir_delete_on_shutdown(driver_context_, true) < 0 ||
            aeron_driver_context_set_threading_mode(driver_context_, AERON_THREADING_MODE_SHARED) < 0) {
            throw driver_error("configuring embedded Aeron driver failed");
        }
        if (aeron_driver_init(&driver_, driver_context_) < 0) {
            throw driver_error("aeron_driver_init failed");
        }
        if (aeron_driver_start(driver_, false) < 0) {
            throw driver_error("aeron_driver_start failed");
        }

        aeron::Context context;
        context.aeronDir(directory);
        aeron_ = aeron::Aeron::connect(context);
    } catch (...) {
        if (driver_ != nullptr) {
            aeron_driver_close(driver_);
            driver_ = nullptr;
        }
        aeron_driver_context_close(driver_context_);
        driver_context_ = nullptr;
        throw;
    }
}

AeronRuntime::~AeronRuntime() {
    aeron_.reset();
    if (driver_ != nullptr) {
        aeron_driver_close(driver_);
    }
    if (driver_context_ != nullptr) {
        aeron_driver_context_close(driver_context_);
    }
}

std::shared_ptr<aeron::Publication> AeronRuntime::add_publication(
    const std::string& channel, std::int32_t stream_id, std::chrono::milliseconds timeout) {
    const auto registration_id = aeron_->addPublication(channel, stream_id);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto publication = aeron_->findPublication(registration_id)) {
            return publication;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("timed out creating Aeron publication: " + channel);
}

std::shared_ptr<aeron::Subscription> AeronRuntime::add_subscription(
    const std::string& channel, std::int32_t stream_id, std::chrono::milliseconds timeout) {
    const auto registration_id = aeron_->addSubscription(channel, stream_id);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto subscription = aeron_->findSubscription(registration_id)) {
            return subscription;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("timed out creating Aeron subscription: " + channel);
}

std::int64_t AeronRuntime::offer(aeron::Publication& publication, std::span<const std::byte> bytes) {
    aeron::concurrent::AtomicBuffer buffer(
        reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(bytes.data())), bytes.size());
    return publication.offer(buffer, 0, static_cast<aeron::util::index_t>(bytes.size()));
}

}  // namespace velocita
