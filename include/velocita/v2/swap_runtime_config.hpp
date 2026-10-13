#pragma once
#include "velocita/v2/swap_reconcile_session.hpp"
#include "velocita/v2/swap_signer_service.hpp"

namespace velocita::v2 {
inline constexpr std::size_t kSwapRuntimeSocketBytes=104;
struct SwapRuntimeBinding {
    swap::Binding allocation{};
    Digest256 authority_domain{};
    SwapBalanceSessionIdentity account_session{};
    std::array<std::byte,32> owner_wallet{};
    std::array<std::byte,33> signer_public_key{};
    std::array<char,kSwapRuntimeSocketBytes> socket{};
    std::uint16_t socket_size{};
    std::uint32_t gateway_uid{},signer_uid{};
    CallerId gateway_caller{},signer_caller{};
    int ipc_timeout_ms{};
    // Explicit output-atom deduction cap, bound into the reviewed runtime identity.
    std::uint64_t max_buy_output_deduction_lamports{};
    [[nodiscard]] std::string_view socket_view()const noexcept{return {socket.data(),socket_size};}
};
struct SwapGatewayStartupConfig {
    SwapRuntimeBinding binding{};
    Digest256 bearer_digest{};
    std::uint64_t expires_at_ms{};
};
struct SwapSignerStartupConfig {
    SwapRuntimeBinding binding{};
    Digest256 signer_instance{};
    std::uint64_t key_version{};
    Tick state_timeout{};
};
enum class SwapRuntimeCode:std::uint8_t {
    valid,invalid_binding,invalid_public_key,invalid_socket,invalid_principals,
    invalid_timeout,invalid_session,expired_session,invalid_descriptor,unsafe_descriptor,
    invalid_bearer,session_digest_mismatch,process_restriction_failed,key_refused,socket_refused
};
[[nodiscard]] SwapRuntimeCode validate_swap_runtime_binding(const SwapRuntimeBinding&)noexcept;
[[nodiscard]] SwapRuntimeCode validate_swap_gateway_startup(const SwapGatewayStartupConfig&,std::uint64_t now_ms)noexcept;
[[nodiscard]] SwapRuntimeCode validate_swap_signer_startup(const SwapSignerStartupConfig&)noexcept;
[[nodiscard]] Digest256 swap_runtime_identity_digest(const SwapRuntimeBinding&)noexcept;
class SwapInheritedSecretFd final {
public:
    explicit SwapInheritedSecretFd(int fd)noexcept:fd_(fd){}
    ~SwapInheritedSecretFd();
    SwapInheritedSecretFd(const SwapInheritedSecretFd&)=delete;
    SwapInheritedSecretFd&operator=(const SwapInheritedSecretFd&)=delete;
    SwapInheritedSecretFd(SwapInheritedSecretFd&&)noexcept;
    [[nodiscard]] int get()const noexcept{return fd_;}
private:int fd_{-1};
};
class SwapGatewayCredentials final {
public:
    ~SwapGatewayCredentials();
    SwapGatewayCredentials(const SwapGatewayCredentials&)=delete;
    SwapGatewayCredentials&operator=(const SwapGatewayCredentials&)=delete;
    [[nodiscard]] static std::unique_ptr<SwapGatewayCredentials> load(SwapGatewayStartupConfig,
        SwapInheritedSecretFd,std::uint64_t now_ms,SwapRuntimeCode&);
    [[nodiscard]] std::string_view bearer(std::uint64_t now_ms)noexcept;
    [[nodiscard]] const SwapRuntimeBinding& binding()const noexcept{return config_.binding;}
    [[nodiscard]] std::unique_ptr<swap_signer::Client> signer_client()const;
private:
    explicit SwapGatewayCredentials(SwapGatewayStartupConfig c):config_(c){}
    SwapGatewayStartupConfig config_{};
    std::array<char,4096> bearer_{};
    std::uint16_t bearer_size_{};
    std::uint64_t last_time_{};
};
class SwapProtectedSignerStartup final {
public:
    SwapProtectedSignerStartup(const SwapProtectedSignerStartup&)=delete;
    SwapProtectedSignerStartup&operator=(const SwapProtectedSignerStartup&)=delete;
    [[nodiscard]] static std::unique_ptr<SwapProtectedSignerStartup> open(SwapSignerStartupConfig,
        SwapInheritedSecretFd,SwapRuntimeCode&);
    [[nodiscard]] SwapSignerKey& key()noexcept{return *key_;}
    [[nodiscard]] swap_signer::Server& server()noexcept{return server_;}
    [[nodiscard]] SwapSignerConfig session_config()const noexcept;
private:
    explicit SwapProtectedSignerStartup(SwapSignerStartupConfig c):config_(c){}
    SwapSignerStartupConfig config_{};
    std::unique_ptr<SwapSignerKey> key_{};
    swap_signer::Server server_{};
};
}
