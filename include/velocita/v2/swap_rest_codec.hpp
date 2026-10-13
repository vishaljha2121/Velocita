#pragma once

#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/solana_payload_view.hpp"
#include "velocita/v2/swap_signing_stamp.hpp"

namespace velocita::v2::truemarkets {

inline constexpr std::string_view kSwapSolAssetId="495e07ac-fa3e-4179-85b7-b1e8cece3dc4";
inline constexpr std::string_view kSwapSolMint="So11111111111111111111111111111111111111112";
inline constexpr std::string_view kSwapUsdcMint="EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v";

struct SwapRestInput { Side side{}; std::uint64_t quantity_atoms{}; };
struct SwapRestRequest {
    std::array<char,160> path{};
    std::array<char,1024> body{};
    std::uint16_t path_size{},body_size{};
    [[nodiscard]] std::string_view path_view() const noexcept { return {path.data(),path_size}; }
    [[nodiscard]] std::string_view body_view() const noexcept { return {body.data(),body_size}; }
};
struct SwapRestPrepared {
    TmText handle{};
    std::array<char,kMaxSolanaEnvelope> payload{};
    std::uint16_t payload_size{};
    Digest256 payload_digest{};
    std::uint64_t quoted_input{},quoted_output{},quoted_fee{};
    [[nodiscard]] std::string_view payload_view() const noexcept { return {payload.data(),payload_size}; }
};
enum class SwapRestCode : std::uint8_t {
    encoded, decoded_unsigned_only, invalid_input, invalid_http, malformed,
    unsafe_handle, issue, unsupported, payload_digest_mismatch, quote_mismatch
};

[[nodiscard]] SwapRestCode encode_swap_rest_create(SwapRestInput,SwapRestRequest&) noexcept;
[[nodiscard]] SwapRestCode encode_swap_rest_execute(const TmText&,const swap_signing::ApiKeyStamp&,SwapRestRequest&) noexcept;
[[nodiscard]] SwapRestCode decode_swap_rest_prepare(unsigned,std::string_view,SwapRestInput,SwapRestPrepared&) noexcept;

}
