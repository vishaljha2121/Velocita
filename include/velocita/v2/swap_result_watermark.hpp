#pragma once

#include "velocita/v2/digest.hpp"
#include <atomic>

namespace velocita::v2 {

struct SwapDurableResultSeed {
    Digest256 incarnation{}, evidence_digest{};
    std::int64_t index{};
};

class SwapResultWatermark final {
public:
    explicit SwapResultWatermark(Digest256 incarnation) noexcept : incarnation_(incarnation) {}
    [[nodiscard]] bool restore(const SwapDurableResultSeed&) noexcept;
    [[nodiscard]] bool publish(std::int64_t committed_index) noexcept;
    [[nodiscard]] std::int64_t index() const noexcept { return index_.load(std::memory_order_acquire); }
    [[nodiscard]] Digest256 incarnation() const noexcept { return incarnation_; }
private:
    Digest256 incarnation_{};
    std::atomic<std::int64_t> index_{};
};

}
