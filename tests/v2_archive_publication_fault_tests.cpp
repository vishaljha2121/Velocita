#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include "velocita/v2/archive_store.hpp"
#include "velocita/v2/archive_file.hpp"
#include "velocita/v2/wal_arena.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace archive_fault {
enum class Call { openat, mkdirat, write, fsync, renameat, no_replace, close };
enum class Effect { eio, interrupt_once, short_write };
struct Event { Call call; unsigned segment; bool essential; };
struct Plan {
    std::array<Event, 256> events{};
    std::size_t count{}, target{};
    unsigned segment{};
    Effect effect{};
    bool fired{}, overflow{};
};
thread_local Plan* active{};
bool hit(Call call, bool essential = true) noexcept {
    if (!active) return false;
    if (active->count == active->events.size()) {
        active->overflow = true;
        errno = EIO;
        return true;
    }
    active->events[active->count++] = {call, active->segment, essential};
    if (active->target != active->count || active->fired) return false;
    active->fired = true;
    errno = active->effect == Effect::interrupt_once ? EINTR : EIO;
    return true;
}
int openat(int dir, const char* name, int flags, ...) {
    mode_t mode{};
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int)); va_end(args);
    }
    if (active && (flags & O_CREAT) &&
        std::strcmp(name, "v2-archive-segment.bin.tmp") == 0) ++active->segment;
    struct stat st{};
    const bool absent_catalog = !(flags & O_CREAT) &&
        std::strcmp(name, "v2-archive-catalog.bin") == 0 &&
        ::fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
    if (hit(Call::openat, !absent_catalog)) return -1;
    return ::openat(dir, name, flags, mode);
}
int mkdirat(int dir, const char* name, mode_t mode) {
    if (hit(Call::mkdirat)) return -1;
    return ::mkdirat(dir, name, mode);
}
ssize_t write(int fd, const void* data, size_t size) {
    if (hit(Call::write)) {
        if (active->effect == Effect::short_write && size > 1)
            return ::write(fd, data, size / 2);
        return -1;
    }
    return ::write(fd, data, size);
}
int fsync(int fd) {
    if (hit(Call::fsync)) return -1;
    return ::fsync(fd);
}
int renameat(int from, const char* source, int to, const char* dest) {
    if (hit(Call::renameat)) return -1;
    return ::renameat(from, source, to, dest);
}
#if defined(__APPLE__)
int renameatx_np(int from, const char* source, int to, const char* dest, unsigned flags) {
    if (hit(Call::no_replace)) return -1;
    return ::renameatx_np(from, source, to, dest, flags);
}
#endif
#if defined(__linux__)
long syscall(long number, int from, const char* source, int to, const char* dest, unsigned flags) {
    if (number != SYS_renameat2) {
        if (active) active->overflow = true;
        errno = EINVAL;
        return -1;
    }
    if (hit(Call::no_replace)) return -1;
    return ::syscall(number, from, source, to, dest, flags);
}
#endif
int close_fd(int fd) {
    const bool fail = hit(Call::close);
    const int saved = errno;
    const int result = ::close(fd);
    if (fail) { errno = saved; return -1; }
    return result;
}
}
int store_close(int fd) { return archive_fault::close_fd(fd); }

#define openat archive_fault::openat
#define mkdirat archive_fault::mkdirat
#define write archive_fault::write
#define fsync archive_fault::fsync
#define renameat archive_fault::renameat
#if defined(__APPLE__)
#define renameatx_np archive_fault::renameatx_np
#endif
#if defined(__linux__)
#define syscall archive_fault::syscall
#endif
#define close store_close
#include "../src/v2_archive_store.cpp"
#undef openat
#undef mkdirat
#undef write
#undef fsync
#undef renameat
#if defined(__APPLE__)
#undef renameatx_np
#endif
#if defined(__linux__)
#undef syscall
#endif
#undef close

#define main archive_store_fixture_main
#include "v2_archive_store_tests.cpp"
#undef main

namespace {
using namespace velocita::v2;
struct Fixture {
    Temp temp;
    const VerifiedExecutionCatalog& catalog{catalog_test::standard_catalog()};
    StaticIdentity raw{identity_test::identity(17, {17}, catalog)};
    VerifiedStaticIdentity identity{identity_test::verified_identity(raw)};
    std::array<std::byte, kMaxExecutionCatalogBytes> catalog_bytes{};
    std::size_t catalog_size{};
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::size_t identity_size{};
    ArchiveStore store;
    Source source{hex("56324b43380000000a00060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000004c4d7905b073c3f1141a95f18113055108729d74a81dc69a6d6bb6327d3241cd")};
    Fixture() {
        const auto row = catalog_test::row(AccountId{5},
            {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
             InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1),
             PriceTicks::from_atoms(100), QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
            {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
             kVenueDirectivePlaceLimit | kVenueDirectiveCancel, kVenueTifGtc, 1});
        const auto ce = encode_execution_catalog(std::span{&row, 1}, catalog_bytes);
        require(ce.error == ExecutionCatalogError::none, "fixture catalog");
        catalog_size = ce.bytes_written;
        const auto ie = encode_static_identity(raw, identity_bytes);
        require(ie.error == StaticIdentityCodecError::none, "fixture identity");
        identity_size = ie.bytes_written;
    }
    ArchiveStoreError open(ArchiveStore& out) const {
        return ArchiveStore::open(temp.path, identity,
            std::span{identity_bytes}.first(identity_size),
            std::span{catalog_bytes}.first(catalog_size), catalog, {}, out);
    }
    ArchivePublishResult first() {
        const ArchiveRecordSource records{&source, 1, &Source::get};
        return ArchiveStoreTestAccess::publish(store, {1, 1, 1, 1, 3, 0, 0}, records);
    }
};
ArchiveImage full_image(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root / "v2-archive")) return {};
    auto image = archive_image(root);
    image.emplace_back("/", std::vector<char>{});
    return image;
}
struct Expected {
    const std::vector<std::byte>& payload;
    std::uint64_t cut{};
    bool many{};
    std::vector<std::uint64_t> indices;
    static bool visit(const ArchiveRecordView& row, void* ptr) noexcept {
        auto& e = *static_cast<Expected*>(ptr);
        if (!row.index || row.index > e.cut || row.term != 3 || row.entry_type != 0 ||
            row.entry_id != (e.many || row.index != 1 ? static_cast<std::int32_t>(row.index) : 7) ||
            !std::ranges::equal(row.payload, e.payload) || e.indices.size() >= e.cut) return false;
        e.indices.push_back(row.index);
        return true;
    }
};
void restart(Fixture& f, std::uint64_t old_cut, std::uint64_t candidate,
             bool success, const std::vector<std::byte>& payload, bool many) {
    f.store = ArchiveStore{};
    const auto before = full_image(f.temp.path);
    bool present = false;
    const auto verified = ArchiveStore::verify_existing(f.temp.path, f.identity,
        std::span{f.identity_bytes}.first(f.identity_size),
        std::span{f.catalog_bytes}.first(f.catalog_size), f.catalog, {}, present);
    require(full_image(f.temp.path) == before, "restart verification changed fault evidence");
    if (verified != ArchiveStoreError::none) {
        require(!success && !present, "failed verification advertised usable authority");
        return;
    }
    if (!present) {
        require(!success && old_cut == 0, "restart lost a successfully published archive");
        return;
    }
    ArchiveStore reopened;
    require(f.open(reopened) == ArchiveStoreError::none, "verified image cannot reopen");
    const auto cut = reopened.covered_through();
    require(cut == old_cut || cut == candidate, "restart invented partial history");
    require(!success || cut == candidate, "successful publication lost its coverage");
    Expected expected{payload, cut, many, {}};
    expected.indices.reserve(cut);
    for (const auto& path : segments(f.temp.path)) {
        std::ifstream input(path, std::ios::binary);
        require(input.good(), "read reopened archive segment");
        const std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
        ArchiveSegmentScratch scratch;
        ArchiveSegmentMetadata metadata;
        const auto decoded = decode_archive_segment(std::as_bytes(std::span{bytes}),
            f.identity, &Expected::visit, &expected, scratch, metadata);
        require(decoded.error == ArchiveSegmentError::none, "restart changed exact historical tuple or payload");
    }
    std::sort(expected.indices.begin(), expected.indices.end());
    require(expected.indices.size() == cut, "restart missing history records");
    for (std::size_t i = 0; i < expected.indices.size(); ++i)
        require(expected.indices[i] == i + 1, "restart duplicated or skipped a history index");
}
enum class Scenario { creation, first, replacement, multiple };
archive_fault::Plan trial(Scenario scenario, std::size_t target = 0,
                         archive_fault::Effect effect = archive_fault::Effect::eio) {
    Fixture f;
    if (scenario != Scenario::creation)
        require(f.open(f.store) == ArchiveStoreError::none, "fixture archive open");
    if (scenario == Scenario::replacement)
        require(f.first().error == ArchiveStoreError::none, "seed prior catalog");
    const bool multiple = scenario == Scenario::multiple;
    ManySource many{multiple ? maximum_command() : f.source.payload, multiple ? 2264U : 1U};
    OffsetSource next{f.source.payload, 2, 3};
    const auto before = multiple ? many.payload : f.source.payload;
    archive_fault::Plan plan;
    plan.target = target;
    plan.effect = effect;
    archive_fault::active = &plan;
    bool success{};
    if (scenario == Scenario::creation) {
        success = f.open(f.store) == ArchiveStoreError::none;
    } else {
        ArchivePublishResult result;
        if (scenario == Scenario::first) result = f.first();
        else if (scenario == Scenario::replacement) {
            ArchiveRecordSource records{&next, 1, &OffsetSource::get};
            result = ArchiveStoreTestAccess::publish(f.store, {2, 1, 2, 2, 3, 3, 0}, records);
        } else {
            ArchiveRecordSource records{&many, many.count, &ManySource::get};
            result = ArchiveStoreTestAccess::publish(f.store, {1, 1, many.count, many.count, 3, 0, 0}, records);
        }
        success = result.error == ArchiveStoreError::none;
        require(success == result.capability.local_publication_durable(),
                "status and reclamation capability disagree");
    }
    archive_fault::active = nullptr;
    require(!plan.overflow, "syscall trace exceeded fixed harness capacity");
    require(!target || plan.fired, "targeted syscall never fired");
    if (!target || effect != archive_fault::Effect::eio)
        require(success, "baseline or retryable fault failed publication");
    else
        require(!success, "EIO released publication success");
    require((multiple ? many.payload : f.source.payload) == before &&
            (scenario != Scenario::replacement || next.payload == before),
            "fault changed owned source bytes");
    if (multiple && target)
        require(segments(f.temp.path).size() == 1, "second-object fault did not retain first immutable object");
    const auto candidate = scenario == Scenario::creation ? 0U :
        scenario == Scenario::replacement ? 2U : multiple ? many.count : 1U;
    restart(f, scenario == Scenario::replacement ? 1 : 0, candidate, success, before, multiple);
    return plan;
}
}
int main() {
    try {
        std::size_t failed_sites{};
        for (const auto scenario : {Scenario::creation, Scenario::first, Scenario::replacement}) {
            const auto baseline = trial(scenario);
            for (std::size_t i = 0; i < baseline.count; ++i) {
                if (!baseline.events[i].essential) continue;
                trial(scenario, i + 1);
                ++failed_sites;
            }
        }
        const auto first = trial(Scenario::first);
        auto ordinal = [](const archive_fault::Plan& plan, archive_fault::Call call, unsigned segment) {
            for (std::size_t i = 0; i < plan.count; ++i)
                if (plan.events[i].call == call && plan.events[i].segment == segment) return i + 1;
            throw std::runtime_error("requested syscall absent from successful trace");
        };
        trial(Scenario::first, ordinal(first, archive_fault::Call::write, 1),
              archive_fault::Effect::interrupt_once);
        trial(Scenario::first, ordinal(first, archive_fault::Call::write, 1),
              archive_fault::Effect::short_write);
        trial(Scenario::first, ordinal(first, archive_fault::Call::fsync, 1),
              archive_fault::Effect::interrupt_once);
        const auto multi = trial(Scenario::multiple);
        for (auto call : {archive_fault::Call::write, archive_fault::Call::fsync,
                          archive_fault::Call::no_replace}) {
            trial(Scenario::multiple, ordinal(multi, call, 2));
            ++failed_sites;
        }
        std::cout << "archive publication: " << failed_sites
                  << " reached EIO sites, 3 successful retries; exact restart history checked\n";
        return 0;
    } catch (const std::exception& e) {
        archive_fault::active = nullptr;
        std::cerr << "archive publication fault tests: " << e.what() << '\n';
        return 1;
    }
}
