#include "velocita/storage.hpp"
#include "velocita/wire.hpp"

#include <filesystem>
#include <iostream>
#include <span>

int main(int argc, char** argv) {
    try {
        const std::filesystem::path data_dir = argc > 1 ? argv[1] : "/var/lib/velocita-raft";
        const velocita::MetadataStore metadata_store(data_dir);
        const velocita::WriteAheadLog wal(data_dir);
        const auto metadata = metadata_store.load();
        std::cout << "metadata term=" << metadata.term << " voted_for=" << metadata.voted_for
                  << " commit=" << metadata.commit_index << '\n';
        for (const auto& record : wal.read_all()) {
            const auto command = velocita::decode_command(record.payload);
            std::cout << "index=" << record.index << " term=" << record.term
                      << " entry_id=" << record.entry_id << " type=" << record.entry_type
                      << " command=" << velocita::to_string(command.type)
                      << " request_id=" << command.request_id << " amount=" << command.amount << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "raft-dump failed: " << error.what() << '\n';
        return 1;
    }
}

