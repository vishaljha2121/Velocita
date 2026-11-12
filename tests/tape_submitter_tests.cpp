// The recorder's submitter: unacknowledged observations are resent with backoff and a per-window cap.
#include "velocita/tape/recorder.hpp"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace velocita::tape;
using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

Item gap_item(std::uint64_t n) {
    Item item;
    item.is_event = true;
    item.event.kind = EventKind::gap;
    item.event.expected = n;
    item.event.received = n + 1;
    return item;
}

void test_unacknowledged_items_back_off() {
    Ring ring(4096);
    for (std::uint64_t n = 0; n < 20; ++n) ring.push(gap_item(n));
    std::vector<std::chrono::steady_clock::time_point> sends;
    auto now = std::chrono::steady_clock::time_point{} + 1h;
    Submitter s(1, 77, ring, 4096, 2000ms, [&](std::span<const std::byte>) { sends.push_back(now); });
    s.step(now);
    const auto first = sends.size();
    check(first >= 20, "every item is sent once at once");
    for (int ms = 0; ms < 60000; ms += 10) {
        now += 10ms;
        s.step(now);
    }
    const auto resends = sends.size() - first;
    check(resends >= first, "unacknowledged items are resent");
    check(resends <= first * 5, "resends back off instead of repeating every interval (" + std::to_string(resends) +
                                    " resends of " + std::to_string(first) + " items in 60 s)");
    std::size_t worst = 0;
    for (std::size_t i = first; i < sends.size(); ++i) {
        std::size_t in_window = 0;
        for (std::size_t j = i; j < sends.size() && sends[j] - sends[i] < 2000ms; ++j) ++in_window;
        if (in_window > worst) worst = in_window;
    }
    check(worst <= 64, "at most 64 resends in any resend interval");
    s.on_ack(77, s.next_request() - 1);
    const auto before = sends.size();
    for (int ms = 0; ms < 70000; ms += 10) {
        now += 10ms;
        s.step(now);
    }
    check(s.unacked() == 0 && sends.size() == before, "an acknowledgement stops every resend");
}

void test_a_long_outage_is_capped_per_window() {
    Ring ring(4096);
    for (std::uint64_t n = 0; n < 1000; ++n) ring.push(gap_item(n));
    std::vector<std::chrono::steady_clock::time_point> sends;
    auto now = std::chrono::steady_clock::time_point{} + 1h;
    Submitter s(2, 88, ring, 4096, 2000ms, [&](std::span<const std::byte>) { sends.push_back(now); });
    s.step(now);
    const auto first = sends.size();
    for (int ms = 0; ms < 30000; ms += 10) {
        now += 10ms;
        s.step(now);
    }
    check((sends.size() - first) <= 64 * 15 + 64, "a backlog of 1000 is resent at no more than 64 per interval");
}

void test_sending_stays_within_the_byte_budget() {
    Ring ring(4096);
    for (std::uint64_t n = 0; n < 2000; ++n) ring.push(gap_item(n));
    std::map<long long, std::size_t> per_second;
    std::size_t total = 0;
    auto now = std::chrono::steady_clock::time_point{} + 1h;
    const auto origin = now;
    Submitter s(3, 99, ring, 4096, 2000ms, [&](std::span<const std::byte> e) {
        per_second[std::chrono::duration_cast<std::chrono::seconds>(now - origin).count()] += e.size();
        total += e.size();
    }, 2 * 1024, 4 * 1024);
    for (int ms = 0; ms < 30000; ms += 10) {
        s.step(now);
        now += 10ms;
    }
    std::size_t worst = 0;
    for (const auto& [second, bytes] : per_second) worst = std::max(worst, bytes);
    check(worst <= 2 * 1024 + 4 * 1024, "no second sends more than the budget plus one burst (" + std::to_string(worst) + " bytes)");
    check(total >= 2 * 1024 * 25, "the budget is used, not starved (" + std::to_string(total) + " bytes in 30 s)");
}

}

int main() {
    test_unacknowledged_items_back_off();
    test_a_long_outage_is_capped_per_window();
    test_sending_stays_within_the_byte_budget();
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "tape submitter tests passed\n";
    return 0;
}
