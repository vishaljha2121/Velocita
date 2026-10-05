#define main replication_fixture_suite_main
#include "v2_replication_tests.cpp"
#undef main

#include "velocita/v2/archive_catalog.hpp"
#include "velocita/v2/archive_segment.hpp"
#include "velocita/v2/bound_metadata.hpp"

#include <array>
#include <filesystem>
#include <memory>

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static ArchivePublishResult publish(ArchiveStore& store,
                                        const ArchivePublicationContext& context,
                                        const ArchiveRecordSource& source) {
        return store.publish(context, source);
    }
};
}

namespace {

struct CatalogImage {
    ArchiveCatalogHeader header;
    std::vector<ArchiveCatalogRow> rows;
};

const VerifiedStaticIdentity& fixture_identity() {
    static const auto value = identity_test::verified_identity(
        identity_test::identity(1, {1}));
    return value;
}

CatalogImage read_catalog(const std::filesystem::path& node_root) {
    const auto bytes = read_bytes(node_root / "v2-archive" /
                                  "v2-archive-catalog.bin");
    CatalogImage image;
    image.rows.resize(kMaxArchiveCatalogRows);
    std::size_t count{};
    require(decode_archive_catalog(bytes, fixture_identity(), image.header,
                                   image.rows, count) == ArchiveCatalogError::none,
            "startup fixture catalog did not decode");
    image.rows.resize(count);
    return image;
}

void write_catalog(const std::filesystem::path& node_root,
                   const CatalogImage& image) {
    auto bytes = std::make_unique<std::array<std::byte, kMaxArchiveCatalogBytes>>();
    const auto encoded = encode_archive_catalog(image.header, image.rows,
                                                fixture_identity(), *bytes);
    require(encoded.error == ArchiveCatalogError::none,
            "startup fixture catalog did not encode");
    write_bytes(node_root / "v2-archive" / "v2-archive-catalog.bin",
                std::span{*bytes}.first(encoded.bytes_written));
}

void create_history(const std::filesystem::path& root, std::uint64_t count,
                    bool compact) {
    Cluster cluster(root, 1);
    for (std::uint64_t sequence = 1; sequence <= count; ++sequence)
        (void)cluster.commit(command(sequence,
            AdvanceKillEpoch{KillEpoch{sequence}}));
    cluster.settle(20);
    if (compact)
        require(identity_test::complete_compaction(cluster.elect()) == count,
                "startup fixture compaction covered the wrong prefix");
}

void require_refusal_preserves_bytes(const std::filesystem::path& root,
                                     const char* failure) {
    const auto node_root = root / "1";
    const auto before = directory_image(node_root);
    bool refused{};
    try {
        Cluster recovered(root, 1, 0xAB, true);
    } catch (const std::exception&) {
        refused = true;
    }
    require(refused, failure);
    require(directory_image(node_root) == before,
            "startup refusal changed preserved runtime/archive evidence");
}

void test_future_archive_storage_generation_refuses(
    const std::filesystem::path& root) {
    create_history(root, 2, true);
    auto catalog = read_catalog(root / "1");
    require(catalog.header.publication_storage_generation == 1,
            "future-generation fixture did not start at selected generation one");
    catalog.header.publication_storage_generation = 2;
    write_catalog(root / "1", catalog);
    require_refusal_preserves_bytes(
        root, "startup served with an archive catalog from future storage generation two");
}

void test_archive_ahead_of_recovered_commit_refuses(
    const std::filesystem::path& root) {
    const auto donor = root / "donor";
    const auto recipient = root / "recipient";
    create_history(donor, 3, true);
    create_history(recipient, 2, false);
    std::filesystem::copy(donor / "1" / "v2-archive",
                          recipient / "1" / "v2-archive",
                          std::filesystem::copy_options::recursive);
    require(read_catalog(recipient / "1").rows.back().last_index == 3,
            "ahead fixture did not install archive coverage through three");
    require_refusal_preserves_bytes(
        recipient, "startup served with archive coverage ahead of recovered commit two");
}

void test_conflicting_archive_snapshot_boundary_term_refuses(
    const std::filesystem::path& root) {
    create_history(root, 2, true);
    const auto node_root = root / "1";
    auto catalog = read_catalog(node_root);
    require(catalog.rows.size() == 1 && catalog.rows[0].last_index == 2 &&
                catalog.rows[0].last_term == 1,
            "boundary fixture did not start with term-one archive through two");

    const auto old_name = "s-" + to_hex(catalog.rows[0].segment_digest) + ".bin";
    const auto old_bytes = read_bytes(node_root / "v2-archive" / old_name);
    struct OwnedRecords { std::vector<velocita::WalRecord> values; } records;
    ArchiveSegmentScratch scratch;
    ArchiveSegmentMetadata old_metadata;
    require(decode_archive_segment(old_bytes, fixture_identity(),
        [](const ArchiveRecordView& record, void* raw) noexcept {
            auto& owned = static_cast<OwnedRecords*>(raw)->values;
            owned.push_back({record.index, 2, record.entry_id, record.entry_type,
                             std::vector<std::byte>(record.payload.begin(),
                                                    record.payload.end())});
            return true;
        }, &records, scratch, old_metadata).error == ArchiveSegmentError::none,
        "boundary fixture source segment did not decode");
    const ArchiveRecordSource source{&records.values,
        static_cast<std::uint32_t>(records.values.size()),
        [](void* raw, std::uint32_t ordinal, ArchiveRecordView& view) noexcept {
            const auto& values = *static_cast<const std::vector<velocita::WalRecord>*>(raw);
            if (ordinal >= values.size()) return false;
            const auto& record = values[ordinal];
            view = {record.index, record.term, record.entry_id, record.entry_type,
                    record.payload};
            return true;
        }};
    auto replacement = std::make_unique<
        std::array<std::byte, kMaxArchiveSegmentBytes>>();
    const auto encoded = encode_archive_segment(source, 0, fixture_identity(),
                                                *replacement, scratch);
    require(encoded.error == ArchiveSegmentError::none,
            "term-conflicting replacement segment did not encode");
    ArchiveSegmentMetadata replacement_metadata;
    require(decode_archive_segment(std::span{*replacement}.first(encoded.bytes_written),
        fixture_identity(), nullptr, nullptr, scratch, replacement_metadata).error ==
            ArchiveSegmentError::none,
        "term-conflicting replacement segment did not verify");
    const auto new_name = "s-" + to_hex(encoded.segment_digest) + ".bin";
    write_bytes(node_root / "v2-archive" / new_name,
                std::span{*replacement}.first(encoded.bytes_written));
    std::filesystem::permissions(node_root / "v2-archive" / new_name,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    std::filesystem::remove(node_root / "v2-archive" / old_name);

    auto& row = catalog.rows[0];
    row.last_term = 2;
    row.segment_digest = replacement_metadata.segment_digest;
    std::copy_n(replacement->begin() + encoded.bytes_written - 64, 32,
                row.terminal_record_digest.bytes.begin());
    catalog.header.observed_current_term = 2;
    write_catalog(node_root, catalog);
    require_refusal_preserves_bytes(
        root, "startup served with archive term two conflicting with snapshot term one at index two");
}

void create_decreasing_term_bridge(const std::filesystem::path& root,
                                   const std::uint64_t archived_through) {
    Cluster cluster(root, 1);
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
        (void)cluster.commit(command(sequence,
            AdvanceKillEpoch{KillEpoch{sequence}}));
    cluster.settle(20);
    const auto node_root = root / "1";
    auto records = identity_test::read_wal(node_root);
    require(records.size() == 3, "bridge fixture did not retain three WAL records");
    const auto state = cluster.elect().state();
    identity_test::snapshot_store(node_root).save(state, 3, 1);
    identity_test::metadata_store(node_root).save({2, -1, 3});

    auto identity_bytes = std::array<std::byte, kMaxStaticIdentityBytes>{};
    const auto identity_encoded = encode_static_identity(
        fixture_identity().value(), identity_bytes);
    auto execution_bytes = std::array<std::byte, kMaxExecutionCatalogBytes>{};
    const auto execution_encoded = catalog_test::standard_catalog().encode(execution_bytes);
    require(identity_encoded.error == StaticIdentityCodecError::none &&
                execution_encoded.error == ExecutionCatalogError::none,
            "bridge fixture dependencies did not encode");
    ArchiveStore archive;
    require(ArchiveStore::open(node_root, fixture_identity(),
                std::span{identity_bytes}.first(identity_encoded.bytes_written),
                std::span{execution_bytes}.first(execution_encoded.bytes_written),
                catalog_test::standard_catalog(), {}, archive) == ArchiveStoreError::none,
            "bridge fixture archive did not open");
    records.resize(archived_through);
    for (auto& record : records) record.term = 2;
    const ArchiveRecordSource source{&records,
        static_cast<std::uint32_t>(records.size()),
        [](void* raw, std::uint32_t ordinal, ArchiveRecordView& view) noexcept {
            const auto& values = *static_cast<const std::vector<velocita::WalRecord>*>(raw);
            if (ordinal >= values.size()) return false;
            const auto& record = values[ordinal];
            view = {record.index, record.term, record.entry_id, record.entry_type,
                    record.payload};
            return true;
        }};
    auto published = ArchiveStoreTestAccess::publish(archive,
        {1, 1, 3, 3, 2, 0, static_cast<std::uint32_t>(3 - archived_through)}, source);
    require(published.error == ArchiveStoreError::none,
            "bridge fixture archive did not publish");
    identity_test::wal_segment(node_root).discard_through(archived_through);
}

void test_decreasing_archive_wal_suffix_term_refuses(
    const std::filesystem::path& root) {
    create_decreasing_term_bridge(root, 1);
    require_refusal_preserves_bytes(
        root, "startup served archive term two followed by WAL suffix term one");
}

void test_decreasing_archive_snapshot_bridge_term_refuses(
    const std::filesystem::path& root) {
    create_decreasing_term_bridge(root, 2);
    require_refusal_preserves_bytes(
        root, "startup served archive term two below snapshot term one through a WAL bridge");
}

struct AutomaticRoot {
    std::filesystem::path path;
    AutomaticRoot() {
        std::array<char, 64> pattern{};
        const std::string value = "/tmp/v2-archive-startup-XXXXXX";
        std::copy(value.begin(), value.end(), pattern.begin());
        const auto* created = ::mkdtemp(pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~AutomaticRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

}

int main() {
    try {
        AutomaticRoot root;
        std::size_t failures{};
        const auto run = [&](const char* name, auto&& test) {
            try {
                test();
            } catch (const std::exception& error) {
                ++failures;
                std::cerr << name << ": " << error.what() << '\n';
            }
        };
        run("future-generation", [&] {
            test_future_archive_storage_generation_refuses(root.path / "future");
        });
        run("archive-ahead", [&] {
            test_archive_ahead_of_recovered_commit_refuses(root.path / "ahead");
        });
        run("boundary-term", [&] {
            test_conflicting_archive_snapshot_boundary_term_refuses(root.path / "term");
        });
        run("archive-wal-suffix-term", [&] {
            test_decreasing_archive_wal_suffix_term_refuses(root.path / "suffix-term");
        });
        run("archive-snapshot-bridge-term", [&] {
            test_decreasing_archive_snapshot_bridge_term_refuses(root.path / "snapshot-term");
        });
        if (failures != 0)
            throw std::runtime_error(std::to_string(failures) +
                                     " archive startup regression(s) failed");
        std::cout << "archive startup regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
