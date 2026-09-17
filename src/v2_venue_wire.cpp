#include "velocita/v2/venue_wire.hpp"

#include <algorithm>
#include <array>

namespace velocita::v2 {
namespace {

// Ten to the power of `decimals`, or zero if that would not fit. Used as a
// DIVISOR, so a zero return must be treated as "cannot represent" by every
// caller rather than divided by.
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

}  // namespace

std::size_t render_decimal(const std::int64_t atoms, const std::uint8_t decimals,
                           const std::span<char> out) noexcept {
    if (decimals > 18) return 0;

    // THE SIGN IS HANDLED WITHOUT NEGATING. `-(int64 min)` is undefined
    // behaviour, and that value is reachable - it is what an unchecked
    // subtraction produces. Digits are therefore extracted from the NEGATIVE
    // side, where every int64 is representable, and the sign is written
    // separately.
    //
    // WHAT CATCHES A REGRESSION HERE IS THE SANITIZED BUILD, NOT AN ASSERTION,
    // and that is worth knowing rather than assuming. Rewriting this as
    // `negative ? -atoms : atoms` still renders int64 min correctly on this
    // compiler - the wrap happens to land on the right digits - so every value
    // assertion in the suite passes. UBSan reports it immediately: "negation of
    // -9223372036854775808 cannot be represented in type 'int64_t'". A mutation
    // that reintroduces the UB therefore dies in the Debug ASan+UBSan run that
    // gates every phase, and nowhere else.
    const bool negative = atoms < 0;

    std::array<char, 20> digits{};
    std::size_t digit_count = 0;
    std::int64_t remaining = atoms;
    if (remaining == 0) {
        digits[digit_count++] = '0';
    }
    while (remaining != 0) {
        const auto digit = remaining % 10;
        // `digit` is <= 0 when `remaining` is negative, so negate the DIGIT,
        // which is always in [-9, 0] and never overflows.
        digits[digit_count++] = static_cast<char>('0' + (negative ? -digit : digit));
        remaining /= 10;
    }

    // Left-pad with zeros so there is something in front of the decimal point
    // and enough after it: 1 atom at 6dp is "0.000001", which needs seven digits
    // from a one-digit number.
    while (digit_count <= decimals) digits[digit_count++] = '0';

    // Trim trailing zeros in the FRACTION only, so the text is canonical. The
    // fraction sits at the low end of `digits`, so scanning up from index zero
    // and stopping at the first non-zero drops only TRAILING zeros - which is
    // why an internal run like 1.000001 survives.
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
    // Agreeing about a number is not the same as being about the same thing.
    if (spec.venue_id != profile.venue_id) return false;
    if (spec.instrument_id != profile.instrument_id) return false;
    if (spec.tick_quote_micros <= 0 || spec.lot_base_atoms <= 0) return false;

    const auto base_unit = power_of_ten(spec.base_decimals);
    if (base_unit == 0) return false;

    std::int64_t product = 0;
    if (multiply_overflows(spec.tick_quote_micros, spec.lot_base_atoms, product)) {
        return false;
    }
    // EXACT, not close. A product that does not land on a whole number of quote
    // micros cannot be represented by the replicated contract at all, so
    // rounding it would encode an error as a price.
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
    // Quote currency is carried in micros throughout V2, so six decimals is the
    // scale by construction rather than by choice.
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

}  // namespace velocita::v2
