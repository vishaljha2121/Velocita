#include "velocita/tape/reducer.hpp"

#include "velocita/tape/frame.hpp"
#include "velocita/v2/digest.hpp"

#include <cstring>
#include <limits>

namespace velocita::tape {
namespace {

void put_u8(v2::Sha256& h, std::uint8_t v) noexcept {
    const std::byte b{v};
    h.update({&b, 1});
}

void put_u16(v2::Sha256& h, std::uint16_t v) noexcept {
    std::array<std::byte, 2> b{std::byte(v & 0xFF), std::byte(v >> 8)};
    h.update(b);
}

void put_u32(v2::Sha256& h, std::uint32_t v) noexcept {
    std::array<std::byte, 4> b{};
    for (int i = 0; i < 4; ++i) b[i] = std::byte((v >> (8 * i)) & 0xFF);
    h.update(b);
}

void put_u64(v2::Sha256& h, std::uint64_t v) noexcept {
    std::array<std::byte, 8> b{};
    for (int i = 0; i < 8; ++i) b[i] = std::byte((v >> (8 * i)) & 0xFF);
    h.update(b);
}

void put_i64(v2::Sha256& h, std::int64_t v) noexcept { put_u64(h, static_cast<std::uint64_t>(v)); }

void put_digest(v2::Sha256& h, const Digest& d) noexcept { h.update(d.bytes); }

struct Context {
    TapeState& state;
    const Config& config;
    std::uint64_t index;
    v2::Sha256& leaf;
    CloseObserver observer;
    void* observer_context;
    std::uint16_t closes{};
    std::uint16_t flags{};
};

void set_unknown(TapeState& s, Book& book, Reason reason) noexcept {
    if (book.state != BookState::unknown || book.reason != reason)
        ++s.counters.unknown_reasons[static_cast<std::size_t>(reason)];
    book.state = BookState::unknown;
    book.reason = reason;
    book.staging = false;
    book.staging_compare = false;
    book.held_count = 0;
    book.hold_tick = 0;
    book.hash = Digest{};
    book.compare_pending = false;
}

Reason reason_of(DecimalStatus status) noexcept {
    switch (status) {
        case DecimalStatus::precision: return Reason::precision;
        case DecimalStatus::overflow: return Reason::overflow;
        default: return Reason::malformed;
    }
}

bool parse_level(const LevelText& text, bool bid, Change& out, Reason& reason) noexcept {
    out.bid = bid;
    if (const auto st = parse_atoms(text.price, out.price); st != DecimalStatus::ok) {
        reason = reason_of(st);
        return false;
    }
    if (const auto st = parse_atoms(text.qty, out.qty); st != DecimalStatus::ok) {
        reason = reason_of(st);
        return false;
    }
    std::int64_t count_atoms = 0;
    if (text.count.empty()) {
        out.count = 0;
        return true;
    }
    std::uint64_t count = 0;
    for (const char ch : text.count) {
        if (ch < '0' || ch > '9' || count > std::numeric_limits<std::uint32_t>::max() / 10) {
            reason = Reason::malformed;
            return false;
        }
        count = count * 10 + static_cast<std::uint64_t>(ch - '0');
    }
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        reason = Reason::overflow;
        return false;
    }
    (void)count_atoms;
    out.count = static_cast<std::uint32_t>(count);
    return true;
}

bool better(bool bid, std::int64_t a, std::int64_t b) noexcept { return bid ? a > b : a < b; }

bool set_level(Side& side, bool bid, const Change& change) noexcept {
    std::size_t i = 0;
    while (i < side.size && better(bid, side.levels[i].price, change.price)) ++i;
    const bool exists = i < side.size && side.levels[i].price == change.price;
    if (change.qty == 0) {
        if (exists) {
            for (std::size_t k = i; k + 1 < side.size; ++k) side.levels[k] = side.levels[k + 1];
            --side.size;
            side.levels[side.size] = Level{};
        }
        return true;
    }
    if (exists) {
        side.levels[i] = Level{change.price, change.qty, change.count};
        return true;
    }
    if (side.size == kLevelsPerSide) return false;
    for (std::size_t k = side.size; k > i; --k) side.levels[k] = side.levels[k - 1];
    side.levels[i] = Level{change.price, change.qty, change.count};
    ++side.size;
    return true;
}

struct ParseInto {
    Side* bids;
    Side* asks;
    std::array<Change, kChangesPerHeld>* changes;
    std::uint8_t change_count{};
    bool capacity{};
    bool bad{};
    Reason reason{Reason::none};
};

bool visit_level(void* context, bool bid, const LevelText& text) noexcept {
    auto& p = *static_cast<ParseInto*>(context);
    Change change;
    if (!parse_level(text, bid, change, p.reason)) {
        p.bad = true;
        return false;
    }
    if (p.changes) {
        if (p.change_count == kChangesPerHeld) {
            p.capacity = true;
            return false;
        }
        (*p.changes)[p.change_count++] = change;
        return true;
    }
    if (!set_level(bid ? *p.bids : *p.asks, bid, change)) {
        p.capacity = true;
        return false;
    }
    return true;
}

void rehash(const TapeState& s, std::uint8_t sym, Book& book) noexcept {
    book.hash = book.state == BookState::valid ? book_hash(s.symbols[sym], book) : Digest{};
}

bool apply_changes(Book& book, const HeldUpdate& update) noexcept {
    for (std::size_t i = 0; i < update.change_count; ++i) {
        const auto& c = update.changes[i];
        if (!set_level(c.bid ? book.bids : book.asks, c.bid, c)) return false;
    }
    return true;
}

std::uint16_t compare_sides(const Side& a, const Side& b) noexcept {
    std::uint16_t diff = 0;
    std::size_t i = 0, j = 0;
    while (i < a.size || j < b.size) {
        if (i < a.size && j < b.size && a.levels[i].price == b.levels[j].price) {
            if (a.levels[i].qty != b.levels[j].qty || a.levels[i].count != b.levels[j].count) ++diff;
            ++i;
            ++j;
        } else {
            ++diff;
            if (i < a.size && (j >= b.size || a.levels[i].price != b.levels[j].price)) ++i;
            else ++j;
        }
    }
    return diff;
}

[[nodiscard]] bool finish_compare(Book& book) noexcept {
    book.compare_pending = false;
    return compare_sides(book.bids, book.compare_bids) + compare_sides(book.asks, book.compare_asks) > 0;
}

void drain_held(TapeState& s, std::uint8_t sym, Book& book, std::uint16_t& flags) noexcept {
    bool progressed = true;
    while (progressed && book.held_count > 0) {
        progressed = false;
        for (std::size_t i = 0; i < book.held_count; ++i) {
            auto& h = book.held[i];
            if (h.seqnum < book.position) {
                book.held[i] = book.held[book.held_count - 1];
                --book.held_count;
                progressed = true;
                break;
            }
            if (h.seqnum == book.position) {
                const HeldUpdate copy = h;
                book.held[i] = book.held[book.held_count - 1];
                --book.held_count;
                if (!apply_changes(book, copy)) {
                    set_unknown(s, book, Reason::capacity);
                    return;
                }
                if (book.compare_pending && copy.seqnum == book.compare_seqnum) {
                    for (std::size_t k = 0; k < copy.change_count; ++k) {
                        const auto& ch = copy.changes[k];
                        (void)set_level(ch.bid ? book.compare_bids : book.compare_asks, ch.bid, ch);
                    }
                    if (finish_compare(book)) flags |= kFlagSnapshotDiffers;
                }
                ++book.position;
                progressed = true;
                break;
            }
        }
    }
    if (book.held_count == 0) book.hold_tick = 0;
    (void)sym;
}

bool hold_update(TapeState& s, Book& book, std::uint64_t seqnum, std::uint64_t index,
                 std::span<const std::byte> payload) noexcept {
    for (std::size_t i = 0; i < book.held_count; ++i)
        if (book.held[i].seqnum == seqnum) return true;
    if (book.held_count == kHeldPerBook) {
        set_unknown(s, book, Reason::capacity);
        return false;
    }
    HeldUpdate h;
    h.seqnum = seqnum;
    h.index = index;
    ParseInto p{nullptr, nullptr, &h.changes};
    const std::string_view raw(reinterpret_cast<const char*>(payload.data()), payload.size());
    const bool ok = for_each_level(raw, visit_level, &p);
    if (p.capacity) {
        set_unknown(s, book, Reason::capacity);
        return false;
    }
    if (!ok || p.bad) {
        set_unknown(s, book, p.bad ? p.reason : Reason::malformed);
        return false;
    }
    h.change_count = p.change_count;
    if (book.held_count == 0) book.hold_tick = s.last_tick;
    book.held[book.held_count++] = h;
    return true;
}

void depth_first_copy(Context& c, std::uint8_t sym, UpdateKind kind, std::uint64_t seqnum,
                      std::span<const std::byte> payload) noexcept {
    auto& s = c.state;
    auto& book = s.books[sym];
    const std::string_view raw(reinterpret_cast<const char*>(payload.data()), payload.size());
    switch (kind) {
        case UpdateKind::snapshot: {
            if (book.has_position && seqnum < book.position) {
                c.flags |= kFlagStaleSnapshot;
                return;
            }
            if (!book.staging || book.staging_seqnum != seqnum) {
                book.staging_compare = book.state == BookState::valid && book.has_position && book.position == seqnum;
                book.staging = true;
                book.staging_seqnum = seqnum;
                book.staging_tick = s.last_tick;
                book.staged_bids = Side{};
                book.staged_asks = Side{};
                book.state = BookState::building;
                book.reason = Reason::none;
                book.hash = Digest{};
            }
            ParseInto p{&book.staged_bids, &book.staged_asks, nullptr};
            const bool ok = for_each_level(raw, visit_level, &p);
            if (p.capacity) set_unknown(s, book, Reason::capacity);
            else if (!ok || p.bad) set_unknown(s, book, p.bad ? p.reason : Reason::malformed);
            return;
        }
        case UpdateKind::snapshot_complete: {
            if (!book.staging || book.staging_seqnum != seqnum) {
                if (book.has_position && seqnum < book.position) c.flags |= kFlagStaleSnapshot;
                return;
            }
            book.compare_pending = book.staging_compare && c.config.integrity_checks;
            if (book.compare_pending) {
                book.compare_seqnum = seqnum;
                book.compare_bids = book.bids;
                book.compare_asks = book.asks;
            }
            book.bids = book.staged_bids;
            book.asks = book.staged_asks;
            book.staging = false;
            book.staging_compare = false;
            book.has_position = true;
            book.position = seqnum;
            book.state = BookState::valid;
            book.reason = Reason::none;
            drain_held(s, sym, book, c.flags);
            rehash(s, sym, book);
            return;
        }
        case UpdateKind::update: {
            if (book.staging) {
                if (seqnum >= book.staging_seqnum) hold_update(s, book, seqnum, c.index, payload);
                return;
            }
            if (book.state != BookState::valid) return;
            if (seqnum < book.position) return;
            if (seqnum > book.position) {
                hold_update(s, book, seqnum, c.index, payload);
                return;
            }
            ParseInto p{&book.bids, &book.asks, nullptr};
            const bool ok = for_each_level(raw, visit_level, &p);
            if (p.capacity) {
                set_unknown(s, book, Reason::capacity);
                return;
            }
            if (!ok || p.bad) {
                set_unknown(s, book, p.bad ? p.reason : Reason::malformed);
                return;
            }
            if (book.compare_pending && seqnum == book.compare_seqnum) {
                ParseInto q{&book.compare_bids, &book.compare_asks, nullptr};
                (void)for_each_level(raw, visit_level, &q);
                if (finish_compare(book)) c.flags |= kFlagSnapshotDiffers;
            }
            ++book.position;
            drain_held(s, sym, book, c.flags);
            rehash(s, sym, book);
            return;
        }
        default: return;
    }
}

void ebbo_first_copy(Context& c, std::uint8_t sym, UpdateKind kind, std::uint64_t seqnum,
                     std::span<const std::byte> payload) noexcept {
    if (kind != UpdateKind::snapshot && kind != UpdateKind::update) return;
    auto& best = c.state.ebbo[sym];
    if (best.has && seqnum < best.seqnum) return;
    BestText text;
    if (!read_ebbo({reinterpret_cast<const char*>(payload.data()), payload.size()}, text)) return;
    Best next;
    next.has = true;
    next.seqnum = seqnum;
    Reason ignored{};
    Change change;
    if (text.has_bid && !text.bid.price.empty() && parse_level(text.bid, true, change, ignored)) {
        next.has_bid = true;
        next.bid = Level{change.price, change.qty, change.count};
    }
    if (text.has_ask && !text.ask.price.empty() && parse_level(text.ask, false, change, ignored)) {
        next.has_ask = true;
        next.ask = Level{change.price, change.qty, change.count};
    }
    best = next;
}

void instrument_first_copy(Context& c, std::uint8_t sym, std::span<const std::byte> payload) noexcept {
    std::string_view percent;
    if (!read_instrument_band({reinterpret_cast<const char*>(payload.data()), payload.size()}, percent)) return;
    std::int64_t ppb = 0;
    if (parse_atoms(percent, ppb) == DecimalStatus::ok) c.state.band_ppb[sym] = ppb;
}

void trade_first_copy(Context& c, std::uint8_t sym, UpdateKind kind, std::span<const std::byte> payload) noexcept {
    if (kind != UpdateKind::update || !c.config.integrity_checks) return;
    const auto& book = c.state.books[sym];
    if (book.state != BookState::valid || book.bids.size == 0 || book.asks.size == 0) return;
    std::string_view price_text, qty_text;
    if (!read_trade({reinterpret_cast<const char*>(payload.data()), payload.size()}, price_text, qty_text)) return;
    std::int64_t price = 0;
    if (parse_atoms(price_text, price) != DecimalStatus::ok) return;
    const auto bid = book.bids.levels[0].price, ask = book.asks.levels[0].price;
    if (price < bid || price > ask) c.flags |= kFlagTradeOutsideBook;
    const auto ppb = c.state.band_ppb[sym];
    if (ppb > 0) {
        const __int128 mid2 = static_cast<__int128>(bid) + ask;
        const __int128 band2 = mid2 * ppb / kAtomScale;
        const __int128 p2 = static_cast<__int128>(price) * 2;
        if (p2 < mid2 - band2 || p2 > mid2 + band2) c.flags |= kFlagTradeOutsideBand;
    }
}

void integrity(Context& c, std::uint8_t sym) noexcept {
    if (!c.config.integrity_checks) return;
    auto& book = c.state.books[sym];
    if (book.state != BookState::valid) {
        book.ebbo_mismatch = false;
        book.ebbo_flagged = false;
        return;
    }
    if (book.bids.size > 0 && book.asks.size > 0) {
        const auto bid = book.bids.levels[0].price, ask = book.asks.levels[0].price;
        if (bid > ask) c.flags |= kFlagCrossed;
        else if (bid == ask) c.flags |= kFlagLocked;
    }
    const auto& best = c.state.ebbo[sym];
    if (!best.has) return;
    const bool bid_ok = best.has_bid ? book.bids.size > 0 && book.bids.levels[0].price == best.bid.price &&
                                           book.bids.levels[0].qty == best.bid.qty
                                     : book.bids.size == 0;
    const bool ask_ok = best.has_ask ? book.asks.size > 0 && book.asks.levels[0].price == best.ask.price &&
                                           book.asks.levels[0].qty == best.ask.qty
                                     : book.asks.size == 0;
    if (bid_ok && ask_ok) {
        book.ebbo_mismatch = false;
        book.ebbo_flagged = false;
        return;
    }
    if (!book.ebbo_mismatch) {
        book.ebbo_mismatch = true;
        book.ebbo_mismatch_tick = c.state.last_tick;
        return;
    }
    if (!book.ebbo_flagged && c.state.last_tick > book.ebbo_mismatch_tick + 2) {
        book.ebbo_flagged = true;
        c.flags |= kFlagEbboMismatch;
    }
}

std::size_t skew_bucket(std::uint64_t ns) noexcept {
    constexpr std::array<std::uint64_t, kSkewBuckets - 1> bounds{100'000, 1'000'000, 5'000'000, 10'000'000,
                                                                 50'000'000, 100'000'000, 500'000'000, 1'000'000'000};
    for (std::size_t i = 0; i < bounds.size(); ++i)
        if (ns < bounds[i]) return i;
    return kSkewBuckets - 1;
}

void close_verdict(Context& c, OpenVerdict& v, CloseReason reason) noexcept {
    auto& s = c.state;
    VerdictClose out;
    out.owner = v.owner;
    out.stream = v.stream;
    out.kind = v.kind;
    out.seqnum = v.seqnum;
    out.reason = reason;
    out.first_index = v.first_index;
    out.close_index = c.index;
    out.copies = v.copies;
    std::uint8_t reported = 0, distinct = 0;
    std::array<Digest, kRecorders> seen{};
    std::int64_t lo = std::numeric_limits<std::int64_t>::max(), hi = std::numeric_limits<std::int64_t>::min();
    for (std::size_t r = 0; r < kRecorders; ++r) {
        const auto& copy = v.copies[r];
        if (!copy.present) continue;
        out.reported_mask |= static_cast<std::uint8_t>(1u << r);
        ++reported;
        bool known = false;
        for (std::size_t k = 0; k < distinct; ++k) known = known || seen[k] == copy.hash;
        if (!known) seen[distinct++] = copy.hash;
        lo = copy.utc_recv_ns < lo ? copy.utc_recv_ns : lo;
        hi = copy.utc_recv_ns > hi ? copy.utc_recv_ns : hi;
    }
    out.distinct = distinct;
    out.outcome = distinct > 1      ? Outcome::divergent
                  : reported == 3 ? Outcome::agree_3_of_3
                  : reported == 2 ? Outcome::agree_2_of_3
                                  : Outcome::only_1_of_3;
    ++s.counters.all_outcomes[static_cast<std::size_t>(out.outcome)];
    if (v.kind == UpdateKind::update) {
        ++s.counters.update_outcomes[static_cast<std::size_t>(out.outcome)];
        if (reported >= 2 && hi >= lo) {
            const auto skew = static_cast<std::uint64_t>(hi - lo);
            ++s.counters.skew_buckets[skew_bucket(skew)];
            ++s.counters.skew_samples;
            if (skew > s.counters.skew_max_ns) s.counters.skew_max_ns = skew;
        }
    }
    ++s.counters.close_reasons[static_cast<std::size_t>(reason)];

    put_u8(c.leaf, 0xC1);
    put_u64(c.leaf, out.owner);
    put_u8(c.leaf, out.stream);
    put_u8(c.leaf, static_cast<std::uint8_t>(out.kind));
    put_u64(c.leaf, out.seqnum);
    put_u8(c.leaf, static_cast<std::uint8_t>(out.outcome));
    put_u8(c.leaf, static_cast<std::uint8_t>(out.reason));
    put_u8(c.leaf, out.reported_mask);
    put_u8(c.leaf, out.distinct);
    put_u64(c.leaf, out.first_index);
    for (const auto& copy : out.copies) {
        if (!copy.present) continue;
        put_digest(c.leaf, copy.hash);
        put_i64(c.leaf, copy.utc_recv_ns);
        put_u64(c.leaf, copy.index);
    }
    ++c.closes;
    if (c.observer) c.observer(c.observer_context, out);

    auto& ref = s.closed[s.closed_next];
    ref.used = true;
    ref.owner = v.owner;
    ref.stream = v.stream;
    ref.kind = v.kind;
    ref.seqnum = v.seqnum;
    ref.applied_hash = v.applied_hash;
    s.closed_next = static_cast<std::uint32_t>((s.closed_next + 1) % kRecentClosed);
    v = OpenVerdict{};
}

OpenVerdict* find_open(TapeState& s, std::uint64_t owner, std::uint8_t stream, UpdateKind kind,
                       std::uint64_t seqnum) noexcept {
    for (auto& v : s.verdicts)
        if (v.used && v.owner == owner && v.stream == stream && v.kind == kind && v.seqnum == seqnum) return &v;
    return nullptr;
}

const ClosedRef* find_closed(const TapeState& s, std::uint64_t owner, std::uint8_t stream, UpdateKind kind,
                             std::uint64_t seqnum) noexcept {
    for (const auto& v : s.closed)
        if (v.used && v.owner == owner && v.stream == stream && v.kind == kind && v.seqnum == seqnum) return &v;
    return nullptr;
}

OpenVerdict& open_slot(Context& c) noexcept {
    OpenVerdict* oldest = nullptr;
    for (auto& v : c.state.verdicts) {
        if (!v.used) return v;
        if (!oldest || v.first_index < oldest->first_index) oldest = &v;
    }
    close_verdict(c, *oldest, CloseReason::capacity);
    return *oldest;
}

std::optional<std::uint8_t> symbol_index(TapeState& s, const Symbol& symbol) noexcept {
    for (std::uint8_t i = 0; i < s.symbol_count; ++i)
        if (s.symbols[i] == symbol) return i;
    if (s.symbol_count == kMaxSymbols) return std::nullopt;
    s.symbols[s.symbol_count] = symbol;
    return s.symbol_count++;
}

ApplyCode check_session(TapeState& s, const RecorderHeader& h, bool session_start) noexcept {
    if (h.recorder < 1 || h.recorder > kRecorders) return ApplyCode::unknown_recorder;
    auto& session = s.sessions[h.recorder - 1];
    if (session_start) {
        if (h.request_seq != 1) return ApplyCode::out_of_order_request;
        if (session.started && session.session == h.session) return ApplyCode::duplicate_request;
        session.started = true;
        session.session = h.session;
        session.last_request = 1;
        s.assemblies[h.recorder - 1] = Assembly{};
        return ApplyCode::applied;
    }
    if (!session.started || session.session != h.session) return ApplyCode::out_of_order_request;
    if (h.request_seq <= session.last_request) return ApplyCode::duplicate_request;
    if (h.request_seq != session.last_request + 1) return ApplyCode::out_of_order_request;
    session.last_request = h.request_seq;
    return ApplyCode::applied;
}

ApplyCode process_observation(Context& c, const Observation& o, std::uint8_t& stream_out) noexcept {
    auto& s = c.state;
    if (v2::sha256_digest(o.payload) != o.payload_hash) return ApplyCode::malformed_entry;
    if (o.kind == UpdateKind::heartbeat) {
        ++s.counters.heartbeats;
        return ApplyCode::applied;
    }
    if (!o.has_seqnum || o.kind == UpdateKind::control || o.kind == UpdateKind::unparsed) return ApplyCode::applied;
    const auto header = classify_frame({reinterpret_cast<const char*>(o.payload.data()), o.payload.size()});
    if (!header || header->channel != o.channel || header->kind != o.kind || header->seqnum != o.seqnum ||
        header->symbol != o.symbol.view())
        return ApplyCode::classification_mismatch;
    const auto slot = channel_slot(o.channel);
    if (slot >= kChannels) return ApplyCode::classification_mismatch;
    const auto sym = symbol_index(s, o.symbol);
    if (!sym) return ApplyCode::symbol_capacity;
    const auto stream = stream_id(o.channel, *sym);
    stream_out = stream;
    const auto r = static_cast<std::size_t>(o.header.recorder - 1);
    const Copy copy{true, o.payload_hash, o.utc_recv_ns, c.index};
    const bool divergence_matters = o.channel == Channel::depth;
    const std::uint64_t owner = o.kind == UpdateKind::update ? 0 : reply_owner(o.header);

    if (auto* v = find_open(s, owner, stream, o.kind, o.seqnum)) {
        if (v->copies[r].present) {
            ++s.counters.duplicate_copies;
            return ApplyCode::applied;
        }
        v->copies[r] = copy;
        if (copy.hash != v->applied_hash && divergence_matters) set_unknown(s, s.books[*sym], Reason::divergence);
        std::size_t reported = 0;
        for (const auto& k : v->copies) reported += k.present ? 1 : 0;
        if (reported == kRecorders) close_verdict(c, *v, CloseReason::all_reported);
        return ApplyCode::applied;
    }
    if (const auto* closed = find_closed(s, owner, stream, o.kind, o.seqnum)) {
        ++s.counters.late_copies;
        c.flags |= kFlagLateCopy;
        if (closed->applied_hash != copy.hash) {
            ++s.counters.late_divergences;
            c.flags |= kFlagLateDivergence;
            if (divergence_matters) set_unknown(s, s.books[*sym], Reason::divergence);
        }
        return ApplyCode::applied;
    }
    auto& v = open_slot(c);
    v.used = true;
    v.owner = owner;
    v.stream = stream;
    v.kind = o.kind;
    v.seqnum = o.seqnum;
    v.first_index = c.index;
    v.first_tick = s.last_tick;
    v.copies[r] = copy;
    v.applied_hash = copy.hash;

    auto& last = s.last_utc_ns[stream];
    if (last != 0 && o.utc_recv_ns > last) {
        const auto gap = o.utc_recv_ns - last;
        if (gap > s.max_silence_ns[stream]) s.max_silence_ns[stream] = gap;
    }
    if (o.utc_recv_ns > last) last = o.utc_recv_ns;

    switch (o.channel) {
        case Channel::depth: depth_first_copy(c, *sym, o.kind, o.seqnum, o.payload); break;
        case Channel::ebbo: ebbo_first_copy(c, *sym, o.kind, o.seqnum, o.payload); break;
        case Channel::trade: trade_first_copy(c, *sym, o.kind, o.payload); break;
        case Channel::instrument: instrument_first_copy(c, *sym, o.payload); break;
        default: break;
    }
    return ApplyCode::applied;
}

ApplyCode process_part(Context& c, const ObservationPart& p, std::uint8_t& stream_out) noexcept {
    auto& a = c.state.assemblies[p.header.recorder - 1];
    if (p.part_index == 0) {
        a = Assembly{};
        a.used = true;
        a.session = p.header.session;
        a.first_request = p.header.request_seq;
        a.part_count = p.part_count;
        a.whole_size = p.whole_size;
        a.whole_hash = p.whole_hash;
    }
    if (!a.used || a.session != p.header.session || a.first_request != p.first_request_seq ||
        a.part_count != p.part_count || a.received != p.part_index || a.whole_size != p.whole_size ||
        a.whole_hash != p.whole_hash || a.filled + p.chunk.size() > a.whole_size ||
        a.filled + p.chunk.size() > a.bytes.size()) {
        a = Assembly{};
        return ApplyCode::part_refused;
    }
    std::memcpy(a.bytes.data() + a.filled, p.chunk.data(), p.chunk.size());
    a.filled += static_cast<std::uint32_t>(p.chunk.size());
    ++a.received;
    if (a.received < a.part_count) return ApplyCode::part_stored;
    const std::span<const std::byte> whole(a.bytes.data(), a.filled);
    const bool complete = a.filled == a.whole_size && v2::sha256_digest(whole) == a.whole_hash;
    if (!complete) {
        a = Assembly{};
        return ApplyCode::part_refused;
    }
    const auto entry = decode_entry(whole);
    const auto* o = entry ? std::get_if<Observation>(&*entry) : nullptr;
    if (!o || o->header.recorder != p.header.recorder || o->header.session != p.header.session ||
        o->header.request_seq != a.first_request) {
        a = Assembly{};
        return ApplyCode::part_refused;
    }
    const auto code = process_observation(c, *o, stream_out);
    a = Assembly{};
    return code;
}

void on_tick(Context& c) noexcept {
    auto& s = c.state;
    for (auto& v : s.verdicts)
        if (v.used && s.last_tick >= v.first_tick + c.config.verdict_ticks) close_verdict(c, v, CloseReason::n_ticks);
    for (std::uint8_t sym = 0; sym < s.symbol_count; ++sym) {
        auto& book = s.books[sym];
        if (book.staging && s.last_tick >= book.staging_tick + c.config.verdict_ticks)
            set_unknown(s, book, Reason::snapshot_incomplete);
        else if (book.held_count > 0 && s.last_tick >= book.hold_tick + c.config.verdict_ticks)
            set_unknown(s, book, Reason::gap);
    }
}

}

std::unique_ptr<TapeState> make_tape_state() { return std::make_unique<TapeState>(); }

std::uint64_t reply_owner(const RecorderHeader& header) noexcept {
    const auto tag = header.session ^ (header.request_seq * 0x9E3779B97F4A7C15ull) ^
                     (static_cast<std::uint64_t>(header.recorder) << 56);
    return tag == 0 ? 1 : tag;
}

std::uint8_t stream_id(Channel channel, std::uint8_t symbol) noexcept {
    return static_cast<std::uint8_t>(channel_slot(channel) * kMaxSymbols + symbol);
}

Channel stream_channel(std::uint8_t stream) noexcept {
    constexpr std::array<Channel, kChannels> order{Channel::instrument, Channel::ebbo, Channel::depth, Channel::trade};
    return stream / kMaxSymbols < kChannels ? order[stream / kMaxSymbols] : Channel::none;
}

std::uint8_t stream_symbol(std::uint8_t stream) noexcept { return static_cast<std::uint8_t>(stream % kMaxSymbols); }

Digest book_hash(const Symbol& symbol, const Book& book) noexcept {
    v2::Sha256 h;
    const char tag[] = "velocita-tape/v1/book";
    h.update(std::as_bytes(std::span(tag, sizeof(tag) - 1)));
    put_u8(h, symbol.size);
    h.update(std::as_bytes(std::span(symbol.bytes.data(), symbol.size)));
    put_u64(h, book.position);
    for (const Side* side : {&book.bids, &book.asks}) {
        put_u16(h, side->size);
        for (std::size_t i = 0; i < side->size; ++i) {
            put_i64(h, side->levels[i].price);
            put_i64(h, side->levels[i].qty);
            put_u32(h, side->levels[i].count);
        }
    }
    return h.finish();
}

const PendingHead* find_head(const TapeState& state, std::uint64_t tick) noexcept {
    for (const auto& head : state.heads)
        if (head.used && head.tick == tick) return &head;
    return nullptr;
}

std::array<std::byte, 8 + 8 + 8 + 32 + 16> head_message(std::uint64_t incarnation, std::uint64_t tick,
                                                         std::uint64_t tree_size, const Digest& root) noexcept {
    std::array<std::byte, 8 + 8 + 8 + 32 + 16> out{};
    const char tag[] = "velocita-tape/v1";
    std::memcpy(out.data(), tag, 16);
    std::size_t at = 16;
    for (const auto v : {incarnation, tick, tree_size})
        for (int i = 0; i < 8; ++i) out[at++] = std::byte((v >> (8 * i)) & 0xFF);
    std::memcpy(out.data() + at, root.bytes.data(), root.bytes.size());
    return out;
}

ApplyResult apply_entry(TapeState& state, const Config& config, std::uint64_t index,
                        std::span<const std::byte> entry_bytes, CloseObserver observer,
                        void* observer_context) noexcept {
    ApplyResult result;
    result.entry_digest = v2::sha256_digest(entry_bytes);
    v2::Sha256 leaf;
    const std::byte prefix{0x00};
    leaf.update({&prefix, 1});
    const char tag[] = "velocita-tape/v1/leaf";
    leaf.update(std::as_bytes(std::span(tag, sizeof(tag) - 1)));
    put_u64(leaf, index);
    put_digest(leaf, result.entry_digest);
    Context c{state, config, index, leaf, observer, observer_context};
    state.applied_index = index;
    ++state.counters.entries;

    const auto entry = decode_entry(entry_bytes);
    if (!entry) {
        result.code = ApplyCode::malformed_entry;
    } else {
        result.decoded = true;
        result.type = entry_type(*entry);
        ++state.counters.entries_by_type[static_cast<std::size_t>(result.type)];
        if (const auto* o = std::get_if<Observation>(&*entry)) {
            result.code = o->payload.size() > kMaxInlinePayload ? ApplyCode::malformed_entry
                                                               : check_session(state, o->header, false);
            if (result.code == ApplyCode::applied) result.code = process_observation(c, *o, result.stream);
        } else if (const auto* p = std::get_if<ObservationPart>(&*entry)) {
            result.code = check_session(state, p->header, false);
            if (result.code == ApplyCode::applied) result.code = process_part(c, *p, result.stream);
        } else if (const auto* e = std::get_if<RecorderEvent>(&*entry)) {
            result.code = check_session(state, e->header, e->kind == EventKind::session_start);
            if (result.code == ApplyCode::applied)
                ++state.counters.events[e->header.recorder - 1][static_cast<std::size_t>(e->kind)];
        } else if (const auto* t = std::get_if<Tick>(&*entry)) {
            if (t->number == state.last_tick + 1) {
                state.last_tick = t->number;
                on_tick(c);
            } else {
                result.code = ApplyCode::tick_refused;
            }
        } else if (const auto* g = std::get_if<SegmentStart>(&*entry)) {
            if (state.segment_started) {
                result.code = ApplyCode::segment_refused;
            } else {
                state.segment_started = true;
                state.segment = *g;
            }
        } else if (const auto* h = std::get_if<HeadSignature>(&*entry)) {
            result.code = ApplyCode::head_refused;
            for (auto& head : state.heads) {
                if (!head.used || head.tick != h->tick) continue;
                if (head.tree_size == h->tree_size && head.root == h->root) {
                    result.code = ApplyCode::applied;
                    if (!head.signed_by[h->voter - 1]) {
                        head.signed_by[h->voter - 1] = true;
                        head.signatures[h->voter - 1] = h->signature;
                    }
                }
            }
        }
    }
    ++state.counters.apply_codes[static_cast<std::size_t>(result.code)];

    if (result.stream != 0xFF && stream_channel(result.stream) != Channel::none) {
        const auto sym = stream_symbol(result.stream);
        integrity(c, sym);
        result.book_state = state.books[sym].state;
        result.book_hash = state.books[sym].hash;
    }
    for (std::size_t bit = 0; bit < 16; ++bit)
        if (c.flags & (1u << bit)) ++state.counters.flags[bit];
    result.flags = c.flags;
    result.closes = c.closes;

    put_u8(leaf, 0xE0);
    put_u8(leaf, static_cast<std::uint8_t>(result.code));
    put_u8(leaf, result.decoded ? static_cast<std::uint8_t>(result.type) : 0);
    put_u8(leaf, result.stream);
    put_u8(leaf, static_cast<std::uint8_t>(result.book_state));
    put_digest(leaf, result.book_hash);
    put_u16(leaf, result.flags);
    put_u16(leaf, result.closes);
    result.leaf_hash = leaf.finish();
    state.frontier.append(result.leaf_hash);

    if (result.decoded && result.type == EntryType::tick && result.code == ApplyCode::applied &&
        config.head_every_ticks > 0 && state.last_tick % config.head_every_ticks == 0) {
        auto& head = state.heads[state.head_next % kPendingHeads];
        head = PendingHead{};
        head.used = true;
        head.tick = state.last_tick;
        head.tree_size = state.frontier.size;
        head.root = state.frontier.root();
        ++state.head_next;
        result.head_recorded = true;
    }
    return result;
}

}
