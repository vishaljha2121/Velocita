#include "velocita/tape/recorder.hpp"

#include "velocita/tape/websocket.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <set>

#ifdef __linux__
#include <sys/timex.h>
#endif

namespace velocita::tape {
namespace {

constexpr std::size_t kMaxResendsPerWindow = 32;

std::int64_t utc_now() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

std::uint64_t mono_now() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000 + static_cast<std::uint64_t>(ts.tv_nsec);
}

std::string json_list(const std::vector<std::string>& xs) {
    std::string out = "[";
    for (std::size_t i = 0; i < xs.size(); ++i) out += (i ? ",\"" : "\"") + xs[i] + "\"";
    return out + "]";
}

std::vector<std::byte> encode(const Entry& e) {
    std::vector<std::byte> out(kMaxEntryBytes + 256);
    out.resize(encode_entry(e, out));
    return out;
}

}

std::uint32_t clock_error_us() noexcept {
#ifdef __linux__
    timex tx{};
    const int state = ::ntp_adjtime(&tx);
    if (state == TIME_ERROR || (tx.status & STA_UNSYNC)) return kUnknownClockError;
    return tx.esterror < 0 ? kUnknownClockError : static_cast<std::uint32_t>(tx.esterror);
#else
    return kUnknownClockError;
#endif
}

bool Ring::push(Item&& item) {
    std::lock_guard lock(mutex_);
    if (items_.size() >= capacity_) return false;
    items_.push_back(std::move(item));
    return true;
}

std::optional<Item> Ring::pop() {
    std::lock_guard lock(mutex_);
    if (items_.empty()) return std::nullopt;
    auto item = std::move(items_.front());
    items_.pop_front();
    return item;
}

std::size_t Ring::size() const {
    std::lock_guard lock(mutex_);
    return items_.size();
}

Reader::Reader(RecorderConfig config, Ring& ring) : config_(std::move(config)), ring_(ring) {}

Reader::~Reader() { stop(); }

void Reader::start() {
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void Reader::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Reader::event(EventKind kind, Channel channel, std::string_view symbol, std::uint64_t expected,
                   std::uint64_t received) {
    Item item;
    item.is_event = true;
    item.event.kind = kind;
    item.event.channel = channel;
    if (const auto s = Symbol::from(symbol)) item.event.symbol = *s;
    item.event.expected = expected;
    item.event.received = received;
    item.event.utc_ns = utc_now();
    if (!ring_.push(std::move(item))) ++lost_;
}

void Reader::subscribe(WebSocket& ws, const char* type, const std::vector<std::string>& channels,
                       const std::vector<std::string>& items) {
    const auto ts = std::to_string(std::time(nullptr));
    ws.send_text(std::string("{\"type\":\"") + type + "\",\"item_names\":" + json_list(items) +
                 ",\"channels\":" + json_list(channels) + ",\"timestamp\":\"" + ts + "\"}");
}

namespace {

std::set<std::pair<std::string, std::string>> server_subscriptions(const std::string& raw) {
    std::set<std::pair<std::string, std::string>> out;
    const auto start = raw.find("\"subscriptions\":[");
    if (start == std::string::npos) return out;
    std::size_t at = start + 17;
    int depth = 1;
    std::size_t end = at;
    for (; end < raw.size() && depth > 0; ++end) {
        if (raw[end] == '[') ++depth;
        else if (raw[end] == ']') --depth;
    }
    const auto body = raw.substr(at, end - at);
    std::size_t pos = 0;
    while (true) {
        const auto ch = body.find("\"channel\":\"", pos);
        if (ch == std::string::npos) break;
        const auto ch_end = body.find('"', ch + 11);
        if (ch_end == std::string::npos) break;
        const auto channel = body.substr(ch + 11, ch_end - ch - 11);
        const auto items = body.find("\"item_names\":[", ch_end);
        if (items == std::string::npos) break;
        const auto items_end = body.find(']', items);
        if (items_end == std::string::npos) break;
        std::size_t q = items + 14;
        while (true) {
            const auto a = body.find('"', q);
            if (a == std::string::npos || a > items_end) break;
            const auto b = body.find('"', a + 1);
            if (b == std::string::npos || b > items_end) break;
            out.emplace(channel, body.substr(a + 1, b - a - 1));
            q = b + 1;
        }
        pos = items_end;
    }
    return out;
}

}

void Reader::request(ControlRequest r) { control_queue_.push_back(std::move(r)); }

void Reader::pump_control(WebSocket& ws, std::chrono::steady_clock::time_point now) {
    if (in_flight_ && now - in_flight_at_ > std::chrono::seconds(5)) {
        control_queue_.push_front(std::move(*in_flight_));
        in_flight_.reset();
    }
    if (in_flight_ || now < retry_at_ || control_queue_.empty()) return;
    in_flight_ = std::move(control_queue_.front());
    control_queue_.pop_front();
    in_flight_at_ = now;
    subscribe(ws, in_flight_->type, in_flight_->channels, in_flight_->items);
}

void Reader::on_control_reply(const std::string& raw, std::chrono::steady_clock::time_point now) {
    if (raw.find("\"status\":\"ERROR\"") != std::string::npos) {
        if (in_flight_) {
            control_queue_.push_front(std::move(*in_flight_));
            in_flight_.reset();
        }
        retry_at_ = now + std::chrono::milliseconds(500);
        return;
    }
    in_flight_.reset();
    if (!control_queue_.empty() || now < next_reconcile_ || raw.find("\"subscriptions\":[") == std::string::npos) return;
    next_reconcile_ = now + std::chrono::seconds(30);
    const auto actual = server_subscriptions(raw);
    for (const auto& channel : config_.channels) {
        ControlRequest missing{"SUBSCRIBE_NO_AUTH", {channel}, {}};
        for (const auto& symbol : config_.symbols)
            if (!actual.contains({channel, symbol})) missing.items.push_back(symbol);
        if (missing.items.empty()) continue;
        for (const auto& symbol : missing.items) {
            streams_[{channel, symbol}].known = false;
            event(EventKind::resubscribe, parse_channel(channel), symbol, 0, 0);
        }
        request(std::move(missing));
    }
}

void Reader::flush_overflow() {
    if (!overflowing_) return;
    for (const auto& [key, range] : overflow_ranges_) {
        const Channel channel = parse_channel(key.first);
        Item item;
        item.is_event = true;
        item.event.kind = EventKind::overflow;
        item.event.channel = channel;
        if (const auto s = Symbol::from(key.second)) item.event.symbol = *s;
        item.event.expected = range.first;
        item.event.received = range.second;
        item.event.utc_ns = utc_now();
        if (!ring_.push(std::move(item))) return;
        pending_resync_.push_back(key);
    }
    overflow_ranges_.clear();
    overflowing_ = false;
}

void Reader::handle(WebSocket& ws, std::string&& raw, std::int64_t utc, std::uint64_t mono, std::uint32_t error) {
    ++frames_;
    const auto header = classify_frame(raw);
    if (header && header->channel == Channel::control) on_control_reply(raw, std::chrono::steady_clock::now());
    if (header && header->has_seqnum) {
        const auto key = std::make_pair(std::string(to_string(header->channel)), std::string(header->symbol));
        auto& st = streams_[key];
        if (header->kind == UpdateKind::snapshot_complete) {
            st.known = true;
            st.expected = header->seqnum;
        } else if (header->kind == UpdateKind::update && st.known) {
            if (header->seqnum > st.expected) {
                event(EventKind::gap, header->channel, header->symbol, st.expected, header->seqnum);
                st.known = false;
                pending_resync_.push_back(key);
            } else if (header->seqnum < st.expected) {
                event(EventKind::regression, header->channel, header->symbol, st.expected, header->seqnum);
            } else {
                st.expected = header->seqnum + 1;
            }
        }
    }
    if (overflowing_ && ring_.size() < config_.ring_capacity / 2) flush_overflow();
    Item item;
    item.raw = std::move(raw);
    item.utc_ns = utc;
    item.mono_ns = mono;
    item.clock_error_us = error;
    if (!overflowing_ && ring_.push(std::move(item))) return;
    ++lost_;
    overflowing_ = true;
    if (header && header->has_seqnum) {
        const auto key = std::make_pair(std::string(to_string(header->channel)), std::string(header->symbol));
        auto [it, inserted] = overflow_ranges_.try_emplace(key, header->seqnum, header->seqnum);
        if (!inserted) {
            it->second.first = std::min(it->second.first, header->seqnum);
            it->second.second = std::max(it->second.second, header->seqnum);
        }
        streams_[key].known = false;
    }
    (void)ws;
}

void Reader::session(WebSocket& ws) {
    control_queue_.clear();
    in_flight_.reset();
    retry_at_ = next_reconcile_ = {};
    request({"SUBSCRIBE_NO_AUTH", [&] {
        auto c = config_.channels;
        c.push_back("HEARTBEAT");
        return c;
    }(), config_.symbols});
    pump_control(ws, std::chrono::steady_clock::now());
    auto last_frame = std::chrono::steady_clock::now();
    const auto offset = config_.refresh_every * (config_.recorder_id - 1) / 3;
    auto next_refresh = last_frame + offset + config_.refresh_every / static_cast<long>(std::max<std::size_t>(1, config_.symbols.size()));
    while (running_) {
        auto result = ws.read();
        const auto now = std::chrono::steady_clock::now();
        if (result.status == ReadStatus::message) {
            last_frame = now;
            handle(ws, std::move(result.message), utc_now(), mono_now(), clock_error_us());
        } else if (result.status == ReadStatus::timeout) {
            if (now - last_frame >= config_.dead_after) {
                event(EventKind::dead_connection, Channel::none, {}, 0, 0);
                return;
            }
        } else {
            std::cerr << "recorder " << int(config_.recorder_id) << ": connection ended ("
                      << (result.status == ReadStatus::closed ? "closed" : "error") << ") " << result.message.substr(0, 120)
                      << "\n";
            event(EventKind::dead_connection, Channel::none, {}, 0, 1);
            return;
        }
        if (overflowing_ && ring_.size() < config_.ring_capacity / 2) flush_overflow();
        for (const auto& [channel, symbol] : pending_resync_) {
            request({"UNSUBSCRIBE_NO_AUTH", {channel}, {symbol}});
            request({"SUBSCRIBE_NO_AUTH", {channel}, {symbol}});
            event(EventKind::resubscribe, parse_channel(channel), symbol, 0, 0);
        }
        pending_resync_.clear();
        pump_control(ws, now);
        if (config_.refresh_every.count() > 0 && now >= next_refresh && !config_.symbols.empty()) {
            const auto& symbol = config_.symbols[refresh_next_++ % config_.symbols.size()];
            streams_[{"DEPTH", symbol}].known = false;
            pending_resync_.push_back({"DEPTH", symbol});
            next_refresh = now + config_.refresh_every / static_cast<long>(config_.symbols.size());
        }
    }
}

void Reader::run() {
    auto backoff = config_.backoff_start;
    bool first = true;
    while (running_) {
        try {
            WebSocket ws(Url::parse(config_.url), config_.read_timeout, config_.ca_file);
            if (!first) event(EventKind::reconnect, Channel::none, {}, 0, 0);
            first = false;
            backoff = config_.backoff_start;
            streams_.clear();
            session(ws);
            ws.close();
        } catch (const std::exception& e) {
            std::cerr << "recorder " << int(config_.recorder_id) << ": " << e.what() << "\n";
        }
        const auto until = std::chrono::steady_clock::now() + backoff;
        while (running_ && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        backoff = std::min(backoff * 2, config_.backoff_cap);
    }
}

Submitter::Submitter(std::uint8_t recorder, std::uint64_t session, Ring& ring, std::size_t unacked_capacity,
                     std::chrono::milliseconds resend_after, Send send, std::size_t bytes_per_second,
                     std::size_t burst_bytes)
    : recorder_(recorder), session_(session), ring_(ring), capacity_(unacked_capacity), resend_after_(resend_after),
      send_(std::move(send)), rate_(static_cast<double>(bytes_per_second)), burst_(static_cast<double>(burst_bytes)) {}

bool Submitter::spend(std::size_t bytes, std::chrono::steady_clock::time_point now) {
    if (!tokens_started_) {
        tokens_started_ = true;
        tokens_ = burst_;
        tokens_at_ = now;
    }
    const auto elapsed = std::chrono::duration<double>(now - tokens_at_).count();
    tokens_ = std::min(burst_, tokens_ + rate_ * elapsed);
    tokens_at_ = now;
    if (tokens_ < static_cast<double>(bytes)) return false;
    tokens_ -= static_cast<double>(bytes);
    return true;
}

void Submitter::enqueue(std::vector<std::byte> bytes, std::chrono::steady_clock::time_point now) {
    const bool sent = spend(bytes.size(), now);
    if (sent) send_(bytes);
    unacked_.push_back({next_request_ - 1, std::move(bytes), now, 0, sent});
}

void Submitter::step(std::chrono::steady_clock::time_point now) {
    if (next_request_ == 1) {
        RecorderEvent start;
        start.header = {recorder_, session_, next_request_++};
        start.kind = EventKind::session_start;
        start.utc_ns = utc_now();
        enqueue(encode(start), now);
    }
    while (unacked_.size() + kMaxParts < capacity_) {
        auto item = ring_.pop();
        if (!item) break;
        if (item->is_event) {
            item->event.header = {recorder_, session_, next_request_++};
            enqueue(encode(item->event), now);
            continue;
        }
        if (item->raw.size() > kMaxFrameBytes) {
            RecorderEvent e;
            e.header = {recorder_, session_, next_request_++};
            e.kind = EventKind::oversize;
            e.received = item->raw.size();
            e.utc_ns = item->utc_ns;
            enqueue(encode(e), now);
            continue;
        }
        Observation o;
        const auto header = classify_frame(item->raw);
        if (header) {
            o.channel = header->channel;
            o.kind = header->kind;
            o.has_seqnum = header->has_seqnum;
            o.seqnum = header->seqnum;
            if (const auto s = Symbol::from(header->symbol)) o.symbol = *s;
        } else {
            o.kind = UpdateKind::unparsed;
        }
        o.payload = std::as_bytes(std::span(item->raw.data(), item->raw.size()));
        o.payload_hash = v2::sha256_digest(o.payload);
        o.utc_recv_ns = item->utc_ns;
        o.mono_recv_ns = item->mono_ns;
        o.clock_error_us = item->clock_error_us;
        if (item->raw.size() <= kMaxInlinePayload) {
            o.header = {recorder_, session_, next_request_++};
            enqueue(encode(o), now);
            continue;
        }
        const auto first = next_request_;
        o.header = {recorder_, session_, first};
        std::vector<std::byte> whole(kMaxFrameBytes + kMaxEntryBytes);
        whole.resize(encode_observation_whole(o, whole));
        const auto digest = v2::sha256_digest(whole);
        const auto count = static_cast<std::uint16_t>((whole.size() + kMaxInlinePayload - 1) / kMaxInlinePayload);
        for (std::uint16_t i = 0; i < count; ++i) {
            ObservationPart p;
            p.header = {recorder_, session_, next_request_++};
            p.first_request_seq = first;
            p.part_index = i;
            p.part_count = count;
            p.whole_size = static_cast<std::uint32_t>(whole.size());
            p.whole_hash = digest;
            const auto at = static_cast<std::size_t>(i) * kMaxInlinePayload;
            p.chunk = std::span(whole).subspan(at, std::min(kMaxInlinePayload, whole.size() - at));
            enqueue(encode(p), now);
        }
    }
    if (now - window_start_ >= resend_after_) {
        window_start_ = now;
        window_resends_ = 0;
    }
    for (auto& p : unacked_) {
        if (!p.sent_once) {
            if (!spend(p.bytes.size(), now)) break;
            send_(p.bytes);
            p.sent = now;
            p.sent_once = true;
            continue;
        }
        if (window_resends_ >= kMaxResendsPerWindow) continue;
        if (now - p.sent >= resend_after_ * (1u << std::min(p.attempts, 4u))) {
            if (!spend(p.bytes.size(), now)) break;
            send_(p.bytes);
            p.sent = now;
            ++p.attempts;
            ++window_resends_;
        }
    }
}

void Submitter::on_ack(std::uint64_t session, std::uint64_t applied) {
    if (session != session_ || applied <= acked_) return;
    acked_ = applied;
    while (!unacked_.empty() && unacked_.front().request <= acked_) unacked_.pop_front();
}

}
