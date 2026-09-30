#pragma once

#include "v2_catalog_test_support.hpp"

// SHADOW-ONLY execution catalog for True Markets cbBTC/USDC on Base. The units
// are the venue's published facts (GET /v1/defi/core/assets, 2026-09-28:
// cbBTC 8 decimals, USDC 6); the capabilities are the narrowest honest reading
// of an RFQ venue. There is no encoder or venue contract behind this row; it
// lets shadow decisions price real BTC and is replaced by M5 closure.

namespace shadow_catalog {
using namespace velocita::v2;

inline constexpr AccountId kAccount{1};
inline constexpr VenueId kVenue{20};
inline constexpr InstrumentId kCbbtcUsdc{1};
inline constexpr InstrumentSpecVersion kSpec{1};
inline constexpr VenueCapabilitiesVersion kCapabilities{1};

inline ExecutionCatalogRow truemarkets_cbbtc_row() {
    // One tick is $1 per whole BTC; one lot is 0.00001 BTC (1000 atoms of 1e-8):
    // $1 x 0.00001 = 10 USDC micros per tick per lot.
    auto row = catalog_test::row(kAccount,
        {kInstrumentExecutionProfileSchemaVersion, kVenue, kCbbtcUsdc, kSpec, MoneyMicros::from_atoms(10),
         PriceTicks::from_atoms(1), PriceTicks::from_atoms(10'000'000),      // $1 .. $10M
         QuantityLots::from_atoms(1), QuantityLots::from_atoms(100'000)},    // 0.00001 .. 1 BTC
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

inline const VerifiedExecutionCatalog& truemarkets_catalog() {
    static const auto value = [] {
        const std::array rows{truemarkets_cbbtc_row()};
        return catalog_test::catalog(rows);
    }();
    return value;
}
}  // namespace shadow_catalog
