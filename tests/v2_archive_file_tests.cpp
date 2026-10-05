#include "velocita/v2/archive_file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(const std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](const std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t at = 0; at < text.size(); at += 2)
        bytes.push_back(std::byte(std::stoul(std::string(text.substr(at, 2)), nullptr, 16)));
    return bytes;
}

const VerifiedStaticIdentity& identity() {
    static const auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0900090001000100010001000101030011000000010100000100010011000000"
        "a6326543a7df7e9c89692344318dadf2f97dc3f06b3dabfacdcd3875fb29334d");
    static const auto value = [&] {
        VerifiedStaticIdentity result;
        require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
                "identity fixture refused");
        return result;
    }();
    return value;
}

const std::vector<std::byte>& segment() {
    static const auto bytes = hex(
        "56324153010060002401000001000000070000000000000007000000000000000200000000000000030000000000000003000000000000003ce5d6e3e9a886ab694852a129348bbe9413c2b55077a84804efa3cd645cc3e00000000000000000"
        "070000000000000003000000000000000700000000000000640000000000000056324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b"
        "336ac13bf56b16211fefc1a962079ada43108ed3ee5219da84483faeef49fa9e"
        "3ad66acf194db10a4824dd7ad15b8b737b1934e73ca3f92900b3e107a90bdcd7");
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "fixture write failed");
}

void run(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    write_bytes(root / "valid", segment());
    const int directory = ::open(root.c_str(), O_RDONLY | O_DIRECTORY);
    require(directory >= 0, "directory open failed");
    std::vector<std::byte> storage(kMaxArchiveSegmentBytes);
    ArchiveSegmentScratch scratch;
    ArchiveSegmentMetadata metadata{};
    auto result = read_archive_segment_at(directory, "valid", identity(), storage, scratch, metadata);
    require(result.error == ArchiveFileError::none && result.bytes_read == segment().size(),
            "valid archive file refused");
    require(metadata.first_index == 7 && metadata.last_index == 7 &&
                metadata.total_bytes == segment().size(), "metadata mismatch");
    std::vector<std::byte> exact_storage(segment().size(), std::byte{0x5a});
    ArchiveSegmentMetadata exact_metadata{};
    result = read_archive_segment_at(directory, "valid", identity(), exact_storage,
                                     scratch, exact_metadata);
    require(result.error == ArchiveFileError::none && exact_storage == segment(),
            "exact-size caller buffer did not receive every byte");
    allocation_probe::count = 0;
    allocation_probe::enabled = true;
    ArchiveSegmentMetadata noalloc_metadata{};
    result = read_archive_segment_at(directory, "valid", identity(), storage,
                                     scratch, noalloc_metadata);
    allocation_probe::enabled = false;
    require(result.error == ArchiveFileError::none && allocation_probe::count == 0,
            "archive file verifier allocated");

    const ArchiveSegmentMetadata sentinel{91, 92, 93, 94, 95, 96, 97, {}};
    auto expect_failure = [&](std::string_view name, std::span<std::byte> buffer,
                              ArchiveFileError error) {
        auto unchanged = sentinel;
        const auto failed = read_archive_segment_at(directory, name, identity(), buffer,
                                                    scratch, unchanged);
        if (failed.error != error)
            std::cerr << "file=" << name << " expected=" << static_cast<int>(error)
                      << " actual=" << static_cast<int>(failed.error) << '\n';
        require(failed.error == error, "unexpected typed file error");
        require(unchanged == sentinel, "failure changed destination metadata");
    };

    expect_failure("missing", storage, ArchiveFileError::open_failed);
    expect_failure("valid", std::span<std::byte>{storage}.first(segment().size() - 1),
                   ArchiveFileError::buffer_too_small);
    std::vector<std::byte> larger_buffer(kMaxArchiveSegmentBytes + 1);
    ArchiveSegmentMetadata larger_metadata{};
    require(read_archive_segment_at(directory, "valid", identity(), larger_buffer,
                                    scratch, larger_metadata).error == ArchiveFileError::none,
            "larger caller buffer was incorrectly refused");
    expect_failure("", storage, ArchiveFileError::invalid_argument);
    expect_failure(".", storage, ArchiveFileError::invalid_argument);
    expect_failure("..", storage, ArchiveFileError::invalid_argument);
    expect_failure("subdir/valid", storage, ArchiveFileError::invalid_argument);
    const std::string embedded_nul{"valid\0ignored", 13};
    expect_failure(embedded_nul, storage, ArchiveFileError::invalid_argument);

    write_bytes(root / "short", std::span<const std::byte>{segment()}.first(100));
    expect_failure("short", storage, ArchiveFileError::codec_failed);
    auto trailing = segment(); trailing.push_back(std::byte{});
    write_bytes(root / "trailing", trailing);
    expect_failure("trailing", storage, ArchiveFileError::codec_failed);
    auto corrupt = segment(); corrupt.back() ^= std::byte{1};
    write_bytes(root / "corrupt", corrupt);
    expect_failure("corrupt", storage, ArchiveFileError::codec_failed);
    write_bytes(root / "empty", std::span<const std::byte>{});
    expect_failure("empty", storage, ArchiveFileError::length_out_of_range);
    write_bytes(root / "oversized", {segment().data(), 1});
    std::filesystem::resize_file(root / "oversized", kMaxArchiveSegmentBytes + 1);
    expect_failure("oversized", storage, ArchiveFileError::length_out_of_range);

    ArchiveSegmentMetadata invalid_identity_destination = sentinel;
    VerifiedStaticIdentity invalid_identity;
    const auto invalid = read_archive_segment_at(directory, "valid", invalid_identity,
                                                 storage, scratch,
                                                 invalid_identity_destination);
    require(invalid.error == ArchiveFileError::codec_failed &&
                invalid.codec_error == ArchiveSegmentError::invalid_identity,
            "codec error was not preserved");
    require(invalid_identity_destination == sentinel,
            "codec refusal changed destination metadata");

    std::filesystem::create_directory(root / "subdir");
    expect_failure("subdir", storage, ArchiveFileError::not_regular_single_link);
    require(::mkfifo((root / "fifo").c_str(), 0600) == 0, "mkfifo fixture failed");
    expect_failure("fifo", storage, ArchiveFileError::not_regular_single_link);
    std::filesystem::create_symlink("valid", root / "symlink");
    expect_failure("symlink", storage, ArchiveFileError::open_failed);
    std::filesystem::create_hard_link(root / "valid", root / "hardlink");
    expect_failure("hardlink", storage, ArchiveFileError::not_regular_single_link);
    ::close(directory);
}

}

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::filesystem::exists(argv[1]))) {
        std::cerr << "usage: v2_archive_file_tests [NEW_ROOT]\n";
        return 2;
    }
    try {
        std::filesystem::path root;
        if (argc == 2) {
            root = argv[1];
        } else {
            std::array<char, 40> pattern{};
            const std::string prefix = "/tmp/v2-archive-file-tests-XXXXXX";
            std::copy(prefix.begin(), prefix.end(), pattern.begin());
            const auto* created = ::mkdtemp(pattern.data());
            require(created != nullptr, "mkdtemp failed");
            root = created;
        }
        run(root);
        std::cout << "v2 archive file tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
