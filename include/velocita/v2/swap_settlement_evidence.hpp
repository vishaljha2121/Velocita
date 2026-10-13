#pragma once
#include "velocita/v2/solana_resolved_accounts.hpp"
#include "velocita/v2/swap_owner_accounting.hpp"
#include "velocita/v2/swap_submission_store.hpp"
#include <optional>

namespace velocita::v2 {
inline constexpr std::size_t kSettlementRpcBytes=32768;
struct SettlementRpcResponse {
    bool transport_complete{};
    unsigned http_status{};
    std::array<char,kSettlementRpcBytes> body{};
    std::size_t size{};
};
class SwapFinalizedRpcPort {
public:
    virtual ~SwapFinalizedRpcPort()=default;
    [[nodiscard]] virtual Digest256 provider_identity()const noexcept=0;
    [[nodiscard]] virtual SettlementRpcResponse request(std::string_view body,std::uint64_t timeout_ms)=0;
};
class SwapFinalizedReadClock {
public:
    virtual ~SwapFinalizedReadClock()=default;
    [[nodiscard]] virtual std::uint64_t now_ms()noexcept=0;
};
class VerifiedFinalizedRead {
public:
    [[nodiscard]] std::string_view body()const noexcept{return {body_.data(),size_};}
    [[nodiscard]] Digest256 response_digest()const noexcept{return response_digest_;}
    [[nodiscard]] Digest256 provider_identity()const noexcept{return provider_;}
    [[nodiscard]] const SwapSubmissionTransaction& signature()const noexcept{return signature_;}
    [[nodiscard]] std::uint64_t slot()const noexcept{return slot_;}
private:
    friend class SwapFinalizedReader;
    VerifiedFinalizedRead()=default;
    std::array<char,kSettlementRpcBytes> body_{};
    std::size_t size_{};
    Digest256 response_digest_{},provider_{};
    SwapSubmissionTransaction signature_{};
    std::uint64_t slot_{};
};
enum class FinalizedReadCode:std::uint8_t {refused,transport_refused,not_found,malformed,stale,attributed_finalized_response};
struct FinalizedReadResult {
    FinalizedReadCode code{FinalizedReadCode::refused};
    std::optional<VerifiedFinalizedRead> evidence{};
};
class SwapFinalizedReader {
public:
    SwapFinalizedReader(SwapFinalizedRpcPort&,SwapFinalizedReadClock&,Digest256 provider,std::uint64_t timeout_ms);
    [[nodiscard]] FinalizedReadResult read(const SwapSubmissionTransaction&,std::uint64_t request_id,std::uint64_t minimum_slot);
private:
    SwapFinalizedRpcPort& port_;SwapFinalizedReadClock& clock_;
    Digest256 provider_{};std::uint64_t timeout_{};
};
struct SettlementTokenBalance {
    bool present{};
    truemarkets::SolanaAccountKey mint{},owner{},program{};
    std::uint64_t atoms{};
    std::uint8_t decimals{};
};
struct SettlementAccountBalance {
    truemarkets::SolanaAccountKey key{};
    std::uint64_t before_lamports{},after_lamports{};
    SettlementTokenBalance before_token{},after_token{};
};
enum class SettlementEvidenceCode:std::uint8_t {
    invalid_original,malformed,transaction_mismatch,failed_transaction,account_mismatch,
    balance_mismatch,unsupported_owner_asset,observed_transaction_needs_account_lifecycle, observed_failed_transaction
};
struct SettlementEvidence {
    SettlementEvidenceCode code{SettlementEvidenceCode::invalid_original};
    Digest256 response_digest{},original_payload_digest{},provider{};
    SwapSubmissionTransaction signature{};
    std::uint64_t slot{},network_fee{};
    std::uint16_t account_count{},owner_index{};
    truemarkets::SolanaAccountKey owner{};
    std::array<SettlementAccountBalance,256> accounts{};
};
[[nodiscard]] std::optional<truemarkets::SolanaAccountKey> decode_solana_account_key(std::string_view) noexcept;
[[nodiscard]] SettlementEvidence decode_settlement_evidence(const VerifiedFinalizedRead&,
    std::string_view original_envelope,std::string_view organization,std::string_view signer,
    std::span<const truemarkets::SolanaLookupEvidence>,const truemarkets::SolanaAccountKey& owner)noexcept;
enum class SettlementPropertyCode:std::uint8_t {invalid_evidence,scope_mismatch,property_mismatch,corroborated_declarations_only};
struct SettlementPropertyResult {
    SettlementPropertyCode code{SettlementPropertyCode::invalid_evidence};
    OwnerPropertyMeasurement measurement{};
};
[[nodiscard]] SettlementPropertyResult corroborate_settlement_properties(const SettlementEvidence&,
    std::span<const DeclaredOwnedAccount>,std::span<const OwnerAccountChange>)noexcept;
}
