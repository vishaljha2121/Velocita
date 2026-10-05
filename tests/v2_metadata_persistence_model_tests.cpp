#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace vpm {

struct PowerCut final {};

struct Inode {
    std::vector<std::byte> volatile_bytes;
    std::vector<std::byte> durable_bytes;
};

struct Handle {
    bool directory{};
    int inode{-1};
};

struct FileSystem {
    std::string directory;
    std::map<int, Inode> inodes;
    std::map<std::string, int> volatile_names;
    std::map<std::string, int> durable_names;
    std::map<int, Handle> handles;
    int next_inode{1};
    int next_fd{10};
    int event{};
    int cut_after{-1};
    int fail_before{-1};
    bool short_first_write{};
    bool short_write_done{};
    bool crashed{};

    void before_operation() {
        if (crashed) throw PowerCut{};
        if (event + 1 == fail_before) {
            ++event;
            errno = EIO;
            throw std::system_error(EIO, std::generic_category(), "modeled syscall failure");
        }
    }

    void after_operation() {
        ++event;
        if (event == cut_after) {
            crashed = true;
            throw PowerCut{};
        }
    }

    [[nodiscard]] std::string base(const char* raw) const {
        return std::filesystem::path(raw).filename().string();
    }

    int open(const char* path, int flags) {
        before_operation();
        const int fd = next_fd++;
        if ((flags & O_DIRECTORY) != 0 || std::filesystem::path(path) == directory) {
            handles.emplace(fd, Handle{true, -1});
        } else {
            const auto name = base(path);
            auto found = volatile_names.find(name);
            if (found == volatile_names.end()) {
                const int inode = next_inode++;
                inodes.emplace(inode, Inode{});
                volatile_names.emplace(name, inode);
                found = volatile_names.find(name);
            }
            if ((flags & O_TRUNC) != 0) inodes.at(found->second).volatile_bytes.clear();
            handles.emplace(fd, Handle{false, found->second});
        }
        after_operation();
        return fd;
    }

    ssize_t write(int fd, const void* data, std::size_t size) {
        before_operation();
        auto& bytes = inodes.at(handles.at(fd).inode).volatile_bytes;
        std::size_t accepted = size;
        if (short_first_write && !short_write_done && size > 1) {
            accepted = size / 2;
            short_write_done = true;
        }
        const auto* first = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), first, first + accepted);
        after_operation();
        return static_cast<ssize_t>(accepted);
    }

    int fsync(int fd) {
        before_operation();
        const auto handle = handles.at(fd);
        if (handle.directory) {
            durable_names = volatile_names;
        } else {
            inodes.at(handle.inode).durable_bytes = inodes.at(handle.inode).volatile_bytes;
        }
        after_operation();
        return 0;
    }

    int close(int fd) {
        before_operation();
        handles.erase(fd);
        after_operation();
        return 0;
    }

    int rename(const char* from, const char* to) {
        before_operation();
        const auto source = base(from);
        const auto target = base(to);
        const auto found = volatile_names.find(source);
        if (found == volatile_names.end()) {
            errno = ENOENT;
            throw std::system_error(ENOENT, std::generic_category(), "modeled rename source");
        }
        const int inode = found->second;
        volatile_names.erase(found);
        volatile_names[target] = inode;
        after_operation();
        return 0;
    }

    [[nodiscard]] std::vector<std::byte> recovered(const std::string& name) const {
        const auto found = durable_names.find(name);
        if (found == durable_names.end()) return {};
        return inodes.at(found->second).durable_bytes;
    }
};

thread_local FileSystem* active{};

}

int vpm_open(const char* path, int flags, ...) {
    try {
        return vpm::active->open(path, flags);
    } catch (const std::system_error& error) {
        errno = error.code().value();
        return -1;
    }
}

ssize_t vpm_write(int fd, const void* data, std::size_t size) {
    try {
        return vpm::active->write(fd, data, size);
    } catch (const std::system_error& error) {
        errno = error.code().value();
        return -1;
    }
}

int vpm_fsync(int fd) {
    try {
        return vpm::active->fsync(fd);
    } catch (const std::system_error& error) {
        errno = error.code().value();
        return -1;
    }
}

int vpm_close(int fd) {
    try {
        return vpm::active->close(fd);
    } catch (const vpm::PowerCut&) {
        throw;
    } catch (...) {
        errno = EIO;
        return -1;
    }
}

int vpm_rename(const char* from, const char* to) {
    try {
        return vpm::active->rename(from, to);
    } catch (const std::system_error& error) {
        errno = error.code().value();
        return -1;
    }
}

#define velocita vpm_storage
#include "velocita/storage.hpp"
#define open vpm_open
#define write vpm_write
#define fsync vpm_fsync
#define close vpm_close
#define rename vpm_rename
#ifndef VPM_STORAGE_IMPLEMENTATION
#define VPM_STORAGE_IMPLEMENTATION "../src/storage.cpp"
#endif
#include VPM_STORAGE_IMPLEMENTATION
#undef rename
#undef close
#undef fsync
#undef write
#undef open
#undef velocita

namespace {

using Metadata = vpm_storage::PersistentMetadata;
constexpr const char* kName = vpm_storage::kV2MetadataFileName;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

void require(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

template <typename T>
std::optional<T> take_le(std::span<const std::byte> bytes, std::size_t& offset) {
    using U = std::make_unsigned_t<T>;
    if (offset + sizeof(T) > bytes.size()) return std::nullopt;
    U value{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<U>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (8U * index);
    }
    offset += sizeof(T);
    return static_cast<T>(value);
}

std::uint32_t independent_crc(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

template <typename T>
void append_le(std::vector<std::byte>& bytes, T value) {
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        bytes.push_back(static_cast<std::byte>((bits >> (8U * index)) & 0xFFU));
}

std::vector<std::byte> encode(const Metadata& value) {
    std::vector<std::byte> bytes;
    append_le<std::uint32_t>(bytes, 0x3141544DU);
    append_le<std::uint16_t>(bytes, 1);
    append_le<std::uint16_t>(bytes, 0);
    append_le(bytes, value.term);
    append_le(bytes, value.voted_for);
    append_le<std::int32_t>(bytes, 0);
    append_le(bytes, value.commit_index);
    append_le(bytes, independent_crc(bytes));
    return bytes;
}

std::optional<Metadata> decode(std::span<const std::byte> bytes) {
    if (bytes.size() != 36) return std::nullopt;
    std::size_t offset{};
    const auto magic = take_le<std::uint32_t>(bytes, offset);
    const auto version = take_le<std::uint16_t>(bytes, offset);
    const auto reserved1 = take_le<std::uint16_t>(bytes, offset);
    const auto term = take_le<std::int64_t>(bytes, offset);
    const auto vote = take_le<std::int32_t>(bytes, offset);
    const auto reserved2 = take_le<std::int32_t>(bytes, offset);
    const auto commit = take_le<std::int64_t>(bytes, offset);
    const auto crc = take_le<std::uint32_t>(bytes, offset);
    if (!magic || !version || !reserved1 || !term || !vote || !reserved2 || !commit || !crc ||
        *magic != 0x3141544DU || *version != 1 || *reserved1 != 0 || *reserved2 != 0 ||
        *crc != independent_crc(bytes.first(32)))
        return std::nullopt;
    return Metadata{*term, *vote, *commit};
}

bool equal(const Metadata& left, const Metadata& right) {
    return left.term == right.term && left.voted_for == right.voted_for &&
           left.commit_index == right.commit_index;
}

struct Fixture {
    std::filesystem::path root;
    vpm::FileSystem fs;
    Metadata old_state{7, 2, 19};
    Metadata new_state{8, 3, 23};

    explicit Fixture(std::filesystem::path path) : root(std::move(path)) {
        std::filesystem::create_directories(root);
        fs.directory = root.string();
        const int old_inode = fs.next_inode++;
        const auto old_bytes = encode(old_state);
        fs.inodes.emplace(old_inode, vpm::Inode{old_bytes, old_bytes});
        fs.volatile_names.emplace(kName, old_inode);
        fs.durable_names.emplace(kName, old_inode);
        const auto recovered = decode(fs.recovered(kName));
        require(recovered && equal(*recovered, old_state), "fixture did not durably establish old tuple");
    }

    bool save_new() {
        vpm::active = &fs;
        try {
            vpm_storage::MetadataStore(root, kName).save(new_state);
            return true;
        } catch (const vpm::PowerCut&) {
            return false;
        } catch (const std::exception&) {
            return false;
        }
    }

    void check_oracle(bool acknowledged, const std::string& context) const {
        const auto recovered_bytes = fs.recovered(kName);
        const auto recovered = decode(recovered_bytes);
        require(recovered.has_value(), context + ": power recovery produced invalid metadata (" +
                                           std::to_string(recovered_bytes.size()) + " durable bytes)");
        const bool old = equal(*recovered, old_state);
        const bool fresh = equal(*recovered, new_state);
        require(old || fresh, context + ": recovery produced neither independent old nor new tuple");
        require(!acknowledged || fresh,
                context + ": acknowledged save recovered tuple {term=" +
                    std::to_string(recovered->term) + ", vote=" +
                    std::to_string(recovered->voted_for) + ", commit=" +
                    std::to_string(recovered->commit_index) + "}, not the new tuple");

        const auto materialized_name = std::string("recovered-") + std::to_string(fs.event) + ".bin";
        const auto materialized_path = root / materialized_name;
        std::ofstream output(materialized_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(recovered_bytes.data()),
                     static_cast<std::streamsize>(recovered_bytes.size()));
        output.close();
        require(static_cast<bool>(output), context + ": could not materialize recovered bytes");
        const auto production_loaded = vpm_storage::MetadataStore(root, materialized_name).load();
        require(equal(production_loaded, *recovered),
                context + ": production load disagreed with independent recovery oracle");
    }
};

std::filesystem::path unique_root(const std::string& label, int index) {
    return std::filesystem::temp_directory_path() /
           ("velocita-metadata-model-" + std::to_string(::getpid()) + "-" + label + "-" +
            std::to_string(index));
}

void test_all_power_cut_boundaries() {
    Fixture trace(unique_root("trace", 0));
    require(trace.save_new(), "uncut save failed");
    trace.check_oracle(true, "uncut save");
    const int events = trace.fs.event;
    require(events >= 8, "model observed too few syscall boundaries");

    for (int cut = 1; cut <= events; ++cut) {
        Fixture fixture(unique_root("cut", cut));
        fixture.fs.cut_after = cut;
        const bool acknowledged = fixture.save_new();
        require(!acknowledged, "power cut unexpectedly returned success");
        fixture.check_oracle(false, "cut after syscall " + std::to_string(cut));
    }
}

void test_short_write_and_syscall_failures() {
    Fixture short_write(unique_root("short", 0));
    short_write.fs.short_first_write = true;
    const bool acknowledged = short_write.save_new();
    require(acknowledged, "write_all did not retry the modeled short write to completion");
    short_write.check_oracle(acknowledged, "short write retry");

    const int short_write_events = short_write.fs.event;
    for (int cut = 1; cut <= short_write_events; ++cut) {
        Fixture fixture(unique_root("short-prefix-cut", cut));
        fixture.fs.short_first_write = true;
        fixture.fs.cut_after = cut;
        const bool cut_acknowledged = fixture.save_new();
        require(!cut_acknowledged, "short-write-prefix power cut unexpectedly returned success");
        fixture.check_oracle(false, "short-write-prefix cut after syscall " +
                                        std::to_string(cut));
    }

    Fixture trace(unique_root("failure-trace", 0));
    require(trace.save_new(), "failure trace save failed");
    const int events = trace.fs.event;
    for (int failure = 1; failure <= events; ++failure) {
        Fixture fixture(unique_root("failure", failure));
        fixture.fs.fail_before = failure;
        const bool acknowledged = fixture.save_new();
        fixture.check_oracle(acknowledged, "failure before syscall " + std::to_string(failure));
    }
}

void test_acknowledged_save_is_durable() {
    Fixture fixture(unique_root("acknowledged", 0));
    const bool acknowledged = fixture.save_new();
    require(acknowledged, "save did not acknowledge");
    fixture.check_oracle(true, "acknowledged save");
}

}

int main() {
    try {
        test_all_power_cut_boundaries();
        test_short_write_and_syscall_failures();
        test_acknowledged_save_is_durable();
        std::cout << "metadata persistence ordering model passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "metadata persistence ordering model failed: " << error.what() << '\n';
        return 1;
    }
}
