#include "v2_shadow_catalog_rows.hpp"
#include "velocita/v2/shadow_program.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

// The True Markets cbBTC/USDC shadow row, applied through the REAL reducer.
// Expected money is worked out here in dollars from the venue's published
// facts (cbBTC 8 decimals, USDC 6; GET /v1/defi/core/assets, 2026-09-28),
// never read back from the row under test.

namespace {

using namespace velocita::v2;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string text_of(const CatalogText& t) { return std::string(t.bytes.data(), t.length); }

void the_row_carries_the_venue_published_units() {
    const auto row = shadow_catalog::truemarkets_cbbtc_row();
    require(row.base_decimals == 8, "cbBTC has 8 decimals on True Markets");
    // One tick is $1 per whole BTC; one lot is 0.00001 BTC = 1000 atoms of 1e-8.
    require(row.tick_quote_micros == 1'000'000 && row.lot_base_atoms == 1'000, "tick or lot size differs");
    // $1 x 0.00001 = $0.00001 = 10 USDC micros per tick per lot.
    require(row.instrument.quote_micros_per_tick_lot == MoneyMicros::from_atoms(10),
            "the money factor does not follow from the tick and lot");
    require(text_of(row.network_identity) == "base" &&
                text_of(row.base_asset_identity) == "0xcbB7C0000aB88B473b1f5aFd9ef808440eed33Bf" &&
                text_of(row.quote_asset_identity) == "0x833589fcd6edb6e08f4c7c32d4f71b54bda02913",
            "the asset identities are not the venue's Base contracts");
    // An RFQ quote fills whole or not at all, and there is no resting book.
    require(row.capabilities.time_in_force_mask == kVenueTifFok && row.capabilities.supports_post_only == 0,
            "capabilities claim more than an RFQ venue offers");
    require(shadow_catalog::truemarkets_catalog().row_count() == 1, "the shadow catalog has one row");
}

ShadowProgram btc_trend(MoneyMicros cap) {
    ShadowProgram p;
    p.policy_id = PolicyId{21};
    p.policy_version = PolicyVersion{1};
    p.feature_schema_id = FeatureSchemaId{31};
    p.risk_policy_version = RiskPolicyVersion{1};
    p.model_digest.bytes[0] = std::byte{0x5a};
    p.valid_from_ns = 1'000;
    p.valid_until_ns = 1'000'000'000'000;
    p.generic_features = 4;
    p.rules = {{0, PredicateOperator::greater_than, 0, ShadowAction::enter_long},
               {0, PredicateOperator::less_than_or_equal, 0, ShadowAction::exit}};
    p.row = shadow_catalog::truemarkets_cbbtc_row();
    p.time_in_force = TimeInForce::fill_or_kill;
    p.post_only = PostOnlyMode::prohibited;
    p.order_quantity = QuantityLots::from_atoms(10);  // 0.0001 BTC
    p.max_notional = cap;
    p.max_market_data_age_ns = 5'000'000'000;
    return p;
}

// Ceremony, setup and one uptrend tick at PRICE_TICKS through the reducer.
ApplyResult decide(ShadowProgram program, std::int64_t price_ticks, EconomicActionProposal* proposal = nullptr) {
    const auto& catalog = shadow_catalog::truemarkets_catalog();
    TradingState state{};
    ApplyScratch scratch;
    std::uint64_t sequence = 1;
    const auto run = [&](const Command& command) { return apply(state, command, scratch, &catalog); };
    const auto op = [&](CommandBody body) {
        Digest256 digest;
        digest.bytes[0] = std::byte{0x0c};
        digest.bytes[1] = static_cast<std::byte>(sequence);
        return Command{{CallerId{7}, RequestSequence{sequence++}, digest}, std::move(body)};
    };
    require(run(op(AdvanceKillEpoch{KillEpoch{1}})).code == ApplyCode::applied &&
                run(op(SetExecutorGeneration{ExecutorGeneration{1}})).code == ApplyCode::applied &&
                run(op(InstallExecutionCatalog{catalog.root(), KillEpoch{1}, ExecutorGeneration{1}})).code ==
                    ApplyCode::applied,
            "the operator ceremony refused the True Markets catalog");
    const auto setup = shadow_setup(program, CallerId{7}, sequence, state.kill_epoch, state.executor_generation);
    sequence += setup.commands.size();
    for (const auto& command : setup.commands)
        require(run(command).code == ApplyCode::applied, "setup refused with the True Markets row");
    ShadowTick t;
    t.source = SourceId{41};
    t.generation = SourceGeneration{1};
    t.sequence = SourceSequence{1};
    t.event_time_ns = 2'000;
    t.receive_time_ns = 2'010;
    t.market_data_age_ns = 10;
    t.features = {50'000, 0, 0, 0, price_ticks};
    t.evidence_digest.bytes[0] = std::byte{0x77};
    const auto commands = shadow_tick(program, t, CallerId{7}, sequence, DecisionId{1});
    require(run(commands[0]).code == ApplyCode::applied, "source event refused");
    const auto decision = run(commands[1]);
    if (proposal != nullptr)
        for (std::size_t i = 0; i < state.decision_count; ++i)
            if (state.decision_hot[i].decision_id == DecisionId{1}) *proposal = state.decision_cold[i].proposal;
    return decision;
}

void a_real_btc_price_resolves_to_a_bounded_buy() {
    EconomicActionProposal buy{};
    // BTC at $83,000 is 83,000 ticks; the clip costs 0.0001 x $83,000 = $8.30.
    const auto result = decide(btc_trend(MoneyMicros::from_atoms(25'000'000)), 83'000, &buy);
    require(result.decision_code == DecisionCode::shadow_recorded &&
                result.action_resolution_code == ActionResolutionCode::proposed &&
                result.proposed_directive == Directive::place_limit &&
                result.effective_directive == Directive::no_action,
            "a real BTC price was not proposed in shadow: resolution " +
                std::to_string(static_cast<int>(result.action_resolution_code)));
    require(buy.side == Side::buy && buy.price == PriceTicks::from_atoms(83'000) &&
                buy.quantity == QuantityLots::from_atoms(10) && buy.notional == MoneyMicros::from_atoms(8'300'000),
            "the proposal is not a $8.30 buy of 0.0001 BTC at $83,000");
}

void the_notional_cap_binds_in_dollars() {
    // $8.30 clip against an $8.29 cap: refused; against $8.30: proposed.
    require(decide(btc_trend(MoneyMicros::from_atoms(8'290'000)), 83'000).action_resolution_code ==
                ActionResolutionCode::notional_cap_exceeded,
            "an $8.30 clip passed an $8.29 cap");
    require(decide(btc_trend(MoneyMicros::from_atoms(8'300'000)), 83'000).action_resolution_code ==
                ActionResolutionCode::proposed,
            "an $8.30 clip was refused by an $8.30 cap");
}

void resting_order_modes_are_refused() {
    auto gtc = btc_trend(MoneyMicros::from_atoms(25'000'000));
    gtc.time_in_force = TimeInForce::good_til_cancel;
    require(decide(gtc, 83'000).action_resolution_code == ActionResolutionCode::unsupported_time_in_force,
            "a GTC order was proposed to an RFQ venue");
}

}  // namespace

int main() {
    try {
        the_row_carries_the_venue_published_units();
        a_real_btc_price_resolves_to_a_bounded_buy();
        the_notional_cap_binds_in_dollars();
        resting_order_modes_are_refused();
        std::cout << "shadow catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shadow catalog tests failed: " << error.what() << '\n';
        return 1;
    }
}
