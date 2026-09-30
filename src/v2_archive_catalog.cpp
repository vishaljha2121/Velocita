#include "velocita/v2/archive_catalog.hpp"

#include "velocita/v2/archive_segment.hpp"

#include <array>
#include <bit>
#include <limits>
#include <string_view>

namespace velocita::v2 {
namespace {

constexpr std::string_view kDomain = "velocita.v2.archive-catalog.v1";
static_assert(kDomain.size() == 30);
constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'A'}, std::byte{'C'}};
static_assert(kArchiveCatalogHeaderBytes + kMaxArchiveCatalogRows * kArchiveCatalogRowBytes +
                  kArchiveCatalogDigestBytes == kMaxArchiveCatalogBytes);

void put(std::span<std::byte> bytes, std::size_t at, std::size_t width,
         std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < width; ++i)
        bytes[at + i] = std::byte((value >> (8U * i)) & 0xffU);
}

std::uint64_t get(std::span<const std::byte> bytes, std::size_t at,
                  std::size_t width) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[at + i]))
                 << (8U * i);
    return value;
}

std::int64_t geti64(std::span<const std::byte> b, std::size_t at) noexcept {
    return std::bit_cast<std::int64_t>(get(b, at, 8));
}
std::int32_t geti32(std::span<const std::byte> b, std::size_t at) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(b, at, 4)));
}

bool equal_digest(std::span<const std::byte> bytes, const Digest256& digest) noexcept {
    std::uint8_t difference{};
    for (std::size_t i = 0; i < digest.bytes.size(); ++i)
        difference |= std::to_integer<std::uint8_t>(bytes[i] ^ digest.bytes[i]);
    return difference == 0;
}

bool local_is_voter(const VerifiedStaticIdentity& identity) noexcept {
    const auto& v = identity.value();
    for (std::size_t i = 0; i < v.voter_count; ++i)
        if (v.voters[i] == v.local_node_id) return true;
    return false;
}

ArchiveCatalogError validate_identity(const VerifiedStaticIdentity& identity) noexcept {
    if (!identity.valid()) return ArchiveCatalogError::invalid_identity;
    if (!local_is_voter(identity)) return ArchiveCatalogError::foreign_publisher;
    return ArchiveCatalogError::none;
}

ArchiveCatalogError validate(const ArchiveCatalogHeader& h,
                             std::span<const ArchiveCatalogRow> rows) noexcept {
    constexpr auto kSignedMax = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if (rows.size() > kMaxArchiveCatalogRows) return ArchiveCatalogError::capacity_exceeded;
    if (h.generation == 0 || h.publication_storage_generation == 0)
        return ArchiveCatalogError::invalid_generation;
    if (h.observed_commit_index > kSignedMax || h.observed_durable_index > kSignedMax ||
        h.observed_current_term < 0)
        return ArchiveCatalogError::invalid_coordinates;
    std::uint64_t previous_last{};
    std::int64_t previous_term{};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row.first_index == 0 || row.first_index > kSignedMax ||
            row.last_index < row.first_index || row.last_index > kSignedMax ||
            row.predecessor_term < 0 || row.last_term <= 0 ||
            row.predecessor_term > row.last_term || row.record_count == 0 ||
            row.record_count > kMaxArchiveRecords ||
            row.segment_file_length < kArchiveSegmentHeaderBytes + kArchiveDigestBytes ||
            row.segment_file_length > kMaxArchiveSegmentBytes ||
            row.source_storage_generation == 0 ||
            row.source_storage_generation > h.publication_storage_generation ||
            row.last_index - row.first_index + 1 != row.record_count)
            return ArchiveCatalogError::invalid_coordinates;
        if ((i == 0 && (row.first_index != 1 || row.predecessor_term != 0)) ||
            (i != 0 && (previous_last == std::numeric_limits<std::uint64_t>::max() ||
                        row.first_index != previous_last + 1 ||
                        row.predecessor_term != previous_term)))
            return ArchiveCatalogError::invalid_coordinates;
        previous_last = row.last_index;
        previous_term = row.last_term;
    }
    if (!rows.empty() &&
        (h.observed_commit_index < previous_last || h.observed_durable_index < previous_last ||
         h.observed_current_term < previous_term))
        return ArchiveCatalogError::invalid_coordinates;
    return ArchiveCatalogError::none;
}

void write_row(std::span<std::byte> b, const ArchiveCatalogRow& row) noexcept {
    put(b, 0, 8, row.first_index); put(b, 8, 8, row.last_index);
    put(b, 16, 8, std::bit_cast<std::uint64_t>(row.predecessor_term));
    put(b, 24, 8, std::bit_cast<std::uint64_t>(row.last_term));
    put(b, 32, 4, row.record_count); put(b, 36, 4, row.segment_file_length);
    put(b, 40, 8, row.source_storage_generation);
    for (std::size_t i = 0; i < 32; ++i) {
        b[48 + i] = row.segment_digest.bytes[i];
        b[80 + i] = row.terminal_record_digest.bytes[i];
    }
}

ArchiveCatalogRow read_row(std::span<const std::byte> b) noexcept {
    ArchiveCatalogRow row;
    row.first_index = get(b, 0, 8); row.last_index = get(b, 8, 8);
    row.predecessor_term = geti64(b, 16); row.last_term = geti64(b, 24);
    row.record_count = static_cast<std::uint32_t>(get(b, 32, 4));
    row.segment_file_length = static_cast<std::uint32_t>(get(b, 36, 4));
    row.source_storage_generation = get(b, 40, 8);
    for (std::size_t i = 0; i < 32; ++i) {
        row.segment_digest.bytes[i] = b[48 + i];
        row.terminal_record_digest.bytes[i] = b[80 + i];
    }
    return row;
}

Digest256 digest_body(std::span<const std::byte> body) noexcept {
    Sha256 hash;
    hash.update(std::as_bytes(std::span{kDomain.data(), kDomain.size()}));
    hash.update(body);
    return hash.finish();
}

} // namespace

ArchiveCatalogEncodeResult encode_archive_catalog(
    const ArchiveCatalogHeader& header, std::span<const ArchiveCatalogRow> rows,
    const VerifiedStaticIdentity& identity, std::span<std::byte> output) noexcept {
    if (const auto error = validate_identity(identity); error != ArchiveCatalogError::none)
        return {error};
    if (const auto error = validate(header, rows); error != ArchiveCatalogError::none)
        return {error};
    const auto total = kArchiveCatalogHeaderBytes + rows.size() * kArchiveCatalogRowBytes +
                       kArchiveCatalogDigestBytes;
    if (output.size() < total) return {ArchiveCatalogError::output_too_small};

    auto body = output.first(total - kArchiveCatalogDigestBytes);
    for (std::size_t i = 0; i < 4; ++i) body[i] = kMagic[i];
    put(body, 4, 2, kArchiveCatalogSchemaVersion); put(body, 6, 2, kArchiveCatalogHeaderBytes);
    put(body, 8, 4, total); put(body, 12, 4, rows.size());
    for (std::size_t i = 0; i < 32; ++i) body[16 + i] = identity.manifest_digest().bytes[i];
    put(body, 48, 4, std::bit_cast<std::uint32_t>(identity.value().local_node_id));
    put(body, 52, 4, 0); put(body, 56, 8, header.generation);
    put(body, 64, 8, header.publication_storage_generation);
    put(body, 72, 8, header.observed_commit_index);
    put(body, 80, 8, header.observed_durable_index);
    put(body, 88, 8, std::bit_cast<std::uint64_t>(header.observed_current_term));
    for (std::size_t i = 0; i < rows.size(); ++i)
        write_row(body.subspan(kArchiveCatalogHeaderBytes + i * kArchiveCatalogRowBytes,
                               kArchiveCatalogRowBytes), rows[i]);
    const auto digest = digest_body(body);
    for (std::size_t i = 0; i < 32; ++i) output[body.size() + i] = digest.bytes[i];
    return {ArchiveCatalogError::none, total, digest};
}

ArchiveCatalogError decode_archive_catalog(
    std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    ArchiveCatalogHeader& header, std::span<ArchiveCatalogRow> destination_rows,
    std::size_t& destination_count) noexcept {
    if (const auto error = validate_identity(identity); error != ArchiveCatalogError::none)
        return error;
    if (input.size() < kArchiveCatalogHeaderBytes + kArchiveCatalogDigestBytes)
        return ArchiveCatalogError::truncated;
    for (std::size_t i = 0; i < 4; ++i)
        if (input[i] != kMagic[i]) return ArchiveCatalogError::invalid_magic;
    if (get(input, 4, 2) != kArchiveCatalogSchemaVersion ||
        get(input, 6, 2) != kArchiveCatalogHeaderBytes)
        return ArchiveCatalogError::unsupported_schema;
    const auto total = get(input, 8, 4);
    const auto count = get(input, 12, 4);
    if (count > kMaxArchiveCatalogRows || total > kMaxArchiveCatalogBytes)
        return ArchiveCatalogError::capacity_exceeded;
    const auto expected = kArchiveCatalogHeaderBytes + count * kArchiveCatalogRowBytes +
                          kArchiveCatalogDigestBytes;
    if (total != input.size() || total != expected) return ArchiveCatalogError::length_mismatch;
    if (!equal_digest(input.subspan(16, 32), identity.manifest_digest()))
        return ArchiveCatalogError::foreign_manifest;
    if (geti32(input, 48) != identity.value().local_node_id)
        return ArchiveCatalogError::foreign_publisher;
    if (get(input, 52, 4) != 0) return ArchiveCatalogError::nonzero_flags;
    const auto body = input.first(input.size() - kArchiveCatalogDigestBytes);
    const auto digest = digest_body(body);
    if (!equal_digest(input.last(kArchiveCatalogDigestBytes), digest))
        return ArchiveCatalogError::digest_mismatch;
    if (destination_rows.size() < count) return ArchiveCatalogError::capacity_exceeded;

    ArchiveCatalogHeader candidate{get(input, 56, 8), get(input, 64, 8),
        get(input, 72, 8), get(input, 80, 8), geti64(input, 88)};
    // Pass one validates directly from immutable input without changing caller state.
    std::uint64_t previous_last{};
    std::int64_t previous_term{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto row = read_row(input.subspan(kArchiveCatalogHeaderBytes +
            i * kArchiveCatalogRowBytes, kArchiveCatalogRowBytes));
        // Per-row checks are duplicated here because whole-catalog validation cannot
        // require a 4096-row stack allocation.
        constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (row.first_index == 0 || row.first_index > max || row.last_index < row.first_index ||
            row.last_index > max || row.predecessor_term < 0 || row.last_term <= 0 ||
            row.predecessor_term > row.last_term || row.record_count == 0 ||
            row.record_count > kMaxArchiveRecords ||
            row.segment_file_length < kArchiveSegmentHeaderBytes + kArchiveDigestBytes ||
            row.segment_file_length > kMaxArchiveSegmentBytes || row.source_storage_generation == 0 ||
            row.source_storage_generation > candidate.publication_storage_generation ||
            row.last_index - row.first_index + 1 != row.record_count ||
            (i == 0 && (row.first_index != 1 || row.predecessor_term != 0)) ||
            (i != 0 && (previous_last == std::numeric_limits<std::uint64_t>::max() ||
                row.first_index != previous_last + 1 || row.predecessor_term != previous_term)))
            return ArchiveCatalogError::invalid_coordinates;
        previous_last = row.last_index; previous_term = row.last_term;
    }
    if (candidate.generation == 0 || candidate.publication_storage_generation == 0)
        return ArchiveCatalogError::invalid_generation;
    constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (candidate.observed_commit_index > max || candidate.observed_durable_index > max ||
        candidate.observed_current_term < 0 ||
        (count != 0 && (candidate.observed_commit_index < previous_last ||
            candidate.observed_durable_index < previous_last ||
            candidate.observed_current_term < previous_term)))
        return ArchiveCatalogError::invalid_coordinates;

    // Pass two performs the bounded caller-owned copy only after validation succeeds.
    for (std::size_t i = 0; i < count; ++i)
        destination_rows[i] = read_row(input.subspan(kArchiveCatalogHeaderBytes +
            i * kArchiveCatalogRowBytes, kArchiveCatalogRowBytes));
    header = candidate;
    destination_count = count;
    return ArchiveCatalogError::none;
}

} // namespace velocita::v2
