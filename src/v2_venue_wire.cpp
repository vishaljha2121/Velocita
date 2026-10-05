#include "velocita/v2/venue_wire.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace velocita::v2 {
namespace {

[[nodiscard]] std::int64_t power_of_ten(const std::uint8_t decimals) noexcept {
    if (decimals > 18) return 0;
    std::int64_t value = 1;
    for (std::uint8_t i = 0; i < decimals; ++i) value *= 10;
    return value;
}

[[nodiscard]] bool multiply_overflows(const std::int64_t left,
                                      const std::int64_t right,
                                      std::int64_t& product) noexcept {
    return __builtin_mul_overflow(left, right, &product);
}

}

std::size_t render_decimal(const std::int64_t atoms, const std::uint8_t decimals,
                           const std::span<char> out) noexcept {
    if (decimals > 18) return 0;

    const bool negative = atoms < 0;

    std::array<char, 20> digits{};
    std::size_t digit_count = 0;
    std::int64_t remaining = atoms;
    if (remaining == 0) {
        digits[digit_count++] = '0';
    }
    while (remaining != 0) {
        const auto digit = remaining % 10;
        digits[digit_count++] = static_cast<char>('0' + (negative ? -digit : digit));
        remaining /= 10;
    }

    while (digit_count <= decimals) digits[digit_count++] = '0';

    std::size_t fraction_digits = decimals;
    while (fraction_digits > 0 && digits[decimals - fraction_digits] == '0') {
        --fraction_digits;
    }
    const std::size_t integer_digits = digit_count - decimals;
    const std::size_t needed = (negative ? 1u : 0u) + integer_digits +
                               (fraction_digits > 0 ? 1u + fraction_digits : 0u);
    if (out.size() < needed) return 0;

    std::size_t written = 0;
    if (negative) out[written++] = '-';
    for (std::size_t i = 0; i < integer_digits; ++i) {
        out[written++] = digits[digit_count - 1 - i];
    }
    if (fraction_digits > 0) {
        out[written++] = '.';
        for (std::size_t i = 0; i < fraction_digits; ++i) {
            out[written++] = digits[decimals - 1 - i];
        }
    }
    return written;
}

bool spec_agrees_with_profile(const VenueInstrumentSpec& spec,
                              const InstrumentExecutionProfile& profile) noexcept {
    if (spec.venue_id != profile.venue_id) return false;
    if (spec.instrument_id != profile.instrument_id) return false;
    if (spec.tick_quote_micros <= 0 || spec.lot_base_atoms <= 0) return false;

    const auto base_unit = power_of_ten(spec.base_decimals);
    if (base_unit == 0) return false;

    std::int64_t product = 0;
    if (multiply_overflows(spec.tick_quote_micros, spec.lot_base_atoms, product)) {
        return false;
    }
    if (product % base_unit != 0) return false;

    return product / base_unit == profile.quote_micros_per_tick_lot.atoms();
}

std::size_t render_price(const PriceTicks price, const VenueInstrumentSpec& spec,
                         const std::span<char> out) noexcept {
    if (spec.tick_quote_micros <= 0) return 0;
    std::int64_t quote_micros = 0;
    if (multiply_overflows(price.atoms(), spec.tick_quote_micros, quote_micros)) {
        return 0;
    }
    return render_decimal(quote_micros, 6, out);
}

std::size_t render_quantity(const QuantityLots quantity,
                            const VenueInstrumentSpec& spec,
                            const std::span<char> out) noexcept {
    if (spec.lot_base_atoms <= 0) return 0;
    std::int64_t base_atoms = 0;
    if (multiply_overflows(quantity.atoms(), spec.lot_base_atoms, base_atoms)) {
        return 0;
    }
    return render_decimal(base_atoms, spec.base_decimals, out);
}

namespace {

[[nodiscard]] OrderBodyResult body_failure(const OrderBodyError error) noexcept {
    return {error, 0};
}

}

AssetSymbol asset_symbol(const std::string_view text) noexcept {
    AssetSymbol symbol;
    if (text.size() > kMaxAssetSymbolBytes) return symbol;
    for (std::size_t i = 0; i < text.size(); ++i) symbol.text[i] = text[i];
    symbol.length = static_cast<std::uint8_t>(text.size());
    return symbol;
}

OrderBodyResult encode_market_order_body(const OrderEffect&,
                                         const VenueInstrumentSpec&,
                                         const AssetSymbol,
                                         const AssetSymbol,
                                         const std::span<char>) noexcept {
    return body_failure(OrderBodyError::unsupported_operation);
}

OrderBodyResult encode_limit_order_body(const OrderEffect& effect,
                                        const VenueInstrumentSpec&,
                                        const AssetSymbol,
                                        const AssetSymbol,
                                        const VenueOrderSchema&,
                                        const std::span<char>) noexcept {
    if (effect.operation != EffectOperation::submit) {
        return body_failure(OrderBodyError::unsupported_operation);
    }
    return body_failure(OrderBodyError::schema_unverified);
}

}
