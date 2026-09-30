#include "velocita/v2/archive_file.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace file_io {
enum class Fault { none, interrupt, open_interrupt, stat_interrupt,
                   lookahead_interrupt, read_error, early_eof, trailing,
                   size_change, inode_change, link_change, stat_error, close_error };
std::vector<std::byte> bytes;
Fault fault{};
std::size_t cursor{}, reads{}, stats{}, closes{}, opens{};
bool lookahead_interrupted{};
int flags{};
int openat(int, const char*, int requested, ...) {
    if (++opens == 1 && fault == Fault::open_interrupt) { errno = EINTR; return -1; }
    flags = requested;
    return 71;
}
int fstat(int, struct stat* value) {
    ++stats;
    if (fault == Fault::stat_interrupt && stats == 1) { errno = EINTR; return -1; }
    if (fault == Fault::stat_error && stats == 2) { errno = EIO; return -1; }
    *value = {};
    value->st_mode = S_IFREG | 0600;
    value->st_nlink = fault == Fault::link_change && stats > 1 ? 2 : 1;
    value->st_dev = 3;
    value->st_ino = fault == Fault::inode_change && stats > 1 ? 43 : 42;
    value->st_size = static_cast<off_t>(bytes.size() +
        (fault == Fault::size_change && stats > 1 ? 1 : 0));
    return 0;
}
ssize_t read(int, void* destination, std::size_t length) {
    ++reads;
    if (fault == Fault::interrupt && reads == 1) { errno = EINTR; return -1; }
    if (fault == Fault::read_error && reads == 2) { errno = EIO; return -1; }
    if (fault == Fault::early_eof && reads == 2) return 0;
    if (cursor == bytes.size()) {
        if (fault == Fault::lookahead_interrupt && !lookahead_interrupted) {
            lookahead_interrupted = true; errno = EINTR; return -1;
        }
        if (fault == Fault::trailing) {
            *static_cast<std::byte*>(destination) = std::byte{0x55};
            return 1;
        }
        return 0;
    }
    const auto count = std::min({length, bytes.size() - cursor, std::size_t{7}});
    std::memcpy(destination, bytes.data() + cursor, count);
    cursor += count;
    return static_cast<ssize_t>(count);
}
int close(int) {
    ++closes;
    if (fault == Fault::close_error) { errno = EINTR; return -1; }
    return 0;
}
void reset(Fault value) {
    fault = value; cursor = reads = stats = closes = opens = 0;
    flags = 0; lookahead_interrupted = false;
}
}

// The production reader is compiled verbatim; only its filesystem boundary is
// replaced. This tests interrupted/short reads and changes without timing races.
#define openat file_io::openat
#define fstat file_io::fstat
#define read file_io::read
#define close file_io::close
#include "../src/v2_archive_file.cpp"
#undef openat
#undef fstat
#undef read
#undef close

namespace {
using namespace velocita::v2;
void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
std::vector<std::byte> from_hex(std::string_view value) {
    std::vector<std::byte> output;
    for (std::size_t i = 0; i < value.size(); i += 2)
        output.push_back(static_cast<std::byte>(std::stoul(std::string{value.substr(i, 2)}, nullptr, 16)));
    return output;
}
VerifiedStaticIdentity fixture_identity() {
    const auto bytes = from_hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0900090001000100010001000101030011000000010100000100010011000000"
        "a6326543a7df7e9c89692344318dadf2f97dc3f06b3dabfacdcd3875fb29334d");
    VerifiedStaticIdentity identity;
    check(VerifiedStaticIdentity::decode(bytes, identity) == StaticIdentityCodecError::none,
          "independent identity fixture");
    return identity;
}
void exercise(file_io::Fault fault, bool success) {
    file_io::reset(fault);
    std::array<std::byte, 1024> buffer{};
    ArchiveSegmentScratch scratch{};
    ArchiveSegmentMetadata destination{91, 92, 93, 94, 95, 96, 97, {}};
    const auto original = destination;
    const auto result = read_archive_segment_at(19, "fixture", fixture_identity(), buffer,
                                               scratch, destination);
    constexpr std::array names{"none", "read-EINTR", "open-EINTR", "stat-EINTR",
        "EOF-EINTR", "read-EIO", "early-EOF", "trailing", "size-change",
        "inode-change", "link-change", "stat-EIO", "close-EINTR"};
    std::cout << "case=" << names[static_cast<std::size_t>(fault)]
              << " success=" << (result.error == ArchiveFileError::none)
              << " reads=" << file_io::reads << " closes=" << file_io::closes << '\n';
    if (success) {
        check(result.error == ArchiveFileError::none, "short/EINTR reads refused valid archive");
        check(result.bytes_read == 292 && destination.first_index == 7 &&
              destination.last_index == 7 && destination.last_term == 3 &&
              destination.record_count == 1, "authored archive coordinates changed");
        check(std::equal(file_io::bytes.begin(), file_io::bytes.end(), buffer.begin()),
              "short/EINTR reads changed full payload");
        check(file_io::reads > 2 && file_io::stats >= 2, "read/stat checks were not exercised");
    } else {
        check(result.error != ArchiveFileError::none, "changed or failed file accepted");
        check(destination == original, "failed read published metadata");
    }
    check(file_io::closes == 1, "reader leaked or multiply closed descriptor");
    check((file_io::flags & (O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)) ==
          (O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC), "reader omitted safe open flags");
}
}

int main() {
    try {
        // Existing independent CP-028 golden, not production-encoder output.
        file_io::bytes = from_hex(
            "56324153010060002401000001000000070000000000000007000000000000000200000000000000030000000000000003000000000000003ce5d6e3e9a886ab694852a129348bbe9413c2b55077a84804efa3cd645cc3e00000000000000000"
            "070000000000000003000000000000000700000000000000640000000000000056324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b"
            "336ac13bf56b16211fefc1a962079ada43108ed3ee5219da84483faeef49fa9e"
            "3ad66acf194db10a4824dd7ad15b8b737b1934e73ca3f92900b3e107a90bdcd7");
        check(file_io::bytes.size() == 292, "golden size");
        exercise(file_io::Fault::none, true);
        exercise(file_io::Fault::interrupt, true);
        exercise(file_io::Fault::open_interrupt, true);
        exercise(file_io::Fault::stat_interrupt, true);
        exercise(file_io::Fault::lookahead_interrupt, true);
        for (auto fault : {file_io::Fault::read_error, file_io::Fault::early_eof,
                           file_io::Fault::trailing, file_io::Fault::size_change,
                           file_io::Fault::inode_change, file_io::Fault::link_change,
                           file_io::Fault::stat_error,
                           file_io::Fault::close_error}) exercise(fault, false);
        std::cout << "archive file syscall boundary passed: short/open/stat/read/EOF EINTR and eight refusal paths\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "archive file syscall boundary: " << error.what() << '\n';
        return 1;
    }
}
