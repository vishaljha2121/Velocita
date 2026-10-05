// Renders an order effect in a venue's order format.
#pragma once

#include "velocita/v2/action_template.hpp"
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/domain.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace velocita::v2 {

inline constexpr std::size_t kMaxDecimalTextBytes = 24;

[[nodiscard]] std::size_t render_decimal(std::int64_t atoms, std::uint8_t decimals,
                                         std::span<char> out) noexcept;

struct VenueInstrumentSpec {
    VenueId venue_id{};
    InstrumentId instrument_id{};
    std::int64_t tick_quote_micros{};
    std::int64_t lot_base_atoms{};
    std::uint8_t base_decimals{};

    constexpr auto operator<=>(const VenueInstrumentSpec&) const noexcept = default;
};

[[nodiscard]] bool spec_agrees_with_profile(
    const VenueInstrumentSpec& spec,
    const InstrumentExecutionProfile& profile) noexcept;

[[nodiscard]] std::size_t render_price(PriceTicks price,
                                       const VenueInstrumentSpec& spec,
                                       std::span<char> out) noexcept;

[[nodiscard]] std::size_t render_quantity(QuantityLots quantity,
                                          const VenueInstrumentSpec& spec,
                                          std::span<char> out) noexcept;

inline constexpr std::size_t kMaxAssetSymbolBytes = 16;
inline constexpr std::size_t kMaxVenueFieldBytes = 24;
inline constexpr std::size_t kMaxOrderRequestBytes = 512;

struct AssetSymbol {
    std::array<char, kMaxAssetSymbolBytes> text{};
    std::uint8_t length{};

    constexpr auto operator<=>(const AssetSymbol&) const noexcept = default;
};

[[nodiscard]] AssetSymbol asset_symbol(std::string_view text) noexcept;

struct VenueOrderSchema {
    bool verified{};
    std::array<char, kMaxVenueFieldBytes> price_field{};
    std::uint8_t price_field_length{};
    std::array<char, kMaxVenueFieldBytes> base_qty_unit{};
    std::uint8_t base_qty_unit_length{};
};

enum class OrderBodyError : std::uint8_t {
    none = 0,
    output_too_small,
    unsafe_text,
    unrepresentable_value,
    schema_unverified,
    unsupported_operation,
};

struct OrderBodyResult {
    OrderBodyError error{OrderBodyError::none};
    std::size_t bytes_written{};
};

[[nodiscard]] OrderBodyResult encode_market_order_body(
    const OrderEffect& effect, const VenueInstrumentSpec& spec,
    AssetSymbol base, AssetSymbol quote, std::span<char> out) noexcept;

[[nodiscard]] OrderBodyResult encode_limit_order_body(
    const OrderEffect& effect, const VenueInstrumentSpec& spec,
    AssetSymbol base, AssetSymbol quote, const VenueOrderSchema& schema,
    std::span<char> out) noexcept;

}
