#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>

namespace velocita::v2::truemarkets {
namespace {
SolanaAccountKey read_key(const DecodedSolanaPayload& payload, std::size_t offset) noexcept {
    SolanaAccountKey key{};
    std::copy_n(payload.transaction.begin() + offset, key.size(), key.begin());
    return key;
}
Digest256 address_digest(const SolanaLookupEvidence& evidence) noexcept {
    const std::array<std::byte, 2> count{static_cast<std::byte>(evidence.address_count & 255U),
                                       static_cast<std::byte>(evidence.address_count >> 8U)};
    Sha256 hash;
    hash.update(count);
    for (std::size_t i = 0; i < evidence.address_count; ++i)
        hash.update(std::as_bytes(std::span{evidence.addresses[i]}));
    return hash.finish();
}
SolanaResolutionError append(ResolvedSolanaAccounts& result, SolanaAccountKey key,
                             bool signer, bool writable) noexcept {
    if (result.account_count >= result.accounts.size()) return SolanaResolutionError::capacity;
    for (std::size_t i = 0; i < result.account_count; ++i)
        if (result.accounts[i].key == key) return SolanaResolutionError::account_alias;
    result.accounts[result.account_count++] = {key, signer, writable};
    return SolanaResolutionError::none;
}
}

SolanaResolutionError resolve_solana_accounts(
    const DecodedSolanaPayload& decoded, std::string_view expected_organization,
    std::string_view expected_sign_with, std::span<const SolanaLookupEvidence> evidence,
    ResolvedSolanaAccounts& out) noexcept {
    out = {};
    if (decoded.original_size > decoded.original.size() || decoded.transaction_size > decoded.transaction.size() ||
        evidence.size() > 32) return SolanaResolutionError::capacity;
    DecodedSolanaPayload checked;
    if (decode_solana_signing_envelope({decoded.original.data(), decoded.original_size}, expected_organization,
                                      expected_sign_with, checked) != SolanaParseError::none)
        return SolanaResolutionError::invalid_payload;
    if (decoded != checked) return SolanaResolutionError::decoded_mismatch;
    if (evidence.size() != checked.lookup_count) return SolanaResolutionError::evidence_count;
    for (std::size_t i = 0; i < evidence.size(); ++i) {
        if (evidence[i].address_count > evidence[i].addresses.size()) return SolanaResolutionError::capacity;
        if (evidence[i].address_count == 0 || evidence[i].observed_slot == 0 || evidence[i].evidence_digest.is_zero())
            return SolanaResolutionError::invalid_evidence;
        for (std::size_t j = 0; j < i; ++j)
            if (evidence[i].table == evidence[j].table) return SolanaResolutionError::duplicate_table;
    }
    ResolvedSolanaAccounts result;
    std::array<const SolanaLookupEvidence*, 32> mapped{};
    for (std::size_t i = 0; i < checked.lookup_count; ++i) {
        const auto table = read_key(checked, checked.lookups[i].key_offset);
        for (std::size_t j = 0; j < i; ++j)
            if (result.lookups[j].table == table) return SolanaResolutionError::duplicate_table;
        for (const auto& image : evidence) if (image.table == table) mapped[i] = &image;
        if (!mapped[i]) return SolanaResolutionError::table_mismatch;
        const auto& image = *mapped[i];
        result.lookups[i] = {table, image.observed_slot, image.evidence_digest, address_digest(image), image.address_count};
    }
    const auto header = 1U + 64U * checked.signature_count + (checked.version_zero ? 1U : 0U);
    const auto readonly_signers = checked.transaction[header + 1];
    const auto readonly_others = checked.transaction[header + 2];
    for (std::size_t i = 0; i < checked.static_key_count; ++i) {
        const bool signer = i < checked.required_signatures;
        const bool writable = signer ? i < static_cast<unsigned>(checked.required_signatures - readonly_signers)
                                     : i < static_cast<unsigned>(checked.static_key_count - readonly_others);
        const auto error = append(result, read_key(checked, checked.static_keys_offset + i * 32), signer, writable);
        if (error != SolanaResolutionError::none) return error;
    }
    for (const bool writable : {true, false}) {
        for (std::size_t i = 0; i < checked.lookup_count; ++i) {
            const auto& lookup = checked.lookups[i];
            const auto& image = *mapped[i];
            const auto count = writable ? lookup.writable_count : lookup.readonly_count;
            const auto offset = writable ? lookup.writable_offset : lookup.readonly_offset;
            for (std::size_t n = 0; n < count; ++n) {
                const auto index = checked.transaction[offset + n];
                if (index >= image.address_count) return SolanaResolutionError::lookup_index;
                const auto error = append(result, image.addresses[index], false, writable);
                if (error != SolanaResolutionError::none) return error;
            }
        }
    }
    result.lookup_count = checked.lookup_count;
    result.transaction_digest = sha256_digest(std::as_bytes(std::span{checked.transaction.data(), checked.transaction_size}));
    out = result;
    return SolanaResolutionError::none;
}
}
