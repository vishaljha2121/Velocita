// The compaction path's archive check must not cost more as the archive grows
// (M3 finding 53: every compaction reread and resynced every segment, twice,
// and the pause passed the election timeout within two days). These tests pin
// what the cheap check still catches at once, what the rotating scrub catches
// and by when, and that the full check is unchanged.
#include "velocita/v2/archive_store.hpp"
#include "v2_identity_test_support.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static ArchivePublishResult publish(ArchiveStore& store, const ArchivePublicationContext& context,
                                        const ArchiveRecordSource& source) {
        return store.publish(context, source);
    }
    static ArchiveStoreError layout(ArchiveStore& store) { return store.verify_layout(); }
    static ArchiveStoreError full(ArchiveStore& store) { return store.verify_current(); }
    static ArchiveStoreError incremental(ArchiveStore& store) { return store.verify_incremental(); }
};
}  // namespace velocita::v2

namespace {
using namespace velocita::v2;
using Access = ArchiveStoreTestAccess;

void require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<std::byte> hex(const std::string_view text) {
    std::vector<std::byte> out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(std::byte(std::stoul(std::string{text.substr(i, 2)}, nullptr, 16)));
    return out;
}

struct Temp {
    std::filesystem::path path;
    Temp() {
        std::array<char, 64> name{};
        std::strcpy(name.data(), "/tmp/v2-archive-scrub-XXXXXX");
        path = ::mkdtemp(name.data());
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// One record at INDEX, term 3.
struct OneRecord {
    std::vector<std::byte> payload;
    std::uint64_t index{};
    static bool get(void* context, const std::uint32_t ordinal, ArchiveRecordView& out) noexcept {
        if (ordinal) return false;
        auto& self = *static_cast<OneRecord*>(context);
        out = {self.index, 3, static_cast<std::int32_t>(self.index), 0, self.payload};
        return true;
    }
};

std::set<std::string> segment_names(const std::filesystem::path& root) {
    std::set<std::string> out;
    for (const auto& entry : std::filesystem::directory_iterator(root / "v2-archive")) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("s-") && name.ends_with(".bin")) out.insert(name);
    }
    return out;
}

// Flips one byte in the middle of FILE: same length, different content.
void corrupt_in_place(const std::filesystem::path& file) {
    const auto size = std::filesystem::file_size(file);
    std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
    stream.seekg(static_cast<std::streamoff>(size / 2));
    char byte{};
    stream.get(byte);
    stream.seekp(static_cast<std::streamoff>(size / 2));
    stream.put(static_cast<char>(byte ^ 0x40));
    require(stream.good(), "corrupting a segment in place");
}

struct Fixture {
    const VerifiedExecutionCatalog& catalog{catalog_test::standard_catalog()};
    StaticIdentity raw_identity{identity_test::identity(17, {17}, catalog)};
    VerifiedStaticIdentity identity{identity_test::verified_identity(raw_identity)};
    std::array<std::byte, kMaxExecutionCatalogBytes> execution_bytes{};
    std::array<std::byte, kMaxStaticIdentityBytes> identity_bytes{};
    std::size_t execution_length{}, identity_length{};
    std::vector<std::byte> payload{hex(
        "56324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000"
        "000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b")};

    Fixture() {
        const auto row = catalog_test::row(
            AccountId{5},
            {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4}, InstrumentSpecVersion{7},
             MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
             QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
            {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
             kVenueDirectivePlaceLimit | kVenueDirectiveCancel, kVenueTifGtc, 1});
        const auto execution = encode_execution_catalog(std::span{&row, 1}, execution_bytes);
        require(execution.error == ExecutionCatalogError::none, "catalog encode");
        execution_length = execution.bytes_written;
        const auto encoded = encode_static_identity(raw_identity, identity_bytes);
        require(encoded.error == StaticIdentityCodecError::none, "identity encode");
        identity_length = encoded.bytes_written;
    }

    void open(const Temp& temp, ArchiveStore& store) const {
        require(ArchiveStore::open(temp.path, identity, std::span{identity_bytes}.first(identity_length),
                                   std::span{execution_bytes}.first(execution_length), catalog, {}, store) ==
                    ArchiveStoreError::none,
                "open");
    }

    // Publishes record INDEX as its own segment and returns that segment's file.
    std::filesystem::path publish(const Temp& temp, ArchiveStore& store, const std::uint64_t index) const {
        const auto before = segment_names(temp.path);
        OneRecord record{payload, index};
        const ArchiveRecordSource source{&record, 1, &OneRecord::get};
        const auto result = Access::publish(store, {index, 1, index, index, 3, index == 1 ? 0 : 3, 0}, source);
        require(result.error == ArchiveStoreError::none, "publish");
        std::string added;
        for (const auto& name : segment_names(temp.path))
            if (!before.contains(name)) added = name;
        require(!added.empty(), "a publish adds one segment file");
        return temp.path / "v2-archive" / added;
    }

    // An archive of COUNT one-record segments; FILES[i] is catalog row i.
    void build(const Temp& temp, ArchiveStore& store, const std::uint64_t count,
               std::vector<std::filesystem::path>& files) const {
        open(temp, store);
        for (std::uint64_t index = 1; index <= count; ++index) files.push_back(publish(temp, store, index));
    }
};

// After five publishes the scrub cursor is back at row 0 (publishes scrub rows
// {}, {0}, {0,1}, {0,1}, {2,3}), so the calls below read rows {0,1}, {2,3}, {4,0}.

void same_length_corruption_is_found_when_the_scrub_reaches_it_and_not_before(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    require(Access::full(store) == ArchiveStoreError::none, "an intact archive verifies in full");

    corrupt_in_place(files[3]);
    require(Access::layout(store) == ArchiveStoreError::none, "the layout check does not read segment contents");
    require(Access::incremental(store) == ArchiveStoreError::none, "rows 0 and 1 are intact");
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "the scrub reaches row 3 on the second call");
    require(Access::full(store) == ArchiveStoreError::segment_failed, "the full check finds it at once");
}

void an_intact_archive_passes_more_than_one_rotation(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    for (int call = 0; call < 7; ++call)  // rows {0,1} {2,3} {4,0} {1,2} {3,4} {0,1} {2,3}
        require(Access::incremental(store) == ArchiveStoreError::none, "the scrub wraps past the last row");
}

// The scrub runs inside every publish, so compaction alone rereads the archive.
void publishes_alone_find_corruption_when_their_scrub_reaches_it(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    corrupt_in_place(files[3]);
    files.push_back(f.publish(temp, store, 6));  // scrubs rows 0 and 1: intact
    OneRecord record{f.payload, 7};
    const ArchiveRecordSource source{&record, 1, &OneRecord::get};
    const auto result = Access::publish(store, {7, 1, 7, 7, 3, 3, 0}, source);  // scrubs rows 2 and 3
    require(result.error == ArchiveStoreError::segment_failed, "the second publish reaches row 3 and refuses");
    require(store.catalog_generation() == 6 && segment_names(temp.path).size() == 6, "before writing anything");
}

void every_row_is_reread_within_one_rotation(const Fixture& f) {
    const std::size_t count = 5;
    const std::size_t rotation = (count + kArchiveScrubSegmentsPerVerify - 1) / kArchiveScrubSegmentsPerVerify;
    for (std::size_t row = 0; row < count; ++row) {
        Temp temp;
        ArchiveStore store;
        std::vector<std::filesystem::path> files;
        f.build(temp, store, count, files);
        corrupt_in_place(files[row]);
        bool found = false;
        for (std::size_t call = 0; call < rotation && !found; ++call)
            found = Access::incremental(store) == ArchiveStoreError::segment_failed;
        require(found, "a corrupt row must be found within ceil(count / scrub width) calls");
    }
}

void a_failed_scrub_does_not_advance_past_the_bad_segment(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    corrupt_in_place(files[0]);
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "row 0 is in the first window");
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "and is reported again, not skipped");
}

void a_changed_length_is_found_at_once_wherever_the_scrub_is(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    std::filesystem::resize_file(files[4], std::filesystem::file_size(files[4]) - 1);  // row 4: not in {0,1}
    require(Access::layout(store) == ArchiveStoreError::segment_failed, "a truncated segment fails the layout check");
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "so the compaction path refuses at once");
}

void changed_permissions_are_found_at_once(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    std::filesystem::permissions(files[4], std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                               std::filesystem::perms::group_read,
                                 std::filesystem::perm_options::replace);
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "a 0640 segment is not ours alone");
}

void a_publish_refuses_before_writing_when_an_old_segment_changed_length(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 5, files);
    std::filesystem::resize_file(files[4], std::filesystem::file_size(files[4]) - 1);
    OneRecord record{f.payload, 6};
    const ArchiveRecordSource source{&record, 1, &OneRecord::get};
    const auto result = Access::publish(store, {6, 1, 6, 6, 3, 3, 0}, source);
    require(result.error == ArchiveStoreError::segment_failed, "the publish refuses");
    require(store.catalog_generation() == 5 && segment_names(temp.path).size() == 5, "and writes nothing");
}

void one_segment_is_scrubbed_on_every_call(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    std::vector<std::filesystem::path> files;
    f.build(temp, store, 1, files);
    require(Access::incremental(store) == ArchiveStoreError::none, "one intact segment");
    corrupt_in_place(files[0]);
    require(Access::incremental(store) == ArchiveStoreError::segment_failed, "the only segment is always in the window");
}

void an_empty_archive_verifies(const Fixture& f) {
    Temp temp;
    ArchiveStore store;
    f.open(temp, store);
    require(Access::incremental(store) == ArchiveStoreError::none, "nothing to scrub");
    require(Access::full(store) == ArchiveStoreError::none, "nothing to read");
}

void reopening_still_rereads_every_segment(const Fixture& f) {
    Temp temp;
    std::vector<std::filesystem::path> files;
    {
        ArchiveStore store;
        f.build(temp, store, 5, files);
    }
    corrupt_in_place(files[2]);
    ArchiveStore reopened;
    require(ArchiveStore::open(temp.path, f.identity, std::span{f.identity_bytes}.first(f.identity_length),
                               std::span{f.execution_bytes}.first(f.execution_length), f.catalog, {}, reopened) ==
                ArchiveStoreError::segment_failed,
            "startup verification is still complete");
}
}  // namespace

int main() {
    const Fixture fixture;
    same_length_corruption_is_found_when_the_scrub_reaches_it_and_not_before(fixture);
    an_intact_archive_passes_more_than_one_rotation(fixture);
    publishes_alone_find_corruption_when_their_scrub_reaches_it(fixture);
    every_row_is_reread_within_one_rotation(fixture);
    a_failed_scrub_does_not_advance_past_the_bad_segment(fixture);
    a_changed_length_is_found_at_once_wherever_the_scrub_is(fixture);
    changed_permissions_are_found_at_once(fixture);
    a_publish_refuses_before_writing_when_an_old_segment_changed_length(fixture);
    one_segment_is_scrubbed_on_every_call(fixture);
    an_empty_archive_verifies(fixture);
    reopening_still_rereads_every_segment(fixture);
    return 0;
}
