#include "velocita/tape/peer_codec.hpp"

#include <cstdint>

namespace velocita::tape {
namespace {

constexpr std::uint32_t kMaxEntries = 4096;
constexpr std::uint32_t kMaxEntryPayload = 64 * 1024;

void put(std::vector<std::byte>& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) out.push_back(std::byte((v >> (8 * i)) & 0xFF));
}

struct Reader {
    std::span<const std::byte> in;
    std::size_t at{};
    bool ok{true};
    std::uint64_t take(int n) {
        if (!ok || in.size() - at < static_cast<std::size_t>(n)) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= std::uint64_t(std::to_integer<std::uint8_t>(in[at + i])) << (8 * i);
        at += static_cast<std::size_t>(n);
        return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(take(8)); }
    std::int32_t i32() { return static_cast<std::int32_t>(take(4)); }
    bool flag() {
        const auto v = take(1);
        if (v > 1) ok = false;
        return v == 1;
    }
};

}

std::vector<std::byte> encode_peer(const PeerMessage& m) {
    std::vector<std::byte> out;
    out.push_back(std::byte{static_cast<unsigned char>(m.payload.index() + 1)});
    put(out, static_cast<std::uint32_t>(m.sender_id), 4);
    put(out, static_cast<std::uint32_t>(m.recipient_id), 4);
    if (const auto* v = std::get_if<RequestVote>(&m.payload)) {
        put(out, static_cast<std::uint64_t>(v->term), 8);
        put(out, static_cast<std::uint32_t>(v->candidate_id), 4);
        put(out, static_cast<std::uint64_t>(v->last_log_index), 8);
        put(out, static_cast<std::uint64_t>(v->last_log_term), 8);
        put(out, v->prevote ? 1 : 0, 1);
    } else if (const auto* v = std::get_if<RequestVoteResponse>(&m.payload)) {
        put(out, static_cast<std::uint64_t>(v->term), 8);
        put(out, v->vote_granted ? 1 : 0, 1);
        put(out, v->prevote ? 1 : 0, 1);
    } else if (const auto* v = std::get_if<AppendEntries>(&m.payload)) {
        put(out, static_cast<std::uint64_t>(v->term), 8);
        put(out, static_cast<std::uint64_t>(v->previous_log_index), 8);
        put(out, static_cast<std::uint64_t>(v->previous_log_term), 8);
        put(out, static_cast<std::uint64_t>(v->leader_commit), 8);
        put(out, v->entries.size(), 4);
        for (const auto& e : v->entries) {
            put(out, static_cast<std::uint64_t>(e.term), 8);
            put(out, static_cast<std::uint32_t>(e.id), 4);
            put(out, static_cast<std::uint32_t>(e.type), 4);
            put(out, e.payload.size(), 4);
            out.insert(out.end(), e.payload.begin(), e.payload.end());
        }
    } else if (const auto* v = std::get_if<AppendEntriesResponse>(&m.payload)) {
        put(out, static_cast<std::uint64_t>(v->term), 8);
        put(out, v->success ? 1 : 0, 1);
        put(out, static_cast<std::uint64_t>(v->current_index), 8);
        put(out, static_cast<std::uint64_t>(v->first_index), 8);
    }
    return out;
}

std::optional<PeerMessage> decode_peer(std::span<const std::byte> bytes) {
    Reader r{bytes};
    const auto type = r.take(1);
    PeerMessage m;
    m.sender_id = r.i32();
    m.recipient_id = r.i32();
    switch (type) {
        case 1: {
            RequestVote v;
            v.term = r.i64();
            v.candidate_id = r.i32();
            v.last_log_index = r.i64();
            v.last_log_term = r.i64();
            v.prevote = r.flag();
            m.payload = v;
            break;
        }
        case 2: {
            RequestVoteResponse v;
            v.term = r.i64();
            v.vote_granted = r.flag();
            v.prevote = r.flag();
            m.payload = v;
            break;
        }
        case 3: {
            AppendEntries v;
            v.term = r.i64();
            v.previous_log_index = r.i64();
            v.previous_log_term = r.i64();
            v.leader_commit = r.i64();
            const auto n = static_cast<std::uint32_t>(r.take(4));
            if (n > kMaxEntries) return std::nullopt;
            for (std::uint32_t i = 0; i < n && r.ok; ++i) {
                PeerLogEntry e;
                e.term = r.i64();
                e.id = r.i32();
                e.type = r.i32();
                const auto len = static_cast<std::uint32_t>(r.take(4));
                if (len > kMaxEntryPayload || bytes.size() - r.at < len) return std::nullopt;
                e.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(r.at),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(r.at + len));
                r.at += len;
                v.entries.push_back(std::move(e));
            }
            m.payload = std::move(v);
            break;
        }
        case 4: {
            AppendEntriesResponse v;
            v.term = r.i64();
            v.success = r.flag();
            v.current_index = r.i64();
            v.first_index = r.i64();
            m.payload = v;
            break;
        }
        default: return std::nullopt;
    }
    if (!r.ok || r.at != bytes.size()) return std::nullopt;
    return m;
}

}
