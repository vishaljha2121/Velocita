// Test helper: applies a file of length-prefixed tape entries and prints one JSON line per entry and per verdict close.
#include "velocita/tape/reducer.hpp"
#include "velocita/v2/digest.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace velocita::tape;

namespace {

void on_close(void*, const VerdictClose& v) {
    std::cout << R"({"close":true,"owner":)" << static_cast<unsigned long long>(v.owner) << R"(,"stream":)" << int(v.stream) << R"(,"kind":")" << to_string(v.kind)
              << R"(","seqnum":)" << v.seqnum << R"(,"outcome":")" << to_string(v.outcome) << R"(","reason":")"
              << to_string(v.reason) << R"(","mask":)" << int(v.reported_mask) << R"(,"distinct":)" << int(v.distinct)
              << R"(,"first_index":)" << v.first_index << R"(,"close_index":)" << v.close_index << "}\n";
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: tape-log-apply LOG [--no-integrity] [--verdict-ticks N] [--head-every K]\n";
        return 64;
    }
    Config config;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-integrity") config.integrity_checks = false;
        else if (a == "--verdict-ticks" && i + 1 < argc) config.verdict_ticks = std::stoull(argv[++i]);
        else if (a == "--head-every" && i + 1 < argc) config.head_every_ticks = std::stoull(argv[++i]);
        else {
            std::cerr << "unknown argument " << a << "\n";
            return 64;
        }
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
    auto state = make_tape_state();
    std::size_t at = 0;
    std::uint64_t index = 0;
    while (at + 4 <= data.size()) {
        std::uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= std::uint32_t(static_cast<unsigned char>(data[at + i])) << (8 * i);
        at += 4;
        if (at + n > data.size()) {
            std::cerr << "truncated log\n";
            return 65;
        }
        ++index;
        const auto r = apply_entry(*state, config,
                                   index, std::as_bytes(std::span(data.data() + at, n)), on_close, nullptr);
        at += n;
        std::cout << R"({"index":)" << index << R"(,"code":)" << int(r.code) << R"(,"stream":)" << int(r.stream)
                  << R"(,"state":")" << to_string(r.book_state) << R"(","hash":")"
                  << (r.book_hash.is_zero() ? std::string() : velocita::v2::to_hex(r.book_hash)) << R"(","flags":)"
                  << r.flags << R"(,"leaf":")" << velocita::v2::to_hex(r.leaf_hash) << R"(","head":)"
                  << (r.head_recorded ? "true" : "false") << "}\n";
    }
    std::cout << R"({"final":true,"tree_size":)" << state->frontier.size << R"(,"root":")"
              << velocita::v2::to_hex(state->frontier.root()) << R"(","last_tick":)" << state->last_tick
              << R"(,"symbols":)" << int(state->symbol_count) << "}\n";
    return 0;
}
