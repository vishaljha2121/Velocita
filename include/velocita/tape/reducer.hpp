// The tape's state machine: attestation, books, integrity checks and the Merkle frontier. Pure and bounded.
#pragma once

#include "velocita/tape/entry.hpp"
#include "velocita/tape/merkle.hpp"
#include "velocita/tape/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace velocita::tape {

struct Level {
    std::int64_t price{};
    std::int64_t qty{};
    std::uint32_t count{};
};

struct Change {
    bool bid{};
    std::int64_t price{};
    std::int64_t qty{};
    std::uint32_t count{};
};

struct HeldUpdate {
    std::uint64_t seqnum{};
    std::uint64_t index{};
    std::uint8_t change_count{};
    std::array<Change, kChangesPerHeld> changes{};
};

struct Side {
    std::uint16_t size{};
    std::array<Level, kLevelsPerSide> levels{};
};

struct Book {
    BookState state{BookState::unknown};
    Reason reason{Reason::not_started};
    bool has_position{};
    std::uint64_t position{};
    Side bids;
    Side asks;
    bool staging{};
    bool staging_compare{};
    std::uint64_t staging_seqnum{};
    std::uint64_t staging_tick{};
    Side staged_bids;
    Side staged_asks;
    std::uint8_t held_count{};
    std::uint64_t hold_tick{};
    std::array<HeldUpdate, kHeldPerBook> held{};
    Digest hash;
    bool ebbo_mismatch{};
    bool ebbo_flagged{};
    std::uint64_t ebbo_mismatch_tick{};
};

struct Best {
    bool has{};
    std::uint64_t seqnum{};
    bool has_bid{};
    bool has_ask{};
    Level bid;
    Level ask;
};

struct Copy {
    bool present{};
    Digest hash;
    std::int64_t utc_recv_ns{};
    std::uint64_t index{};
};

struct OpenVerdict {
    bool used{};
    std::uint8_t stream{};
    UpdateKind kind{UpdateKind::none};
    std::uint64_t seqnum{};
    std::uint64_t first_index{};
    std::uint64_t first_tick{};
    std::array<Copy, kRecorders> copies{};
    Digest applied_hash;
};

struct ClosedRef {
    bool used{};
    std::uint8_t stream{};
    UpdateKind kind{UpdateKind::none};
    std::uint64_t seqnum{};
    Digest applied_hash;
};

struct Session {
    bool started{};
    std::uint64_t session{};
    std::uint64_t last_request{};
};

struct Assembly {
    bool used{};
    std::uint64_t session{};
    std::uint64_t first_request{};
    std::uint16_t part_count{};
    std::uint16_t received{};
    std::uint32_t whole_size{};
    std::uint32_t filled{};
    Digest whole_hash;
    std::array<std::byte, kMaxFrameBytes + kMaxEntryBytes> bytes{};
};

struct PendingHead {
    bool used{};
    std::uint64_t tick{};
    std::uint64_t tree_size{};
    Digest root;
    std::array<bool, kVoters> signed_by{};
    std::array<std::array<std::byte, kSignatureBytes>, kVoters> signatures{};
};

inline constexpr std::size_t kSkewBuckets = 9;

struct Counters {
    std::uint64_t entries{};
    std::array<std::uint64_t, 6> entries_by_type{};
    std::array<std::uint64_t, 12> apply_codes{};
    std::array<std::array<std::uint64_t, 9>, kRecorders> events{};
    std::array<std::uint64_t, 5> update_outcomes{};
    std::array<std::uint64_t, 5> all_outcomes{};
    std::array<std::uint64_t, 4> close_reasons{};
    std::uint64_t late_copies{};
    std::uint64_t late_divergences{};
    std::uint64_t duplicate_copies{};
    std::array<std::uint64_t, 10> unknown_reasons{};
    std::array<std::uint64_t, 16> flags{};
    std::array<std::uint64_t, kSkewBuckets> skew_buckets{};
    std::uint64_t skew_samples{};
    std::uint64_t skew_max_ns{};
    std::uint64_t heartbeats{};
};

struct TapeState {
    std::uint64_t applied_index{};
    std::uint64_t last_tick{};
    std::uint8_t symbol_count{};
    std::array<Symbol, kMaxSymbols> symbols{};
    std::array<Book, kMaxSymbols> books{};
    std::array<Best, kMaxSymbols> ebbo{};
    std::array<std::int64_t, kMaxSymbols> band_ppb{};
    std::array<std::int64_t, kMaxStreams> last_utc_ns{};
    std::array<std::int64_t, kMaxStreams> max_silence_ns{};
    std::array<OpenVerdict, kOpenVerdicts> verdicts{};
    std::array<ClosedRef, kRecentClosed> closed{};
    std::uint32_t closed_next{};
    std::array<Session, kRecorders> sessions{};
    std::array<Assembly, kRecorders> assemblies{};
    std::array<PendingHead, kPendingHeads> heads{};
    std::uint64_t head_next{};
    MerkleFrontier frontier;
    Counters counters;
};

struct VerdictClose {
    std::uint8_t stream{};
    UpdateKind kind{UpdateKind::none};
    std::uint64_t seqnum{};
    Outcome outcome{Outcome::only_1_of_3};
    CloseReason reason{CloseReason::all_reported};
    std::uint8_t reported_mask{};
    std::uint8_t distinct{};
    std::uint64_t first_index{};
    std::uint64_t close_index{};
    std::array<Copy, kRecorders> copies{};
};

struct ApplyResult {
    ApplyCode code{ApplyCode::applied};
    EntryType type{EntryType::observation};
    bool decoded{};
    std::uint8_t stream{0xFF};
    BookState book_state{BookState::unknown};
    Digest book_hash;
    std::uint16_t flags{};
    std::uint16_t closes{};
    Digest entry_digest;
    Digest leaf_hash;
    bool head_recorded{};
};

using CloseObserver = void (*)(void* context, const VerdictClose& close);

[[nodiscard]] std::unique_ptr<TapeState> make_tape_state();

ApplyResult apply_entry(TapeState& state, const Config& config, std::uint64_t index,
                        std::span<const std::byte> entry_bytes, CloseObserver observer = nullptr,
                        void* observer_context = nullptr) noexcept;

[[nodiscard]] Digest book_hash(const Symbol& symbol, const Book& book) noexcept;
[[nodiscard]] std::uint8_t stream_id(Channel channel, std::uint8_t symbol_index) noexcept;
[[nodiscard]] Channel stream_channel(std::uint8_t stream) noexcept;
[[nodiscard]] std::uint8_t stream_symbol(std::uint8_t stream) noexcept;
[[nodiscard]] const PendingHead* find_head(const TapeState& state, std::uint64_t tick) noexcept;
[[nodiscard]] std::array<std::byte, 8 + 8 + 8 + 32 + 16> head_message(std::uint64_t incarnation, std::uint64_t tick,
                                                                       std::uint64_t tree_size,
                                                                       const Digest& root) noexcept;

}
