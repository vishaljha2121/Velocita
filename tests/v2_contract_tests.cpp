#include "velocita/v2/domain.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/wire.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Left, typename Right>
concept CheckedAddable = requires(Left left, Right right) {
    left.checked_add(right);
};

std::vector<std::byte> bytes_from_hex(const std::string& hex) {
    if ((hex.size() % 2U) != 0U) throw std::runtime_error("odd test hex length");
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2U);
    for (std::size_t offset = 0; offset < hex.size(); offset += 2U) {
        const auto value = static_cast<unsigned long>(std::stoul(hex.substr(offset, 2), nullptr, 16));
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

void overwrite_checksum(std::vector<std::byte>& bytes) {
    const auto checksum = velocita::v2::crc32_checksum(
        std::span<const std::byte>{bytes}.first(bytes.size() -
                                                 velocita::v2::kEnvelopeChecksumBytes));
    const auto offset = bytes.size() - velocita::v2::kEnvelopeChecksumBytes;
    for (std::size_t index = 0; index < sizeof(checksum); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((checksum >> (index * 8U)) & 0xFFU);
    }
}

void test_strong_ids() {
    using namespace velocita::v2;

    static_assert(std::is_trivially_copyable_v<CallerId>);
    static_assert(std::is_trivially_destructible_v<EffectId>);
    static_assert(!std::is_convertible_v<std::uint64_t, CallerId>);
    static_assert(!std::is_convertible_v<CallerId, std::uint64_t>);
    static_assert(!std::is_constructible_v<EffectId, CallerId>);
    static_assert(!std::is_assignable_v<EffectId&, CallerId>);

    constexpr CallerId caller{17};
    constexpr CallerId same{17};
    constexpr CallerId other{18};
    static_assert(caller.value() == 17);
    static_assert(caller == same);
    static_assert(caller != other);
}

struct UsdcTag;
struct PriceTag;
using UsdcMicros = velocita::v2::FixedPoint<UsdcTag, 1'000'000>;
using UsdcCents = velocita::v2::FixedPoint<UsdcTag, 100>;
using PriceMicros = velocita::v2::FixedPoint<PriceTag, 1'000'000>;

void test_checked_fixed_point() {
    using velocita::v2::RoundingMode;

    static_assert(UsdcMicros::scale == 1'000'000);
    static_assert(!CheckedAddable<UsdcMicros, PriceMicros>);
    static_assert(!CheckedAddable<UsdcMicros, UsdcCents>);

    constexpr auto one = UsdcMicros::from_atoms(1'000'000);
    constexpr auto half = UsdcMicros::from_atoms(500'000);
    constexpr auto sum = one.checked_add(half);
    static_assert(sum.has_value() && sum->atoms() == 1'500'000);

    constexpr auto difference = one.checked_sub(half);
    static_assert(difference.has_value() && difference->atoms() == 500'000);

    constexpr auto product = half.checked_multiply(3);
    static_assert(product.has_value() && product->atoms() == 1'500'000);

    constexpr auto maximum = UsdcMicros::from_atoms(std::numeric_limits<std::int64_t>::max());
    constexpr auto minimum = UsdcMicros::from_atoms(std::numeric_limits<std::int64_t>::min());
    static_assert(!maximum.checked_add(UsdcMicros::from_atoms(1)).has_value());
    static_assert(!minimum.checked_sub(UsdcMicros::from_atoms(1)).has_value());
    static_assert(!maximum.checked_multiply(2).has_value());
    static_assert(!minimum.checked_multiply(-1).has_value());

    constexpr auto positive_half_cent = UsdcMicros::from_atoms(1'235'000);
    constexpr auto negative_half_cent = UsdcMicros::from_atoms(-1'235'000);
    constexpr auto exact_cent = UsdcMicros::from_atoms(1'230'000);

    constexpr auto nearest_positive =
        positive_half_cent.rescale<100>(RoundingMode::nearest_ties_away_from_zero);
    constexpr auto nearest_negative =
        negative_half_cent.rescale<100>(RoundingMode::nearest_ties_away_from_zero);
    constexpr auto toward_zero = positive_half_cent.rescale<100>(RoundingMode::toward_zero);
    constexpr auto away_from_zero = positive_half_cent.rescale<100>(RoundingMode::away_from_zero);
    constexpr auto exact = exact_cent.rescale<100>(RoundingMode::nearest_ties_away_from_zero);

    static_assert(nearest_positive.has_value() && nearest_positive->atoms() == 124);
    static_assert(nearest_negative.has_value() && nearest_negative->atoms() == -124);
    static_assert(toward_zero.has_value() && toward_zero->atoms() == 123);
    static_assert(away_from_zero.has_value() && away_from_zero->atoms() == 124);
    static_assert(exact.has_value() && exact->atoms() == 123);

    constexpr auto cents = UsdcCents::from_atoms(123);
    constexpr auto micros = cents.rescale<1'000'000>(RoundingMode::toward_zero);
    static_assert(micros.has_value() && micros->atoms() == 1'230'000);

    require(sum->atoms() == 1'500'000, "fixed-point runtime result is wrong");
}

velocita::v2::Envelope sample_envelope() {
    using namespace velocita::v2;
    return {
        MessageKind::activate_policy,
        0,
        CallerId{1},
        RequestSequence{2},
        CorrelationId{3},
        SourceId{4},
        SourceGeneration{5},
        SourceSequence{6},
        -7,
        8,
        {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}},
    };
}

void require_same_envelope(const velocita::v2::Envelope& left,
                           const velocita::v2::Envelope& right) {
    require(left.kind == right.kind && left.flags == right.flags &&
                left.caller_id == right.caller_id &&
                left.request_sequence == right.request_sequence &&
                left.correlation_id == right.correlation_id &&
                left.source_id == right.source_id &&
                left.source_generation == right.source_generation &&
                left.source_sequence == right.source_sequence &&
                left.event_time_ns == right.event_time_ns &&
                left.receive_time_ns == right.receive_time_ns &&
                left.payload == right.payload,
            "V2 envelope round trip changed a field");
}

void test_v2_envelope_codec() {
    using namespace velocita::v2;

    const auto envelope = sample_envelope();
    const auto encoded = encode_envelope(envelope);
    require(encoded.error == EnvelopeError::none, "valid V2 envelope did not encode");
    require(encoded.bytes.size() == kEnvelopeFixedBytes + envelope.payload.size(),
            "V2 envelope encoded size is wrong");

    const auto expected = bytes_from_hex(
        "564c543201000200000000000300000001000000000000000200000000000000"
        "0300000000000000040000000000000005000000000000000600000000000000"
        "f9ffffffffffffff0800000000000000aabbccf09191e5");
    require(encoded.bytes == expected, "V2 golden envelope bytes changed");

    const auto decoded = decode_envelope(encoded.bytes);
    require(decoded.error == EnvelopeError::none && decoded.envelope.has_value(),
            "valid V2 envelope did not decode");
    require_same_envelope(envelope, *decoded.envelope);

    auto malformed = encoded.bytes;
    malformed.pop_back();
    require(decode_envelope(malformed).error == EnvelopeError::truncated,
            "truncated V2 envelope was accepted or misclassified");

    malformed = encoded.bytes;
    malformed.push_back(std::byte{0});
    require(decode_envelope(malformed).error == EnvelopeError::length_mismatch,
            "V2 envelope trailing bytes were accepted or misclassified");

    malformed = encoded.bytes;
    malformed[0] = std::byte{0};
    require(decode_envelope(malformed).error == EnvelopeError::invalid_magic,
            "invalid V2 envelope magic was accepted");

    malformed = encoded.bytes;
    malformed[4] = std::byte{2};
    require(decode_envelope(malformed).error == EnvelopeError::unsupported_version,
            "unsupported V2 envelope version was accepted");

    malformed = encoded.bytes;
    malformed[6] = std::byte{0xFF};
    malformed[7] = std::byte{0x7F};
    require(decode_envelope(malformed).error == EnvelopeError::unknown_message_kind,
            "unknown V2 message kind was accepted");

    malformed = encoded.bytes;
    malformed[8] = std::byte{1};
    require(decode_envelope(malformed).error == EnvelopeError::unsupported_flags,
            "unsupported V2 envelope flags were accepted");

    malformed = encoded.bytes;
    malformed[kEnvelopeHeaderBytes] ^= std::byte{1};
    require(decode_envelope(malformed).error == EnvelopeError::checksum_mismatch,
            "corrupt V2 envelope payload was accepted");

    auto invalid_identity = envelope;
    invalid_identity.caller_id = CallerId{0};
    require(encode_envelope(invalid_identity).error == EnvelopeError::invalid_identity,
            "zero V2 caller identity was encoded");

    auto oversized = envelope;
    oversized.payload.resize(kMaxEnvelopePayloadBytes + 1U);
    require(encode_envelope(oversized).error == EnvelopeError::payload_too_large,
            "oversized V2 envelope payload was encoded");
}

void test_initial_message_registry_and_fail_closed_lengths() {
    using namespace velocita::v2;

    constexpr std::array initial_kinds{
        MessageKind::publish_policy,
        MessageKind::activate_policy,
        MessageKind::supersede_policy,
        MessageKind::evaluate_decision_frame,
        MessageKind::set_strategy_mode,
        MessageKind::advance_kill_epoch,
        MessageKind::set_executor_generation,
        MessageKind::observe_order_ack,
        MessageKind::observe_order_reject,
        MessageKind::observe_fill,
        MessageKind::observe_cancel,
        MessageKind::observe_effect_unknown,
        MessageKind::apply_reconciliation,
        MessageKind::command_result,
        MessageKind::order_effect,
    };

    for (const auto kind : initial_kinds) {
        auto envelope = sample_envelope();
        envelope.kind = kind;
        const auto encoded = encode_envelope(envelope);
        require(encoded.error == EnvelopeError::none,
                "registered initial V2 message kind did not encode");
        const auto decoded = decode_envelope(encoded.bytes);
        require(decoded.error == EnvelopeError::none && decoded.envelope.has_value() &&
                    decoded.envelope->kind == kind,
                "registered initial V2 message kind did not round trip");
    }

    auto malformed = encode_envelope(sample_envelope()).bytes;
    malformed[12] = std::byte{0x01};
    malformed[13] = std::byte{0x00};
    malformed[14] = std::byte{0x01};
    malformed[15] = std::byte{0x00};
    require(decode_envelope(malformed).error == EnvelopeError::payload_too_large,
            "declared oversized V2 payload was not rejected before allocation");

    malformed = encode_envelope(sample_envelope()).bytes;
    for (std::size_t index = 16; index < 24; ++index) malformed[index] = std::byte{0};
    overwrite_checksum(malformed);
    require(decode_envelope(malformed).error == EnvelopeError::invalid_identity,
            "zero caller identity in checksummed bytes was accepted");
}

void test_checksum_contract() {
    using namespace velocita::v2;
    const auto bytes = bytes_from_hex("313233343536373839");
    require(crc32_checksum(bytes) == 0xCBF43926U,
            "CRC-32 canonical check value changed");
}

void test_sha256_contract() {
    using namespace velocita::v2;

    const std::vector<std::byte> empty;
    require(to_hex(sha256_digest(empty)) ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-message known-answer test changed");

    const auto abc = bytes_from_hex("616263");
    const auto digest = sha256_digest(abc);
    require(to_hex(digest) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc known-answer test changed");
    require(parse_digest256(to_hex(digest)).has_value() &&
                *parse_digest256(to_hex(digest)) == digest,
            "SHA-256 canonical hex did not round trip");
    require(!parse_digest256("abc").has_value(),
            "invalid digest hex length was accepted");
}

}

int main() {
    try {
        test_strong_ids();
        test_checked_fixed_point();
        test_v2_envelope_codec();
        test_initial_message_registry_and_fail_closed_lengths();
        test_checksum_contract();
        test_sha256_contract();
        std::cout << "all V2 contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 contract test failed: " << error.what() << '\n';
        return 1;
    }
}
