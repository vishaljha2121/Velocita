#include "velocita/v2/wal_arena.hpp"
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) return 2;
    using namespace velocita::v2;
    WalBlockArena arena;
    WalSegment segment(argv[1]);
    for (std::uint64_t index = 1; index <= 2; ++index) {
        velocita::WalRecord record{index, 1, static_cast<int>(index), 0,
                                   std::vector<std::byte>(3000, std::byte{0x5a})};
        if (arena.stage(velocita::encode_wal_record(record), index) != StageOutcome::staged)
            return 2;
    }
    arena.seal_open_block();
    std::cout << "sealed_before=" << arena.sealed_count() << '\n';
    try {
        const auto written = segment.flush(arena);
        const auto records = segment.read_records();
        if (written == 2 && records.size() == 2 && records[0].index == 1 && records[1].index == 2)
            return 0;
    } catch (const std::exception& error) {
        std::cout << "flush_error=" << error.what() << '\n';
        const auto records = segment.read_records();
        for (const auto& record : records) std::cout << "stored_index=" << record.index << '\n';
    }
    std::cerr << "RED: multi-block synchronous flush repeats the front descriptor\n";
    return 1;
}
