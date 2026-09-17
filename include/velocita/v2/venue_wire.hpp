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

// Phase 9. RENDERING AN EFFECT AS SOMETHING A VENUE WILL ACCEPT.
//
// The cluster reasons in TICKS and LOTS, which are venue-relative integers. A
// venue order is a decimal price and a decimal quantity. Converting between them
// is the most dangerous arithmetic in the adapter, for two reasons that have
// nothing to do with difficulty:
//
//   * a value that round-trips through a double is a value nobody chose, and it
//     is accepted by the venue just as readily as a correct one;
//   * a wrong tick size makes every quote invalid, which the must-confirm
//     document names as one of the facts that changes a conclusion.
//
// Every conversion below is integer. There is no floating point in this header
// or its implementation, and that is a property to preserve rather than an
// incidental fact.

// Longest text `render_decimal` can produce: sign, nineteen digits, a point.
inline constexpr std::size_t kMaxDecimalTextBytes = 24;

// Render `atoms / 10^decimals` exactly, trimming trailing zeros so the text is
// canonical. Returns the number of bytes written, or ZERO if the buffer is too
// small.
//
// REFUSAL RATHER THAN TRUNCATION IS THE WHOLE CONTRACT OF THE RETURN VALUE. A
// prefix of a number is a valid-looking number: "1234" cut from "12345" is an
// order at a tenth of the price, and it would be accepted.
[[nodiscard]] std::size_t render_decimal(std::int64_t atoms, std::uint8_t decimals,
                                         std::span<char> out) noexcept;

// Venue-side instrument metadata, which is NOT replicated and must not be.
//
// The replicated `InstrumentExecutionProfile` carries
// `quote_micros_per_tick_lot` - the PRODUCT of tick size and lot size. That is
// what risk needs, because it yields notional. It is not what an order needs,
// which is price and quantity SEPARATELY, so the factors have to come from
// somewhere and the only place they exist is the venue's own catalog.
//
// That leaves two sources for one economic fact. See `spec_agrees_with_profile`
// for what is done about it.
struct VenueInstrumentSpec {
    VenueId venue_id{};
    InstrumentId instrument_id{};
    // Quote currency micros per one tick of price, for one whole base unit.
    std::int64_t tick_quote_micros{};
    // Base-currency atoms per one lot of quantity.
    std::int64_t lot_base_atoms{};
    // Decimal places in one whole base unit, so atoms render as a decimal.
    std::uint8_t base_decimals{};

    constexpr auto operator<=>(const VenueInstrumentSpec&) const noexcept = default;
};

// DO THE TWO SOURCES PROVE EACH OTHER?
//
// tick x lot must reproduce the replicated `quote_micros_per_tick_lot` exactly.
// If they disagree, one of them is wrong - and there is no way to tell which
// from here, so sending either would be a guess with money attached. The
// adapter refuses instead.
//
// Exactness is required, not closeness. A product that does not land on a whole
// number of quote micros cannot be represented by the replicated contract at
// all, and rounding it would encode an error as a price.
[[nodiscard]] bool spec_agrees_with_profile(
    const VenueInstrumentSpec& spec,
    const InstrumentExecutionProfile& profile) noexcept;

// Render a price in ticks as a decimal in quote currency. Zero on overflow or a
// buffer too small - a wrapped price is a real order at an absurd number.
[[nodiscard]] std::size_t render_price(PriceTicks price,
                                       const VenueInstrumentSpec& spec,
                                       std::span<char> out) noexcept;

// Render a quantity in lots as a decimal in base currency. Zero on overflow or a
// buffer too small.
[[nodiscard]] std::size_t render_quantity(QuantityLots quantity,
                                          const VenueInstrumentSpec& spec,
                                          std::span<char> out) noexcept;

// ---------------------------------------------------------------------------
// The create-order request body.

// Longest asset symbol the adapter will carry, plus room for a terminator.
inline constexpr std::size_t kMaxAssetSymbolBytes = 16;
// Longest venue field name the adapter will carry.
inline constexpr std::size_t kMaxVenueFieldBytes = 24;
// Enough for the documented body plus a limit order's extra fields.
inline constexpr std::size_t kMaxOrderRequestBytes = 512;

struct AssetSymbol {
    std::array<char, kMaxAssetSymbolBytes> text{};
    std::uint8_t length{};

    constexpr auto operator<=>(const AssetSymbol&) const noexcept = default;
};

[[nodiscard]] AssetSymbol asset_symbol(std::string_view text) noexcept;

// FIELD NAMES THE VENUE HAS NOT DOCUMENTED, carried explicitly so that not
// knowing them is a state the code can be in rather than a gap somebody fills
// with a plausible guess.
//
// As of 2026-09-17 the create-order request schema does not render on
// `docs.truemarkets.co`; only the market-order example does. So the price field
// name, and whether `qty_unit` accepts a base-denominated quantity, are
// UNKNOWN.
//
// WHY A GUESS HERE IS WORSE THAN A REFUSAL. Many APIs ignore fields they do not
// recognise. If the price field is guessed wrong and ignored, a LIMIT order
// silently becomes a MARKET order - it crosses the spread and pays taker fees on
// every quote, which inverts the maker economics this project exists to capture,
// and it does so while appearing to work. That is the single most expensive
// failure available at this boundary and it is invisible from our side.
struct VenueOrderSchema {
    // Set only when these names came from the venue, never from inference.
    bool verified{};
    std::array<char, kMaxVenueFieldBytes> price_field{};
    std::uint8_t price_field_length{};
    std::array<char, kMaxVenueFieldBytes> base_qty_unit{};
    std::uint8_t base_qty_unit_length{};
};

enum class OrderBodyError : std::uint8_t {
    none = 0,
    // The buffer could not hold the body. Never a partial write.
    output_too_small,
    // A symbol or field name held a character that cannot be placed in JSON
    // without escaping. Refused rather than escaped: an asset symbol is
    // alphanumeric everywhere, so anything else means the metadata is wrong.
    unsafe_text,
    // Empty symbol, empty field name, or a quantity or price that would not
    // convert.
    unrepresentable_value,
    // A limit order was requested against a schema nobody verified. See
    // `VenueOrderSchema`.
    schema_unverified,
    // The effect asks for something the documented surface cannot express.
    unsupported_operation,
};

struct OrderBodyResult {
    OrderBodyError error{OrderBodyError::none};
    std::size_t bytes_written{};
};

// Encode the documented market-order body:
//   {"base_asset":..,"quote_asset":..,"qty":..,"qty_unit":"base","side":..,
//    "type":"market"}
[[nodiscard]] OrderBodyResult encode_market_order_body(
    const OrderEffect& effect, const VenueInstrumentSpec& spec,
    AssetSymbol base, AssetSymbol quote, std::span<char> out) noexcept;

// Encode a limit-order body. REFUSES unless the schema is verified, for the
// reason given on `VenueOrderSchema`.
[[nodiscard]] OrderBodyResult encode_limit_order_body(
    const OrderEffect& effect, const VenueInstrumentSpec& spec,
    AssetSymbol base, AssetSymbol quote, const VenueOrderSchema& schema,
    std::span<char> out) noexcept;

}  // namespace velocita::v2
