#include "velocita/tape/frame.hpp"

#include <limits>

namespace velocita::tape {
namespace {

constexpr int kMaxDepth = 12;

struct Cursor {
    const char* p;
    const char* end;
};

void skip_ws(Cursor& c) noexcept {
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\n' || *c.p == '\r' || *c.p == '\t')) ++c.p;
}

bool expect(Cursor& c, char ch) noexcept {
    skip_ws(c);
    if (c.p >= c.end || *c.p != ch) return false;
    ++c.p;
    return true;
}

bool peek(Cursor& c, char ch) noexcept {
    skip_ws(c);
    return c.p < c.end && *c.p == ch;
}

bool read_string(Cursor& c, std::string_view& out) noexcept {
    if (!expect(c, '"')) return false;
    const char* start = c.p;
    while (c.p < c.end) {
        if (*c.p == '\\') {
            if (c.p + 1 >= c.end) return false;
            c.p += 2;
            continue;
        }
        if (*c.p == '"') {
            out = std::string_view(start, static_cast<std::size_t>(c.p - start));
            ++c.p;
            return true;
        }
        if (static_cast<unsigned char>(*c.p) < 0x20) return false;
        ++c.p;
    }
    return false;
}

bool skip_value(Cursor& c, int depth) noexcept;

bool skip_container(Cursor& c, int depth, char open, char close, bool members) noexcept {
    if (depth > kMaxDepth || !expect(c, open)) return false;
    if (peek(c, close)) {
        ++c.p;
        return true;
    }
    while (true) {
        if (members) {
            std::string_view key;
            if (!read_string(c, key) || !expect(c, ':')) return false;
        }
        if (!skip_value(c, depth + 1)) return false;
        if (peek(c, ',')) {
            ++c.p;
            continue;
        }
        return expect(c, close);
    }
}

bool skip_literal(Cursor& c) noexcept {
    const char* start = c.p;
    while (c.p < c.end && ((*c.p >= 'a' && *c.p <= 'z') || (*c.p >= '0' && *c.p <= '9') || *c.p == '-' ||
                           *c.p == '+' || *c.p == '.' || *c.p == 'E')) ++c.p;
    return c.p > start;
}

bool skip_value(Cursor& c, int depth) noexcept {
    skip_ws(c);
    if (c.p >= c.end) return false;
    switch (*c.p) {
        case '{': return skip_container(c, depth, '{', '}', true);
        case '[': return skip_container(c, depth, '[', ']', false);
        case '"': {
            std::string_view ignored;
            return read_string(c, ignored);
        }
        default: return skip_literal(c);
    }
}

template <class OnMember>
bool each_member(Cursor& c, int depth, OnMember&& on_member) noexcept {
    if (depth > kMaxDepth || !expect(c, '{')) return false;
    if (peek(c, '}')) {
        ++c.p;
        return true;
    }
    while (true) {
        std::string_view key;
        if (!read_string(c, key) || !expect(c, ':')) return false;
        if (!on_member(key, c, depth + 1)) return false;
        if (peek(c, ',')) {
            ++c.p;
            continue;
        }
        return expect(c, '}');
    }
}

template <class OnElement>
bool each_element(Cursor& c, int depth, OnElement&& on_element) noexcept {
    if (depth > kMaxDepth || !expect(c, '[')) return false;
    if (peek(c, ']')) {
        ++c.p;
        return true;
    }
    while (true) {
        if (!on_element(c, depth + 1)) return false;
        if (peek(c, ',')) {
            ++c.p;
            continue;
        }
        return expect(c, ']');
    }
}

Cursor cursor(std::string_view raw) noexcept { return {raw.data(), raw.data() + raw.size()}; }

bool read_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty() || text.size() > 20) return false;
    std::uint64_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

Channel channel_of(std::string_view text) noexcept {
    if (text == "INSTRUMENT") return Channel::instrument;
    if (text == "EBBO") return Channel::ebbo;
    if (text == "DEPTH") return Channel::depth;
    if (text == "TRADE") return Channel::trade;
    if (text == "HEARTBEAT") return Channel::heartbeat;
    if (text == "WEBSOCKET") return Channel::control;
    return Channel::none;
}

UpdateKind kind_of(std::string_view text) noexcept {
    if (text == "SNAPSHOT") return UpdateKind::snapshot;
    if (text == "UPDATE") return UpdateKind::update;
    if (text == "SNAPSHOT_COMPLETE") return UpdateKind::snapshot_complete;
    return UpdateKind::none;
}

bool read_level(Cursor& c, int depth, LevelText& level) noexcept {
    return each_member(c, depth, [&](std::string_view key, Cursor& v, int d) {
        if (key == "price") return read_string(v, level.price);
        if (key == "qty") return read_string(v, level.qty);
        if (key == "order_count" || key == "depth") return read_string(v, level.count);
        return skip_value(v, d);
    });
}

template <class OnData>
bool with_data(std::string_view raw, OnData&& on_data) noexcept {
    Cursor c = cursor(raw);
    bool found = false;
    const bool ok = each_member(c, 0, [&](std::string_view key, Cursor& v, int d) {
        if (key == "data") {
            found = true;
            return on_data(v, d);
        }
        return skip_value(v, d);
    });
    return ok && found;
}

}

std::optional<FrameHeader> classify_frame(std::string_view raw) noexcept {
    FrameHeader header;
    std::string_view channel, update, seqnum, item, data_symbol, info_symbol;
    Cursor c = cursor(raw);
    const bool ok = each_member(c, 0, [&](std::string_view key, Cursor& v, int d) {
        if (key == "channel") return read_string(v, channel);
        if (key == "update") return read_string(v, update);
        if (key == "seqnum") return read_string(v, seqnum);
        if (key == "item") return read_string(v, item);
        if (key == "data" && peek(v, '{')) {
            return each_member(v, d, [&](std::string_view k2, Cursor& v2, int d2) {
                if (k2 == "symbol") return read_string(v2, data_symbol);
                if (k2 == "info" && peek(v2, '{')) {
                    return each_member(v2, d2, [&](std::string_view k3, Cursor& v3, int d3) {
                        if (k3 == "symbol") return read_string(v3, info_symbol);
                        return skip_value(v3, d3);
                    });
                }
                return skip_value(v2, d2);
            });
        }
        return skip_value(v, d);
    });
    skip_ws(c);
    if (!ok || c.p != c.end) return std::nullopt;
    header.channel = channel_of(channel);
    if (header.channel == Channel::none) return std::nullopt;
    if (header.channel == Channel::control) {
        header.kind = UpdateKind::control;
        return header;
    }
    if (header.channel == Channel::heartbeat) {
        header.kind = UpdateKind::heartbeat;
        return header;
    }
    header.kind = kind_of(update);
    if (header.kind == UpdateKind::none) return std::nullopt;
    if (!read_u64(seqnum, header.seqnum)) return std::nullopt;
    header.has_seqnum = true;
    header.symbol = header.kind == UpdateKind::snapshot_complete ? item
                    : !data_symbol.empty()                       ? data_symbol
                                                                 : info_symbol;
    if (header.symbol.empty() || header.symbol.size() > kMaxSymbolBytes) return std::nullopt;
    return header;
}

DecimalStatus parse_atoms(std::string_view text, std::int64_t& out) noexcept {
    if (text.empty()) return DecimalStatus::malformed;
    std::size_t i = 0;
    __int128 whole = 0;
    std::size_t whole_digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        whole = whole * 10 + (text[i] - '0');
        if (whole > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) return DecimalStatus::overflow;
        ++i;
        ++whole_digits;
    }
    if (whole_digits == 0) return DecimalStatus::malformed;
    __int128 fraction = 0;
    std::size_t fraction_digits = 0;
    if (i < text.size()) {
        if (text[i] != '.') return DecimalStatus::malformed;
        ++i;
        if (i == text.size()) return DecimalStatus::malformed;
        while (i < text.size()) {
            if (text[i] < '0' || text[i] > '9') return DecimalStatus::malformed;
            if (fraction_digits == 9) return DecimalStatus::precision;
            fraction = fraction * 10 + (text[i] - '0');
            ++fraction_digits;
            ++i;
        }
    }
    for (std::size_t k = fraction_digits; k < 9; ++k) fraction *= 10;
    const __int128 atoms = whole * kAtomScale + fraction;
    if (atoms > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) return DecimalStatus::overflow;
    out = static_cast<std::int64_t>(atoms);
    return DecimalStatus::ok;
}

bool for_each_level(std::string_view raw, LevelVisitor visit, void* context) noexcept {
    return with_data(raw, [&](Cursor& v, int d) {
        return each_member(v, d, [&](std::string_view key, Cursor& v2, int d2) {
            if (key == "bids" || key == "asks") {
                const bool bid = key == "bids";
                return each_element(v2, d2, [&](Cursor& v3, int d3) {
                    LevelText level;
                    if (!read_level(v3, d3, level)) return false;
                    return visit(context, bid, level);
                });
            }
            return skip_value(v2, d2);
        });
    });
}

bool read_ebbo(std::string_view raw, BestText& out) noexcept {
    return with_data(raw, [&](Cursor& v, int d) {
        return each_member(v, d, [&](std::string_view key, Cursor& v2, int d2) {
            if (key != "info") return skip_value(v2, d2);
            return each_member(v2, d2, [&](std::string_view k3, Cursor& v3, int d3) {
                if (k3 == "best_bid" && peek(v3, '{')) {
                    out.has_bid = true;
                    return read_level(v3, d3, out.bid);
                }
                if (k3 == "best_ask" && peek(v3, '{')) {
                    out.has_ask = true;
                    return read_level(v3, d3, out.ask);
                }
                return skip_value(v3, d3);
            });
        });
    });
}

bool read_trade(std::string_view raw, std::string_view& price, std::string_view& qty) noexcept {
    bool found = false;
    const bool ok = with_data(raw, [&](Cursor& v, int d) {
        return each_member(v, d, [&](std::string_view key, Cursor& v2, int d2) {
            if (key != "info" || !peek(v2, '{')) return skip_value(v2, d2);
            return each_member(v2, d2, [&](std::string_view k3, Cursor& v3, int d3) {
                if (k3 == "price") {
                    found = true;
                    return read_string(v3, price);
                }
                if (k3 == "qty") return read_string(v3, qty);
                return skip_value(v3, d3);
            });
        });
    });
    return ok && found;
}

bool read_instrument_band(std::string_view raw, std::string_view& percent) noexcept {
    bool found = false;
    const bool ok = with_data(raw, [&](Cursor& v, int d) {
        return each_member(v, d, [&](std::string_view key, Cursor& v2, int d2) {
            if (key != "info" || !peek(v2, '{')) return skip_value(v2, d2);
            return each_member(v2, d2, [&](std::string_view k3, Cursor& v3, int d3) {
                if (k3 == "price_bands_percent") {
                    found = true;
                    return read_string(v3, percent);
                }
                return skip_value(v3, d3);
            });
        });
    });
    return ok && found;
}

std::string_view to_string(Channel value) noexcept {
    switch (value) {
        case Channel::instrument: return "INSTRUMENT";
        case Channel::ebbo: return "EBBO";
        case Channel::depth: return "DEPTH";
        case Channel::trade: return "TRADE";
        case Channel::heartbeat: return "HEARTBEAT";
        case Channel::control: return "CONTROL";
        default: return "NONE";
    }
}

std::string_view to_string(UpdateKind value) noexcept {
    switch (value) {
        case UpdateKind::snapshot: return "SNAPSHOT";
        case UpdateKind::update: return "UPDATE";
        case UpdateKind::snapshot_complete: return "SNAPSHOT_COMPLETE";
        case UpdateKind::heartbeat: return "HEARTBEAT";
        case UpdateKind::control: return "CONTROL";
        case UpdateKind::unparsed: return "UNPARSED";
        default: return "NONE";
    }
}

std::string_view to_string(EventKind value) noexcept {
    switch (value) {
        case EventKind::session_start: return "session_start";
        case EventKind::gap: return "gap";
        case EventKind::regression: return "regression";
        case EventKind::overflow: return "overflow";
        case EventKind::dead_connection: return "dead_connection";
        case EventKind::reconnect: return "reconnect";
        case EventKind::resubscribe: return "resubscribe";
        case EventKind::oversize: return "oversize";
    }
    return "unknown";
}

std::string_view to_string(BookState value) noexcept {
    switch (value) {
        case BookState::unknown: return "unknown";
        case BookState::building: return "building";
        case BookState::valid: return "valid";
    }
    return "unknown";
}

std::string_view to_string(Reason value) noexcept {
    switch (value) {
        case Reason::none: return "none";
        case Reason::not_started: return "not_started";
        case Reason::divergence: return "divergence";
        case Reason::malformed: return "malformed";
        case Reason::precision: return "precision";
        case Reason::overflow: return "overflow";
        case Reason::capacity: return "capacity";
        case Reason::gap: return "gap";
        case Reason::snapshot_incomplete: return "snapshot_incomplete";
    }
    return "unknown";
}

std::string_view to_string(Outcome value) noexcept {
    switch (value) {
        case Outcome::agree_3_of_3: return "3/3";
        case Outcome::agree_2_of_3: return "2/3";
        case Outcome::only_1_of_3: return "1/3";
        case Outcome::divergent: return "divergent";
    }
    return "unknown";
}

std::string_view to_string(CloseReason value) noexcept {
    switch (value) {
        case CloseReason::all_reported: return "all_reported";
        case CloseReason::n_ticks: return "n_ticks";
        case CloseReason::capacity: return "capacity";
    }
    return "unknown";
}

}
