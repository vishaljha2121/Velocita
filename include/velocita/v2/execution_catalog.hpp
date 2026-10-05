// The immutable catalog of tradable account, venue and instrument rows.
#pragma once

#include "velocita/v2/action_template.hpp"
#include "velocita/v2/contracts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace velocita::v2 {

inline constexpr std::uint16_t kExecutionCatalogSchemaVersion = 1;
inline constexpr std::size_t kMaxExecutionCatalogRows = 16;
inline constexpr std::size_t kMaxCatalogTextBytes = 96;
inline constexpr std::size_t kMaxExecutionCatalogBytes = 8192;

struct CatalogText {
    std::array<char, kMaxCatalogTextBytes> bytes{};
    std::uint8_t length{};

    constexpr auto operator<=>(const CatalogText &) const noexcept = default;
};

struct ExecutionCatalogRow {
    AccountId account_id{};
    InstrumentExecutionProfile instrument{};
    VenueCapabilities capabilities{};
    std::int64_t tick_quote_micros{};
    std::int64_t lot_base_atoms{};
    std::uint8_t base_decimals{};
    std::uint16_t encoder_family_id{};
    std::uint16_t encoder_schema_version{};
    CatalogText network_identity{};
    CatalogText base_asset_identity{};
    CatalogText quote_asset_identity{};
    CatalogText routing_key{};

    constexpr auto
    operator<=>(const ExecutionCatalogRow &) const noexcept = default;
};

enum class ExecutionCatalogError : std::uint8_t {
    none = 0,
    output_too_small,
    truncated,
    invalid_magic,
    unsupported_schema,
    row_count_out_of_range,
    length_mismatch,
    invalid_value,
    non_canonical_order,
    duplicate_key,
    cross_row_conflict,
    account_denomination_conflict,
    digest_mismatch,
};

struct EncodeExecutionCatalogResult {
    ExecutionCatalogError error{ExecutionCatalogError::none};
    std::size_t bytes_written{};
    Digest256 root{};
};

[[nodiscard]] EncodeExecutionCatalogResult
encode_execution_catalog(std::span<const ExecutionCatalogRow> rows,
                         std::span<std::byte> output) noexcept;

class VerifiedExecutionCatalog final {
public:
    constexpr VerifiedExecutionCatalog() noexcept = default;

    [[nodiscard]] static ExecutionCatalogError
    decode(std::span<const std::byte> bytes, const Digest256 &expected_root,
           VerifiedExecutionCatalog &destination) noexcept;

    [[nodiscard]] constexpr std::size_t row_count() const noexcept {
        return row_count_;
    }
    [[nodiscard]] constexpr const Digest256 &root() const noexcept {
        return root_;
    }
    [[nodiscard]] EncodeExecutionCatalogResult encode(std::span<std::byte> output) const noexcept {
        return encode_execution_catalog(std::span(rows_).first(row_count_), output);
    }
    [[nodiscard]] const ExecutionCatalogRow *
    lookup(AccountId account_id, VenueId venue_id, InstrumentId instrument_id,
           InstrumentSpecVersion instrument_spec_version,
           VenueCapabilitiesVersion capabilities_version) const noexcept;

private:
    std::array<ExecutionCatalogRow, kMaxExecutionCatalogRows> rows_{};
    std::size_t row_count_{};
    Digest256 root_{};
};

static_assert(sizeof(VerifiedExecutionCatalog) <= 16 * 1024,
              "verified catalog exceeded its reviewed resident-memory ceiling");

}
