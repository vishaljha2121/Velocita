#include "velocita/v2/swap_result_watermark.hpp"

namespace velocita::v2 {
bool SwapResultWatermark::restore(const SwapDurableResultSeed& seed) noexcept {
    if (incarnation_.is_zero() || seed.incarnation != incarnation_ || seed.evidence_digest.is_zero() || seed.index <= 0) return false;
    std::int64_t expected{};
    return index_.compare_exchange_strong(expected, seed.index, std::memory_order_release, std::memory_order_relaxed);
}
bool SwapResultWatermark::publish(std::int64_t committed_index) noexcept {
    if(committed_index<=0) { index_.store(-1,std::memory_order_release);return false; }
    auto current=index_.load(std::memory_order_acquire);
    while(current>0) {
        if(committed_index<current) return false;
        if(committed_index==current) return true;
        if(index_.compare_exchange_weak(current,committed_index,std::memory_order_acq_rel,std::memory_order_acquire)) return true;
    }
    return false;
}
}
