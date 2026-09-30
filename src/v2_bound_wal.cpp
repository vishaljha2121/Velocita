#include "velocita/v2/bound_wal.hpp"
#include "velocita/v2/authority_barrier.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace velocita::v2 {
namespace {
using Error = BoundFrameError;
[[noreturn]] void refuse(Error code, const char* message) { throw BoundWalError(code, message); }
std::uint64_t get(std::span<const std::byte> bytes, std::size_t at, std::size_t width) {
    std::uint64_t value{};
    for (std::size_t i = 0; i < width; ++i)
        value |= std::uint64_t(std::to_integer<unsigned>(bytes[at + i])) << (8 * i);
    return value;
}
void put(std::span<std::byte> bytes, std::size_t at, std::size_t width, std::uint64_t value) {
    for (std::size_t i = 0; i < width; ++i) bytes[at + i] = std::byte((value >> (8 * i)) & 255);
}
void validate(const WalRecord& record, const VerifiedStaticIdentity& identity) {
    if (!identity.valid()) refuse(Error::invalid_identity, "WAL requires verified identity");
    if (!record.index || record.index > std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
        record.term <= 0) refuse(Error::invalid_payload, "invalid WAL coordinates");
    if (record.payload.size() > kMaxKernelCommandBytes)
        refuse(Error::capacity_exceeded, "WAL payload exceeds canonical bound");
    if (record.entry_type == kAuthorityBarrierEntryType) {
        AuthorityBarrier marker;
        if (decode_authority_barrier(record.entry_type, record.payload, marker).error !=
                AuthorityBarrierCodecError::none || marker.authority_domain != identity.authority_domain())
            refuse(Error::invalid_payload, "invalid WAL authority marker");
    } else {
        Command command;
        KernelCommandCodecScratch scratch;
        if (record.entry_type != 0 || decode_kernel_command(record.payload, command, scratch).error !=
                KernelCodecError::none) refuse(Error::invalid_payload, "invalid WAL canonical command");
    }
}
} // namespace
std::vector<std::byte> encode_bound_wal_record(const WalRecord& record,
                                              const VerifiedStaticIdentity& identity) {
    validate(record, identity);
    std::array<std::byte, 24 + kMaxKernelCommandBytes> payload{};
    put(payload, 0, 8, record.index);
    put(payload, 8, 8, static_cast<std::uint64_t>(record.term));
    put(payload, 16, 4, static_cast<std::uint32_t>(record.entry_id));
    put(payload, 20, 4, static_cast<std::uint32_t>(record.entry_type));
    std::copy(record.payload.begin(), record.payload.end(), payload.begin() + 24);
    std::vector<std::byte> bytes(112 + record.payload.size());
    BoundFrameScratch scratch;
    const auto encoded = encode_bound_frame(BoundFrameKind::wal_record, identity,
        std::span{payload}.first(24 + record.payload.size()), bytes, scratch);
    if (encoded.error != Error::none) refuse(encoded.error, "WAL frame encode refused");
    return bytes;
}
WalRecord decode_bound_wal_record(std::span<const std::byte> bytes,
                                  const VerifiedStaticIdentity& identity) {
    BoundFrameScratch scratch;
    BoundFrameView view;
    const auto result = decode_bound_frame(bytes, BoundFrameKind::wal_record, identity, view, scratch);
    if (result != Error::none) refuse(result, "WAL frame refused");
    WalRecord record;
    record.index = get(view.payload, 0, 8);
    record.term = std::bit_cast<std::int64_t>(get(view.payload, 8, 8));
    record.entry_id = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(view.payload, 16, 4)));
    record.entry_type = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(view.payload, 20, 4)));
    record.payload.assign(view.payload.begin() + 24, view.payload.end());
    validate(record, identity);
    return record;
}
std::vector<WalRecord> decode_bound_wal_records(std::span<const std::byte> bytes,
                                              const VerifiedStaticIdentity& identity) {
    std::vector<WalRecord> records;
    while (!bytes.empty()) {
        if (bytes.size() < kBoundFrameHeaderBytes) refuse(Error::truncated, "partial record in complete WAL block");
        const auto length = get(bytes, 8, 4);
        if (length > kMaxBoundWalRecordBytes) refuse(Error::capacity_exceeded, "WAL record exceeds frame bound");
        if (length < 112 || length > bytes.size()) refuse(Error::length_mismatch, "partial record in complete WAL block");
        records.push_back(decode_bound_wal_record(bytes.first(length), identity));
        bytes = bytes.subspan(length);
    }
    return records;
}
} // namespace velocita::v2
