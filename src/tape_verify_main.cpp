// velocita-tape-verify: rebuilds the tape from a voter's log and checks every leaf, head, signature and tick.
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
        else {
            std::cerr << "usage: velocita-tape-verify --wal FILE [--heads DIR --key PUB1 --key PUB2 --key PUB3 --incarnation N]\n"
                         "       [--compare-wal OTHER_FILE]\n"
                         "       [--verdict-ticks N --head-every K --tick-ms T] [--book SYMBOL --until-seq N]\n"
                         "       [--previous-head HEAD_FILE_OF_THE_PREVIOUS_SEGMENT]\n";
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
        for (const auto& e : entries) {
            if (e.index != leaves.size() + 1) fail("index sequence", e.index, std::to_string(leaves.size() + 1), std::to_string(e.index));
            const auto result = apply_entry(*state, config, e.index, e.bytes);
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
