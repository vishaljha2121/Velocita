// Whole milliseconds elapsed between calls, carrying the remainder so that no time is lost.
#pragma once

#include <chrono>

namespace velocita::v2 {

template <typename Clock>
class ElapsedMilliseconds final {
public:
    explicit ElapsedMilliseconds(const typename Clock::time_point start) noexcept : last_(start) {}

    [[nodiscard]] int take(const typename Clock::time_point now) noexcept {
        if (now <= last_) return 0;
        const auto whole = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_);
        last_ += whole;
        return static_cast<int>(whole.count());
    }

private:
    typename Clock::time_point last_;
};

}
