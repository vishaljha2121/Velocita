#pragma once

#include "Aeron.h"
#include "Publication.h"
#include "Subscription.h"

extern "C" {
#include "aeronmd.h"
}

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace velocita {

class AeronRuntime {
public:
    explicit AeronRuntime(std::string directory);
    ~AeronRuntime();
    AeronRuntime(const AeronRuntime&) = delete;
    AeronRuntime& operator=(const AeronRuntime&) = delete;

    std::shared_ptr<aeron::Publication> add_publication(
        const std::string& channel, std::int32_t stream_id,
        std::chrono::milliseconds timeout = std::chrono::seconds(10));
    std::shared_ptr<aeron::Subscription> add_subscription(
        const std::string& channel, std::int32_t stream_id,
        std::chrono::milliseconds timeout = std::chrono::seconds(10));
    static std::int64_t offer(aeron::Publication& publication, std::span<const std::byte> bytes);

private:
    aeron_driver_context_t* driver_context_{};
    aeron_driver_t* driver_{};
    std::shared_ptr<aeron::Aeron> aeron_;
};

}  // namespace velocita

