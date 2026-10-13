#pragma once

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/solana_payload_view.hpp"

#include <span>

namespace velocita::v2::truemarkets {

using SolanaAccountKey = std::array<std::uint8_t, 32>;

struct SolanaLookupEvidence {
    SolanaAccountKey table{};
    std::uint64_t observed_slot{};
    Digest256 evidence_digest{};
    std::uint16_t address_count{};
    std::array<SolanaAccountKey, 256> addresses{};
};

struct ResolvedSolanaAccount {
    SolanaAccountKey key{};
    bool signer{}, writable{};
    bool operator==(const ResolvedSolanaAccount&) const = default;
};

struct ResolvedSolanaLookup {
    SolanaAccountKey table{};
    std::uint64_t observed_slot{};
    Digest256 evidence_digest{}, addresses_digest{};
    std::uint16_t address_count{};
    bool operator==(const ResolvedSolanaLookup&) const = default;
};

struct ResolvedSolanaAccounts {
    Digest256 transaction_digest{};
    std::uint16_t account_count{}, lookup_count{};
    std::array<ResolvedSolanaAccount, 256> accounts{};
    std::array<ResolvedSolanaLookup, 32> lookups{};
    bool operator==(const ResolvedSolanaAccounts&) const = default;
};

enum class SolanaResolutionError : std::uint8_t {
    none, invalid_payload, decoded_mismatch, capacity, evidence_count,
    invalid_evidence, table_mismatch, duplicate_table, lookup_index, account_alias
};

[[nodiscard]] SolanaResolutionError resolve_solana_accounts(
    const DecodedSolanaPayload& decoded, std::string_view expected_organization,
    std::string_view expected_sign_with, std::span<const SolanaLookupEvidence> evidence,
    ResolvedSolanaAccounts& out) noexcept;

}
