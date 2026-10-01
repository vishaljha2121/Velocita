#pragma once

#include "velocita/v2/paper_venue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace velocita::v2::paper {

// The paper venue's durable state: one file, rewritten whole (temporary file,
// fsync, rename, fsync of the directory) after every change and before the
// answer that depends on it is returned. A restart therefore answers exactly as
// the process before it would have: the same balances, the same known orders
// (duplicates and queries), the same owned reports.

inline constexpr std::uint32_t kPaperStateVersion = 1;
inline constexpr std::array<std::byte, 8> kPaperStateMagic{
    std::byte{'V'}, std::byte{'P'}, std::byte{'A'}, std::byte{'P'},
    std::byte{'E'}, std::byte{'R'}, std::byte{'S'}, std::byte{'T'}};

// How the paper account came to exist. Log indices and positions are one
// cluster's, so a file is bound to that cluster's incarnation.
struct PaperStateOrigin {
    std::array<std::byte, 16> cluster_incarnation{};
    MoneyMicros funding{};  // the quote the operator funded the account with
    bool adopted{};
    QuantityLots adopted_lots{};
    MoneyMicros adopted_cost{};  // operator-supplied cost basis of those lots, fees included
    std::int64_t adopted_at_index{};
    std::uint64_t adopted_at_state_version{};

    constexpr bool operator==(const PaperStateOrigin&) const noexcept = default;
};

struct PaperState {
    PaperStateOrigin origin{};
    PaperVenueImage venue{};
};

enum class PaperStateError : std::uint8_t {
    none = 0,
    missing,          // no file: a first start, which needs an explicit bootstrap
    exists,           // a bootstrap refused because the file is already there
    unreadable,
    torn,             // shorter or longer than its own header says
    bad_magic,
    bad_version,
    digest_mismatch,
    malformed,        // digest-correct bytes that break a field or capacity rule
    other_cluster,    // written for a different cluster incarnation
    other_venue,      // written for a different venue or instrument, or refused by the venue
    write_failed,
};

[[nodiscard]] const char* describe(PaperStateError error) noexcept;

// magic(8) | version u32 | payload bytes u32 | payload | SHA-256 of all preceding bytes.
[[nodiscard]] std::vector<std::byte> encode_paper_state(const PaperState& state);
[[nodiscard]] PaperStateError decode_paper_state(std::span<const std::byte> bytes, PaperState& out);

struct PaperStateLoad {
    PaperStateError error{PaperStateError::none};
    std::unique_ptr<PaperState> state;
};

[[nodiscard]] PaperStateLoad load_paper_state(const std::filesystem::path& file);
[[nodiscard]] PaperStateError store_paper_state(const std::filesystem::path& file,
                                                std::span<const std::byte> bytes);
// A first start only: refuses (exists) rather than replace a file.
[[nodiscard]] PaperStateError create_paper_state(const std::filesystem::path& file, const PaperState& state);

struct PaperBootstrap {
    std::string error;  // empty on success
    std::unique_ptr<PaperState> state;
};

// A new, flat account funded with FUNDING. The caller must have found the
// kernel flat for the served row: a fresh venue beside a held position is the
// break this file exists to prevent.
[[nodiscard]] PaperBootstrap fresh_paper_state(const PaperVenueConfig& config,
                                               const std::array<std::byte, 16>& cluster_incarnation,
                                               MoneyMicros funding);

// Adopts a position the kernel already holds: base = KERNEL_LOTS, quote =
// FUNDING - COST, recorded as an adoption at the kernel read it came from
// (that read's applied index is the first reconciliation cut).
[[nodiscard]] PaperBootstrap adopted_paper_state(const PaperVenueConfig& config,
                                                 const std::array<std::byte, 16>& cluster_incarnation,
                                                 MoneyMicros funding, QuantityLots kernel_lots, MoneyMicros cost,
                                                 std::int64_t applied_index, std::uint64_t state_version);

// A PaperVenue whose every change is durable before its answer is returned.
// A failed write latches the venue failed: nothing further is sent, released
// or answered, and a send that executed in memory but not on disk reports
// ambiguous (on disk it never happened). The process must then exit.
class DurablePaperVenue final {
public:
    struct Opened {
        PaperStateError error{PaperStateError::none};
        std::unique_ptr<DurablePaperVenue> venue;
    };
    [[nodiscard]] static Opened open(const std::filesystem::path& file, const PaperVenueConfig& config,
                                     const PaperFaultPolicy& faults,
                                     const std::array<std::byte, 16>& cluster_incarnation);

    DurablePaperVenue(const DurablePaperVenue&) = delete;
    DurablePaperVenue& operator=(const DurablePaperVenue&) = delete;

    // Handles whose context is this object; it must outlive every copy.
    [[nodiscard]] sim::VenueTransport transport() noexcept;
    [[nodiscard]] sim::VenueBookSource book() noexcept;

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const PaperVenue& venue() const noexcept { return venue_; }
    [[nodiscard]] const PaperStateOrigin& origin() const noexcept { return origin_; }

private:
    DurablePaperVenue(std::filesystem::path file, const PaperVenueConfig& config, const PaperFaultPolicy& faults,
                      PaperStateOrigin origin);
    // Writes the current state if it differs from the last write.
    [[nodiscard]] bool persist();

    static SendOutcome send(void* self, const OrderEffect& effect, Tick now);
    static std::size_t poll(void* self, std::span<VenueObservation> out, Tick now);
    static void acknowledge(void* self, const VenueObservation& observation);
    static QueryResult query(void* self, ClusterOrderId order, Tick now);
    static VenueSessionId session(void* self);
    static void read_book(void* self, sim::VenueBook& out);
    static void committed(void* self, const sim::CommittedEvidence& evidence);

    std::filesystem::path file_;
    PaperStateOrigin origin_;
    PaperVenue venue_;
    std::unique_ptr<PaperState> scratch_;
    std::vector<std::byte> stored_;
    bool failed_{};
};

}  // namespace velocita::v2::paper
