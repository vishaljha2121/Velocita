#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace epoch_model {
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }

struct Model {
    std::filesystem::path directory;
    std::map<std::string, ino_t> names;
    std::map<ino_t, std::vector<char>> contents;
    int events{}, cut_after{-1};
    bool crashed{};

    bool cut_now() { return ++events == cut_after && (crashed = true); }
    void persist(int fd) {
        struct stat info{};
        require(::fstat(fd, &info) == 0, "model fstat failed");
        if (S_ISDIR(info.st_mode)) {
            names.clear();
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                struct stat child{};
                require(::lstat(entry.path().c_str(), &child) == 0, "model child stat failed");
                names[entry.path().filename().string()] = child.st_ino;
            }
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            struct stat child{};
            if (::lstat(entry.path().c_str(), &child) == 0 && child.st_ino == info.st_ino) {
                std::ifstream input(entry.path(), std::ios::binary);
                contents[info.st_ino].assign(std::istreambuf_iterator<char>{input}, {});
                return;
            }
        }
        throw std::runtime_error("model could not name a synced file");
    }
    void materialize(const std::filesystem::path& into) const {
        std::filesystem::create_directories(into);
        for (const auto& [name, inode] : names) {
            const auto found = contents.find(inode);
            std::ofstream output(into / name, std::ios::binary);
            if (found != contents.end()) output.write(found->second.data(), static_cast<std::streamsize>(found->second.size()));
            output.close();
            require(::chmod((into / name).c_str(), 0600) == 0, "model chmod failed");
        }
    }
};
thread_local Model* active{};
}

int epoch_model_fsync(int fd) {
    auto* m = epoch_model::active;
    if (m && m->crashed) { errno = EIO; return -1; }
    const int result = ::fsync(fd);
    if (m && result == 0) {
        m->persist(fd);
        if (m->cut_now()) { errno = EIO; return -1; }
    }
    return result;
}
ssize_t epoch_model_write(int fd, const void* bytes, size_t size) {
    auto* m = epoch_model::active;
    if (m && m->crashed) { errno = EIO; return -1; }
    const auto result = ::write(fd, bytes, size);
    if (m && result >= 0 && m->cut_now()) { errno = EIO; return -1; }
    return result;
}
int epoch_model_renameat(int from, const char* old_name, int to, const char* new_name) {
    auto* m = epoch_model::active;
    if (m && m->crashed) { errno = EIO; return -1; }
    const int result = ::renameat(from, old_name, to, new_name);
    if (m && result == 0 && m->cut_now()) { errno = EIO; return -1; }
    return result;
}

#define fsync epoch_model_fsync
#define write epoch_model_write
#define renameat epoch_model_renameat
#include "../src/v2_session_fence.cpp"
#undef renameat
#undef write
#undef fsync

namespace {
using namespace velocita::v2;
using epoch_model::require;

struct Temp {
    std::filesystem::path path;
    Temp() {
        char pattern[] = "/tmp/v2-epoch-model.XXXXXX";
        require(::mkdtemp(pattern) != nullptr, "mkdtemp failed");
        path = std::filesystem::canonical(pattern);
    }
    ~Temp() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

constexpr int kAdvances = 3;

int exercise(int cut) {
    Temp live, recovered;
    epoch_model::Model model;
    model.directory = live.path;
    model.cut_after = cut;
    epoch_model::active = &model;
    const auto file = live.path / "session-epoch.bin";
    std::uint64_t highest_returned = 0;
    bool created = false;
    const auto created_result = create_session_epoch_file(file);
    if (created_result.error == SessionEpochError::none) {
        created = true;
        highest_returned = created_result.epoch;
        for (int i = 0; i < kAdvances && !model.crashed; ++i) {
            const auto advanced = advance_session_epoch_file(file);
            if (advanced.error != SessionEpochError::none) break;
            require(advanced.epoch == highest_returned + 1, "advance skipped or repeated an epoch");
            highest_returned = advanced.epoch;
        }
    }
    epoch_model::active = nullptr;
    if (cut < 0) require(created && highest_returned == 1 + kAdvances, "uncut run did not complete");

    model.materialize(recovered.path);
    const auto after_crash = advance_session_epoch_file(recovered.path / "session-epoch.bin");
    if (!created) {
        require(after_crash.error == SessionEpochError::missing ||
                    (after_crash.error == SessionEpochError::none && after_crash.epoch == 2),
                "interrupted create left a file that is neither absent nor complete");
        return model.events;
    }
    require(after_crash.error == SessionEpochError::none,
            "a returned epoch left no durable, valid epoch file (cut " + std::to_string(cut) + ")");
    require(after_crash.epoch > highest_returned,
            "epoch " + std::to_string(after_crash.epoch) + " reissued after crash; highest returned was " +
                std::to_string(highest_returned) + " (cut " + std::to_string(cut) + ")");
    return model.events;
}

}

int main() {
    try {
        const int events = exercise(-1);
        for (int cut = 1; cut <= events; ++cut) exercise(cut);
        std::cout << "session epoch persistence model passed " << events << " crash cuts\n";
        return 0;
    } catch (const std::exception& error) {
        epoch_model::active = nullptr;
        std::cerr << "session epoch persistence model failed: " << error.what() << '\n';
        return 1;
    }
}
