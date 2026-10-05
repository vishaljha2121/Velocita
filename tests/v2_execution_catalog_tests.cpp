#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

void run_execution_catalog_adversarial_tests();

namespace {

void require(const bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

velocita::v2::CatalogText text(const std::string_view source) {
    velocita::v2::CatalogText result;
    result.length = static_cast<std::uint8_t>(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        result.bytes[index] = source[index];
    }
    return result;
}

velocita::v2::ExecutionCatalogRow row(const std::uint64_t account = 1,
                                      const std::uint64_t venue = 2,
                                      const std::uint64_t instrument = 3,
                                      const std::uint64_t spec = 4,
                                      const std::uint64_t capabilities = 5) {
    using namespace velocita::v2;
    ExecutionCatalogRow value;
    value.account_id = AccountId{account};
    value.instrument = {kInstrumentExecutionProfileSchemaVersion,
                        VenueId{venue},
                        InstrumentId{instrument},
                        InstrumentSpecVersion{spec},
                        MoneyMicros::from_atoms(25),
                        PriceTicks::from_atoms(1),
                        PriceTicks::from_atoms(1'000),
                        QuantityLots::from_atoms(1),
                        QuantityLots::from_atoms(100)};
    value.capabilities = {kVenueCapabilitiesSchemaVersion,
                          VenueId{venue},
                          VenueCapabilitiesVersion{capabilities},
                          static_cast<std::uint8_t>(kVenueDirectivePlaceLimit |
                                                    kVenueDirectiveCancel),
                          kVenueTifGtc,
                          1};
    value.tick_quote_micros = 250;
    value.lot_base_atoms = 100'000;
    value.base_decimals = 6;
    value.encoder_family_id = 77;
    value.encoder_schema_version = 1;
    value.network_identity = text("synthetic-network");
    value.base_asset_identity = text("synthetic-base");
    value.quote_asset_identity = text("synthetic-quote");
    value.routing_key = text("synthetic-route");
    return value;
}

void test_round_trip_and_lookup() {
    using namespace velocita::v2;
    const std::array rows{row()};
    std::array<std::byte, kMaxExecutionCatalogBytes> bytes{};
    const auto encoded = encode_execution_catalog(rows, bytes);
    require(encoded.error == ExecutionCatalogError::none,
            "valid catalog did not encode");
    require(encoded.bytes_written != 0 && !encoded.root.is_zero(),
            "valid catalog did not produce bytes and a root");

    VerifiedExecutionCatalog catalog;
    require(VerifiedExecutionCatalog::decode(
                std::span<const std::byte>{bytes}.first(encoded.bytes_written),
                encoded.root, catalog) == ExecutionCatalogError::none,
            "valid catalog did not decode");
    require(catalog.row_count() == 1 && catalog.root() == encoded.root,
            "decoded catalog did not own its verified identity");
    const auto *found =
        catalog.lookup(AccountId{1}, VenueId{2}, InstrumentId{3},
                       InstrumentSpecVersion{4}, VenueCapabilitiesVersion{5});
    require(found != nullptr && *found == rows[0], "exact lookup failed");
    require(catalog.lookup(AccountId{9}, VenueId{2}, InstrumentId{3},
                           InstrumentSpecVersion{4},
                           VenueCapabilitiesVersion{5}) == nullptr,
            "lookup ignored the account binding");
}

void test_factor_requires_exact_divisibility() {
    using namespace velocita::v2;
    auto inexact = row();
    inexact.lot_base_atoms = 100'001;
    std::array<std::byte, kMaxExecutionCatalogBytes> bytes{};
    bytes.fill(std::byte{0x5a});
    const auto before = bytes;
    const auto encoded = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&inexact, 1}, bytes);
    require(encoded.error == ExecutionCatalogError::invalid_value &&
                encoded.bytes_written == 0 && encoded.root.is_zero(),
            "fractional factor product was accepted");
    require(bytes == before,
            "fractional factor refusal partially changed the output");
}

void test_all_cuts_and_forged_structures_preserve_destination() {
    using namespace velocita::v2;
    const std::array rows{row()};
    std::array<std::byte, kMaxExecutionCatalogBytes> buffer{};
    const auto encoded = encode_execution_catalog(rows, buffer);
    require(encoded.error == ExecutionCatalogError::none, "cut fixture failed");
    const auto bytes = std::span<const std::byte>{buffer}.first(encoded.bytes_written);
    VerifiedExecutionCatalog destination;
    require(VerifiedExecutionCatalog::decode(bytes, encoded.root, destination) ==
                ExecutionCatalogError::none, "cut destination failed");
    const auto refuse = [&](std::span<const std::byte> input, Digest256 expected) {
        require(VerifiedExecutionCatalog::decode(input, expected, destination) !=
                    ExecutionCatalogError::none, "malformed catalog decoded");
        const auto* retained = destination.lookup(AccountId{1}, VenueId{2},
            InstrumentId{3}, InstrumentSpecVersion{4}, VenueCapabilitiesVersion{5});
        require(destination.root() == encoded.root && destination.row_count() == 1 &&
                    retained != nullptr && *retained == rows[0],
                "refused decode changed retained catalog contents");
    };
    const auto repair_length = [](std::vector<std::byte>& input) {
        for (std::size_t i = 0; i < 4; ++i)
            input[8 + i] = static_cast<std::byte>((input.size() >> (8 * i)) & 255);
    };
    const auto matching_root = [](std::span<const std::byte> input) {
        constexpr std::string_view domain = "velocita.execution-catalog.v1";
        std::vector<std::byte> preimage;
        for (char c : domain) preimage.push_back(static_cast<std::byte>(c));
        preimage.push_back(std::byte{0});
        preimage.insert(preimage.end(), input.begin(), input.end());
        return sha256_digest(preimage);
    };
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + cut);
        refuse(truncated, encoded.root);
        if (cut >= 12) {
            repair_length(truncated);
            refuse(truncated, matching_root(truncated));
        }
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, unsigned>, 10>{
            {{4, 2}, {6, 0}, {6, 17}, {20, 2}, {88, 3}, {104, 255},
             {105, 255}, {123, 19}, {128, 97}, {129, 31}}}) {
        std::vector<std::byte> malformed(bytes.begin(), bytes.end());
        malformed[offset] = static_cast<std::byte>(value);
        refuse(malformed, matching_root(malformed));
    }
    std::vector<std::byte> trailing(bytes.begin(), bytes.end());
    trailing.push_back(std::byte{0});
    repair_length(trailing);
    refuse(trailing, matching_root(trailing));
    std::vector<std::byte> oversized(kMaxExecutionCatalogBytes + 1);
    refuse(oversized, encoded.root);
    refuse(bytes, {});
}

void test_encoder_capacity_and_aliasing() {
    using namespace velocita::v2;
    std::array rows{row(), row(2)};
    std::array<std::byte, kMaxExecutionCatalogBytes> canonical{};
    const auto encoded = encode_execution_catalog(rows, canonical);
    require(encoded.error == ExecutionCatalogError::none, "capacity fixture failed");
    std::array<std::byte, kMaxExecutionCatalogBytes> output{};
    output.fill(std::byte{0xa5});
    const auto untouched = output;
    for (std::size_t cut = 0; cut < encoded.bytes_written; ++cut) {
        const auto refused = encode_execution_catalog(rows, std::span{output}.first(cut));
        require(refused.error == ExecutionCatalogError::output_too_small &&
                    refused.bytes_written == 0 && refused.root.is_zero() && output == untouched,
                "short output accepted or partially written");
    }
    require(encode_execution_catalog({}, output).error ==
                ExecutionCatalogError::row_count_out_of_range && output == untouched,
            "empty rows accepted or changed output");
    auto invalid = row();
    invalid.network_identity.length = 97;
    require(encode_execution_catalog(std::span{&invalid, 1}, output).error ==
                ExecutionCatalogError::invalid_value && output == untouched,
            "oversize builder text read beyond storage or changed output");
    auto aliased_output = std::as_writable_bytes(std::span{rows});
    require(aliased_output.size() >= encoded.bytes_written, "alias fixture too small");
    const auto aliased = encode_execution_catalog(rows, aliased_output);
    require(aliased.error == ExecutionCatalogError::none &&
                aliased.root == encoded.root && aliased.bytes_written == encoded.bytes_written &&
                std::equal(canonical.begin(), canonical.begin() + encoded.bytes_written,
                           aliased_output.begin()), "encoder read rows after overwriting them");
}

}

int main() {
    try {
        test_round_trip_and_lookup();
        test_factor_requires_exact_divisibility();
        test_all_cuts_and_forged_structures_preserve_destination();
        test_encoder_capacity_and_aliasing();
        run_execution_catalog_adversarial_tests();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "v2_execution_catalog_tests: PASS\n";
    return 0;
}
