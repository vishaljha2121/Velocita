// A tape recorder: reads the venue feed into a fixed ring and submits numbered entries to the voters.
#pragma once

#include "velocita/tape/auth.hpp"
#include "velocita/tape/entry.hpp"
#include "velocita/tape/frame.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace velocita::tape {

struct RecorderConfig {
    std::uint8_t recorder_id{1};
    std::string url;
    std::string ca_file;
    std::vector<std::string> symbols;
    std::vector<std::string> channels{"INSTRUMENT", "EBBO", "DEPTH", "TRADE"};
    std::size_t ring_capacity{4096};
    std::size_t unacked_capacity{4096};
    std::chrono::milliseconds dead_after{30000};
    std::chrono::milliseconds read_timeout{500};
    std::chrono::milliseconds backoff_start{1000};
    std::chrono::milliseconds backoff_cap{60000};
    std::chrono::milliseconds refresh_every{15 * 60 * 1000};
    std::chrono::milliseconds resend_after{2000};
    std::size_t send_bytes_per_second{32 * 1024};
};

struct Item {
    bool is_event{};
    RecorderEvent event;
    std::string raw;
    std::int64_t utc_ns{};
    std::uint64_t mono_ns{};
    std::uint32_t clock_error_us{kUnknownClockError};
};

class Ring {
public:
    explicit Ring(std::size_t capacity) : capacity_(capacity) {}
    bool push(Item&& item);
    std::optional<Item> pop();
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::deque<Item> items_;
    std::size_t capacity_;
};

class Reader {
public:
    Reader(RecorderConfig config, Ring& ring);
    ~Reader();
    void start();
    void stop();
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] std::uint64_t lost() const noexcept { return lost_; }

private:
    void run();
    void session(class WebSocket& ws);
    void handle(class WebSocket& ws, std::string&& raw, std::int64_t utc, std::uint64_t mono, std::uint32_t error);
    void event(EventKind kind, Channel channel, std::string_view symbol, std::uint64_t expected, std::uint64_t received);
    void subscribe(class WebSocket& ws, const char* type, const std::vector<std::string>& channels,
                   const std::vector<std::string>& items);
    void flush_overflow();

    struct StreamState {
        bool known{};
        std::uint64_t expected{};
    };
    RecorderConfig config_;
    Ring& ring_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::map<std::pair<std::string, std::string>, StreamState> streams_;
    std::uint64_t frames_{};
    std::uint64_t lost_{};
    bool overflowing_{};
    std::map<std::pair<std::string, std::string>, std::pair<std::uint64_t, std::uint64_t>> overflow_ranges_;
    std::vector<std::pair<std::string, std::string>> pending_resync_;
    std::size_t refresh_next_{};
    struct ControlRequest {
        const char* type{};
        std::vector<std::string> channels;
        std::vector<std::string> items;
    };
    void request(ControlRequest r);
    void pump_control(class WebSocket& ws, std::chrono::steady_clock::time_point now);
    void on_control_reply(const std::string& raw, std::chrono::steady_clock::time_point now);
    std::deque<ControlRequest> control_queue_;
    std::optional<ControlRequest> in_flight_;
    std::chrono::steady_clock::time_point in_flight_at_{};
    std::chrono::steady_clock::time_point retry_at_{};
    std::chrono::steady_clock::time_point next_reconcile_{};
};

class Submitter {
public:
    using Send = std::function<void(std::span<const std::byte> entry)>;
    Submitter(std::uint8_t recorder, std::uint64_t session, Ring& ring, std::size_t unacked_capacity,
              std::chrono::milliseconds resend_after, Send send, std::size_t bytes_per_second = 32 * 1024,
              std::size_t burst_bytes = 64 * 1024);
    void step(std::chrono::steady_clock::time_point now);
    void on_ack(std::uint64_t session, std::uint64_t applied);
    [[nodiscard]] std::uint64_t next_request() const noexcept { return next_request_; }
    [[nodiscard]] std::uint64_t acked() const noexcept { return acked_; }
    [[nodiscard]] std::size_t unacked() const noexcept { return unacked_.size(); }

private:
    void enqueue(std::vector<std::byte> bytes, std::chrono::steady_clock::time_point now);
    std::uint8_t recorder_;
    std::uint64_t session_;
    Ring& ring_;
    std::size_t capacity_;
    std::chrono::milliseconds resend_after_;
    Send send_;
    std::uint64_t next_request_{1};
    std::uint64_t acked_{};
    struct Pending {
        std::uint64_t request{};
        std::vector<std::byte> bytes;
        std::chrono::steady_clock::time_point sent;
        unsigned attempts{};
        bool sent_once{};
    };
    bool spend(std::size_t bytes, std::chrono::steady_clock::time_point now);
    double rate_{};
    double burst_{};
    double tokens_{};
    std::chrono::steady_clock::time_point tokens_at_{};
    bool tokens_started_{};
    std::deque<Pending> unacked_;
    std::chrono::steady_clock::time_point window_start_{};
    std::size_t window_resends_{};
};

[[nodiscard]] std::uint32_t clock_error_us() noexcept;

}
