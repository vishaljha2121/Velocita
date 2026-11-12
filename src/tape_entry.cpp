#include "velocita/tape/entry.hpp"

#include <cstring>

namespace velocita::tape {
namespace {

class Writer {
public:
    explicit Writer(std::span<std::byte> out) noexcept : out_(out) {}

    void u8(std::uint8_t v) noexcept { put(&v, 1); }
    void u16(std::uint16_t v) noexcept { le(v); }
    void u32(std::uint32_t v) noexcept { le(v); }
    void u64(std::uint64_t v) noexcept { le(v); }
    void i64(std::int64_t v) noexcept { le(static_cast<std::uint64_t>(v)); }
    void digest(const Digest& d) noexcept { put(d.bytes.data(), d.bytes.size()); }
    void bytes(std::span<const std::byte> b) noexcept { put(b.data(), b.size()); }
    void symbol(const Symbol& s) noexcept {
        u8(s.size);
        put(s.bytes.data(), s.size);
    }
    [[nodiscard]] std::size_t finish() const noexcept { return failed_ ? 0 : at_; }

private:
    template <class T>
    void le(T v) noexcept {
        std::array<std::byte, sizeof(T)> b{};
        for (std::size_t i = 0; i < sizeof(T); ++i) b[i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
        put(b.data(), b.size());
    }
    void put(const void* p, std::size_t n) noexcept {
        if (failed_ || out_.size() - at_ < n) {
            failed_ = true;
            return;
        }
        if (n) std::memcpy(out_.data() + at_, p, n);
        at_ += n;
    }
    std::span<std::byte> out_;
    std::size_t at_{};
    bool failed_{};
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> in) noexcept : in_(in) {}

    bool u8(std::uint8_t& v) noexcept { return le(v); }
    bool u16(std::uint16_t& v) noexcept { return le(v); }
    bool u32(std::uint32_t& v) noexcept { return le(v); }
    bool u64(std::uint64_t& v) noexcept { return le(v); }
    bool i64(std::int64_t& v) noexcept {
        std::uint64_t u{};
        if (!le(u)) return false;
        v = static_cast<std::int64_t>(u);
        return true;
    }
    bool digest(Digest& d) noexcept { return take(d.bytes.data(), d.bytes.size()); }
    bool view(std::size_t n, std::span<const std::byte>& out) noexcept {
        if (in_.size() - at_ < n) return false;
        out = in_.subspan(at_, n);
        at_ += n;
        return true;
    }
    bool symbol(Symbol& s) noexcept {
        std::uint8_t n{};
        if (!u8(n) || n > kMaxSymbolBytes) return false;
        s.size = n;
        if (!take(s.bytes.data(), n)) return false;
        for (std::size_t i = n; i < s.bytes.size(); ++i) s.bytes[i] = 0;
        return true;
    }
    [[nodiscard]] bool done() const noexcept { return at_ == in_.size(); }

private:
    template <class T>
    bool le(T& v) noexcept {
        if (in_.size() - at_ < sizeof(T)) return false;
        T r = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) r |= static_cast<T>(std::to_integer<std::uint8_t>(in_[at_ + i])) << (8 * i);
        v = r;
        at_ += sizeof(T);
        return true;
    }
    bool take(void* p, std::size_t n) noexcept {
        if (in_.size() - at_ < n) return false;
        if (n) std::memcpy(p, in_.data() + at_, n);
        at_ += n;
        return true;
    }
    std::span<const std::byte> in_;
    std::size_t at_{};
};

bool valid_channel(std::uint8_t v) noexcept { return v <= static_cast<std::uint8_t>(Channel::control); }
bool valid_kind(std::uint8_t v) noexcept { return v <= static_cast<std::uint8_t>(UpdateKind::unparsed); }
bool valid_event(std::uint8_t v) noexcept {
    return v >= static_cast<std::uint8_t>(EventKind::session_start) && v <= static_cast<std::uint8_t>(EventKind::oversize);
}
bool valid_recorder(std::uint8_t v) noexcept { return v >= 1 && v <= kRecorders; }
bool valid_voter(std::uint8_t v) noexcept { return v >= 1 && v <= kVoters; }

void write_header(Writer& w, const RecorderHeader& h) noexcept {
    w.u8(h.recorder);
    w.u64(h.session);
    w.u64(h.request_seq);
}

bool read_header(Reader& r, RecorderHeader& h) noexcept {
    return r.u8(h.recorder) && valid_recorder(h.recorder) && r.u64(h.session) && r.u64(h.request_seq);
}

void write_observation_body(Writer& w, const Observation& o) noexcept {
    write_header(w, o.header);
    w.u8(static_cast<std::uint8_t>(o.channel));
    w.u8(static_cast<std::uint8_t>(o.kind));
    w.symbol(o.symbol);
    w.u8(o.has_seqnum ? 1 : 0);
    w.u64(o.seqnum);
    w.digest(o.payload_hash);
    w.i64(o.utc_recv_ns);
    w.u64(o.mono_recv_ns);
    w.u32(o.clock_error_us);
    w.u32(static_cast<std::uint32_t>(o.payload.size()));
    w.bytes(o.payload);
}

bool read_observation_body(Reader& r, Observation& o, std::size_t max_payload) noexcept {
    std::uint8_t channel{}, kind{}, has{};
    std::uint32_t size{};
    if (!read_header(r, o.header) || !r.u8(channel) || !valid_channel(channel) || !r.u8(kind) || !valid_kind(kind) ||
        !r.symbol(o.symbol) || !r.u8(has) || has > 1 || !r.u64(o.seqnum) || !r.digest(o.payload_hash) ||
        !r.i64(o.utc_recv_ns) || !r.u64(o.mono_recv_ns) || !r.u32(o.clock_error_us) || !r.u32(size) ||
        size > max_payload || !r.view(size, o.payload))
        return false;
    o.channel = static_cast<Channel>(channel);
    o.kind = static_cast<UpdateKind>(kind);
    o.has_seqnum = has == 1;
    return has == 1 || o.seqnum == 0;
}

}

std::optional<Symbol> Symbol::from(std::string_view text) noexcept {
    if (text.size() > kMaxSymbolBytes) return std::nullopt;
    Symbol s;
    std::memcpy(s.bytes.data(), text.data(), text.size());
    s.size = static_cast<std::uint8_t>(text.size());
    return s;
}

EntryType entry_type(const Entry& entry) noexcept {
    return static_cast<EntryType>(entry.index() + 1);
}

std::size_t encode_entry(const Entry& entry, std::span<std::byte> out) noexcept {
    Writer w(out);
    w.u8(kEntryVersion);
    w.u8(static_cast<std::uint8_t>(entry_type(entry)));
    if (const auto* o = std::get_if<Observation>(&entry)) {
        if (o->payload.size() > kMaxInlinePayload) return 0;
        write_observation_body(w, *o);
    } else if (const auto* p = std::get_if<ObservationPart>(&entry)) {
        if (p->chunk.size() > kMaxInlinePayload) return 0;
        write_header(w, p->header);
        w.u64(p->first_request_seq);
        w.u16(p->part_index);
        w.u16(p->part_count);
        w.u32(p->whole_size);
        w.digest(p->whole_hash);
        w.u32(static_cast<std::uint32_t>(p->chunk.size()));
        w.bytes(p->chunk);
    } else if (const auto* e = std::get_if<RecorderEvent>(&entry)) {
        write_header(w, e->header);
        w.u8(static_cast<std::uint8_t>(e->kind));
        w.u8(static_cast<std::uint8_t>(e->channel));
        w.symbol(e->symbol);
        w.u64(e->expected);
        w.u64(e->received);
        w.i64(e->utc_ns);
    } else if (const auto* t = std::get_if<Tick>(&entry)) {
        w.u64(t->number);
        w.u8(t->proposer);
        w.i64(t->proposer_utc_ns);
    } else if (const auto* h = std::get_if<HeadSignature>(&entry)) {
        w.u64(h->tick);
        w.u64(h->tree_size);
        w.digest(h->root);
        w.u8(h->voter);
        w.bytes(h->signature);
    } else if (const auto* g = std::get_if<SegmentStart>(&entry)) {
        w.u64(g->previous_incarnation);
        w.u64(g->previous_tick);
        w.u64(g->previous_tree_size);
        w.digest(g->previous_root);
    }
    return w.finish();
}

std::size_t encode_observation_whole(const Observation& observation, std::span<std::byte> out) noexcept {
    Writer w(out);
    w.u8(kEntryVersion);
    w.u8(static_cast<std::uint8_t>(EntryType::observation));
    write_observation_body(w, observation);
    return w.finish();
}

std::optional<Entry> decode_entry(std::span<const std::byte> bytes) noexcept {
    Reader r(bytes);
    std::uint8_t version{}, type{};
    if (!r.u8(version) || version != kEntryVersion || !r.u8(type)) return std::nullopt;
    switch (static_cast<EntryType>(type)) {
        case EntryType::observation: {
            Observation o;
            if (!read_observation_body(r, o, kMaxFrameBytes) || !r.done()) return std::nullopt;
            return Entry{o};
        }
        case EntryType::observation_part: {
            ObservationPart p;
            std::uint32_t size{};
            if (!read_header(r, p.header) || !r.u64(p.first_request_seq) || !r.u16(p.part_index) ||
                !r.u16(p.part_count) || !r.u32(p.whole_size) || !r.digest(p.whole_hash) || !r.u32(size) ||
                size > kMaxInlinePayload || !r.view(size, p.chunk) || !r.done())
                return std::nullopt;
            if (p.part_count < 2 || p.part_count > kMaxParts || p.part_index >= p.part_count ||
                p.whole_size > kMaxFrameBytes + kMaxEntryBytes || size == 0)
                return std::nullopt;
            return Entry{p};
        }
        case EntryType::recorder_event: {
            RecorderEvent e;
            std::uint8_t kind{}, channel{};
            if (!read_header(r, e.header) || !r.u8(kind) || !valid_event(kind) || !r.u8(channel) ||
                !valid_channel(channel) || !r.symbol(e.symbol) || !r.u64(e.expected) || !r.u64(e.received) ||
                !r.i64(e.utc_ns) || !r.done())
                return std::nullopt;
            e.kind = static_cast<EventKind>(kind);
            e.channel = static_cast<Channel>(channel);
            return Entry{e};
        }
        case EntryType::tick: {
            Tick t;
            if (!r.u64(t.number) || !r.u8(t.proposer) || !valid_voter(t.proposer) || !r.i64(t.proposer_utc_ns) ||
                !r.done())
                return std::nullopt;
            return Entry{t};
        }
        case EntryType::head_signature: {
            HeadSignature h;
            std::span<const std::byte> sig;
            if (!r.u64(h.tick) || !r.u64(h.tree_size) || !r.digest(h.root) || !r.u8(h.voter) ||
                !valid_voter(h.voter) || !r.view(kSignatureBytes, sig) || !r.done())
                return std::nullopt;
            std::memcpy(h.signature.data(), sig.data(), kSignatureBytes);
            return Entry{h};
        }
        case EntryType::segment_start: {
            SegmentStart g;
            if (!r.u64(g.previous_incarnation) || !r.u64(g.previous_tick) || !r.u64(g.previous_tree_size) ||
                !r.digest(g.previous_root) || !r.done())
                return std::nullopt;
            return Entry{g};
        }
    }
    return std::nullopt;
}

}
