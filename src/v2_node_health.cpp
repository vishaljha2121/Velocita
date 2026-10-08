#include "velocita/v2/node_health.hpp"
#include "velocita/v2/node.hpp"

#include <algorithm>
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
constexpr std::uint32_t kVersion = 9;
constexpr std::size_t kScalarCells = 40;
constexpr std::size_t kLedgerCells = 9;
constexpr std::size_t kCells = kScalarCells + kNodeHealthLedgers * kLedgerCells;
static_assert(kNodeHealthLedgers == kMaxRiskAccounts);
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
    std::array<std::uint64_t, kCells> v{static_cast<std::uint64_t>(static_cast<std::uint32_t>(h.node_id)), h.pid, h.session_epoch, h.role,
            h.ready, h.failed, std::bit_cast<std::uint64_t>(h.term),
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(h.leader_id)),
            std::bit_cast<std::uint64_t>(h.current_index), std::bit_cast<std::uint64_t>(h.commit_index),
            std::bit_cast<std::uint64_t>(h.applied_index), h.durable_index, h.turns, h.last_turn_monotonic_ns,
            h.auth_refusals, h.fence_refusals, h.peer_busy_holds, h.peer_backpressure, h.ingress_refusals,
            h.results_sent, h.results_dropped, h.secret_reloads, h.secret_reload_failures,
            h.retained_ordinary, h.retained_protected,
            std::bit_cast<std::uint64_t>(h.compaction_floor), std::bit_cast<std::uint64_t>(h.snapshot_index),
            h.stranded_voters, h.compactions, h.last_compaction_ms,
            h.receipts, h.pinned_receipts, h.pinned_receipts_peak,
            h.archive_bytes, h.archive_byte_limit, h.archive_objects, h.archive_object_limit, h.policies, h.ledger_count, h.quorum_step_downs};
    for (std::size_t index = 0; index < kNodeHealthLedgers; ++index) {
        const auto& l = h.ledgers[index];
        const std::array<std::uint64_t, kLedgerCells> cells{
            l.account_id, l.instrument_id, std::bit_cast<std::uint64_t>(l.position_lots),
            std::bit_cast<std::uint64_t>(l.cash_micros), std::bit_cast<std::uint64_t>(l.cost_basis_micros),
            std::bit_cast<std::uint64_t>(l.realized_pnl_micros), std::bit_cast<std::uint64_t>(l.fees_paid_micros),
            std::bit_cast<std::uint64_t>(l.window_realized_pnl_micros),
            std::bit_cast<std::uint64_t>(l.max_window_loss_micros)};
        std::copy(cells.begin(), cells.end(), v.begin() + static_cast<std::ptrdiff_t>(kScalarCells + index * kLedgerCells));
    }
    return v;
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
    h.receipts = v[30]; h.pinned_receipts = v[31]; h.pinned_receipts_peak = v[32];
    h.archive_bytes = v[33]; h.archive_byte_limit = v[34]; h.archive_objects = v[35]; h.archive_object_limit = v[36];
    h.policies = v[37];
    h.ledger_count = v[38];
    h.quorum_step_downs = v[39];
    for (std::size_t index = 0; index < kNodeHealthLedgers; ++index) {
        const auto at = kScalarCells + index * kLedgerCells;
        h.ledgers[index] = {v[at], v[at + 1], std::bit_cast<std::int64_t>(v[at + 2]),
                            std::bit_cast<std::int64_t>(v[at + 3]), std::bit_cast<std::int64_t>(v[at + 4]),
                            std::bit_cast<std::int64_t>(v[at + 5]), std::bit_cast<std::int64_t>(v[at + 6]),
                            std::bit_cast<std::int64_t>(v[at + 7]), std::bit_cast<std::int64_t>(v[at + 8])};
    }
    return h;
}

}

NodeHealth node_health_of(const NodeStatus& status, const NodeHealthContext& context) noexcept {
    const auto& r = status.replica;
    const auto& c = status.counters;
    NodeHealth h;
    h.node_id = r.node_id;
    h.pid = context.pid;
    h.session_epoch = status.session_epoch;
    h.role = r.role == "leader" ? 3 : r.role == "candidate" ? 2 : r.role == "follower" ? 1 : 0;
    h.ready = status.ready;
    h.failed = context.failed;
    h.term = r.term;
    h.leader_id = r.leader_id;
    h.current_index = r.current_index;
    h.commit_index = r.commit_index;
    h.applied_index = r.applied_index;
    h.durable_index = status.durable_index;
    h.turns = context.turns;
    h.last_turn_monotonic_ns = context.monotonic_ns;
    h.auth_refusals = c.auth_refusals;
    h.fence_refusals = c.fence_refusals;
    h.peer_busy_holds = c.peer_busy_holds;
    h.peer_backpressure = c.peer_backpressure;
    h.ingress_refusals = c.ingress_refusals;
    h.results_sent = c.results_sent;
    h.results_dropped = c.results_dropped;
    h.secret_reloads = context.secret_reloads;
    h.secret_reload_failures = context.secret_reload_failures;
    h.retained_ordinary = r.retained_ordinary;
    h.retained_protected = r.retained_protected;
    h.compaction_floor = r.compaction_floor;
    h.snapshot_index = r.snapshot_index;
    h.stranded_voters = r.stranded_voters;
    h.compactions = c.compactions;
    h.last_compaction_ms = c.last_compaction_ms;
    h.receipts = r.receipts;
    h.pinned_receipts = r.pinned_receipts;
    h.pinned_receipts_peak = r.pinned_receipts_peak;
    h.archive_bytes = r.archive_bytes;
    h.archive_byte_limit = r.archive_byte_limit;
    h.archive_objects = r.archive_objects;
    h.archive_object_limit = r.archive_object_limit;
    h.policies = r.policies;
    h.quorum_step_downs = r.quorum_step_downs;
    h.ledger_count = std::min<std::uint64_t>(r.ledger_count, kNodeHealthLedgers);
    for (std::size_t index = 0; index < h.ledger_count; ++index) {
        const auto& l = r.ledgers[index];
        h.ledgers[index] = {l.account_id, l.instrument_id, l.position_lots, l.cash_micros, l.cost_basis_micros,
                            l.realized_pnl_micros, l.fees_paid_micros, l.window_realized_pnl_micros,
                            l.max_window_loss_micros};
    }
    return h;
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
