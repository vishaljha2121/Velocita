#pragma once
#include "velocita/v2/swap_prepare_store.hpp"
#include "velocita/v2/swap_signer_service.hpp"

namespace velocita::v2 {
struct SwapSigningConnectionConfig {
    SwapPrepareBinding preparation{};
    std::array<std::byte,33> signer_public_key{};
};
[[nodiscard]] SwapSubmissionBinding swap_signing_connection_binding(const SwapSigningConnectionConfig&) noexcept;
struct SwapSignedExecution {
    SwapSubmissionIdentity identity{};
    truemarkets::SwapRestRequest request{};
};
enum class SwapSigningConnectionStatus:std::uint8_t {closed,idle,unresolved,signed_once};
// Gateway-side durable IPC request. This is not signing authority: the isolated
// signer still independently checks the committed intent and current quorum.
class SwapSigningConnection final {
public:
    SwapSigningConnection(const SwapPrepareStore&,SwapSubmissionStore&,swap_signer::Client&,SwapSigningConnectionConfig);
    SwapSigningConnection(const SwapSigningConnection&)=delete;
    SwapSigningConnection& operator=(const SwapSigningConnection&)=delete;
    [[nodiscard]] bool sign(std::uint64_t sequence,std::int64_t prepared_index,std::uint64_t sign_by);
    [[nodiscard]] bool retrieve();
    [[nodiscard]] const SwapSignedExecution* execution()const noexcept;
    [[nodiscard]] SwapSigningConnectionStatus status()const noexcept{return status_;}
private:
    [[nodiscard]] bool assemble(std::uint64_t,std::int64_t,std::uint64_t,swap_signer::Request&,SwapSubmissionIdentity&)const;
    [[nodiscard]] bool exchange(swap_signer::Request);
    const SwapPrepareStore& preparation_;
    SwapSubmissionStore& journal_;
    swap_signer::Client& client_;
    SwapSigningConnectionConfig config_;
    SwapSigningConnectionStatus status_{SwapSigningConnectionStatus::closed};
    Digest256 preparation_checksum_{},request_checksum_{};
    SwapSignedExecution execution_{};
};
}
