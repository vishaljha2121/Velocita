// The execution catalog rows: test rows and the True Markets cbBTC and SOL rows.
#pragma once

#include "v2_catalog_test_support.hpp"

namespace shadow_catalog {
using namespace velocita::v2;

inline constexpr AccountId kAccount{1};
inline constexpr AccountId kSolanaAccount{2};
inline constexpr VenueId kVenue{20};
inline constexpr InstrumentId kCbbtcUsdc{1};
inline constexpr InstrumentId kSolUsdc{2};
inline constexpr InstrumentSpecVersion kSpec{1};
inline constexpr VenueCapabilitiesVersion kCapabilities{1};

inline ExecutionCatalogRow truemarkets_cbbtc_row() {
    auto row = catalog_test::row(kAccount,
        {kInstrumentExecutionProfileSchemaVersion, kVenue, kCbbtcUsdc, kSpec, MoneyMicros::from_atoms(10),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(10'000'000),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)},
        {kVenueCapabilitiesSchemaVersion, kVenue, kCapabilities, kVenueDirectivePlaceLimit, kVenueTifFok, 0});
    row.tick_quote_micros = 1'000'000;
    row.lot_base_atoms = 1'000;
    row.base_decimals = 8;
    row.network_identity = catalog_test::text("base");
    row.base_asset_identity = catalog_test::text("0xcbB7C0000aB88B473b1f5aFd9ef808440eed33Bf");
    row.quote_asset_identity = catalog_test::text("0x833589fcd6edb6e08f4c7c32d4f71b54bda02913");
    row.routing_key = catalog_test::text("truemarkets.shadow.base.cbbtc-usdc");
    return row;
}

inline ExecutionCatalogRow truemarkets_sol_row() {
    auto row = catalog_test::row(kSolanaAccount,
        {kInstrumentExecutionProfileSchemaVersion, kVenue, kSolUsdc, kSpec, MoneyMicros::from_atoms(100),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(10'000'000),
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)},
        {kVenueCapabilitiesSchemaVersion, kVenue, kCapabilities, kVenueDirectivePlaceLimit, kVenueTifFok, 0});
    row.tick_quote_micros = 10'000;
    row.lot_base_atoms = 10'000'000;
    row.base_decimals = 9;
    row.network_identity = catalog_test::text("solana");
    row.base_asset_identity = catalog_test::text("So11111111111111111111111111111111111111112");
    row.quote_asset_identity = catalog_test::text("EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v");
    row.routing_key = catalog_test::text("truemarkets.shadow.solana.sol-usdc");
    return row;
}

inline const VerifiedExecutionCatalog& truemarkets_catalog() {
    static const auto value = [] {
        const std::array rows{truemarkets_cbbtc_row()};
        return catalog_test::catalog(rows);
    }();
    return value;
}

inline const VerifiedExecutionCatalog& truemarkets_two_row_catalog() {
    static const auto value = [] {
        const std::array rows{truemarkets_cbbtc_row(), truemarkets_sol_row()};
        return catalog_test::catalog(rows);
    }();
    return value;
}
}
