// Replay protection for authenticated envelopes: session epochs and sequences.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocita::v2 {

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

inline constexpr std::size_t kSessionEpochFileBytes = 16;

enum class SessionEpochError : std::uint8_t {
    none, already_exists, missing, invalid_file, exhausted, io_failure,
};

struct SessionEpochResult {
    SessionEpochError error{SessionEpochError::none};
    std::uint64_t epoch{};
};

[[nodiscard]] SessionEpochResult create_session_epoch_file(const std::filesystem::path& file) noexcept;

[[nodiscard]] SessionEpochResult advance_session_epoch_file(const std::filesystem::path& file) noexcept;

}
