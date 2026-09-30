#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocita::v2 {

// Receiver-side replay fence for authenticated envelopes (M2 decision C).
// Call admit() ONLY after verify_auth_envelope succeeded: an unauthenticated
// frame must never be able to advance the remembered epoch.
//
// Within one sender session the sequence strictly increases. A higher epoch is
// a newer process incarnation and permanently retires every lower one, which
// is the transport fence F5_LOST_VOTER_REPAIR_REVIEW.md requires. State lives
// in memory only; a receiver restart accepts the first authentic epoch again.
class SessionFence final {
public:
    enum class Decision : std::uint8_t { accept, stale_epoch, replayed_sequence, invalid_counter };

    [[nodiscard]] Decision admit(std::uint64_t session_epoch, std::uint64_t sequence) noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] std::uint64_t last_sequence() const noexcept { return sequence_; }

private:
    std::uint64_t epoch_{};
    std::uint64_t sequence_{};
};

// Sender side: hands out strictly increasing sequences for one epoch, starting
// after ALREADY_ISSUED. Nullopt for epoch zero or once exhausted; the owner must
// stop sending rather than wrap.
class SessionSequencer final {
public:
    explicit SessionSequencer(std::uint64_t session_epoch, std::uint64_t already_issued = 0) noexcept
        : epoch_(session_epoch), issued_(already_issued) {}
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] std::optional<std::uint64_t> next() noexcept;

private:
    std::uint64_t epoch_{};
    std::uint64_t issued_{};
};

// The approved 16-byte local file: "VLS1" | u64 LE counter | CRC-32 LE of the
// first 12 bytes. It must live OUTSIDE the kernel root, whose strict inventory
// refuses unknown entries.
inline constexpr std::size_t kSessionEpochFileBytes = 16;

enum class SessionEpochError : std::uint8_t {
    none, already_exists, missing, invalid_file, exhausted, io_failure,
};

struct SessionEpochResult {
    SessionEpochError error{SessionEpochError::none};
    std::uint64_t epoch{};
};

// Bootstrap only: exclusive create of counter 1, file and directory synced
// before returning. Refuses an existing file rather than adopting it.
[[nodiscard]] SessionEpochResult create_session_epoch_file(const std::filesystem::path& file) noexcept;

// Every recovery start: read, validate, write counter+1 to a temporary, sync,
// rename, sync the directory, THEN return the new epoch. A crash before the
// rename leaves the old counter authoritative; the aborted value was never
// used, so reusing it is safe. A leftover temporary is never promoted.
// A missing, torn or corrupt file refuses: the node must not guess an epoch.
[[nodiscard]] SessionEpochResult advance_session_epoch_file(const std::filesystem::path& file) noexcept;

}  // namespace velocita::v2
