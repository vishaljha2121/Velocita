#include "velocita/v2/archive_store.hpp"
#include "v2_catalog_test_support.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace destination_fault {
enum class Target { none, file, directory };
thread_local Target target{};
int fsync(int fd) {
    struct stat info{};
    const bool selected = ::fstat(fd, &info) == 0 &&
        ((target == Target::file && S_ISREG(info.st_mode)) ||
         (target == Target::directory && S_ISDIR(info.st_mode)));
    if (selected) {
        errno = EIO;
        return -1;
    }
    return ::fsync(fd);
}
} // namespace destination_fault

// Compile the real verifier/store privately so the durability failure reaches
// the actual fsync shim. This remains a native simulated trusted transport;
// production gets no raw authentication bypass.
#define fsync destination_fault::fsync
#include "../src/v2_archive_store.cpp"
#undef fsync
#include "../src/v2_archive_destination.cpp"

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static ArchivePublishResult publish(ArchiveStore& store,
        const ArchivePublicationContext& context, const ArchiveRecordSource& source) {
        return store.publish(context, source);
    }
    static ArchiveDestinationRequest request(const ArchiveStore& store,
        std::uint64_t generation, std::uint64_t attempt, const Digest256& challenge,
        const Digest256& destination) {
        return store.make_destination_request(generation, attempt, challenge, destination);
    }
    static ArchiveCoverageCapability coverage(const ArchiveStore& store,
        std::uint64_t generation, std::uint64_t cut) {
        return store.coverage_for_reclamation(generation, cut);
    }
    static bool consume(ArchiveStore& store, ArchiveCoverageCapability&& local,
        ArchiveDestinationEvidence&& independent, const ArchiveDestinationRequest& expected,
        std::uint64_t generation, std::uint64_t cut) {
        return store.consume_for_reclamation(std::move(local), std::move(independent),
                                             expected, generation, cut);
    }
};

struct ArchiveDestinationTestAccess {
    static ArchiveDestinationResult verify(const std::filesystem::path& root,
        const VerifiedStaticIdentity& destination, const VerifiedStaticIdentity& publisher,
        std::span<const std::byte> identity, std::span<const std::byte> catalog,
        const VerifiedExecutionCatalog& execution, const ArchiveStoreConfig& config,
        const ArchiveDestinationRequest& request) {
        return ArchiveDestinationVerifier::verify_existing(root, destination, publisher,
            identity, catalog, execution, config, request);
    }
    static bool consume(ArchiveDestinationEvidence& evidence,
                        const ArchiveDestinationRequest& expected) {
        return evidence.consume(expected);
    }
};
} // namespace velocita::v2

namespace {
using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const char* stem) {
        auto pattern = std::filesystem::temp_directory_path() / (std::string(stem) + "-XXXXXX");
        auto text = pattern.string();
        path = ::mkdtemp(text.data());
        require(!path.empty(), "temporary root creation failed");
    }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

struct Bytes {
    std::array<std::byte, kMaxStaticIdentityBytes> identity{};
    std::size_t identity_size{};
    std::array<std::byte, kMaxExecutionCatalogBytes> catalog{};
    std::size_t catalog_size{};
};

Bytes canonical(const StaticIdentity& identity, const VerifiedExecutionCatalog& catalog) {
    Bytes out;
    const auto encoded_identity = encode_static_identity(identity, out.identity);
    const auto encoded_catalog = catalog.encode(out.catalog);
    require(encoded_identity.error == StaticIdentityCodecError::none &&
            encoded_catalog.error == ExecutionCatalogError::none,
            "canonical fixture encoding failed");
    out.identity_size = encoded_identity.bytes_written;
    out.catalog_size = encoded_catalog.bytes_written;
    return out;
}

Digest256 identity_digest(const StaticIdentity& identity) {
    std::array<std::byte, kMaxStaticIdentityBytes> bytes{};
    const auto encoded = encode_static_identity(identity, bytes);
    require(encoded.error == StaticIdentityCodecError::none, "destination identity encoding failed");
    return sha256_digest(std::span{bytes}.first(encoded.bytes_written));
}

std::vector<std::byte> command_bytes(std::uint64_t sequence) {
    Digest256 payload_digest{};
    payload_digest.bytes[0] = std::byte{0x61};
    Command command{{CallerId{7}, RequestSequence{sequence}, payload_digest},
                    AdvanceKillEpoch{KillEpoch{sequence}}};
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command, bytes);
    require(encoded.error == KernelCodecError::none, "archive command encoding failed");
    return {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written)};
}

struct Source {
    std::vector<std::byte> payload{command_bytes(1)};
    static bool visit(void* context, std::uint32_t at, ArchiveRecordView& out) noexcept {
        if (at != 0) return false;
        auto& self = *static_cast<Source*>(context);
        out = {1, 3, 9, 0, self.payload};
        return true;
    }
};

using Image = std::vector<std::pair<std::string, std::vector<char>>>;
Image image(const std::filesystem::path& root) {
    Image result;
    if (!std::filesystem::exists(root)) return result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        result.emplace_back(std::filesystem::relative(entry.path(), root).string(),
                            std::vector<char>{std::istreambuf_iterator<char>{input}, {}});
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct ExpectedRecord {
    const std::vector<std::byte>* payload{};
    std::size_t seen{};
    static bool visit(const ArchiveRecordView& row, void* context) noexcept {
        auto& expected = *static_cast<ExpectedRecord*>(context);
        if (expected.seen != 0 || row.index != 1 || row.term != 3 || row.entry_id != 9 ||
            row.entry_type != 0 ||
            !std::ranges::equal(row.payload, *expected.payload)) return false;
        ++expected.seen;
        return true;
    }
};

void require_exact_archive_payload(const std::filesystem::path& root,
                                   const VerifiedStaticIdentity& publisher,
                                   const std::vector<std::byte>& payload) {
    ExpectedRecord expected{&payload, 0};
    std::size_t segments{};
    for (const auto& entry : std::filesystem::directory_iterator(root / "v2-archive")) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("s-") || !name.ends_with(".bin")) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        const std::vector<char> stored{std::istreambuf_iterator<char>{input}, {}};
        ArchiveSegmentScratch scratch;
        ArchiveSegmentMetadata metadata;
        const auto decoded = decode_archive_segment(std::as_bytes(std::span{stored}), publisher,
            &ExpectedRecord::visit, &expected, scratch, metadata);
        require(decoded.error == ArchiveSegmentError::none,
                "destination segment did not decode under publisher identity");
        ++segments;
    }
    require(segments == 1 && expected.seen == 1,
            "destination archive did not contain the exact authored historical record");
}

struct Fixture {
    TempRoot source_root{"v2-archive-source"};
    TempRoot destination_root{"v2-archive-destination"};
    const VerifiedExecutionCatalog& catalog{catalog_test::standard_catalog()};
    StaticIdentity publisher_raw{identity_test::identity(1, {1, 2}, catalog)};
    StaticIdentity destination_raw{identity_test::identity(2, {1, 2}, catalog)};
    VerifiedStaticIdentity publisher;
    VerifiedStaticIdentity destination;
    Bytes bytes;
    ArchiveStore source;
    ArchiveStore destination_archive;
    Source records;
    ArchiveCoverageCapability local;
    ArchiveDestinationRequest request;
    Digest256 challenge{};

    Fixture() {
        publisher_raw.deployment_mode = DeploymentMode::two_host_pilot;
        destination_raw.deployment_mode = DeploymentMode::two_host_pilot;
        publisher = identity_test::verified_identity(publisher_raw);
        destination = identity_test::verified_identity(destination_raw);
        bytes = canonical(publisher_raw, catalog);
        require(ArchiveStore::open(source_root.path, publisher,
                    std::span{bytes.identity}.first(bytes.identity_size),
                    std::span{bytes.catalog}.first(bytes.catalog_size), catalog, {}, source) ==
                    ArchiveStoreError::none,
                "source archive open failed");
        require(ArchiveStore::open(destination_root.path, publisher,
                    std::span{bytes.identity}.first(bytes.identity_size),
                    std::span{bytes.catalog}.first(bytes.catalog_size), catalog, {}, destination_archive) ==
                    ArchiveStoreError::none,
                "destination archive open failed");
        ArchiveRecordSource input{&records, 1, &Source::visit};
        auto source_published = ArchiveStoreTestAccess::publish(source, {1, 7, 1, 1, 3, 0, 0}, input);
        require(source_published.error == ArchiveStoreError::none, "source publication failed");
        local = std::move(source_published.capability);
        auto destination_published = ArchiveStoreTestAccess::publish(
            destination_archive, {1, 7, 1, 1, 3, 0, 0}, input);
        require(destination_published.error == ArchiveStoreError::none,
                "destination publication failed");
        challenge.bytes[0] = std::byte{0x42};
        challenge.bytes[31] = std::byte{0xA7};
        request = ArchiveStoreTestAccess::request(
            source, 7, 41, challenge, identity_digest(destination_raw));
        require(request.last_index == 1 && request.attempt == 41,
                "source did not bind a complete destination request");
        const auto destination_view = ArchiveStoreTestAccess::request(
            destination_archive, 7, 41, challenge, identity_digest(destination_raw));
        require(destination_view == request, "source and exact destination archive coordinates differ");
        destination_archive = ArchiveStore{};
        std::ofstream(source_root.path / "v2-wal-blocks.bin", std::ios::binary).write("wal-proof", 9);
    }

    ArchiveDestinationResult verify(const ArchiveDestinationRequest& candidate,
                                    const VerifiedStaticIdentity& pin) const {
        return ArchiveDestinationTestAccess::verify(destination_root.path, pin, publisher,
            std::span{bytes.identity}.first(bytes.identity_size),
            std::span{bytes.catalog}.first(bytes.catalog_size), catalog, {}, candidate);
    }
};

void require_unchanged(const Image& source, const Image& destination, const Fixture& fixture,
                       const std::string& context) {
    require(image(fixture.source_root.path) == source, context + " changed source archive/WAL");
    require(image(fixture.destination_root.path) == destination, context + " changed destination");
}

void test_exact_verification_and_one_use() {
    Fixture fixture;
    const auto source_before = image(fixture.source_root.path);
    const auto destination_before = image(fixture.destination_root.path);
    auto verified = fixture.verify(fixture.request, fixture.destination);
    require(verified.error == ArchiveDestinationError::none && verified.evidence.verified() &&
            verified.evidence.request() == fixture.request,
            "exact independent destination did not mint matching evidence: " +
                std::to_string(static_cast<unsigned>(verified.error)));
    require_exact_archive_payload(fixture.destination_root.path, fixture.publisher,
                                  fixture.records.payload);
    require(ArchiveStoreTestAccess::consume(fixture.source, std::move(fixture.local),
                std::move(verified.evidence), fixture.request, 7, 1),
            "exact local plus independent evidence did not authorize reclamation");
    require(!verified.evidence.verified(), "consumed independent evidence remained reusable");
    auto second_local = ArchiveStoreTestAccess::coverage(fixture.source, 7, 1);
    require(!ArchiveStoreTestAccess::consume(fixture.source, std::move(second_local),
                std::move(verified.evidence), fixture.request, 7, 1),
            "independent destination evidence was accepted twice");
    require_unchanged(source_before, destination_before, fixture, "successful verification");
}

void test_absent_wrong_and_stale_destination() {
    Fixture fixture;
    const auto source_before = image(fixture.source_root.path);
    const auto destination_before = image(fixture.destination_root.path);
    TempRoot absent{"v2-archive-absent-destination"};
    auto missing = ArchiveDestinationTestAccess::verify(absent.path, fixture.destination,
        fixture.publisher, std::span{fixture.bytes.identity}.first(fixture.bytes.identity_size),
        std::span{fixture.bytes.catalog}.first(fixture.bytes.catalog_size), fixture.catalog, {},
        fixture.request);
    require(missing.error == ArchiveDestinationError::archive_missing && !missing.evidence.verified(),
            "absent destination minted evidence");

    auto wrong_raw = identity_test::identity(3, {1, 3}, fixture.catalog, 0xEF);
    wrong_raw.deployment_mode = DeploymentMode::two_host_pilot;
    const auto wrong_pin = identity_test::verified_identity(wrong_raw);
    auto wrong = fixture.verify(fixture.request, wrong_pin);
    require(wrong.error == ArchiveDestinationError::destination_mismatch && !wrong.evidence.verified(),
            "wrong pinned destination minted evidence");

    for (unsigned mutation = 0; mutation != 3; ++mutation) {
        auto stale = fixture.request;
        if (mutation == 0) ++stale.catalog_generation;
        if (mutation == 1) ++stale.publication_storage_generation;
        if (mutation == 2) stale.catalog_digest.bytes[0] ^= std::byte{1};
        auto result = fixture.verify(stale, fixture.destination);
        require(result.error != ArchiveDestinationError::none && !result.evidence.verified(),
                "stale destination request minted evidence: mutation=" +
                    std::to_string(mutation) + " error=" +
                    std::to_string(static_cast<unsigned>(result.error)));
    }

    auto evidence = fixture.verify(fixture.request, fixture.destination);
    require(evidence.error == ArchiveDestinationError::none, "stale consume seed failed");
    auto stale_attempt = fixture.request;
    ++stale_attempt.attempt;
    require(!ArchiveDestinationTestAccess::consume(evidence.evidence, stale_attempt) &&
                !evidence.evidence.verified(),
            "stale attempt consumed independent evidence");
    auto generation_evidence = fixture.verify(fixture.request, fixture.destination);
    auto stale_generation = fixture.request;
    ++stale_generation.local_storage_generation;
    require(!ArchiveDestinationTestAccess::consume(generation_evidence.evidence, stale_generation),
            "stale local generation consumed independent evidence");
    auto challenge_evidence = fixture.verify(fixture.request, fixture.destination);
    auto stale_challenge = fixture.request;
    stale_challenge.challenge.bytes[1] ^= std::byte{1};
    require(!ArchiveDestinationTestAccess::consume(challenge_evidence.evidence, stale_challenge),
            "wrong fresh-start challenge consumed independent evidence");
    require_unchanged(source_before, destination_before, fixture, "destination refusal");
}

void test_payload_dependency_and_durability_refusal() {
    Fixture fixture;
    const auto source_before = image(fixture.source_root.path);
    const auto destination_before = image(fixture.destination_root.path);

    const auto archive = fixture.destination_root.path / "v2-archive";
    std::filesystem::path segment;
    for (const auto& entry : std::filesystem::directory_iterator(archive)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("s-") && name.ends_with(".bin")) segment = entry.path();
    }
    require(!segment.empty(), "destination segment missing");
    {
        std::fstream bytes(segment, std::ios::binary | std::ios::in | std::ios::out);
        constexpr auto payload_offset = kArchiveSegmentHeaderBytes + kArchiveRecordHeaderBytes;
        bytes.seekg(payload_offset);
        char value{};
        bytes.read(&value, 1);
        bytes.seekp(payload_offset);
        value ^= 1;
        bytes.write(&value, 1);
    }
    auto corrupt = fixture.verify(fixture.request, fixture.destination);
    require(corrupt.error == ArchiveDestinationError::archive_invalid && !corrupt.evidence.verified(),
            "payload-corrupt destination minted evidence");
    require(image(fixture.source_root.path) == source_before,
            "payload refusal changed source archive/WAL");

    for (const auto target : {destination_fault::Target::file,
                              destination_fault::Target::directory}) {
        Fixture durable;
        const auto durable_source = image(durable.source_root.path);
        const auto durable_destination = image(durable.destination_root.path);
        destination_fault::target = target;
        auto failed_sync = durable.verify(durable.request, durable.destination);
        destination_fault::target = destination_fault::Target::none;
        require(failed_sync.error == ArchiveDestinationError::archive_invalid &&
                    !failed_sync.evidence.verified(),
                "destination file/directory fsync failure minted evidence");
        require_unchanged(durable_source, durable_destination, durable, "durability failure");
    }

    // A complete archive under another valid catalog/manifest is still not the
    // requested dependency. Self-consistency is not authority to relabel it.
    TempRoot foreign_root{"v2-archive-foreign-catalog"};
    const auto row = catalog_test::row(AccountId{19},
        {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
         InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100), PriceTicks::from_atoms(1),
         PriceTicks::from_atoms(100), QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
        {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
         kVenueDirectivePlaceLimit, kVenueTifGtc, 1});
    const auto foreign_catalog = catalog_test::catalog(std::span{&row, 1});
    auto foreign_raw = identity_test::identity(1, {1, 2}, foreign_catalog, 0x91);
    foreign_raw.deployment_mode = DeploymentMode::two_host_pilot;
    const auto foreign_identity = identity_test::verified_identity(foreign_raw);
    const auto foreign_bytes = canonical(foreign_raw, foreign_catalog);
    ArchiveStore foreign;
    require(ArchiveStore::open(foreign_root.path, foreign_identity,
                std::span{foreign_bytes.identity}.first(foreign_bytes.identity_size),
                std::span{foreign_bytes.catalog}.first(foreign_bytes.catalog_size),
                foreign_catalog, {}, foreign) == ArchiveStoreError::none,
            "foreign self-consistent archive open failed");
    Source foreign_source;
    ArchiveRecordSource input{&foreign_source, 1, &Source::visit};
    require(ArchiveStoreTestAccess::publish(foreign, {1, 7, 1, 1, 3, 0, 0}, input).error ==
                ArchiveStoreError::none,
            "foreign self-consistent archive publication failed");
    auto mismatch = ArchiveDestinationTestAccess::verify(foreign_root.path, fixture.destination,
        fixture.publisher, std::span{fixture.bytes.identity}.first(fixture.bytes.identity_size),
        std::span{fixture.bytes.catalog}.first(fixture.bytes.catalog_size), fixture.catalog, {},
        fixture.request);
    require((mismatch.error == ArchiveDestinationError::dependency_mismatch ||
             mismatch.error == ArchiveDestinationError::request_mismatch ||
             mismatch.error == ArchiveDestinationError::archive_invalid) &&
                !mismatch.evidence.verified(),
            "self-consistent foreign dependency minted evidence: " +
                std::to_string(static_cast<unsigned>(mismatch.error)));
    require(image(fixture.source_root.path) == source_before,
            "dependency refusal changed source archive/WAL");
    require(destination_before != image(fixture.destination_root.path),
            "payload corruption fixture was vacuous");
}
} // namespace

int main() {
    try {
        test_exact_verification_and_one_use();
        test_absent_wrong_and_stale_destination();
        test_payload_dependency_and_durability_refusal();
        std::cout << "archive destination tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        destination_fault::target = destination_fault::Target::none;
        std::cerr << "archive destination tests failed: " << error.what() << '\n';
        return 1;
    }
}
