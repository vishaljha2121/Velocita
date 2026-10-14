#pragma once
#include "velocita/v2/contracts.hpp"
#include "velocita/v2/swap_signing_stamp.hpp"
#include "velocita/v2/swap_transaction_verifier.hpp"
#include <span>
#include <string_view>
namespace velocita::v2::swap_signer {
inline constexpr std::size_t kRequestBytes=88+96+8192+32+32*truemarkets::kMaxLookupPrefix,kReplyBytes=193;
enum class Operation:std::uint8_t {sign=1,retrieve=2};
enum class ReplyCode:std::uint8_t {refused=0,signed_once=1,retrieved=2,fenced=3};
struct Request {
    Operation operation{};EffectId effect{};std::uint64_t sequence{},sign_by_ms{};std::int64_t minimum_index{};
    std::array<char,96> handle{};std::array<std::byte,8192> payload{};std::uint16_t handle_size{},payload_size{};
    truemarkets::JupiterLookupPrefix lookup{};
    [[nodiscard]] std::string_view handle_view()const noexcept{return {handle.data(),handle_size};}
    [[nodiscard]] std::span<const std::byte> payload_view()const noexcept{return std::span(payload).first(payload_size);}
};
struct Reply {
    ReplyCode code{};Digest256 request_digest{};
    std::array<std::byte,33> public_key{};std::array<std::byte,72> der{};std::uint16_t der_size{};
    swap_signing::ApiKeyStamp stamp{};
};
[[nodiscard]] std::size_t frame_length(std::span<const std::byte>)noexcept;
[[nodiscard]] std::size_t encode_request(const Request&,std::span<std::byte>)noexcept;
[[nodiscard]] bool decode_request(std::span<const std::byte>,Request&)noexcept;
[[nodiscard]] std::size_t encode_reply(const Reply&,std::span<std::byte>)noexcept;
[[nodiscard]] bool decode_reply(std::span<const std::byte>,Reply&)noexcept;
}
