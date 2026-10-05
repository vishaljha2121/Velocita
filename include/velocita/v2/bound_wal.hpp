// Write-ahead-log records bound to the static identity.
#pragma once

#include "velocita/storage.hpp"
#include "velocita/v2/bound_frame.hpp"
#include <stdexcept>

namespace velocita::v2 {
class BoundWalError final : public std::runtime_error {
public:
    explicit BoundWalError(BoundFrameError code, const char* message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] BoundFrameError code() const noexcept { return code_; }
private:
    BoundFrameError code_;
};

[[nodiscard]] std::vector<std::byte> encode_bound_wal_record(
    const WalRecord& record, const VerifiedStaticIdentity& identity);
[[nodiscard]] WalRecord decode_bound_wal_record(
    std::span<const std::byte> bytes, const VerifiedStaticIdentity& identity);
[[nodiscard]] std::vector<WalRecord> decode_bound_wal_records(
    std::span<const std::byte> bytes, const VerifiedStaticIdentity& identity);
}
