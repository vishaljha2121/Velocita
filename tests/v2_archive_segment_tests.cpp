#include "velocita/v2/archive_segment.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace allocation_probe { thread_local bool enabled{}; thread_local std::size_t count{}; }
void* operator new(const std::size_t bytes) {
    if (allocation_probe::enabled) ++allocation_probe::count;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](const std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace velocita::v2;

void require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<std::byte> hex(const std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t at = 0; at < text.size(); at += 2)
        bytes.push_back(std::byte(std::stoul(std::string(text.substr(at, 2)), nullptr, 16)));
    return bytes;
}

const VerifiedStaticIdentity& identity() {
    static const auto bytes = hex(
        "563249447c000000010000000102030405060708090a0b0c0d0e0f10"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "0900090001000100010001000101030011000000010100000100010011000000"
        "a6326543a7df7e9c89692344318dadf2f97dc3f06b3dabfacdcd3875fb29334d");
    static const auto value = [&] {
        VerifiedStaticIdentity result;
        require(VerifiedStaticIdentity::decode(bytes, result) == StaticIdentityCodecError::none,
                "identity golden refused");
        return result;
    }();
    return value;
}

const std::vector<std::byte>& command_bytes() {
    static const auto bytes = hex(
        "56324b433800000009000600010000000000000001000000000000000100000000000000000000000000000000000000000000000000000000000000"
        "01000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b");
    return bytes;
}

const std::vector<std::byte>& maximum_command_bytes() {
    static const auto bytes = [] {
        auto nonzero_digest = [] { Digest256 value; value.bytes[0] = std::byte{1}; return value; };
        StageExecutablePolicy stage;
        stage.feature_schema.schema_version = kFeatureSchemaArtifactSchemaVersion;
        stage.feature_schema.feature_schema_id = FeatureSchemaId{3};
        stage.feature_schema.feature_count = kMaxDecisionFeatures;
        for (auto& feature : stage.feature_schema.features)
            feature = {FeatureEconomicKind::price, 1};
        stage.feature_schema.artifact_digest = compute_artifact_digest(stage.feature_schema).digest;
        auto& policy = stage.executable_policy;
        policy.schema_version = kExecutablePolicyArtifactSchemaVersion;
        policy.policy_id = PolicyId{17}; policy.policy_version = PolicyVersion{2};
        policy.policy_artifact_digest = nonzero_digest();
        policy.feature_schema_id = FeatureSchemaId{3};
        policy.feature_schema_digest = stage.feature_schema.artifact_digest;
        policy.template_count = kMaxExecutableActionTemplates;
        for (std::size_t index = 0; index < policy.template_count; ++index) {
            auto& action = policy.templates[index];
            action.schema_version = kActionTemplateSchemaVersion;
            action.directive = Directive::place_limit; action.side = Side::buy;
            action.time_in_force = TimeInForce::good_til_cancel;
            action.post_only = PostOnlyMode::required;
            action.order_slot = LogicalOrderSlot{static_cast<std::uint16_t>(index + 1)};
            action.instrument_spec_version = InstrumentSpecVersion{6};
            action.venue_capabilities_version = VenueCapabilitiesVersion{7};
            action.price_source = {EconomicValueSourceKind::feature,
                static_cast<std::uint16_t>(index), RoundingMode::toward_zero, {}, 1, {}};
            action.quantity_source = {EconomicValueSourceKind::fixed, 0,
                RoundingMode::toward_zero, QuantityLots::from_atoms(1), 0};
            action.safety_caps = {QuantityLots::from_atoms(2), MoneyMicros::from_atoms(1000),
                PriceTicks::from_atoms(2), PriceTicks::from_atoms(1), 100,
                action.price_source};
        }
        policy.rule_binding_count = kMaxPolicyRules;
        for (std::size_t index = 0; index < policy.rule_binding_count; ++index)
            policy.rule_template_slots[index] = ActionTemplateSlot{
                static_cast<std::uint8_t>(index % kMaxExecutableActionTemplates + 1)};
        policy.artifact_digest = compute_artifact_digest(policy).digest;
        stage.instrument = {kInstrumentExecutionProfileSchemaVersion, VenueId{8}, InstrumentId{4},
            InstrumentSpecVersion{6}, MoneyMicros::from_atoms(10), PriceTicks::from_atoms(1),
            PriceTicks::from_atoms(100), QuantityLots::from_atoms(1), QuantityLots::from_atoms(10)};
        stage.venue = {kVenueCapabilitiesSchemaVersion, VenueId{8}, VenueCapabilitiesVersion{7},
            kVenueDirectivePlaceLimit, kVenueTifGtc, 1};
        Command command{{CallerId{12}, RequestSequence{11}, nonzero_digest()}, stage};
        std::array<std::byte, kMaxKernelCommandBytes> storage{};
        const auto encoded = encode_kernel_command(command, storage);
        require(encoded.error == KernelCodecError::none && encoded.bytes_written == storage.size(),
                "maximum canonical command fixture is not exactly 1789 bytes");
        return std::vector<std::byte>(storage.begin(), storage.end());
    }();
    return bytes;
}

const std::array<std::byte, kAuthorityBarrierBytes>& marker_bytes() {
    static const auto bytes = [] {
        AuthorityBarrier marker;
        marker.incarnation[0] = std::byte{0x31}; marker.request_sequence = 1;
        marker.effect_id = EffectId{1}; marker.expected_effect_payload_digest.bytes[0] = std::byte{1};
        marker.expected_kill_epoch = KillEpoch{1};
        marker.expected_executor_generation = ExecutorGeneration{1};
        marker.catalog_root.bytes[0] = std::byte{2};
        marker.authority_domain = identity().authority_domain();
        std::array<std::byte, kAuthorityBarrierBytes> result{};
        require(encode_authority_barrier(kAuthorityBarrierEntryType, marker, result).error ==
                    AuthorityBarrierCodecError::none, "marker fixture failed");
        return result;
    }();
    return bytes;
}

const std::vector<std::byte>& segment_golden() {
    // Independently generated with Python struct.pack/hashlib from the approved
    // byte sheet, not by the implementation under test.
    static const auto bytes = hex(
        "56324153010060002401000001000000070000000000000007000000000000000200000000000000030000000000000003000000000000003ce5d6e3e9a886ab694852a129348bbe9413c2b55077a84804efa3cd645cc3e00000000000000000"
        "070000000000000003000000000000000700000000000000640000000000000056324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b"
        "336ac13bf56b16211fefc1a962079ada43108ed3ee5219da84483faeef49fa9e"
        "3ad66acf194db10a4824dd7ad15b8b737b1934e73ca3f92900b3e107a90bdcd7");
    return bytes;
}

struct VisitCapture {
    std::size_t calls{};
    std::uint64_t index{};
    std::int64_t term{};
    std::int32_t entry_id{};
    std::int32_t entry_type{};
    std::array<std::byte, kMaxKernelCommandBytes> payload{};
    std::size_t payload_size{};
    bool accept{true};
};

bool capture(const ArchiveRecordView& record, void* context) noexcept {
    auto& out = *static_cast<VisitCapture*>(context);
    ++out.calls;
    out.index = record.index; out.term = record.term;
    out.entry_id = record.entry_id; out.entry_type = record.entry_type;
    out.payload_size = record.payload.size();
    std::copy(record.payload.begin(), record.payload.end(), out.payload.begin());
    return out.accept;
}

struct RecordSourceContext {
    std::span<const ArchiveRecordView> records;
    std::size_t calls{};
    std::optional<std::size_t> fail_at{};
};

struct GeneratedSourceContext {
    std::uint64_t first_index{1};
    std::span<const std::byte> payload;
    std::array<std::byte, kMaxKernelCommandBytes> staging{};
    std::size_t calls{};
};

bool generated_record_at(void* context, const std::uint32_t ordinal,
                         ArchiveRecordView& destination) noexcept {
    auto& source = *static_cast<GeneratedSourceContext*>(context);
    ++source.calls;
    std::copy(source.payload.begin(), source.payload.end(), source.staging.begin());
    // Every view aliases the same caller-owned staging buffer, whose contents
    // are only promised until this callback's next invocation.
    destination = {source.first_index + ordinal, 1, -static_cast<std::int32_t>(ordinal), 0,
                   {source.staging.data(), source.payload.size()}};
    return true;
}

struct ExactCapSourceContext { std::array<std::byte, kMaxKernelCommandBytes> staging{}; };
bool exact_cap_record_at(void* context, const std::uint32_t ordinal,
                         ArchiveRecordView& destination) noexcept {
    auto& source = *static_cast<ExactCapSourceContext*>(context);
    std::span<const std::byte> payload;
    std::int32_t type{};
    if (ordinal < 2250) payload = maximum_command_bytes();
    else if (ordinal < 2311) { payload = marker_bytes(); type = kAuthorityBarrierEntryType; }
    else payload = command_bytes();
    std::copy(payload.begin(), payload.end(), source.staging.begin());
    destination = {static_cast<std::uint64_t>(ordinal) + 1, 1,
                   -static_cast<std::int32_t>(ordinal + 1), type,
                   {source.staging.data(), payload.size()}};
    return true;
}

bool record_at(void* context, const std::uint32_t ordinal,
               ArchiveRecordView& destination) noexcept {
    auto& source = *static_cast<RecordSourceContext*>(context);
    ++source.calls;
    if (source.fail_at && ordinal == *source.fail_at) return false;
    if (ordinal >= source.records.size()) return false;
    destination = source.records[ordinal];
    return true;
}

ArchiveRecordSource source(RecordSourceContext& context) {
    return {&context, static_cast<std::uint32_t>(context.records.size()), record_at};
}

void test_independent_golden_and_decode() {
    ArchiveSegmentScratch scratch;
    const std::array<ArchiveRecordView, 1> records{{
        {7, 3, 7, 0, command_bytes()}}};
    RecordSourceContext record_source{records};
    std::vector<std::byte> output(segment_golden().size(), std::byte{0xcc});
    const auto encoded = encode_archive_segment(source(record_source), 2, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::none &&
                encoded.bytes_written == segment_golden().size() && output == segment_golden(),
            "archive encoder disagrees with independent golden");

    VisitCapture seen;
    ArchiveSegmentMetadata metadata{99, 99, 99, 99, 99, 99, 99, {}};
    const auto decoded = decode_archive_segment(output, identity(), capture, &seen,
                                                 scratch, metadata);
    require(decoded.error == ArchiveSegmentError::none && seen.calls == 1 &&
                seen.index == 7 && seen.term == 3 && seen.entry_id == 7 &&
                seen.entry_type == 0 && seen.payload_size == command_bytes().size() &&
                std::equal(command_bytes().begin(), command_bytes().end(), seen.payload.begin()),
            "archive golden did not visit exact record bytes");
    require(metadata.first_index == 7 && metadata.last_index == 7 &&
                metadata.predecessor_term == 2 && metadata.first_term == 3 &&
                metadata.last_term == 3 && metadata.record_count == 1 &&
                metadata.total_bytes == output.size() &&
                metadata.segment_digest == encoded.segment_digest,
            "archive metadata changed golden fields");
}

void test_failed_authority_attempt_is_preserved() {
    AuthorityBarrier marker;
    marker.incarnation[0] = std::byte{0xf1}; // gateway identity, deliberately not cluster identity
    marker.request_sequence = 77;
    marker.effect_id = EffectId{9};
    marker.expected_effect_payload_digest.bytes[0] = std::byte{1};
    marker.expected_kill_epoch = KillEpoch{4};
    marker.expected_executor_generation = ExecutorGeneration{5};
    marker.catalog_root.bytes[0] = std::byte{0xee}; // deliberately not installed catalog
    marker.authority_domain = identity().authority_domain();
    std::array<std::byte, kAuthorityBarrierBytes> marker_bytes{};
    require(encode_authority_barrier(kAuthorityBarrierEntryType, marker, marker_bytes).error ==
                AuthorityBarrierCodecError::none, "marker fixture encode failed");

    const std::array<ArchiveRecordView, 1> records{{
        {8, 3, -17, kAuthorityBarrierEntryType, marker_bytes}}};
    RecordSourceContext record_source{records};
    std::array<std::byte, kArchiveSegmentHeaderBytes + kArchiveRecordHeaderBytes +
                              kAuthorityBarrierBytes + 2 * kDigestBytes> output{};
    ArchiveSegmentScratch scratch;
    const auto encoded = encode_archive_segment(source(record_source), 3, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::none, "historically admitted failed marker refused");
    VisitCapture seen;
    ArchiveSegmentMetadata metadata{};
    require(decode_archive_segment(output, identity(), capture, &seen, scratch, metadata).error ==
                ArchiveSegmentError::none && seen.calls == 1 && seen.entry_id == -17 &&
                seen.payload_size == marker_bytes.size() &&
                std::equal(marker_bytes.begin(), marker_bytes.end(), seen.payload.begin()),
            "failed authority attempt was filtered or changed");
}

void test_prevalidation_and_two_pass_refusal_are_nonmutating() {
    ArchiveSegmentScratch scratch;
    std::array<std::byte, 512> output{};
    output.fill(std::byte{0x5a});
    const auto before = output;
    const std::array<ArchiveRecordView, 1> bad{{{0, 1, 1, 0, command_bytes()}}};
    RecordSourceContext bad_source{bad};
    require(encode_archive_segment(source(bad_source), 0, identity(), output, scratch).error ==
                ArchiveSegmentError::invalid_coordinates && output == before,
            "encoder changed output before rejecting invalid coordinates");

    auto corrupt = segment_golden();
    corrupt[corrupt.size() - kDigestBytes - 1] ^= std::byte{1};
    VisitCapture seen;
    ArchiveSegmentMetadata destination{91, 92, 93, 94, 95, 96, 97, {}};
    const auto destination_before = destination;
    require(decode_archive_segment(corrupt, identity(), capture, &seen, scratch, destination).error ==
                ArchiveSegmentError::digest_mismatch && seen.calls == 0 &&
                destination == destination_before,
            "late corruption reached visitor or changed destination");

    seen.accept = false;
    require(decode_archive_segment(segment_golden(), identity(), capture, &seen, scratch,
                                   destination).error == ArchiveSegmentError::visitor_refused &&
                seen.calls == 1 && destination == destination_before,
            "visitor refusal changed decoded metadata or was rolled back dishonestly");
}

void test_lineage_caps_and_no_allocations() {
    ArchiveSegmentScratch scratch;
    auto foreign_value = identity_test::identity(1, {1, 2, 17},
        catalog_test::standard_catalog(), 0xbc);
    const auto foreign = identity_test::verified_identity(foreign_value);
    VisitCapture seen;
    ArchiveSegmentMetadata destination{};
    require(decode_archive_segment(segment_golden(), foreign, capture, &seen, scratch,
                                   destination).error == ArchiveSegmentError::foreign_manifest &&
                seen.calls == 0, "foreign archive lineage was accepted");

    std::vector<std::byte> oversized(kMaxArchiveSegmentBytes + 1, std::byte{});
    require(decode_archive_segment(oversized, identity(), nullptr, nullptr, scratch,
                                   destination).error == ArchiveSegmentError::capacity_exceeded,
            "archive byte cap was not enforced before parsing");

    const std::array<ArchiveRecordView, 1> records{{{7, 3, 7, 0, command_bytes()}}};
    RecordSourceContext record_source{records};
    std::array<std::byte, 512> encoded{};
    allocation_probe::count = 0; allocation_probe::enabled = true;
    const auto made = encode_archive_segment(source(record_source), 2, identity(), encoded, scratch);
    const auto read = decode_archive_segment(
        std::span<const std::byte>{encoded}.first(made.bytes_written), identity(),
        capture, &seen, scratch, destination);
    allocation_probe::enabled = false;
    require(made.error == ArchiveSegmentError::none && read.error == ArchiveSegmentError::none &&
                allocation_probe::count == 0, "archive codec allocated");
}

void test_record_count_cap_output_capacity_and_borrowed_source() {
    ArchiveSegmentScratch scratch;
    GeneratedSourceContext generated{1, command_bytes()};
    ArchiveRecordSource maximum{&generated, static_cast<std::uint32_t>(kMaxArchiveRecords),
                                generated_record_at};
    const auto required = kArchiveSegmentHeaderBytes + kArchiveDigestBytes +
        kMaxArchiveRecords * (kArchiveRecordHeaderBytes + command_bytes().size() +
                              kArchiveDigestBytes);
    std::vector<std::byte> output(required, std::byte{0xa7});
    const auto encoded = encode_archive_segment(maximum, 0, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::none && encoded.bytes_written == required &&
                generated.calls == 2 * kMaxArchiveRecords,
            "8192 borrowed records were not accepted in two passes");
    ArchiveSegmentMetadata metadata{};
    require(decode_archive_segment(output, identity(), nullptr, nullptr, scratch, metadata).error ==
                ArchiveSegmentError::none && metadata.record_count == kMaxArchiveRecords,
            "null visitor did not publish valid maximum-count metadata");

    std::array<std::byte, 256> unchanged{};
    unchanged.fill(std::byte{0x6d});
    const auto before = unchanged;
    ArchiveRecordSource excessive{&generated,
        static_cast<std::uint32_t>(kMaxArchiveRecords + 1), generated_record_at};
    generated.calls = 0;
    require(encode_archive_segment(excessive, 0, identity(), unchanged, scratch).error ==
                ArchiveSegmentError::capacity_exceeded && unchanged == before &&
                generated.calls == 0,
            "8193-record refusal touched source or output");

    const std::array<ArchiveRecordView, 1> one{{{1, 1, 1, 0, command_bytes()}}};
    RecordSourceContext one_source{one};
    require(encode_archive_segment(source(one_source), 0, identity(),
                                   std::span<std::byte>{unchanged}.first(1), scratch)
                .error == ArchiveSegmentError::output_too_small && unchanged == before,
            "output-too-small refusal changed output");
    RecordSourceContext bad_predecessor{one};
    require(encode_archive_segment(source(bad_predecessor), 1, identity(), unchanged, scratch).error ==
                ArchiveSegmentError::invalid_coordinates && unchanged == before,
            "index-one accepted a nonzero predecessor term");
}

void test_exact_byte_cap_and_mixed_records() {
    ArchiveSegmentScratch scratch;
    ExactCapSourceContext context;
    ArchiveRecordSource exact{&context, 2370, exact_cap_record_at};
    std::vector<std::byte> output(kMaxArchiveSegmentBytes, std::byte{0x91});
    const auto encoded = encode_archive_segment(exact, 0, identity(), output, scratch);
    require(encoded.error == ArchiveSegmentError::none &&
                encoded.bytes_written == kMaxArchiveSegmentBytes,
            "exact 4 MiB mixed archive was refused");
    ArchiveSegmentMetadata metadata{};
    require(decode_archive_segment(output, identity(), nullptr, nullptr, scratch, metadata).error ==
                ArchiveSegmentError::none && metadata.record_count == 2370,
            "exact 4 MiB mixed archive did not validate");

    output.assign(kMaxArchiveSegmentBytes, std::byte{0x53});
    const auto before = output;
    ArchiveRecordSource excessive{&context, 2371, exact_cap_record_at};
    require(encode_archive_segment(excessive, 0, identity(), output, scratch).error ==
                ArchiveSegmentError::capacity_exceeded && output == before,
            "one canonical record beyond 4 MiB changed output or was accepted");
}
}

int main() {
    try {
        test_independent_golden_and_decode();
        test_failed_authority_attempt_is_preserved();
        test_prevalidation_and_two_pass_refusal_are_nonmutating();
        test_lineage_caps_and_no_allocations();
        test_record_count_cap_output_capacity_and_borrowed_source();
        test_exact_byte_cap_and_mixed_records();
        std::cout << "archive segment tests passed\n";
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << "archive segment tests failed: " << error.what() << '\n';
        return 1;
    }
}
