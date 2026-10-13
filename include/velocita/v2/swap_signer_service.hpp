#pragma once
#include "velocita/v2/swap_signer_protocol.hpp"
#include "velocita/v2/swap_signer_session.hpp"
#include <filesystem>
namespace velocita::v2::swap_signer {
[[nodiscard]] bool restrict_process(std::uint32_t expected_signer_uid,std::uint32_t gateway_uid)noexcept;
class Server final {
public:
    Server()noexcept=default;
    ~Server();
    Server(const Server&)=delete;Server&operator=(const Server&)=delete;
    [[nodiscard]] bool listen(const std::filesystem::path&,std::uint32_t allowed_client_uid)noexcept;
    [[nodiscard]] bool serve_once(SwapSignerSession&,int timeout_ms);
    [[nodiscard]] std::uint64_t peer_refusals()const noexcept{return peer_refusals_;}
private:
    int fd_{-1},directory_{-1};std::filesystem::path path_;
    std::uint64_t device_{},inode_{},peer_refusals_{};std::uint32_t client_uid_{};
};
class Client final {
public:
    Client(std::filesystem::path,std::uint32_t signer_uid,std::array<std::byte,33> signer_public_key,int timeout_ms);
    [[nodiscard]] Reply request(const Request&);
private:
    std::filesystem::path path_;std::uint32_t signer_uid_{};std::array<std::byte,33> public_key_{};int timeout_ms_{};
};
}
