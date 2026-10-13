#include "velocita/v2/archive_segment.hpp"
#include "velocita/v2/leader_read_barrier.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>

namespace velocita::v2 {
namespace {

constexpr std::string_view kSegmentDomain = "velocita.v2.archive-segment.v1";
constexpr std::string_view kRecordDomain = "velocita.v2.archive-record.v1";
static_assert(kSegmentDomain.size() == 30 && kRecordDomain.size() == 29);
constexpr std::array<std::byte, 4> kMagic{
    std::byte{'V'}, std::byte{'2'}, std::byte{'A'}, std::byte{'S'}};

void put(std::span<std::byte> bytes, const std::size_t at,
         const std::size_t width, const std::uint64_t value) noexcept {
    for (std::size_t offset = 0; offset < width; ++offset)
        bytes[at + offset] = std::byte((value >> (offset * 8U)) & 0xffU);
}

std::uint64_t get(const std::span<const std::byte> bytes,
                  const std::size_t at, const std::size_t width) noexcept {
    std::uint64_t value{};
    for (std::size_t offset = 0; offset < width; ++offset)
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(bytes[at + offset]))
                 << (offset * 8U);
    return value;
}

std::int64_t get_i64(const std::span<const std::byte> bytes,
                     const std::size_t at) noexcept {
    return std::bit_cast<std::int64_t>(get(bytes, at, 8));
}

std::int32_t get_i32(const std::span<const std::byte> bytes,
                     const std::size_t at) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(bytes, at, 4)));
}

void hash_ascii(Sha256& hash, const std::string_view text) noexcept {
    hash.update(std::as_bytes(std::span{text.data(), text.size()}));
}

bool digest_matches(const std::span<const std::byte> encoded,
                    const Digest256& expected) noexcept {
    std::uint8_t difference{};
    for (std::size_t index = 0; index < expected.bytes.size(); ++index)
        difference |= std::to_integer<std::uint8_t>(encoded[index] ^ expected.bytes[index]);
    return difference == 0;
}

ArchiveSegmentError validate_payload(const ArchiveRecordView& record,
                                     const VerifiedStaticIdentity& identity,
                                     ArchiveSegmentScratch& scratch) noexcept {
    if (record.index == 0 ||
        record.index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        record.term <= 0)
        return ArchiveSegmentError::invalid_coordinates;
    if (record.payload.size() > kMaxKernelCommandBytes)
        return ArchiveSegmentError::capacity_exceeded;
    if (record.entry_type == kLeaderReadBarrierEntryType)
        return valid_leader_read_barrier(record.payload, identity.authority_domain())
            ? ArchiveSegmentError::none : ArchiveSegmentError::invalid_payload;
    if (record.entry_type == kAuthorityBarrierEntryType || record.entry_type == kSwapAuthorityBarrierEntryType) {
        if (record.payload.size() != kAuthorityBarrierBytes)
            return ArchiveSegmentError::invalid_payload;
        AuthorityBarrier marker;
        if (decode_authority_barrier(kAuthorityBarrierEntryType, record.payload, marker).error !=
                AuthorityBarrierCodecError::none ||
            marker.authority_domain != identity.authority_domain())
            return ArchiveSegmentError::invalid_payload;
        return ArchiveSegmentError::none;
    }
    if (record.entry_type != 0) return ArchiveSegmentError::invalid_entry_type;
    Command command;
    if (decode_kernel_command(record.payload, command, scratch.command).error !=
        KernelCodecError::none)
        return ArchiveSegmentError::invalid_payload;
    return ArchiveSegmentError::none;
}

Digest256 record_digest(const std::span<const std::byte> header,
                        const std::span<const std::byte> payload,
                        const Digest256& manifest) noexcept {
    Sha256 hash;
    hash_ascii(hash, kRecordDomain);
    hash.update(manifest.bytes);
    hash.update(header);
    hash.update(payload);
    return hash.finish();
}

struct ValidatedSource {
    std::uint64_t first_index{};
    std::uint64_t last_index{};
    std::int64_t first_term{};
    std::int64_t last_term{};
    std::size_t total_bytes{};
};

ArchiveSegmentError validate_source(const ArchiveRecordSource& source,
                                    const std::int64_t predecessor_term,
                                    const VerifiedStaticIdentity& identity,
                                    ArchiveSegmentScratch& scratch,
                                    ValidatedSource& validated) noexcept {
    if (!identity.valid()) return ArchiveSegmentError::invalid_identity;
    if (source.record_at == nullptr) return ArchiveSegmentError::source_read_failed;
    if (source.count == 0) return ArchiveSegmentError::invalid_coordinates;
    if (source.count > kMaxArchiveRecords) return ArchiveSegmentError::capacity_exceeded;

    std::size_t total = kArchiveSegmentHeaderBytes + kArchiveDigestBytes;
    std::uint64_t previous_index{};
    std::int64_t previous_term{};
    for (std::uint32_t ordinal = 0; ordinal < source.count; ++ordinal) {
        ArchiveRecordView record;
        if (!source.record_at(source.context, ordinal, record))
            return ArchiveSegmentError::source_read_failed;
        const auto payload_error = validate_payload(record, identity, scratch);
        if (payload_error != ArchiveSegmentError::none) return payload_error;
        if (ordinal == 0) {
            validated.first_index = record.index;
            validated.first_term = record.term;
            if ((record.index == 1 && predecessor_term != 0) ||
                (record.index != 1 && predecessor_term <= 0) ||
                predecessor_term > record.term)
                return ArchiveSegmentError::invalid_coordinates;
        } else {
            if (previous_index == std::numeric_limits<std::uint64_t>::max() ||
                record.index != previous_index + 1 || record.term < previous_term)
                return ArchiveSegmentError::invalid_coordinates;
        }
        previous_index = record.index;
        previous_term = record.term;
        const auto record_bytes = kArchiveRecordHeaderBytes + record.payload.size() +
                                  kArchiveDigestBytes;
        if (record_bytes > kMaxArchiveSegmentBytes ||
            total > kMaxArchiveSegmentBytes - record_bytes)
            return ArchiveSegmentError::capacity_exceeded;
        total += record_bytes;
    }
    validated.last_index = previous_index;
    validated.last_term = previous_term;
    validated.total_bytes = total;
    if (validated.last_index - validated.first_index + 1 != source.count)
        return ArchiveSegmentError::invalid_coordinates;
    return ArchiveSegmentError::none;
}

ArchiveSegmentError validate_record_region(
    const std::span<const std::byte> input, const std::size_t records_end,
    const std::uint32_t count, const std::uint64_t first_index,
    const std::int64_t first_term, const std::int64_t last_term,
    const VerifiedStaticIdentity& identity, ArchiveSegmentScratch& scratch,
    ArchiveRecordVisitor visitor, void* visitor_context) noexcept {
    std::size_t offset = kArchiveSegmentHeaderBytes;
    std::int64_t previous_term{};
    for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
        if (offset > records_end || records_end - offset <
                kArchiveRecordHeaderBytes + kArchiveDigestBytes)
            return ArchiveSegmentError::truncated;
        const auto header = input.subspan(offset, kArchiveRecordHeaderBytes);
        const auto index = get(header, 0, 8);
        const auto term = get_i64(header, 8);
        const auto entry_id = get_i32(header, 16);
        const auto entry_type = get_i32(header, 20);
        const auto payload_size = static_cast<std::size_t>(get(header, 24, 4));
        if (get(header, 28, 4) != 0) return ArchiveSegmentError::nonzero_reserved;
        if (payload_size > kMaxKernelCommandBytes) return ArchiveSegmentError::capacity_exceeded;
        const auto record_size = kArchiveRecordHeaderBytes + payload_size + kArchiveDigestBytes;
        if (record_size > records_end - offset) return ArchiveSegmentError::truncated;
        if (first_index > std::numeric_limits<std::uint64_t>::max() - ordinal ||
            index != first_index + ordinal || term <= 0 ||
            (ordinal != 0 && term < previous_term))
            return ArchiveSegmentError::invalid_coordinates;
        if ((ordinal == 0 && term != first_term) ||
            (ordinal + 1 == count && term != last_term))
            return ArchiveSegmentError::invalid_coordinates;
        const auto payload = input.subspan(offset + kArchiveRecordHeaderBytes, payload_size);
        const ArchiveRecordView record{index, term, entry_id, entry_type, payload};
        const auto payload_error = validate_payload(record, identity, scratch);
        if (payload_error != ArchiveSegmentError::none) return payload_error;
        const auto calculated = record_digest(header, payload, identity.manifest_digest());
        const auto encoded_digest = input.subspan(
            offset + kArchiveRecordHeaderBytes + payload_size, kArchiveDigestBytes);
        if (!digest_matches(encoded_digest, calculated))
            return ArchiveSegmentError::digest_mismatch;
        if (visitor != nullptr && !visitor(record, visitor_context))
            return ArchiveSegmentError::visitor_refused;
        previous_term = term;
        offset += record_size;
    }
    return offset == records_end ? ArchiveSegmentError::none
                                 : ArchiveSegmentError::length_mismatch;
}

}

ArchiveSegmentEncodeResult encode_archive_segment(
    const ArchiveRecordSource& source, const std::int64_t predecessor_term,
    const VerifiedStaticIdentity& identity, const std::span<std::byte> output,
    ArchiveSegmentScratch& scratch) noexcept {
    ValidatedSource validated;
    const auto validation = validate_source(source, predecessor_term, identity, scratch, validated);
    if (validation != ArchiveSegmentError::none) return {validation, 0, {}};
    if (output.size() < validated.total_bytes)
        return {ArchiveSegmentError::output_too_small, 0, {}};

    const auto segment = output.first(validated.total_bytes);
    std::fill(segment.begin(), segment.begin() + kArchiveSegmentHeaderBytes, std::byte{});
    std::copy(kMagic.begin(), kMagic.end(), segment.begin());
    put(segment, 4, 2, kArchiveSegmentSchemaVersion);
    put(segment, 6, 2, kArchiveSegmentHeaderBytes);
    put(segment, 8, 4, validated.total_bytes);
    put(segment, 12, 4, source.count);
    put(segment, 16, 8, validated.first_index);
    put(segment, 24, 8, validated.last_index);
    put(segment, 32, 8, std::bit_cast<std::uint64_t>(predecessor_term));
    put(segment, 40, 8, std::bit_cast<std::uint64_t>(validated.first_term));
    put(segment, 48, 8, std::bit_cast<std::uint64_t>(validated.last_term));
    std::copy(identity.manifest_digest().bytes.begin(),
              identity.manifest_digest().bytes.end(), segment.begin() + 56);

    std::size_t offset = kArchiveSegmentHeaderBytes;
    std::int64_t previous_term{};
    for (std::uint32_t ordinal = 0; ordinal < source.count; ++ordinal) {
        ArchiveRecordView record;
        if (!source.record_at(source.context, ordinal, record))
            return {ArchiveSegmentError::source_read_failed, 0, {}};
        const auto payload_error = validate_payload(record, identity, scratch);
        if (payload_error != ArchiveSegmentError::none) return {payload_error, 0, {}};
        if (record.index != validated.first_index + ordinal || record.term <= 0 ||
            (ordinal == 0 && record.term != validated.first_term) ||
            (ordinal != 0 && record.term < previous_term) ||
            (ordinal + 1 == source.count && record.term != validated.last_term))
            return {ArchiveSegmentError::invalid_coordinates, 0, {}};
        const auto record_size = kArchiveRecordHeaderBytes + record.payload.size() +
                                 kArchiveDigestBytes;
        if (offset > validated.total_bytes - kArchiveDigestBytes ||
            record_size > validated.total_bytes - kArchiveDigestBytes - offset)
            return {ArchiveSegmentError::source_read_failed, 0, {}};
        const auto header = segment.subspan(offset, kArchiveRecordHeaderBytes);
        std::fill(header.begin(), header.end(), std::byte{});
        put(header, 0, 8, record.index);
        put(header, 8, 8, std::bit_cast<std::uint64_t>(record.term));
        put(header, 16, 4, std::bit_cast<std::uint32_t>(record.entry_id));
        put(header, 20, 4, std::bit_cast<std::uint32_t>(record.entry_type));
        put(header, 24, 4, record.payload.size());
        std::copy(record.payload.begin(), record.payload.end(),
                  segment.begin() + static_cast<std::ptrdiff_t>(offset + kArchiveRecordHeaderBytes));
        const auto payload = std::span<const std::byte>{segment}.subspan(
            offset + kArchiveRecordHeaderBytes, record.payload.size());
        const auto digest = record_digest(header, payload, identity.manifest_digest());
        std::copy(digest.bytes.begin(), digest.bytes.end(),
                  segment.begin() + static_cast<std::ptrdiff_t>(
                      offset + kArchiveRecordHeaderBytes + record.payload.size()));
        offset += record_size;
        previous_term = record.term;
    }
    if (offset != validated.total_bytes - kArchiveDigestBytes)
        return {ArchiveSegmentError::source_read_failed, 0, {}};
    Sha256 hash;
    hash_ascii(hash, kSegmentDomain);
    hash.update(std::span<const std::byte>{segment}.first(offset));
    const auto digest = hash.finish();
    std::copy(digest.bytes.begin(), digest.bytes.end(), segment.begin() + offset);
    return {ArchiveSegmentError::none, validated.total_bytes, digest};
}

ArchiveSegmentDecodeResult decode_archive_segment(
    const std::span<const std::byte> input, const VerifiedStaticIdentity& identity,
    const ArchiveRecordVisitor visitor, void* const visitor_context,
    ArchiveSegmentScratch& scratch, ArchiveSegmentMetadata& destination) noexcept {
    if (!identity.valid()) return {ArchiveSegmentError::invalid_identity};
    if (input.size() > kMaxArchiveSegmentBytes) return {ArchiveSegmentError::capacity_exceeded};
    if (input.size() < kArchiveSegmentHeaderBytes + kArchiveDigestBytes)
        return {ArchiveSegmentError::truncated};
    if (!std::equal(kMagic.begin(), kMagic.end(), input.begin()))
        return {ArchiveSegmentError::invalid_magic};
    if (get(input, 4, 2) != kArchiveSegmentSchemaVersion)
        return {ArchiveSegmentError::unsupported_schema};
    if (get(input, 6, 2) != kArchiveSegmentHeaderBytes)
        return {ArchiveSegmentError::length_mismatch};
    const auto total = static_cast<std::size_t>(get(input, 8, 4));
    if (total > kMaxArchiveSegmentBytes) return {ArchiveSegmentError::capacity_exceeded};
    if (total != input.size()) return {ArchiveSegmentError::length_mismatch};
    const auto count = static_cast<std::uint32_t>(get(input, 12, 4));
    if (count == 0) return {ArchiveSegmentError::invalid_coordinates};
    if (count > kMaxArchiveRecords) return {ArchiveSegmentError::capacity_exceeded};
    if (get(input, 88, 4) != 0)
        return {ArchiveSegmentError::unsupported_schema};
    if (get(input, 92, 4) != 0)
        return {ArchiveSegmentError::nonzero_reserved};
    if (!std::equal(identity.manifest_digest().bytes.begin(),
                    identity.manifest_digest().bytes.end(), input.begin() + 56))
        return {ArchiveSegmentError::foreign_manifest};

    const auto first_index = get(input, 16, 8);
    const auto last_index = get(input, 24, 8);
    const auto predecessor_term = get_i64(input, 32);
    const auto first_term = get_i64(input, 40);
    const auto last_term = get_i64(input, 48);
    if (first_index == 0 || last_index == 0 ||
        first_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        last_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        last_index < first_index || first_term <= 0 || last_term <= 0 ||
        last_index - first_index + 1 != count ||
        (first_index == 1 && predecessor_term != 0) ||
        (first_index != 1 && predecessor_term <= 0) || predecessor_term > first_term)
        return {ArchiveSegmentError::invalid_coordinates};

    const auto records_end = input.size() - kArchiveDigestBytes;
    Sha256 segment_hash;
    hash_ascii(segment_hash, kSegmentDomain);
    segment_hash.update(input.first(records_end));
    if (!digest_matches(input.last(kArchiveDigestBytes), segment_hash.finish()))
        return {ArchiveSegmentError::digest_mismatch};
    const auto pass_one = validate_record_region(input, records_end, count, first_index,
        first_term, last_term, identity, scratch, nullptr, nullptr);
    if (pass_one != ArchiveSegmentError::none) return {pass_one};
    const auto pass_two = validate_record_region(input, records_end, count, first_index,
        first_term, last_term, identity, scratch, visitor, visitor_context);
    if (pass_two != ArchiveSegmentError::none) return {pass_two};

    ArchiveSegmentMetadata decoded;
    decoded.first_index = first_index;
    decoded.last_index = last_index;
    decoded.predecessor_term = predecessor_term;
    decoded.first_term = first_term;
    decoded.last_term = last_term;
    decoded.record_count = count;
    decoded.total_bytes = static_cast<std::uint32_t>(total);
    std::copy(input.end() - static_cast<std::ptrdiff_t>(kArchiveDigestBytes), input.end(),
              decoded.segment_digest.bytes.begin());
    destination = decoded;
    return {ArchiveSegmentError::none};
}

}
