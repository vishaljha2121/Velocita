// The paper venue's durable state file.
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

inline constexpr std::uint32_t kPaperStateVersion = 1;
inline constexpr std::array<std::byte, 8> kPaperStateMagic{
    std::byte{'V'}, std::byte{'P'}, std::byte{'A'}, std::byte{'P'},
    std::byte{'E'}, std::byte{'R'}, std::byte{'S'}, std::byte{'T'}};

struct PaperStateOrigin {
    std::array<std::byte, 16> cluster_incarnation{};
    MoneyMicros funding{};
    bool adopted{};
    QuantityLots adopted_lots{};
    MoneyMicros adopted_cost{};
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
    missing,
    exists,
    unreadable,
    torn,
    bad_magic,
    bad_version,
    digest_mismatch,
    malformed,
    other_cluster,
    other_venue,
    write_failed,
};

[[nodiscard]] const char* describe(PaperStateError error) noexcept;

[[nodiscard]] std::vector<std::byte> encode_paper_state(const PaperState& state);
[[nodiscard]] PaperStateError decode_paper_state(std::span<const std::byte> bytes, PaperState& out);

struct PaperStateLoad {
    PaperStateError error{PaperStateError::none};
    std::unique_ptr<PaperState> state;
};

[[nodiscard]] PaperStateLoad load_paper_state(const std::filesystem::path& file);
[[nodiscard]] PaperStateError store_paper_state(const std::filesystem::path& file,
                                                std::span<const std::byte> bytes);
[[nodiscard]] PaperStateError create_paper_state(const std::filesystem::path& file, const PaperState& state);

struct PaperBootstrap {
    std::string error;
    std::unique_ptr<PaperState> state;
};

[[nodiscard]] PaperBootstrap fresh_paper_state(const PaperVenueConfig& config,
                                               const std::array<std::byte, 16>& cluster_incarnation,
                                               MoneyMicros funding);

[[nodiscard]] PaperBootstrap adopted_paper_state(const PaperVenueConfig& config,
                                                 const std::array<std::byte, 16>& cluster_incarnation,
                                                 MoneyMicros funding, QuantityLots kernel_lots, MoneyMicros cost,
                                                 std::int64_t applied_index, std::uint64_t state_version);

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

    [[nodiscard]] sim::VenueTransport transport() noexcept;
    [[nodiscard]] sim::VenueBookSource book() noexcept;

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const PaperVenue& venue() const noexcept { return venue_; }
    [[nodiscard]] const PaperStateOrigin& origin() const noexcept { return origin_; }

private:
    DurablePaperVenue(std::filesystem::path file, const PaperVenueConfig& config, const PaperFaultPolicy& faults,
                      PaperStateOrigin origin);
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

}
