#pragma once

#include "velocita/v2/kernel.hpp"
#include "velocita/v2/replay.hpp"
#include "velocita/v2/state_snapshot.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace catalog_test {
using namespace velocita::v2;

inline CatalogText text(std::string_view value) {
    if (value.size() > kMaxCatalogTextBytes) throw std::logic_error("test text too long");
    CatalogText result;
    result.length = static_cast<std::uint8_t>(value.size());
    std::copy(value.begin(), value.end(), result.bytes.begin());
    return result;
}

inline ExecutionCatalogRow row(AccountId account,
                               InstrumentExecutionProfile instrument,
                               VenueCapabilities capabilities) {
    ExecutionCatalogRow result;
    result.account_id = account;
    result.instrument = instrument;
    result.capabilities = capabilities;
    result.tick_quote_micros = instrument.quote_micros_per_tick_lot.atoms();
    result.lot_base_atoms = 1;
    result.encoder_family_id = 1;
    result.encoder_schema_version = 1;
    result.network_identity = text("test-network");
    result.base_asset_identity = text("test-base");
    result.quote_asset_identity = text("test-quote");
    result.routing_key = text("test-route");
    return result;
}

inline VerifiedExecutionCatalog catalog(std::span<const ExecutionCatalogRow> rows) {
    std::array<std::byte, kMaxExecutionCatalogBytes> bytes{};
    const auto encoded = encode_execution_catalog(rows, bytes);
    VerifiedExecutionCatalog result;
    if (encoded.error != ExecutionCatalogError::none ||
        VerifiedExecutionCatalog::decode(std::span{bytes}.first(encoded.bytes_written),
            encoded.root, result) != ExecutionCatalogError::none)
        throw std::logic_error("invalid synthetic catalog fixture");
    return result;
}

inline const VerifiedExecutionCatalog& standard_catalog() {
    static const auto value = [] {
        const std::array rows{row(AccountId{5},
            {kInstrumentExecutionProfileSchemaVersion, VenueId{9}, InstrumentId{4},
             InstrumentSpecVersion{7}, MoneyMicros::from_atoms(100),
             PriceTicks::from_atoms(1), PriceTicks::from_atoms(100),
             QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)},
            {kVenueCapabilitiesSchemaVersion, VenueId{9}, VenueCapabilitiesVersion{8},
             (kVenueDirectivePlaceLimit | kVenueDirectiveCancel), kVenueTifGtc, 1})};
        return catalog(rows);
    }();
    return value;
}

inline void grant(TradingState& state, const std::uint64_t caller, const CallerRole role) {
    for (std::size_t index = 0; index < state.caller_cursor_count; ++index) {
        if (state.caller_cursors[index].caller_id == CallerId{caller}) {
            state.caller_cursors[index].role = role;
            return;
        }
    }
    if (state.caller_cursor_count == state.caller_cursors.size())
        throw std::logic_error("role fixture has no free caller slot");
    state.caller_cursors[state.caller_cursor_count++] =
        {CallerId{caller}, RequestSequence{}, RequestSequence{}, role};
}

inline TradingState state(const VerifiedExecutionCatalog& value = standard_catalog()) {
    TradingState result;
    result.execution_catalog_root = value.root();
    grant(result, 22, CallerRole::gateway);
    grant(result, 24, CallerRole::gateway);
    return result;
}

inline ApplyResult apply(TradingState& state, const Command& command, ApplyScratch& scratch) {
    return velocita::v2::apply(state, command, scratch, &standard_catalog());
}

inline ReplayApplyResult replay(TradingState& state, std::span<const std::byte> bytes,
    ApplyScratch& scratch, KernelCommandCodecScratch& codec_scratch,
    std::span<std::byte> output) {
    return apply_encoded_kernel_command(state, bytes, scratch, codec_scratch, output,
                                         &standard_catalog());
}

inline Digest256 schema7_projection(const TradingState& state) {
    std::array<std::byte, kMaxStateSnapshotBytes> bytes{};
    const auto encoded = encode_state_snapshot(state, bytes);
    if (encoded.error != StateSnapshotError::none)
        throw std::logic_error("projection fixture invalid");
    const auto old_payload = encoded.bytes_written - kStateSnapshotPayloadOffset - 2 * kDigestBytes;
    for (std::size_t i = 0; i < 4; ++i) bytes[4 + i] = std::byte((old_payload >> (8 * i)) & 255);
    bytes[8] = std::byte{7};
    bytes[9] = std::byte{0};
    return sha256_digest(std::span{bytes}.subspan(kStateSnapshotDigestInputOffset,
        kStateSnapshotPayloadOffset + old_payload - kStateSnapshotDigestInputOffset));
}
}
