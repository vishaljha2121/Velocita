// velocita-tape-verify: rebuilds the tape from a voter's log and checks every leaf, head, signature and tick.
#include "velocita/tape/frame.hpp"
#include "velocita/tape/head_file.hpp"
#include "velocita/tape/reducer.hpp"
#include "velocita/tape/signing.hpp"
#include "velocita/tape/wal.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace velocita;
using namespace velocita::tape;

namespace {

struct Head {
    std::filesystem::path file;
    std::uint64_t tick{};
    std::uint64_t tree_size{};
    Digest root;
    std::map<int, std::vector<std::byte>> signatures;
};

[[noreturn]] void fail(const std::string& what, std::uint64_t index, const std::string& expected, const std::string& actual) {
    std::cout << "{\"ok\":false,\"index\":" << index << ",\"check\":\"" << what << "\",\"expected\":\"" << expected
              << "\",\"actual\":\"" << actual << "\"}\n";
    std::exit(1);
}

std::string bytes_hex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto b : bytes) {
        out.push_back(digits[std::to_integer<unsigned>(b) >> 4]);
        out.push_back(digits[std::to_integer<unsigned>(b) & 0xF]);
    }
    return out;
}

std::string side_json(const Side& side) {
    std::ostringstream out;
    out << "[";
    for (std::size_t k = 0; k < side.size; ++k)
        out << (k ? "," : "") << "[\"" << side.levels[k].price << "\",\"" << side.levels[k].qty << "\","
            << side.levels[k].count << "]";
    out << "]";
    return out.str();
}

std::vector<std::byte> hex_bytes(const std::string& hex) {
    std::vector<std::byte> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(std::byte(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

Head read_head(const std::filesystem::path& file) {
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    std::smatch m;
    Head h;
    h.file = file;
    if (!std::regex_search(text, m, std::regex(R"re("tick":(\d+),"tree_size":(\d+),"root":"([0-9a-f]{64})")re")))
        throw std::runtime_error("unreadable head " + file.string());
    h.tick = std::stoull(m[1]);
    h.tree_size = std::stoull(m[2]);
    h.root = *v2::parse_digest256(m[3].str());
    const std::regex sig(R"re("(\d)":"([0-9a-f]{128})")re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), sig); it != std::sregex_iterator(); ++it)
        h.signatures[std::stoi((*it)[1])] = hex_bytes((*it)[2]);
    return h;
}

}

int main(int argc, char** argv) {
    std::filesystem::path wal, heads_dir, compare_wal;
    std::vector<std::filesystem::path> keys;
    std::uint64_t incarnation = 0, tick_ms = 1000;
    Config config;
    std::string book_symbol;
    std::optional<std::uint64_t> until_seq;
    std::filesystem::path previous_head;
    std::filesystem::path extract_path;
    std::filesystem::path bundle_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--wal") wal = next();
        else if (a == "--heads") heads_dir = next();
        else if (a == "--compare-wal") compare_wal = next();
        else if (a == "--key") keys.emplace_back(next());
        else if (a == "--incarnation") incarnation = std::stoull(next());
        else if (a == "--verdict-ticks") config.verdict_ticks = std::stoull(next());
        else if (a == "--head-every") config.head_every_ticks = std::stoull(next());
        else if (a == "--tick-ms") tick_ms = std::stoull(next());
        else if (a == "--book") book_symbol = next();
        else if (a == "--until-seq") until_seq = std::stoull(next());
        else if (a == "--previous-head") previous_head = next();
        else if (a == "--extract") extract_path = next();
        else if (a == "--bundle") bundle_path = next();
        else {
            std::cerr << "usage: velocita-tape-verify --wal FILE [--heads DIR --key PUB1 --key PUB2 --key PUB3 --incarnation N]\n"
                         "       [--compare-wal OTHER_FILE]\n"
                         "       [--verdict-ticks N --head-every K --tick-ms T] [--book SYMBOL --until-seq N]\n"
                         "       [--previous-head HEAD_FILE_OF_THE_PREVIOUS_SEGMENT] [--extract ROWS.tsv]\n"
                         "       [--book SYMBOL --until-seq N --bundle PROOF.json]\n";
            return 64;
        }
    }
    try {
        const auto entries = read_wal_file(wal, true);
        if (!compare_wal.empty()) {
            const auto other = read_wal_file(compare_wal, true);
            const auto n = std::min(entries.size(), other.size());
            for (std::size_t i = 0; i < n; ++i)
                if (entries[i].bytes != other[i].bytes || entries[i].term != other[i].term)
                    fail("entry differs from the other voter's log", entries[i].index,
                         v2::to_hex(v2::sha256_digest(other[i].bytes)), v2::to_hex(v2::sha256_digest(entries[i].bytes)));
        }
        std::vector<Head> heads;
        if (!heads_dir.empty())
            for (const auto& f : std::filesystem::directory_iterator(heads_dir))
                if (f.path().extension() == ".json") heads.push_back(read_head(f.path()));
        std::sort(heads.begin(), heads.end(), [](const Head& a, const Head& b) { return a.tick < b.tick; });
        std::vector<VerifyKey> verify;
        for (const auto& k : keys) verify.push_back(VerifyKey::load(k));

        auto state = make_tape_state();
        std::vector<Digest> leaves;
        std::map<std::uint64_t, Digest> roots_at;
        std::set<std::uint64_t> wanted;
        for (const auto& h : heads) wanted.insert(h.tree_size);
        std::int64_t last_tick_utc = 0;
        std::uint64_t last_tick = 0, spacing_flags = 0, refused_ticks = 0;
        std::uint64_t book_index = 0;
        Digest book_digest;
        std::string book_dump;
        ApplyResult book_leaf;
        Book book_copy;
        std::string book_name;
        std::ofstream rows;
        if (!extract_path.empty()) {
            rows.open(extract_path, std::ios::trunc);
            if (!rows) throw std::runtime_error("cannot write " + extract_path.string());
        }
        struct CloseSink {
            std::ofstream* rows;
            const TapeState* state;
        } sink{&rows, state.get()};
        const auto on_close = [](void* ctx, const VerdictClose& v) {
            auto& s = *static_cast<CloseSink*>(ctx);
            if (!s.rows->is_open()) return;
            *s.rows << "C\t" << v.close_index << '\t' << s.state->symbols[stream_symbol(v.stream)].view() << '\t'
                    << to_string(stream_channel(v.stream)) << '\t' << to_string(v.kind) << '\t' << v.seqnum << '\t'
                    << to_string(v.outcome) << '\t' << to_string(v.reason) << '\t' << int(v.distinct);
            for (const auto& c : v.copies) *s.rows << '\t' << (c.present ? c.utc_recv_ns : 0);
            *s.rows << '\n';
        };
        std::set<std::pair<std::string, std::uint64_t>> trades_seen;
        const auto symbol_index = [&](std::string_view name) -> int {
            for (std::uint8_t i = 0; i < state->symbol_count; ++i)
                if (state->symbols[i].view() == name) return i;
            return -1;
        };
        const auto level_qty = [](const Side& side, std::int64_t price) -> std::int64_t {
            for (std::uint16_t k = 0; k < side.size; ++k)
                if (side.levels[k].price == price) return side.levels[k].qty;
            return 0;
        };
        for (const auto& e : entries) {
            if (e.index != leaves.size() + 1) fail("index sequence", e.index, std::to_string(leaves.size() + 1), std::to_string(e.index));
            std::optional<Entry> before_entry;
            int depth_sym = -1;
            Side before_bids, before_asks;
            if (rows.is_open()) {
                before_entry = decode_entry(e.bytes);
                if (const auto* o = before_entry ? std::get_if<Observation>(&*before_entry) : nullptr) {
                    const int sym = symbol_index(o->symbol.view());
                    if (o->channel == Channel::depth && sym >= 0) {
                        depth_sym = sym;
                        before_bids = state->books[sym].bids;
                        before_asks = state->books[sym].asks;
                    } else if (o->channel == Channel::trade && o->kind == UpdateKind::update && o->has_seqnum && sym >= 0 &&
                               trades_seen.insert({std::string(o->symbol.view()), o->seqnum}).second) {
                        const std::string_view raw(reinterpret_cast<const char*>(o->payload.data()), o->payload.size());
                        std::string_view price_text, qty_text;
                        std::int64_t price = 0, qty = 0;
                        if (read_trade(raw, price_text, qty_text) && parse_atoms(price_text, price) == DecimalStatus::ok &&
                            parse_atoms(qty_text, qty) == DecimalStatus::ok) {
                            const char side = raw.find("\"taking_side\":\"BUY\"") != std::string_view::npos ? 'B'
                                              : raw.find("\"taking_side\":\"SELL\"") != std::string_view::npos ? 'S' : '?';
                            const auto& b = state->books[sym];
                            const bool valid = b.state == BookState::valid && b.bids.size && b.asks.size;
                            const auto passive = side == 'B' ? level_qty(b.asks, price) : side == 'S' ? level_qty(b.bids, price) : 0;
                            rows << "X\t" << e.index << '\t' << o->symbol.view() << '\t' << o->seqnum << '\t' << o->utc_recv_ns
                                 << '\t' << side << '\t' << price << '\t' << qty << '\t';
                            if (valid) rows << b.bids.levels[0].price << '\t' << b.asks.levels[0].price;
                            else rows << "-\t-";
                            rows << '\t' << passive << '\n';
                        }
                    }
                }
            }
            const auto result = apply_entry(*state, config, e.index, e.bytes, on_close, &sink);
            if (depth_sym >= 0) {
                const auto& o = std::get<Observation>(*before_entry);
                const auto diff = [&](char side_tag, const Side& was, const Side& now) {
                    for (std::uint16_t k = 0; k < was.size; ++k) {
                        const auto q = level_qty(now, was.levels[k].price);
                        if (q != was.levels[k].qty)
                            rows << "L\t" << e.index << '\t' << o.symbol.view() << '\t' << o.utc_recv_ns << '\t' << side_tag << '\t'
                                 << was.levels[k].price << '\t' << was.levels[k].qty << '\t' << q << '\n';
                    }
                    for (std::uint16_t k = 0; k < now.size; ++k)
                        if (level_qty(was, now.levels[k].price) == 0 && now.levels[k].qty != 0)
                            rows << "L\t" << e.index << '\t' << o.symbol.view() << '\t' << o.utc_recv_ns << '\t' << side_tag << '\t'
                                 << now.levels[k].price << "\t0\t" << now.levels[k].qty << '\n';
                };
                diff('B', before_bids, state->books[depth_sym].bids);
                diff('A', before_asks, state->books[depth_sym].asks);
            }
            if (rows.is_open() && result.decoded) {
                const auto decoded = decode_entry(e.bytes);
                if (const auto* o = decoded ? std::get_if<Observation>(&*decoded) : nullptr) {
                    rows << "O\t" << e.index << '\t' << int(o->header.recorder) << '\t' << to_string(o->channel) << '\t'
                         << to_string(o->kind) << '\t' << o->symbol.view() << '\t' << (o->has_seqnum ? std::to_string(o->seqnum) : "-")
                         << '\t' << o->utc_recv_ns << '\t' << o->clock_error_us << '\t' << int(result.code) << '\t'
                         << result.flags << '\n';
                } else if (const auto* pt = decoded ? std::get_if<ObservationPart>(&*decoded) : nullptr) {
                    rows << "P\t" << e.index << '\t' << int(pt->header.recorder) << '\t' << int(result.code) << '\t'
                         << result.flags << '\n';
                } else if (const auto* ev = decoded ? std::get_if<RecorderEvent>(&*decoded) : nullptr) {
                    rows << "E\t" << e.index << '\t' << int(ev->header.recorder) << '\t' << to_string(ev->kind) << '\t'
                         << to_string(ev->channel) << '\t' << ev->symbol.view() << '\t' << ev->expected << '\t' << ev->received
                         << '\t' << ev->utc_ns << '\n';
                } else if (const auto* tk = decoded ? std::get_if<Tick>(&*decoded) : nullptr) {
                    if (result.code == ApplyCode::applied)
                        rows << "T\t" << e.index << '\t' << tk->number << '\t' << tk->proposer_utc_ns << '\n';
                }
                if (result.stream != 0xFF && stream_channel(result.stream) == Channel::depth) {
                    const auto sym = stream_symbol(result.stream);
                    const auto& b = state->books[sym];
                    std::int64_t utc = 0;
                    if (const auto* o = decoded ? std::get_if<Observation>(&*decoded) : nullptr) utc = o->utc_recv_ns;
                    rows << "B\t" << e.index << '\t' << state->symbols[sym].view() << '\t' << to_string(b.state) << '\t';
                    if (b.bids.size) rows << b.bids.levels[0].price << '\t' << b.bids.levels[0].qty;
                    else rows << "-\t-";
                    rows << '\t';
                    if (b.asks.size) rows << b.asks.levels[0].price << '\t' << b.asks.levels[0].qty;
                    else rows << "-\t-";
                    rows << '\t' << utc << '\n';
                }
            }
            leaves.push_back(result.leaf_hash);
            if (state->frontier.size != leaves.size()) fail("tree size", e.index, std::to_string(leaves.size()), std::to_string(state->frontier.size));
            if (wanted.contains(leaves.size())) roots_at[leaves.size()] = state->frontier.root();
            if (result.decoded && result.type == EntryType::tick) {
                if (result.code != ApplyCode::applied) ++refused_ticks;
                else {
                    const auto t = std::get<Tick>(*decode_entry(e.bytes));
                    if (last_tick && (t.proposer_utc_ns - last_tick_utc < static_cast<std::int64_t>(tick_ms) * 500'000 ||
                                      t.proposer_utc_ns - last_tick_utc > static_cast<std::int64_t>(tick_ms) * 10'000'000)) {
                        ++spacing_flags;
                        std::cerr << "tick spacing flagged at index " << e.index << ": "
                                  << (t.proposer_utc_ns - last_tick_utc) / 1'000'000 << " ms\n";
                    }
                    last_tick = t.number;
                    last_tick_utc = t.proposer_utc_ns;
                }
            }
            if (!book_symbol.empty() && until_seq && book_index == 0) {
                for (std::uint8_t i = 0; i < state->symbol_count; ++i) {
                    const auto& b = state->books[i];
                    if (state->symbols[i].view() == book_symbol && b.state == BookState::valid && b.position > *until_seq) {
                        book_index = e.index;
                        book_digest = b.hash;
                        book_leaf = result;
                        book_copy = b;
                        book_name = std::string(state->symbols[i].view());
                        std::ostringstream dump;
                        dump << "\"bids\":[";
                        for (std::size_t k = 0; k < b.bids.size; ++k)
                            dump << (k ? "," : "") << "[" << b.bids.levels[k].price << "," << b.bids.levels[k].qty << "]";
                        dump << "],\"asks\":[";
                        for (std::size_t k = 0; k < b.asks.size; ++k)
                            dump << (k ? "," : "") << "[" << b.asks.levels[k].price << "," << b.asks.levels[k].qty << "]";
                        dump << "],\"position\":" << b.position;
                        book_dump = dump.str();
                    }
                }
            }
        }
        if (!bundle_path.empty() && (book_symbol.empty() || !until_seq))
            fail("bundle needs --book and --until-seq", 0, "both", "missing");
        std::uint64_t verified_heads = 0;
        const Head* previous = nullptr;
        for (const auto& h : heads) {
            if (h.tree_size > leaves.size()) fail("head beyond log", h.tree_size, std::to_string(leaves.size()), std::to_string(h.tree_size));
            const auto root = roots_at.at(h.tree_size);
            if (root != h.root) fail("head root " + h.file.filename().string(), h.tree_size, v2::to_hex(root), v2::to_hex(h.root));
            const auto message = head_message(incarnation, h.tick, h.tree_size, h.root);
            std::size_t valid = 0;
            for (const auto& [voter, sig] : h.signatures)
                if (voter >= 1 && static_cast<std::size_t>(voter) <= verify.size() && verify[voter - 1].verify(message, sig)) ++valid;
            if (valid < 2) fail("head signatures " + h.file.filename().string(), h.tick, ">=2 valid", std::to_string(valid));
            if (previous && previous->tree_size <= h.tree_size) {
                const auto proof = consistency_proof(std::span(leaves).first(h.tree_size), previous->tree_size);
                if (!verify_consistency(previous->tree_size, h.tree_size, previous->root, h.root, proof))
                    fail("consistency", h.tree_size, v2::to_hex(previous->root), v2::to_hex(h.root));
            }
            previous = &h;
            ++verified_heads;
        }
        if (!previous_head.empty()) {
            if (!state->segment_started) fail("segment link", 0, "a segment_start entry", "none");
            const auto prev = read_head_file(previous_head);
            std::array<std::optional<VerifyKey>, 3> prev_keys;
            for (std::size_t k = 0; k < 3 && k < keys.size(); ++k) prev_keys[k] = VerifyKey::load(keys[k]);
            if (valid_head_signatures(prev, prev_keys) < 2)
                fail("previous head signatures", prev.tick, ">=2 valid", "fewer");
            const auto& g = state->segment;
            if (g.previous_incarnation != prev.incarnation || g.previous_tick != prev.tick ||
                g.previous_tree_size != prev.tree_size || g.previous_root != prev.root)
                fail("segment link", prev.tick, v2::to_hex(prev.root) + "@" + std::to_string(prev.tree_size),
                     v2::to_hex(g.previous_root) + "@" + std::to_string(g.previous_tree_size));
        }
        if (!bundle_path.empty()) {
            if (!book_index) fail("bundle: book not found", 0, book_symbol, "none");
            if (heads.empty() || heads.back().tree_size < book_index)
                fail("bundle: no signed head covers the book", book_index, "a head", "none");
            if (book_leaf.stream == 0xFF || state->symbols[stream_symbol(book_leaf.stream)].view() != book_name ||
                book_leaf.book_hash != book_digest)
                fail("bundle: the book is not the one in its leaf", book_index, v2::to_hex(book_digest),
                     v2::to_hex(book_leaf.book_hash));
        }
        std::cout << "{\"ok\":true,\"entries\":" << entries.size() << ",\"tree_size\":" << leaves.size()
                  << ",\"root\":\"" << v2::to_hex(state->frontier.root()) << "\",\"heads_verified\":" << verified_heads
                  << ",\"last_tick\":" << last_tick << ",\"refused_ticks\":" << refused_ticks
                  << ",\"tick_spacing_flags\":" << spacing_flags << ",\"symbols\":" << int(state->symbol_count);
        if (state->segment_started)
            std::cout << ",\"previous_segment\":{\"incarnation\":" << state->segment.previous_incarnation
                      << ",\"tick\":" << state->segment.previous_tick << ",\"tree_size\":" << state->segment.previous_tree_size
                      << ",\"root\":\"" << v2::to_hex(state->segment.previous_root) << "\",\"linked\":"
                      << (previous_head.empty() ? "false" : "true") << "}";
        if (!book_symbol.empty()) {
            std::cout << ",\"book\":{\"symbol\":\"" << book_symbol << "\",\"found\":" << (book_index ? "true" : "false");
            if (book_index) {
                std::cout << ",\"log_index\":" << book_index << ",\"hash\":\"" << v2::to_hex(book_digest) << "\","
                          << book_dump;
                if (!heads.empty() && heads.back().tree_size >= book_index) {
                    const auto& h = heads.back();
                    const auto proof = inclusion_proof(std::span(leaves).first(h.tree_size), book_index - 1);
                    const bool ok = verify_inclusion(leaves[book_index - 1], book_index - 1, h.tree_size, proof, h.root);
                    std::cout << ",\"inclusion\":{\"head_tick\":" << h.tick << ",\"proof_hashes\":" << proof.size()
                              << ",\"verified\":" << (ok ? "true" : "false") << "}";
                    if (!ok) {
                        std::cout << "}}\n";
                        return 1;
                    }
                    if (!bundle_path.empty()) {
                        std::ostringstream json;
                        json << "{\"version\":1,\"symbol\":\"" << book_name << "\",\"book\":{\"position\":\""
                             << book_copy.position << "\",\"bids\":" << side_json(book_copy.bids)
                             << ",\"asks\":" << side_json(book_copy.asks) << "},\"leaf\":{\"index\":\"" << book_index
                             << "\",\"entry_digest\":\"" << v2::to_hex(book_leaf.entry_digest) << "\",\"code\":"
                             << int(book_leaf.code) << ",\"type\":" << (book_leaf.decoded ? int(book_leaf.type) : 0)
                             << ",\"stream\":" << int(book_leaf.stream) << ",\"book_state\":" << int(book_leaf.book_state)
                             << ",\"flags\":" << book_leaf.flags << ",\"closes\":" << book_leaf.closes
                             << "},\"proof\":[";
                        for (std::size_t k = 0; k < proof.size(); ++k) json << (k ? "," : "") << "\"" << v2::to_hex(proof[k]) << "\"";
                        json << "],\"head\":{\"incarnation\":\"" << incarnation << "\",\"tick\":\"" << h.tick
                             << "\",\"tree_size\":\"" << h.tree_size << "\",\"root\":\"" << v2::to_hex(h.root)
                             << "\",\"signatures\":{";
                        bool first = true;
                        for (const auto& [voter, sig] : h.signatures) {
                            json << (first ? "" : ",") << "\"" << voter << "\":\"" << bytes_hex(sig) << "\"";
                            first = false;
                        }
                        json << "}},\"keys\":{";
                        for (std::size_t k = 0; k < verify.size(); ++k)
                            json << (k ? "," : "") << "\"" << k + 1 << "\":\"" << bytes_hex(verify[k].raw()) << "\"";
                        json << "}}\n";
                        const auto temporary = std::filesystem::path(bundle_path.string() + ".tmp");
                        {
                            std::ofstream out(temporary, std::ios::trunc);
                            out << json.str();
                            if (!out.flush()) throw std::runtime_error("cannot write " + temporary.string());
                        }
                        std::filesystem::rename(temporary, bundle_path);
                        std::cout << ",\"bundle\":\"" << bundle_path.string() << "\"";
                    }
                }
            }
            std::cout << "}";
        }
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "{\"ok\":false,\"error\":\"" << e.what() << "\"}\n";
        return 1;
    }
}
