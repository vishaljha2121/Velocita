#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace velocita::v2::truemarkets {

inline constexpr std::size_t kMaxSolanaEnvelope = 8192;
inline constexpr std::size_t kMaxSolanaTransaction = 1232;

enum class SolanaParseError : std::uint8_t {
    none, envelope, identity, hex, unsupported_version, transaction, capacity
};

struct SolanaInstructionView {
    std::uint8_t program_index{};
    std::uint16_t accounts_offset{}, accounts_count{}, data_offset{}, data_size{};
    bool operator==(const SolanaInstructionView&) const = default;
};

struct SolanaLookupView {
    std::uint16_t key_offset{}, writable_offset{}, writable_count{}, readonly_offset{}, readonly_count{};
    bool operator==(const SolanaLookupView&) const = default;
};

struct DecodedSolanaPayload {
    std::array<char, kMaxSolanaEnvelope> original{};
    std::array<std::uint8_t, kMaxSolanaTransaction> transaction{};
    std::uint16_t original_size{}, transaction_size{};
    std::uint16_t static_keys_offset{}, static_key_count{}, blockhash_offset{};
    std::uint16_t instruction_count{}, lookup_count{}, loaded_key_count{};
    std::uint8_t signature_count{}, required_signatures{}, signer_index{};
    std::uint8_t nonzero_signatures{};
    bool version_zero{};
    std::int64_t timestamp_ms{};
    std::array<SolanaInstructionView, 64> instructions{};
    std::array<SolanaLookupView, 32> lookups{};
    [[nodiscard]] bool needs_lookup_resolution() const noexcept { return lookup_count != 0; }
    [[nodiscard]] std::string_view original_payload() const noexcept { return {original.data(), original_size}; }
    bool operator==(const DecodedSolanaPayload&) const = default;
};

[[nodiscard]] SolanaParseError decode_solana_signing_envelope(
    std::string_view payload, std::string_view expected_organization,
    std::string_view expected_sign_with, DecodedSolanaPayload& out) noexcept;

}
