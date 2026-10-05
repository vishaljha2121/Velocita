#include "velocita/v2/execution_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace velocita::v2;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint8_t nibble(const char value) {
    return value >= '0' && value <= '9'
               ? static_cast<std::uint8_t>(value - '0')
               : static_cast<std::uint8_t>(value - 'a' + 10);
}

template <std::size_t N>
std::array<std::byte, N / 2> hex_bytes(const char (&hex)[N]) {
    static_assert(N % 2 == 1);
    std::array<std::byte, N / 2> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::byte>((nibble(hex[2 * i]) << 4U) |
                                           nibble(hex[2 * i + 1]));
    }
    return result;
}

Digest256 digest(const char (&hex)[65]) {
    Digest256 result{};
    result.bytes = hex_bytes(hex);
    return result;
}

CatalogText text(const std::string_view value) {
    CatalogText result{};
    require(value.size() <= result.bytes.size(), "test text exceeds capacity");
    result.length = static_cast<std::uint8_t>(value.size());
    std::copy(value.begin(), value.end(), result.bytes.begin());
    return result;
}

ExecutionCatalogRow row(const std::uint64_t account = 11) {
    ExecutionCatalogRow result{};
    result.account_id = AccountId{account};
    result.instrument = {kInstrumentExecutionProfileSchemaVersion,
                         VenueId{22}, InstrumentId{33},
                         InstrumentSpecVersion{44},
                         MoneyMicros::from_atoms(10), PriceTicks::from_atoms(1),
                         PriceTicks::from_atoms(100000),
                         QuantityLots::from_atoms(1),
                         QuantityLots::from_atoms(1000)};
    result.capabilities = {kVenueCapabilitiesSchemaVersion, VenueId{22},
                           VenueCapabilitiesVersion{55},
                           static_cast<std::uint8_t>(kVenueDirectivePlaceLimit |
                                                     kVenueDirectiveCancel),
                           static_cast<std::uint8_t>(kVenueTifGtc | kVenueTifIoc),
                           1};
    result.tick_quote_micros = 10000;
    result.lot_base_atoms = 100000;
    result.base_decimals = 8;
    result.encoder_family_id = 7;
    result.encoder_schema_version = 9;
    result.network_identity = text("net-A");
    result.base_asset_identity = text("BASE");
    result.quote_asset_identity = text("QUOTE");
    result.routing_key = text("BASE/QUOTE");
    return result;
}

void require_encode_error(const ExecutionCatalogRow& candidate,
                          const ExecutionCatalogError expected,
                          const char* message) {
    std::array<std::byte, kMaxExecutionCatalogBytes> output{};
    output.fill(std::byte{0xa5});
    const auto result = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&candidate, 1}, output);
    require(result.error == expected, message);
    require(std::all_of(output.begin(), output.end(),
                        [](const std::byte value) { return value == std::byte{0xa5}; }),
            "refused encode changed caller output");
}

void test_independent_golden_and_owned_decode() {
    const auto golden = hex_bytes(
        "56324543010001009c0000000b000000000000000100160000000000000021000000"
        "000000002c000000000000000a000000000000000100000000000000a08601000000"
        "00000100000000000000e80300000000000001001600000000000000370000000000"
        "00000303011027000000000000a0860100000000000807000900056e65742d410442"
        "4153450551554f54450a424153452f51554f5445");
    const auto expected_root =
        digest("a299122f237ec96dca84da1912cfaddaacf83cd506bcf60bbe6d04dfcbc3fbf0");

    std::array<std::byte, kMaxExecutionCatalogBytes> encoded{};
    const auto source = row();
    const auto encoded_result = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&source, 1}, encoded);
    require(encoded_result.error == ExecutionCatalogError::none,
            "golden row encode refused");
    require(encoded_result.bytes_written == golden.size(),
            "golden encoded length changed");
    require(std::equal(golden.begin(), golden.end(), encoded.begin()),
            "catalog canonical bytes changed");
    require(encoded_result.root == expected_root, "catalog domain root changed");

    std::vector<std::byte> caller_bytes(golden.begin(), golden.end());
    VerifiedExecutionCatalog decoded;
    require(VerifiedExecutionCatalog::decode(caller_bytes, expected_root, decoded) ==
                ExecutionCatalogError::none,
            "independent golden decode refused");
    caller_bytes.assign(caller_bytes.size(), std::byte{0});
    const auto* found = decoded.lookup(AccountId{11}, VenueId{22}, InstrumentId{33},
                                       InstrumentSpecVersion{44},
                                       VenueCapabilitiesVersion{55});
    require(found != nullptr && found->routing_key == text("BASE/QUOTE"),
            "decoded context borrowed mutable caller bytes");
}

void test_digest_factor_identity_and_preserved_destination() {
    const auto golden = hex_bytes(
        "56324543010001009c0000000b000000000000000100160000000000000021000000"
        "000000002c000000000000000a000000000000000100000000000000a08601000000"
        "00000100000000000000e80300000000000001001600000000000000370000000000"
        "00000303011027000000000000a0860100000000000807000900056e65742d410442"
        "4153450551554f54450a424153452f51554f5445");
    const auto expected_root =
        digest("a299122f237ec96dca84da1912cfaddaacf83cd506bcf60bbe6d04dfcbc3fbf0");
    const auto alternate_root =
        digest("49fd4478812b43e262ec92e21b25d9a8fba772f808f1a5e44e32ac8470d213ff");
    auto alternate = row();
    alternate.tick_quote_micros = 100000;
    alternate.lot_base_atoms = 10000;
    std::array<std::byte, kMaxExecutionCatalogBytes> alternate_bytes{};
    const auto alternate_encoded = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&alternate, 1}, alternate_bytes);
    require(alternate_encoded.error == ExecutionCatalogError::none,
            "valid equal-product alternate factors refused");
    require(alternate_encoded.root == alternate_root &&
                alternate_encoded.root != expected_root,
            "codec collapsed or incorrectly hashed alternate factor identity");
    VerifiedExecutionCatalog alternate_context;
    require(VerifiedExecutionCatalog::decode(
                std::span<const std::byte>{alternate_bytes}.first(
                    alternate_encoded.bytes_written),
                expected_root, alternate_context) ==
                ExecutionCatalogError::digest_mismatch,
            "alternate factors decoded under original expected root");
    require(alternate_context.row_count() == 0 && alternate_context.root().is_zero(),
            "alternate-root refusal changed destination");
    require(VerifiedExecutionCatalog::decode(
                std::span<const std::byte>{alternate_bytes}.first(
                    alternate_encoded.bytes_written),
                alternate_root, alternate_context) == ExecutionCatalogError::none,
            "alternate factors refused under their independent root");
    const auto* alternate_found = alternate_context.lookup(
        AccountId{11}, VenueId{22}, InstrumentId{33}, InstrumentSpecVersion{44},
        VenueCapabilitiesVersion{55});
    require(alternate_found != nullptr &&
                alternate_found->tick_quote_micros == 100000 &&
                alternate_found->lot_base_atoms == 10000,
            "alternate decoded context lost exact factors");

    auto malformed = golden;
    malformed[0] = std::byte{'X'};
    VerifiedExecutionCatalog destination;
    require(VerifiedExecutionCatalog::decode(malformed, expected_root, destination) ==
                ExecutionCatalogError::invalid_magic,
            "malformed magic did not refuse");
    require(destination.root().is_zero() && destination.row_count() == 0,
            "malformed decode changed destination");
    require(VerifiedExecutionCatalog::decode(golden, alternate_root, destination) ==
                ExecutionCatalogError::digest_mismatch,
            "wrong expected root did not refuse");
    require(destination.root().is_zero() && destination.row_count() == 0,
            "digest refusal changed destination");

    VerifiedExecutionCatalog retained;
    require(VerifiedExecutionCatalog::decode(golden, expected_root, retained) ==
                ExecutionCatalogError::none,
            "could not seed immutable destination");
    require(VerifiedExecutionCatalog::decode(malformed, expected_root, retained) ==
                ExecutionCatalogError::invalid_magic,
            "malformed replacement did not report its wire error");
    const auto* retained_row = retained.lookup(
        AccountId{11}, VenueId{22}, InstrumentId{33}, InstrumentSpecVersion{44},
        VenueCapabilitiesVersion{55});
    require(retained.root() == expected_root && retained.row_count() == 1 &&
                retained_row != nullptr && *retained_row == row(),
            "refused replacement changed retained row bytes");
}

void test_exact_maximum_and_row_count_bounds() {
    std::array<ExecutionCatalogRow, 16> rows{};
    const std::string_view max_text(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        "!#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ 12");
    require(max_text.size() == 96, "maximum-text fixture is not 96 bytes");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i] = row(i + 1);
        rows[i].network_identity = text(max_text);
        rows[i].base_asset_identity = text(max_text);
        rows[i].quote_asset_identity = text(max_text);
        rows[i].quote_asset_identity.bytes[0] = 'Z';
        rows[i].routing_key = text(max_text);
    }
    std::array<std::byte, kMaxExecutionCatalogBytes> output{};
    const auto encoded = encode_execution_catalog(rows, output);
    require(encoded.error == ExecutionCatalogError::none,
            "sixteen maximum rows refused");
    require(encoded.bytes_written == 8076,
            "sixteen maximum rows are not exactly 8076 bytes");
    VerifiedExecutionCatalog decoded;
    require(VerifiedExecutionCatalog::decode(
                std::span<const std::byte>{output}.first(encoded.bytes_written),
                encoded.root, decoded) == ExecutionCatalogError::none,
            "maximum-size catalog did not decode");
    require(decoded.row_count() == 16 &&
                decoded.lookup(AccountId{16}, VenueId{22}, InstrumentId{33},
                               InstrumentSpecVersion{44},
                               VenueCapabilitiesVersion{55}) != nullptr,
            "maximum-size roundtrip lost boundary row");

    std::array<ExecutionCatalogRow, 17> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) too_many[i] = row(i + 1);
    output.fill(std::byte{0x6b});
    const auto rejected = encode_execution_catalog(too_many, output);
    require(rejected.error == ExecutionCatalogError::row_count_out_of_range,
            "seventeen rows did not refuse");
    require(std::all_of(output.begin(), output.end(), [](const std::byte value) {
                return value == std::byte{0x6b};
            }),
            "row-count refusal changed output");
}

void test_field_and_text_boundaries() {
    auto candidate = row();
    candidate.base_decimals = 19;
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "base decimals 19 accepted");
    candidate = row();
    candidate.encoder_family_id = 0;
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "zero encoder family accepted");
    candidate = row();
    candidate.tick_quote_micros = 0;
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "zero tick factor accepted");
    candidate = row();
    candidate.lot_base_atoms = std::numeric_limits<std::int64_t>::max();
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "overflowing/inexact factor product accepted");
    candidate = row();
    candidate.base_asset_identity = candidate.quote_asset_identity;
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "identical base and quote identities accepted");
    candidate = row();
    candidate.network_identity = {};
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "empty identity accepted");
    candidate = row();
    candidate.network_identity.bytes[0] = static_cast<char>(0x1f);
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "non-printable low byte accepted");
    candidate = row();
    candidate.network_identity.bytes[0] = static_cast<char>(0x7f);
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "non-printable high byte accepted");
    candidate = row();
    candidate.instrument.venue_id = VenueId{23};
    require_encode_error(candidate, ExecutionCatalogError::invalid_value,
                         "profile/capability venue mismatch accepted");
}

void test_order_and_cross_row_identity_constraints() {
    std::array<std::byte, kMaxExecutionCatalogBytes> output{};
    std::array<ExecutionCatalogRow, 2> rows{row(2), row(1)};
    require(encode_execution_catalog(rows, output).error ==
                ExecutionCatalogError::non_canonical_order,
            "unsorted rows accepted");
    rows = {row(1), row(1)};
    require(encode_execution_catalog(rows, output).error ==
                ExecutionCatalogError::duplicate_key,
            "duplicate key accepted");

    rows = {row(1), row(2)};
    rows[1].routing_key = text("OTHER/ROUTE");
    require(encode_execution_catalog(rows, output).error ==
                ExecutionCatalogError::cross_row_conflict,
            "cross-account same-version conflicting identity accepted");

    rows = {row(1), row(1)};
    rows[1].instrument.instrument_id = InstrumentId{34};
    rows[1].instrument.instrument_spec_version = InstrumentSpecVersion{45};
    rows[1].quote_asset_identity = text("OTHER");
    require(encode_execution_catalog(rows, output).error ==
                ExecutionCatalogError::account_denomination_conflict,
            "one account with conflicting quote identity accepted");
}

void test_catalog_text_unused_tail_is_not_identity() {
    auto clean = row();
    auto dirty = clean;
    for (CatalogText* value : {&dirty.network_identity, &dirty.base_asset_identity,
                               &dirty.quote_asset_identity, &dirty.routing_key}) {
        std::fill(value->bytes.begin() + value->length, value->bytes.end(), 'x');
    }
    std::array<std::byte, kMaxExecutionCatalogBytes> clean_bytes{};
    std::array<std::byte, kMaxExecutionCatalogBytes> dirty_bytes{};
    const auto clean_encoded = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&clean, 1}, clean_bytes);
    const auto dirty_encoded = encode_execution_catalog(
        std::span<const ExecutionCatalogRow>{&dirty, 1}, dirty_bytes);
    require(clean_encoded.error == ExecutionCatalogError::none &&
                dirty_encoded.error == ExecutionCatalogError::none &&
                clean_encoded.bytes_written == dirty_encoded.bytes_written &&
                clean_encoded.root == dirty_encoded.root &&
                std::equal(clean_bytes.begin(),
                           clean_bytes.begin() + clean_encoded.bytes_written,
                           dirty_bytes.begin()),
            "unused CatalogText tail changed wire identity");

    std::array<ExecutionCatalogRow, 2> repeated{clean, dirty};
    repeated[1].account_id = AccountId{12};
    require(encode_execution_catalog(repeated, dirty_bytes).error ==
                ExecutionCatalogError::none,
            "unused text tails caused false cross-row conflict");

    auto same_asset = row();
    same_asset.quote_asset_identity = same_asset.base_asset_identity;
    std::fill(same_asset.quote_asset_identity.bytes.begin() +
                  same_asset.quote_asset_identity.length,
              same_asset.quote_asset_identity.bytes.end(), 'q');
    require_encode_error(same_asset, ExecutionCatalogError::invalid_value,
                         "same logical asset hidden by unused tails was accepted");
}

}

void run_execution_catalog_adversarial_tests() {
    test_independent_golden_and_owned_decode();
    test_digest_factor_identity_and_preserved_destination();
    test_exact_maximum_and_row_count_bounds();
    test_field_and_text_boundaries();
    test_order_and_cross_row_identity_constraints();
    test_catalog_text_unused_tail_is_not_identity();
}
