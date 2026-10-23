#define main original_venue_suite_main
#include "../../../tests/v2_venue_wire_tests.cpp"
#undef main

int main() {
    std::array<char, kMaxOrderRequestBytes> out{};
    auto effect = limit_submit();
    effect.post_only = PostOnlyMode::required;
    const auto good = spec_for(10'000, 100'000, 8);
    const auto aliased = spec_for(100'000, 10'000, 8);
    const auto market = encode_market_order_body(effect, good,
        asset_symbol("BTC"), asset_symbol("USDC"), out);
    std::cout << "post_only_limit_to_market=" << (market.error == OrderBodyError::none)
              << " body=" << std::string(out.data(), market.bytes_written) << '\n';
    const bool same_product = spec_agrees_with_profile(good, profile_with(10)) &&
                              spec_agrees_with_profile(aliased, profile_with(10));
    std::array<char, 24> a{}, b{};
    const auto na = render_price(effect.price, good, a);
    const auto nb = render_price(effect.price, aliased, b);
    std::cout << "both_specs_agree=" << same_product << " price_a="
              << std::string(a.data(), na) << " price_b=" << std::string(b.data(), nb) << '\n';
    auto wrong = good;
    wrong.instrument_id = InstrumentId{999};
    const auto limit = encode_limit_order_body(effect, wrong, asset_symbol("BTC"),
        asset_symbol("USDC"), verified_schema(), out);
    std::cout << "wrong_instrument_encoded=" << (limit.error == OrderBodyError::none) << '\n';
    if (market.error == OrderBodyError::none || same_product || limit.error == OrderBodyError::none) {
        std::cerr << "RED: wire helpers do not enforce unchanged committed economics\n";
        return 1;
    }
    return 0;
}
