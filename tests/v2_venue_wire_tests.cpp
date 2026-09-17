#include "velocita/v2/venue_wire.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace velocita::v2;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string rendered(const std::int64_t atoms, const std::uint8_t decimals) {
    std::array<char, kMaxDecimalTextBytes> buffer{};
    const auto written = render_decimal(atoms, decimals, buffer);
    require(written > 0, "render_decimal refused a value it should accept");
    return std::string(buffer.data(), written);
}

void test_decimal_rendering_is_exact_and_never_floating_point() {
    // THE MOST DANGEROUS ARITHMETIC IN THE ADAPTER. A price that round-trips
    // through a double is a price the venue may reject, or worse, accept at a
    // value nobody chose. Every conversion here is integer.
    require(rendered(5, 0) == "5", "an integral value did not render as itself");
    require(rendered(0, 0) == "0", "zero did not render as zero");
    require(rendered(0, 8) == "0", "scaled zero did not render as zero");

    // Trailing zeros are trimmed so the text is canonical - two encodings of the
    // same number would otherwise hash differently in any evidence that records
    // what was sent.
    require(rendered(5'000'000, 6) == "5", "5.000000 did not trim to 5");
    require(rendered(5'100'000, 6) == "5.1", "5.100000 did not trim to 5.1");
    require(rendered(1, 6) == "0.000001", "the smallest positive unit lost precision");
    require(rendered(123'456, 6) == "0.123456", "a sub-unit value lost precision");
    require(rendered(1'000'001, 6) == "1.000001",
            "an internal zero run was trimmed, which changes the value");

    // Negative values exist in this codebase (position, PnL) even if an order
    // quantity never is, and a sign dropped silently is worse than a refusal.
    require(rendered(-1, 6) == "-0.000001", "a negative sub-unit lost its sign");
    require(rendered(-5'000'000, 6) == "-5", "a negative integral value mis-rendered");

    // The extremes, because a buffer that fits the typical case and not the
    // worst one fails exactly when the number is unusual.
    require(rendered(std::numeric_limits<std::int64_t>::max(), 0) ==
                "9223372036854775807",
            "int64 max did not render exactly");
    require(rendered(std::numeric_limits<std::int64_t>::min(), 0) ==
                "-9223372036854775808",
            "int64 min did not render exactly, which is the value that overflows "
            "when a sign is handled by negation");
}

void test_rendering_refuses_rather_than_truncating() {
    // A short buffer must REFUSE. Producing a prefix of a number is the single
    // worst available failure: "1234" from "12345" is a valid-looking order at
    // a tenth of the price.
    std::array<char, 3> tiny{};
    require(render_decimal(123'456, 0, tiny) == 0,
            "a value too long for the buffer was truncated rather than refused");

    std::array<char, kMaxDecimalTextBytes> ample{};
    require(render_decimal(123'456, 0, ample) == 6,
            "a value that fits was refused");
}

VenueInstrumentSpec spec_for(const std::int64_t tick_quote_micros,
                             const std::int64_t lot_base_atoms,
                             const std::uint8_t base_decimals) {
    VenueInstrumentSpec spec;
    spec.instrument_id = InstrumentId{4};
    spec.venue_id = VenueId{9};
    spec.tick_quote_micros = tick_quote_micros;
    spec.lot_base_atoms = lot_base_atoms;
    spec.base_decimals = base_decimals;
    return spec;
}

InstrumentExecutionProfile profile_with(const std::int64_t quote_micros_per_tick_lot) {
    InstrumentExecutionProfile profile;
    profile.schema_version = kInstrumentExecutionProfileSchemaVersion;
    profile.venue_id = VenueId{9};
    profile.instrument_id = InstrumentId{4};
    profile.quote_micros_per_tick_lot =
        MoneyMicros::from_atoms(quote_micros_per_tick_lot);
    profile.minimum_price = PriceTicks::from_atoms(1);
    profile.maximum_price = PriceTicks::from_atoms(1'000'000);
    profile.minimum_quantity = QuantityLots::from_atoms(1);
    profile.maximum_quantity = QuantityLots::from_atoms(1'000'000);
    return profile;
}

void test_the_spec_must_agree_with_the_replicated_contract() {
    // G-062, and the reason this check exists at all.
    //
    // The cluster's `InstrumentExecutionProfile` carries
    // `quote_micros_per_tick_lot`, which is the PRODUCT of tick size and lot
    // size - enough to compute notional, which is what risk needs. It is NOT
    // enough to render an order, which needs price and quantity separately. So
    // the adapter must carry tick and lot itself, from venue metadata that is
    // not replicated.
    //
    // That creates two sources for one economic fact, and the only safe thing to
    // do with two sources is make them prove each other. tick x lot must
    // reproduce the replicated product exactly, or one of them is wrong and
    // sending either would be a guess with money attached.

    // 1 tick = 0.01 quote (10,000 micros); 1 lot = 0.001 base (1e5 atoms at 8dp).
    // product = 10,000 * 1e5 / 1e8 = 10 quote micros per tick-lot.
    const auto good = spec_for(10'000, 100'000, 8);
    require(spec_agrees_with_profile(good, profile_with(10)),
            "a spec that reproduces the replicated product was rejected");

    // A tick size off by a factor of ten is the classic venue-metadata error,
    // and it is exactly the one that makes every quote invalid.
    require(!spec_agrees_with_profile(spec_for(1'000, 100'000, 8), profile_with(10)),
            "a tick size wrong by 10x was accepted");
    require(!spec_agrees_with_profile(spec_for(10'000, 10'000, 8), profile_with(10)),
            "a lot size wrong by 10x was accepted");
    require(!spec_agrees_with_profile(good, profile_with(11)),
            "a spec disagreeing with the replicated product was accepted");

    // Wrong instrument or venue is a mismatch even if the arithmetic happens to
    // work out - agreeing about a number is not the same as being about the same
    // thing.
    auto other_instrument = good;
    other_instrument.instrument_id = InstrumentId{5};
    require(!spec_agrees_with_profile(other_instrument, profile_with(10)),
            "a spec for a different instrument was accepted");
    auto other_venue = good;
    other_venue.venue_id = VenueId{11};
    require(!spec_agrees_with_profile(other_venue, profile_with(10)),
            "a spec for a different venue was accepted");

    // A product that does not divide exactly cannot be represented, and the
    // honest answer is refusal rather than a rounded one.
    require(!spec_agrees_with_profile(spec_for(3, 1, 8), profile_with(0)),
            "a spec whose product is not an exact number of quote micros was "
            "accepted, so a rounding error would have been encoded as a price");

    // Degenerate specs are refused rather than divided by.
    require(!spec_agrees_with_profile(spec_for(0, 100'000, 8), profile_with(10)),
            "a zero tick size was accepted");
    require(!spec_agrees_with_profile(spec_for(10'000, 0, 8), profile_with(10)),
            "a zero lot size was accepted");
}

void test_price_and_quantity_convert_through_the_spec_exactly() {
    const auto spec = spec_for(10'000, 100'000, 8);

    // 250 ticks x 0.01 = 2.50 quote.
    std::array<char, kMaxDecimalTextBytes> price{};
    const auto price_len =
        render_price(PriceTicks::from_atoms(250), spec, price);
    require(price_len > 0 && std::string(price.data(), price_len) == "2.5",
            "250 ticks at 0.01 did not render as 2.5");

    // 3 lots x 0.001 base = 0.003 base.
    std::array<char, kMaxDecimalTextBytes> quantity{};
    const auto quantity_len =
        render_quantity(QuantityLots::from_atoms(3), spec, quantity);
    require(quantity_len > 0 && std::string(quantity.data(), quantity_len) == "0.003",
            "3 lots at 0.001 did not render as 0.003");

    // Overflow must refuse, not wrap. A wrapped price is a real order at an
    // absurd number.
    std::array<char, kMaxDecimalTextBytes> overflow{};
    require(render_price(PriceTicks::from_atoms(
                             std::numeric_limits<std::int64_t>::max()),
                         spec, overflow) == 0,
            "a price whose tick conversion overflows was rendered rather than "
            "refused");
}

}  // namespace

int main() {
    try {
        test_decimal_rendering_is_exact_and_never_floating_point();
        test_rendering_refuses_rather_than_truncating();
        test_the_spec_must_agree_with_the_replicated_contract();
        test_price_and_quantity_convert_through_the_spec_exactly();
        std::cout << "all V2 venue wire tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 venue wire test failed: " << error.what() << "\n";
        return 1;
    }
}
