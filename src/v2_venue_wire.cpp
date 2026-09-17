#include "velocita/v2/venue_wire.hpp"

#include <algorithm>
#include <array>
#include <string_view>

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

namespace {

// SAFE MEANS ALPHANUMERIC, and the restriction is deliberate rather than lazy.
// Asset symbols are alphanumeric on every venue; a field name likewise. Anything
// else means the metadata is wrong, and refusing says so - whereas escaping
// would carry a corrupt symbol into a live order and let the venue decide what
// it meant.
[[nodiscard]] bool json_safe(const char* text, const std::size_t length) noexcept {
    if (length == 0) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

// Append, refusing rather than truncating. Every caller checks, and a single
// failed append poisons the whole body - which is what keeps a partial write
// from ever reaching a venue.
struct Appender {
    std::span<char> out;
    std::size_t written{};
    bool overflowed{};

    void put(const char* text, const std::size_t length) noexcept {
        if (overflowed) return;
        if (written + length > out.size()) {
            overflowed = true;
            return;
        }
        for (std::size_t i = 0; i < length; ++i) out[written++] = text[i];
    }
    void put(const std::string_view text) noexcept { put(text.data(), text.size()); }
};

[[nodiscard]] bool append_decimal_field(Appender& appender, const std::string_view name,
                                        const std::int64_t atoms,
                                        const std::uint8_t decimals) noexcept {
    std::array<char, kMaxDecimalTextBytes> text{};
    const auto length = render_decimal(atoms, decimals, text);
    if (length == 0) return false;
    appender.put("\"");
    appender.put(name);
    appender.put("\":\"");
    appender.put(text.data(), length);
    appender.put("\"");
    return true;
}

[[nodiscard]] OrderBodyResult body_failure(const OrderBodyError error) noexcept {
    return {error, 0};
}

// The documented fields, shared by both order types.
[[nodiscard]] OrderBodyResult begin_body(Appender& appender, const OrderEffect& effect,
                                         const VenueInstrumentSpec& spec,
                                         const AssetSymbol& base,
                                         const AssetSymbol& quote) noexcept {
    if (!json_safe(base.text.data(), base.length) ||
        !json_safe(quote.text.data(), quote.length)) {
        return body_failure(OrderBodyError::unsafe_text);
    }
    if (effect.quantity.atoms() <= 0) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }
    std::int64_t base_atoms = 0;
    if (spec.lot_base_atoms <= 0 ||
        __builtin_mul_overflow(effect.quantity.atoms(), spec.lot_base_atoms,
                               &base_atoms)) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }

    appender.put("{\"base_asset\":\"");
    appender.put(base.text.data(), base.length);
    appender.put("\",\"quote_asset\":\"");
    appender.put(quote.text.data(), quote.length);
    appender.put("\",");
    if (!append_decimal_field(appender, "qty", base_atoms, spec.base_decimals)) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }
    appender.put(",\"side\":\"");
    appender.put(effect.side == Side::buy ? "buy" : "sell");
    appender.put("\"");
    return {OrderBodyError::none, 0};
}

[[nodiscard]] OrderBodyResult finish_body(Appender& appender) noexcept {
    appender.put("}");
    if (appender.overflowed) return body_failure(OrderBodyError::output_too_small);
    return {OrderBodyError::none, appender.written};
}

}  // namespace

AssetSymbol asset_symbol(const std::string_view text) noexcept {
    AssetSymbol symbol;
    if (text.size() > kMaxAssetSymbolBytes) return symbol;
    for (std::size_t i = 0; i < text.size(); ++i) symbol.text[i] = text[i];
    symbol.length = static_cast<std::uint8_t>(text.size());
    return symbol;
}

OrderBodyResult encode_market_order_body(const OrderEffect& effect,
                                         const VenueInstrumentSpec& spec,
                                         const AssetSymbol base,
                                         const AssetSymbol quote,
                                         const std::span<char> out) noexcept {
    if (effect.operation != EffectOperation::submit) {
        return body_failure(OrderBodyError::unsupported_operation);
    }
    Appender appender{out};
    if (const auto started = begin_body(appender, effect, spec, base, quote);
        started.error != OrderBodyError::none) {
        return started;
    }
    // `qty_unit` is BASE here rather than the quickstart's "quote", because the
    // cluster authorizes a quantity of the instrument and not a spend. The
    // quickstart's example buys five dollars of BTC; nothing in V2 can express
    // that, and pretending otherwise would let a price move change the size of
    // an order the reducer already sized.
    appender.put(",\"qty_unit\":\"base\",\"type\":\"market\"");
    return finish_body(appender);
}

OrderBodyResult encode_limit_order_body(const OrderEffect& effect,
                                        const VenueInstrumentSpec& spec,
                                        const AssetSymbol base,
                                        const AssetSymbol quote,
                                        const VenueOrderSchema& schema,
                                        const std::span<char> out) noexcept {
    if (effect.operation != EffectOperation::submit) {
        return body_failure(OrderBodyError::unsupported_operation);
    }
    // THE REFUSAL THAT MATTERS. See `VenueOrderSchema`: a guessed price field
    // that the venue ignores turns this limit order into a market order, which
    // pays taker fees on every quote and looks like it worked.
    if (!schema.verified) return body_failure(OrderBodyError::schema_unverified);
    if (!json_safe(schema.price_field.data(), schema.price_field_length) ||
        !json_safe(schema.base_qty_unit.data(), schema.base_qty_unit_length)) {
        return body_failure(OrderBodyError::unsafe_text);
    }
    if (effect.price.atoms() <= 0) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }
    std::int64_t quote_micros = 0;
    if (spec.tick_quote_micros <= 0 ||
        __builtin_mul_overflow(effect.price.atoms(), spec.tick_quote_micros,
                               &quote_micros)) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }

    Appender appender{out};
    if (const auto started = begin_body(appender, effect, spec, base, quote);
        started.error != OrderBodyError::none) {
        return started;
    }
    appender.put(",\"qty_unit\":\"");
    appender.put(schema.base_qty_unit.data(), schema.base_qty_unit_length);
    appender.put("\",\"type\":\"limit\",");
    if (!append_decimal_field(
            appender,
            std::string_view(schema.price_field.data(), schema.price_field_length),
            quote_micros, 6)) {
        return body_failure(OrderBodyError::unrepresentable_value);
    }
    return finish_body(appender);
}

}  // namespace velocita::v2
