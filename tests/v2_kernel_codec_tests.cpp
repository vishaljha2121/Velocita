#include "velocita/v2/digest.hpp"
#include "velocita/v2/kernel_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

velocita::v2::Digest256 digest(const std::uint8_t marker) {
    velocita::v2::Digest256 value;
    value.bytes[0] = static_cast<std::byte>(marker);
    return value;
}

velocita::v2::PolicyArtifact policy() {
    using namespace velocita::v2;
    PolicyArtifact value;
    value.policy_id = PolicyId{17};
    value.policy_version = PolicyVersion{2};
    value.schema_version = kTradingContractSchemaVersion;
    value.feature_schema_id = FeatureSchemaId{3};
    value.instrument_id = InstrumentId{4};
    value.account_id = AccountId{5};
    value.model_digest = digest(0x11);
    value.artifact_digest = digest(0x22);
    value.valid_from_event_time_ns = 100;
    value.valid_until_event_time_ns = 200;
    value.risk_policy_version = RiskPolicyVersion{6};
    value.rule_count = 1;
    value.rules[0] = PolicyRule{7,
                                PredicateOperator::greater_than_or_equal,
                                FeatureMicros::from_atoms(-1'250'000),
                                Directive::place_limit};
    return value;
}

velocita::v2::Command command(const std::uint64_t sequence,
                              velocita::v2::CommandBody body) {
    using namespace velocita::v2;
    return Command{CommandIdentity{CallerId{12}, RequestSequence{sequence}, digest(0x44)},
                   std::move(body)};
}

void write_u16(std::span<std::byte> bytes, const std::size_t offset,
               const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void replace_command_digest(std::span<std::byte> frame) {
    using namespace velocita::v2;
    const auto digest_offset = frame.size() - kDigestBytes;
    const auto calculated = sha256_digest(frame.subspan(
        kKernelCommandDigestInputOffset,
        digest_offset - kKernelCommandDigestInputOffset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              frame.begin() + static_cast<std::ptrdiff_t>(digest_offset));
}

void test_every_command_alternative_round_trips_canonically() {
    using namespace velocita::v2;
    const std::array commands{
        command(1, PublishPolicy{policy()}),
        command(2, ValidatePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(3, ActivatePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(4, SupersedePolicy{PolicyId{17}, PolicyVersion{2}}),
        command(5, SetStrategyMode{StrategyMode::cancel_only, KillEpoch{4},
                                   ExecutorGeneration{8}}),
        command(6, AdvanceKillEpoch{KillEpoch{5}}),
        command(7, SetExecutorGeneration{ExecutorGeneration{9}}),
        command(8, RecordSourceEvent{SourceId{10}, SourceGeneration{2},
                                     SourceSequence{3}, digest(0x55)}),
    };

    for (const auto& original : commands) {
        std::array<std::byte, kMaxKernelCommandBytes> bytes{};
        const auto encoded = encode_kernel_command(original, bytes);
        require(encoded.error == KernelCodecError::none,
                "valid command did not encode");

        Command restored;
        KernelCommandCodecScratch scratch;
        const auto decoded = decode_kernel_command(
            {bytes.data(), encoded.bytes_written}, restored, scratch);
        require(decoded.error == KernelCodecError::none,
                "canonical command did not decode");

        std::array<std::byte, kMaxKernelCommandBytes> round_trip{};
        const auto reencoded = encode_kernel_command(restored, round_trip);
        require(reencoded.error == KernelCodecError::none &&
                    reencoded.bytes_written == encoded.bytes_written &&
                    std::equal(bytes.begin(),
                               bytes.begin() + static_cast<std::ptrdiff_t>(
                                                   encoded.bytes_written),
                               round_trip.begin()),
                "command round trip changed canonical bytes");
    }
}

void test_command_identity_and_kind_are_explicit() {
    using namespace velocita::v2;
    const auto original = command(
        42, RecordSourceEvent{SourceId{10}, SourceGeneration{2}, SourceSequence{3},
                              digest(0x55)});
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(original, bytes);
    require(encoded.error == KernelCodecError::none,
            "source command fixture did not encode");

    const std::array expected_prefix{
        std::byte{'V'}, std::byte{'2'}, std::byte{'K'}, std::byte{'C'},
        std::byte{104}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{1}, std::byte{0},
        std::byte{8}, std::byte{0},
        std::byte{12}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{42}};
    require(std::equal(expected_prefix.begin(), expected_prefix.end(), bytes.begin()),
            "command framing, kind, or identity order changed");
    require(to_hex(encoded.frame_digest) ==
                "ad06f4bf1ab44345dcd26e02c9dd6ba31217ba633cf0ec4bc859f76d856bb894" &&
                to_hex(sha256_digest({bytes.data(), encoded.bytes_written})) ==
                    "8d08b0a23e6918b4bc8ec6a373a7e6b8a7e9d1128b394254ee757fd94894a5db",
            "source-command golden corpus changed without schema migration");

    Command restored;
    KernelCommandCodecScratch scratch;
    const auto decoded = decode_kernel_command(
        {bytes.data(), encoded.bytes_written}, restored, scratch);
    require(decoded.error == KernelCodecError::none &&
                restored.identity == original.identity &&
                std::holds_alternative<RecordSourceEvent>(restored.body),
            "command identity or kind was not preserved");
}

void test_result_round_trip_is_canonical() {
    using namespace velocita::v2;
    const ApplyResult original{ApplyCode::source_sequence_gap,
                               ContractError::none,
                               PolicyId{17},
                               PolicyVersion{2},
                               PolicyLifecycle::active,
                               99,
                               StrategyMode::reduce_only,
                               KillEpoch{4},
                               ExecutorGeneration{8},
                               SourceId{10},
                               SourceGeneration{2},
                               SourceSequence{3}};
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(original, bytes);
    require(encoded.error == KernelCodecError::none &&
                encoded.bytes_written == kKernelResultBytes,
            "valid result did not encode to its fixed size");

    ApplyResult restored;
    const auto decoded = decode_kernel_result(
        {bytes.data(), encoded.bytes_written}, restored);
    require(decoded.error == KernelCodecError::none && restored == original,
            "canonical result did not round trip exactly");
    require(to_hex(encoded.frame_digest) ==
                "4d323608f11d183b48f5f202a55560194dd1d06735091243b1251b2920a01cab" &&
                to_hex(sha256_digest(bytes)) ==
                    "0d592606abcfb67db92426ffd8e661c6ffa83563aa27d1a9d9fd11968a2b9ff4",
            "apply-result golden corpus changed without schema migration");
}

void test_result_decode_is_transactional_and_fail_closed() {
    using namespace velocita::v2;
    const ApplyResult original{ApplyCode::applied,
                               ContractError::none,
                               {}, {}, PolicyLifecycle::none, 7,
                               StrategyMode::halted, {}, {}, {}, {}, {}};
    std::array<std::byte, kKernelResultBytes> bytes{};
    const auto encoded = encode_kernel_result(original, bytes);
    require(encoded.error == KernelCodecError::none, "result fixture did not encode");

    const ApplyResult sentinel{ApplyCode::policy_not_found,
                               ContractError::none,
                               PolicyId{99}, PolicyVersion{3},
                               PolicyLifecycle::none, 8,
                               StrategyMode::cancel_only, KillEpoch{2},
                               ExecutorGeneration{4}, {}, {}, {}};

    auto reject_without_publish = [&](const KernelCodecError expected,
                                       std::span<const std::byte> candidate,
                                       const std::string& context) {
        auto destination = sentinel;
        const auto result = decode_kernel_result(candidate, destination);
        require(result.error == expected && destination == sentinel,
                context + " was misclassified or partially published");
    };

    auto corrupt = bytes;
    corrupt[20] ^= std::byte{1};
    reject_without_publish(KernelCodecError::digest_mismatch, corrupt,
                           "result corruption");
    reject_without_publish(KernelCodecError::truncated,
                           std::span<const std::byte>{bytes}.first(bytes.size() - 1),
                           "result truncation");

    auto invalid_enum = bytes;
    invalid_enum[kKernelResultPayloadOffset] = std::byte{0xff};
    const auto digest_offset = invalid_enum.size() - kDigestBytes;
    const auto calculated = sha256_digest(std::span<const std::byte>{invalid_enum}.subspan(
        kKernelResultDigestInputOffset,
        digest_offset - kKernelResultDigestInputOffset));
    std::copy(calculated.bytes.begin(), calculated.bytes.end(),
              invalid_enum.begin() + static_cast<std::ptrdiff_t>(digest_offset));
    reject_without_publish(KernelCodecError::invalid_value, invalid_enum,
                           "unknown result enum");
}

void test_command_decode_is_transactional_and_fail_closed() {
    using namespace velocita::v2;
    std::array<std::byte, kMaxKernelCommandBytes> bytes{};
    const auto encoded = encode_kernel_command(command(1, PublishPolicy{policy()}), bytes);
    require(encoded.error == KernelCodecError::none, "fixture did not encode");

    const auto sentinel = command(999, AdvanceKillEpoch{KillEpoch{7}});
    std::array<std::byte, kMaxKernelCommandBytes> sentinel_bytes{};
    const auto sentinel_encoded = encode_kernel_command(sentinel, sentinel_bytes);
    KernelCommandCodecScratch scratch;

    auto reject_without_publish = [&](const KernelCodecError expected,
                                       std::span<const std::byte> candidate,
                                       const std::string& context) {
        auto destination = sentinel;
        const auto result = decode_kernel_command(candidate, destination, scratch);
        require(result.error == expected, context + " returned wrong error");
        std::array<std::byte, kMaxKernelCommandBytes> after{};
        const auto after_encoded = encode_kernel_command(destination, after);
        require(after_encoded.error == KernelCodecError::none &&
                    after_encoded.bytes_written == sentinel_encoded.bytes_written &&
                    std::equal(sentinel_bytes.begin(),
                               sentinel_bytes.begin() + static_cast<std::ptrdiff_t>(
                                                            sentinel_encoded.bytes_written),
                               after.begin()),
                context + " partially published a command");
    };

    auto corrupt = bytes;
    corrupt[30] ^= std::byte{1};
    reject_without_publish(KernelCodecError::digest_mismatch,
                           {corrupt.data(), encoded.bytes_written}, "corruption");
    reject_without_publish(KernelCodecError::truncated,
                           {bytes.data(), encoded.bytes_written - 1}, "truncation");

    auto trailing = bytes;
    trailing[encoded.bytes_written] = std::byte{0};
    reject_without_publish(KernelCodecError::length_mismatch,
                           {trailing.data(), encoded.bytes_written + 1},
                           "trailing byte");

    auto unknown_schema = bytes;
    write_u16(unknown_schema, kKernelCommandSchemaOffset, 2);
    replace_command_digest({unknown_schema.data(), encoded.bytes_written});
    reject_without_publish(KernelCodecError::unsupported_schema,
                           {unknown_schema.data(), encoded.bytes_written},
                           "unknown schema");

    auto unknown_kind = bytes;
    write_u16(unknown_kind, kKernelCommandKindOffset, 99);
    replace_command_digest({unknown_kind.data(), encoded.bytes_written});
    reject_without_publish(KernelCodecError::unknown_kind,
                           {unknown_kind.data(), encoded.bytes_written},
                           "unknown command kind");
}

void test_encode_bounds_and_invalid_values() {
    using namespace velocita::v2;
    const auto valid = command(1, PublishPolicy{policy()});
    std::array<std::byte, 16> too_small{};
    require(encode_kernel_command(valid, too_small).error ==
                KernelCodecError::output_too_small,
            "undersized command output was accepted");

    auto invalid_identity = valid;
    invalid_identity.identity.caller_id = CallerId{};
    std::array<std::byte, kMaxKernelCommandBytes> command_bytes{};
    require(encode_kernel_command(invalid_identity, command_bytes).error ==
                KernelCodecError::invalid_value,
            "missing command identity was encoded");

    const ApplyResult invalid_result{static_cast<ApplyCode>(255)};
    std::array<std::byte, kKernelResultBytes> result_bytes{};
    require(encode_kernel_result(invalid_result, result_bytes).error ==
                KernelCodecError::invalid_value,
            "unknown result enum was encoded");
}

}  // namespace

int main() {
    try {
        test_every_command_alternative_round_trips_canonically();
        test_command_identity_and_kind_are_explicit();
        test_result_round_trip_is_canonical();
        test_result_decode_is_transactional_and_fail_closed();
        test_command_decode_is_transactional_and_fail_closed();
        test_encode_bounds_and_invalid_values();
        std::cout << "all V2 kernel codec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 kernel codec test failed: " << error.what() << '\n';
        return 1;
    }
}
