#include "velocita/v2/solana_payload_view.hpp"
#include "velocita/v2/truemarkets_wire.hpp"

#include <algorithm>
#include <span>

namespace velocita::v2::truemarkets {
namespace {

std::size_t nonspace(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return c != ' ' && c != '\n' && c != '\r' && c != '\t';
    }));
}

template<std::size_t N>
bool exact_members(std::string_view object, const std::array<std::string_view, N>& keys) noexcept {
    if (validate_json_object(object) != JsonError::none) return false;
    std::size_t size = 2 + N - 1;
    for (const auto key : keys) {
        const auto value = json_member_raw(object, key);
        if (!value) return false;
        size += key.size() + 3 + nonspace(*value);
    }
    return nonspace(object) == size;
}

bool public_key(std::string_view text, std::array<std::uint8_t, 32>& out) noexcept {
    constexpr std::string_view alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (text.empty() || text.size() > 44) return false;
    out.fill(0);
    for (const char c : text) {
        const auto digit = alphabet.find(c);
        if (digit == std::string_view::npos) return false;
        unsigned carry = static_cast<unsigned>(digit);
        for (std::size_t i = out.size(); i-- > 0;) {
            carry += 58U * out[i];
            out[i] = static_cast<std::uint8_t>(carry);
            carry >>= 8U;
        }
        if (carry != 0) return false;
    }
    const auto leading_text = text.find_first_not_of('1');
    const auto zero_bytes = static_cast<std::size_t>(std::find_if(out.begin(), out.end(),
        [](auto x) { return x != 0; }) - out.begin());
    return (leading_text == std::string_view::npos ? text.size() : leading_text) == zero_bytes;
}

int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position{};
    bool take(std::size_t count, std::uint16_t& offset) noexcept {
        if (count > bytes.size() - position) return false;
        offset = static_cast<std::uint16_t>(position);
        position += count;
        return true;
    }
    bool byte(std::uint8_t& value) noexcept {
        std::uint16_t at{};
        if (!take(1, at)) return false;
        value = bytes[at];
        return true;
    }
    bool compact(std::uint16_t& value) noexcept {
        std::uint32_t result{};
        for (unsigned i = 0; i < 3; ++i) {
            std::uint8_t b{};
            if (!byte(b)) return false;
            result |= static_cast<std::uint32_t>(b & 127U) << (7U * i);
            if ((b & 128U) == 0) {
                if (result > 65535 || (i != 0 && result < (1U << (7U * i)))) return false;
                value = static_cast<std::uint16_t>(result);
                return true;
            }
        }
        return false;
    }
};

SolanaParseError transaction(DecodedSolanaPayload& v,
                             const std::array<std::uint8_t, 32>& signer) noexcept {
    Reader r{{v.transaction.data(), v.transaction_size}};
    std::uint16_t signatures{}, offset{};
    if (!r.compact(signatures) || signatures == 0 || signatures > 12 ||
        !r.take(signatures * 64U, offset)) return SolanaParseError::transaction;
    v.signature_count = static_cast<std::uint8_t>(signatures);
    for (std::size_t i = 0; i < signatures; ++i)
        if (std::any_of(v.transaction.begin() + offset + i * 64,
                        v.transaction.begin() + offset + (i + 1) * 64,
                        [](auto b) { return b != 0; })) ++v.nonzero_signatures;
    std::uint8_t first{}, readonly_signers{}, readonly_others{};
    if (!r.byte(first)) return SolanaParseError::transaction;
    if ((first & 128U) != 0) {
        if (first != 128) return SolanaParseError::unsupported_version;
        v.version_zero = true;
        if (!r.byte(first)) return SolanaParseError::transaction;
    }
    v.required_signatures = first;
    if (first != signatures || !r.byte(readonly_signers) || !r.byte(readonly_others) ||
        readonly_signers >= first || !r.compact(v.static_key_count) ||
        v.static_key_count < first || v.static_key_count > 256 ||
        readonly_others > v.static_key_count - first ||
        !r.take(v.static_key_count * 32U, v.static_keys_offset) ||
        !r.take(32, v.blockhash_offset)) return SolanaParseError::transaction;
    bool found{};
    for (std::size_t i = 0; i < v.static_key_count; ++i) {
        const auto key = v.transaction.begin() + v.static_keys_offset + i * 32;
        for (std::size_t j = 0; j < i; ++j)
            if (std::equal(key, key + 32, v.transaction.begin() + v.static_keys_offset + j * 32))
                return SolanaParseError::transaction;
        if (i < first && std::equal(key, key + 32, signer.begin())) {
            found = true;
            v.signer_index = static_cast<std::uint8_t>(i);
        }
    }
    if (!found) return SolanaParseError::identity;
    if (!r.compact(v.instruction_count)) return SolanaParseError::transaction;
    if (v.instruction_count > v.instructions.size()) return SolanaParseError::capacity;
    for (std::size_t i = 0; i < v.instruction_count; ++i) {
        auto& instruction = v.instructions[i];
        if (!r.byte(instruction.program_index) || !r.compact(instruction.accounts_count) ||
            !r.take(instruction.accounts_count, instruction.accounts_offset) ||
            !r.compact(instruction.data_size) || !r.take(instruction.data_size, instruction.data_offset))
            return SolanaParseError::transaction;
    }
    if (v.version_zero) {
        if (!r.compact(v.lookup_count)) return SolanaParseError::transaction;
        if (v.lookup_count > v.lookups.size()) return SolanaParseError::capacity;
        for (std::size_t i = 0; i < v.lookup_count; ++i) {
            auto& table = v.lookups[i];
            if (!r.take(32, table.key_offset) || !r.compact(table.writable_count) ||
                !r.take(table.writable_count, table.writable_offset) ||
                !r.compact(table.readonly_count) || !r.take(table.readonly_count, table.readonly_offset))
                return SolanaParseError::transaction;
            if (table.writable_count == 0 && table.readonly_count == 0) return SolanaParseError::transaction;
            const auto count = static_cast<unsigned>(v.loaded_key_count) + table.writable_count + table.readonly_count;
            if (count + v.static_key_count > 256) return SolanaParseError::capacity;
            v.loaded_key_count = static_cast<std::uint16_t>(count);
        }
    }
    if (r.position != r.bytes.size()) return SolanaParseError::transaction;
    const auto total = v.static_key_count + v.loaded_key_count;
    for (std::size_t i = 0; i < v.instruction_count; ++i) {
        const auto& instruction = v.instructions[i];
        if (instruction.program_index == 0 || instruction.program_index >= v.static_key_count)
            return SolanaParseError::transaction;
        for (std::size_t j = 0; j < instruction.accounts_count; ++j)
            if (v.transaction[instruction.accounts_offset + j] >= total) return SolanaParseError::transaction;
    }
    return SolanaParseError::none;
}

}

SolanaParseError decode_solana_signing_envelope(
    std::string_view payload, std::string_view expected_organization,
    std::string_view expected_sign_with, DecodedSolanaPayload& out) noexcept {
    out = {};
    if (payload.size() > kMaxSolanaEnvelope) return SolanaParseError::capacity;
    constexpr std::array<std::string_view, 4> keys{"organizationId", "parameters", "timestampMs", "type"};
    constexpr std::array<std::string_view, 3> parameter_keys{"signWith", "type", "unsignedTransaction"};
    if (!exact_members(payload, keys) ||
        json_member_string(payload, "type") != "ACTIVITY_TYPE_SIGN_TRANSACTION_V2") return SolanaParseError::envelope;
    const auto parameters = json_member_raw(payload, "parameters");
    if (!parameters || !exact_members(*parameters, parameter_keys) ||
        json_member_string(*parameters, "type") != "TRANSACTION_TYPE_SOLANA") return SolanaParseError::envelope;
    if (expected_organization.empty() || expected_organization.size() > 96 ||
        json_member_string(payload, "organizationId") != expected_organization ||
        json_member_string(*parameters, "signWith") != expected_sign_with) return SolanaParseError::identity;
    std::array<std::uint8_t, 32> signer{};
    if (!public_key(expected_sign_with, signer)) return SolanaParseError::identity;
    const auto timestamp_text = json_member_string(payload, "timestampMs");
    const auto timestamp = timestamp_text ? parse_decimal_atoms(*timestamp_text, 0) : std::nullopt;
    if (!timestamp || *timestamp <= 0) return SolanaParseError::envelope;
    const auto hex = json_member_string(*parameters, "unsignedTransaction");
    if (!hex || hex->empty() || hex->size() % 2 != 0) return SolanaParseError::hex;
    if (hex->size() / 2 > kMaxSolanaTransaction) return SolanaParseError::capacity;
    DecodedSolanaPayload value;
    value.timestamp_ms = *timestamp;
    value.transaction_size = static_cast<std::uint16_t>(hex->size() / 2);
    for (std::size_t i = 0; i < value.transaction_size; ++i) {
        const auto high = hex_digit((*hex)[i * 2]), low = hex_digit((*hex)[i * 2 + 1]);
        if (high < 0 || low < 0) return SolanaParseError::hex;
        value.transaction[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    if (const auto error = transaction(value, signer); error != SolanaParseError::none) return error;
    value.original_size = static_cast<std::uint16_t>(payload.size());
    std::copy(payload.begin(), payload.end(), value.original.begin());
    out = value;
    return SolanaParseError::none;
}

}
