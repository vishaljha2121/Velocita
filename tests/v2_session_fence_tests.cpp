#include "velocita/v2/session_fence.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// File goldens come from tests/v2_session_fence_goldens.py (Python struct +
// zlib), never from the code under test. Fence expectations are hand-derived.

namespace {

using namespace velocita::v2;
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using Decision = SessionFence::Decision;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes hex(std::string_view text) {
    Bytes out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<unsigned char>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16)));
    return out;
}

const Bytes kEpoch1 = hex("564c53310100000000000000ef5b02d3");
const Bytes kEpoch2 = hex("564c533102000000000000000c5c8d5d");
const Bytes kEpoch4 = hex("564c533104000000000000008b55e29b");
const Bytes kEpochMax = hex("564c5331ffffffffffffffff045bce5b");

Bytes read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(out), "fixture write failed");
}

struct TempDir {
    fs::path path;
    TempDir() {
        auto pattern = (fs::temp_directory_path() / "v2-session-fence-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
        path = fs::canonical(bytes.data());
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path, ignored); }
};

void fence_follows_hand_derived_table() {
    struct Step { std::uint64_t epoch, sequence; Decision decision; std::uint64_t after_epoch, after_sequence; };
    const std::array<Step, 11> steps{{
        {0, 1, Decision::invalid_counter, 0, 0},
        {1, 0, Decision::invalid_counter, 0, 0},
        {5, 10, Decision::accept, 5, 10},          // first authentic session, any start
        {5, 10, Decision::replayed_sequence, 5, 10},
        {5, 9, Decision::replayed_sequence, 5, 10},
        {5, 11, Decision::accept, 5, 11},
        {4, 100, Decision::stale_epoch, 5, 11},     // older incarnation, even with higher sequence
        {6, 1, Decision::accept, 6, 1},             // newer incarnation restarts its sequence
        {5, 12, Decision::stale_epoch, 6, 1},       // the old process is now retired for good
        {6, 1, Decision::replayed_sequence, 6, 1},
        {UINT64_MAX, UINT64_MAX, Decision::accept, UINT64_MAX, UINT64_MAX},
    }};
    SessionFence fence;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& step = steps[i];
        require(fence.admit(step.epoch, step.sequence) == step.decision,
                "fence decision differs at step " + std::to_string(i));
        require(fence.epoch() == step.after_epoch && fence.last_sequence() == step.after_sequence,
                "fence state differs after step " + std::to_string(i));
    }
}

void sequencer_is_strict_and_refuses_exhaustion() {
    SessionSequencer sequencer(3);
    require(sequencer.next() == 1 && sequencer.next() == 2 && sequencer.next() == 3,
            "sequencer did not start at one and increase by one");
    SessionSequencer edge(3, UINT64_MAX - 1);
    require(edge.next() == UINT64_MAX, "sequencer did not issue the final value");
    require(!edge.next().has_value() && !edge.next().has_value(), "sequencer wrapped after exhaustion");
    SessionSequencer zero(0);
    require(!zero.next().has_value(), "epoch zero issued a sequence");
}

void create_and_advance_match_goldens() {
    TempDir dir;
    const auto file = dir.path / "session-epoch.bin";
    require(advance_session_epoch_file(file).error == SessionEpochError::missing, "missing file not reported");
    const auto created = create_session_epoch_file(file);
    require(created.error == SessionEpochError::none && created.epoch == 1, "create did not return epoch 1");
    require(read(file) == kEpoch1, "created file differs from the Python oracle");
    struct stat info {};
    require(::lstat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode) && (info.st_mode & 0777) == 0600,
            "epoch file is not a private regular file");
    require(create_session_epoch_file(file).error == SessionEpochError::already_exists && read(file) == kEpoch1,
            "second create adopted or changed an existing file");
    const auto second = advance_session_epoch_file(file);
    require(second.error == SessionEpochError::none && second.epoch == 2 && read(file) == kEpoch2,
            "first advance differs from the Python oracle");
    (void)advance_session_epoch_file(file);
    const auto fourth = advance_session_epoch_file(file);
    require(fourth.error == SessionEpochError::none && fourth.epoch == 4 && read(file) == kEpoch4,
            "repeated advance differs from the Python oracle");
    require(!fs::exists(dir.path / "session-epoch.bin.tmp"), "temporary left behind after advance");
}

void corrupt_files_refuse_without_change() {
    TempDir dir;
    const auto file = dir.path / "session-epoch.bin";
    for (std::size_t at = 0; at < kEpoch2.size(); ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            auto corrupt = kEpoch2;
            corrupt[at] ^= static_cast<unsigned char>(1U << bit);
            write(file, corrupt);
            require(advance_session_epoch_file(file).error == SessionEpochError::invalid_file,
                    "bit flip accepted at byte " + std::to_string(at));
            require(read(file) == corrupt, "refusal rewrote a corrupt file");
        }
    }
    write(file, Bytes(kEpoch2.begin(), kEpoch2.end() - 1));
    require(advance_session_epoch_file(file).error == SessionEpochError::invalid_file, "torn file accepted");
    auto extended = kEpoch2;
    extended.push_back(0);
    write(file, extended);
    require(advance_session_epoch_file(file).error == SessionEpochError::invalid_file, "extended file accepted");
    write(file, kEpochMax);
    require(advance_session_epoch_file(file).error == SessionEpochError::exhausted && read(file) == kEpochMax,
            "exhausted counter wrapped or changed");
}

void temporaries_and_links_are_never_trusted() {
    TempDir dir;
    const auto file = dir.path / "session-epoch.bin";
    write(file, kEpoch2);
    write(dir.path / "session-epoch.bin.tmp", kEpochMax);  // an aborted advance's garbage
    const auto advanced = advance_session_epoch_file(file);
    require(advanced.error == SessionEpochError::none && advanced.epoch == 3,
            "leftover temporary was promoted or blocked recovery");
    require(!fs::exists(dir.path / "session-epoch.bin.tmp"), "leftover temporary survived");

    const auto target = dir.path / "real.bin";
    write(target, kEpoch2);
    const auto link = dir.path / "linked.bin";
    fs::create_symlink(target, link);
    require(advance_session_epoch_file(link).error != SessionEpochError::none, "symlinked epoch file followed");
    require(read(target) == kEpoch2, "symlink target modified");
    require(create_session_epoch_file(dir.path / "absent" / "e.bin").error == SessionEpochError::io_failure,
            "create in a missing directory not reported as io failure");
}

}  // namespace

int main() {
    try {
        fence_follows_hand_derived_table();
        sequencer_is_strict_and_refuses_exhaustion();
        create_and_advance_match_goldens();
        corrupt_files_refuse_without_change();
        temporaries_and_links_are_never_trusted();
        std::cout << "session fence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "session fence tests failed: " << error.what() << '\n';
        return 1;
    }
}
