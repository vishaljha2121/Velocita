#include "velocita/v2/node_health.hpp"

#include <atomic>
#include <bit>
#include <fcntl.h>
#include <new>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
namespace {

constexpr std::uint64_t kMagic = 0x3148484C56ULL;
constexpr std::uint32_t kVersion = 4;
constexpr std::size_t kCells = 30;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "cross-process seqlock cells must be address-free lock-free atomics");

}

struct NodeHealthPage::Page {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t cells;
    std::atomic<std::uint64_t> sequence;
    std::array<std::atomic<std::uint64_t>, kCells> value;
};

namespace {

std::array<std::uint64_t, kCells> flatten(const NodeHealth& h) noexcept {
    return {static_cast<std::uint64_t>(static_cast<std::uint32_t>(h.node_id)), h.pid, h.session_epoch, h.role,
            h.ready, h.failed, std::bit_cast<std::uint64_t>(h.term),
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(h.leader_id)),
            std::bit_cast<std::uint64_t>(h.current_index), std::bit_cast<std::uint64_t>(h.commit_index),
            std::bit_cast<std::uint64_t>(h.applied_index), h.durable_index, h.turns, h.last_turn_monotonic_ns,
            h.auth_refusals, h.fence_refusals, h.peer_busy_holds, h.peer_backpressure, h.ingress_refusals,
            h.results_sent, h.results_dropped, h.secret_reloads, h.secret_reload_failures,
            h.retained_ordinary, h.retained_protected,
            std::bit_cast<std::uint64_t>(h.compaction_floor), std::bit_cast<std::uint64_t>(h.snapshot_index),
            h.stranded_voters, h.compactions, h.last_compaction_ms};
}

NodeHealth unflatten(const std::array<std::uint64_t, kCells>& v) noexcept {
    NodeHealth h;
    h.node_id = static_cast<std::int32_t>(static_cast<std::uint32_t>(v[0]));
    h.pid = v[1]; h.session_epoch = v[2]; h.role = static_cast<std::uint8_t>(v[3]);
    h.ready = v[4] != 0; h.failed = v[5] != 0; h.term = std::bit_cast<std::int64_t>(v[6]);
    h.leader_id = static_cast<std::int32_t>(static_cast<std::uint32_t>(v[7]));
    h.current_index = std::bit_cast<std::int64_t>(v[8]); h.commit_index = std::bit_cast<std::int64_t>(v[9]);
    h.applied_index = std::bit_cast<std::int64_t>(v[10]); h.durable_index = v[11]; h.turns = v[12];
    h.last_turn_monotonic_ns = v[13]; h.auth_refusals = v[14]; h.fence_refusals = v[15];
    h.peer_busy_holds = v[16]; h.peer_backpressure = v[17]; h.ingress_refusals = v[18];
    h.results_sent = v[19]; h.results_dropped = v[20];
    h.secret_reloads = v[21]; h.secret_reload_failures = v[22];
    h.retained_ordinary = v[23]; h.retained_protected = v[24];
    h.compaction_floor = std::bit_cast<std::int64_t>(v[25]); h.snapshot_index = std::bit_cast<std::int64_t>(v[26]);
    h.stranded_voters = v[27]; h.compactions = v[28]; h.last_compaction_ms = v[29];
    return h;
}

}

std::unique_ptr<NodeHealthPage> NodeHealthPage::create(const std::filesystem::path& file) {
    const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) throw std::runtime_error("health page create failed: " + file.string());
    if (::ftruncate(fd, sizeof(Page)) != 0) {
        ::close(fd);
        throw std::runtime_error("health page size failed");
    }
    void* mapping = ::mmap(nullptr, sizeof(Page), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) throw std::runtime_error("health page map failed");
    auto* page = new (mapping) Page{};
    page->version = kVersion;
    page->cells = kCells;
    std::atomic_thread_fence(std::memory_order_release);
    page->magic = kMagic;
    return std::unique_ptr<NodeHealthPage>(new NodeHealthPage(page));
}

NodeHealthPage::~NodeHealthPage() { ::munmap(page_, sizeof(Page)); }

void NodeHealthPage::publish(const NodeHealth& health) noexcept {
    const auto values = flatten(health);
    const auto sequence = page_->sequence.load(std::memory_order_relaxed);
    page_->sequence.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t i = 0; i < kCells; ++i) page_->value[i].store(values[i], std::memory_order_relaxed);
    page_->sequence.store(sequence + 2, std::memory_order_release);
}

std::optional<NodeHealth> read_node_health(const std::filesystem::path& file) {
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        static_cast<std::size_t>(info.st_size) < sizeof(NodeHealthPage::Page)) {
        ::close(fd);
        return std::nullopt;
    }
    void* mapping = ::mmap(nullptr, sizeof(NodeHealthPage::Page), PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) return std::nullopt;
    const auto* page = static_cast<const NodeHealthPage::Page*>(mapping);
    std::optional<NodeHealth> result;
    if (page->magic == kMagic && page->version == kVersion && page->cells == kCells) {
        for (int attempt = 0; attempt < 1000 && !result; ++attempt) {
            const auto before = page->sequence.load(std::memory_order_acquire);
            if (before == 0) break;
            if (before % 2 != 0) continue;
            std::array<std::uint64_t, kCells> values{};
            for (std::size_t i = 0; i < kCells; ++i) values[i] = page->value[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (page->sequence.load(std::memory_order_relaxed) == before) result = unflatten(values);
        }
    }
    ::munmap(const_cast<void*>(mapping), sizeof(NodeHealthPage::Page));
    return result;
}

}
