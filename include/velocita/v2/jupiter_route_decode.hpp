#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace velocita::v2::truemarkets {

inline constexpr std::size_t kJupiterRouteSteps = 8;
enum class JupiterRouteStatus : std::uint8_t { unsupported, invalid, decoded_data_only };
enum class JupiterSwapKind : std::uint8_t { invariant = 18, raydium_clmm = 26, obsidian = 109, kipseli_v3 = 181, bison_fi_v2 = 141, byreal_dynamic_v3 = 156, deriverse = 161, bison_fi_market_backed = 177 };
struct JupiterRouteStep {
    JupiterSwapKind kind{};
    std::uint8_t side_or_direction{}, percent{}, input_index{}, output_index{};
    std::uint32_t instruction_id{};
    bool operator==(const JupiterRouteStep&) const = default;
};
struct JupiterSharedRouteData {
    std::uint8_t authority_id{}, step_count{};
    std::array<JupiterRouteStep, kJupiterRouteSteps> steps{};
    std::uint64_t input_atoms{}, quoted_output_atoms{};
    std::uint16_t slippage_bps{};
    std::uint8_t platform_fee_bps{};
    bool operator==(const JupiterSharedRouteData&) const = default;
};
struct JupiterRouteDecodeResult {
    JupiterRouteStatus status{JupiterRouteStatus::unsupported};
    JupiterSharedRouteData data{};
    bool operator==(const JupiterRouteDecodeResult&) const = default;
};
[[nodiscard]] JupiterRouteDecodeResult decode_jupiter_shared_route_data(
    std::span<const std::uint8_t> bytes) noexcept;
// Direct `route` has no shared-account authority-id byte. Keep its result type
// separate so callers cannot pass it off as the captured shared-account route.
struct JupiterDirectRouteData {
    std::uint8_t step_count{};
    std::array<JupiterRouteStep, kJupiterRouteSteps> steps{};
    std::uint64_t input_atoms{}, quoted_output_atoms{};
    std::uint16_t slippage_bps{};
    std::uint8_t platform_fee_bps{};
    bool operator==(const JupiterDirectRouteData&) const = default;
};
struct JupiterDirectRouteDecodeResult {
    JupiterRouteStatus status{JupiterRouteStatus::unsupported};
    JupiterDirectRouteData data{};
    bool operator==(const JupiterDirectRouteDecodeResult&) const = default;
};
[[nodiscard]] JupiterDirectRouteDecodeResult decode_jupiter_direct_route_data(
    std::span<const std::uint8_t> bytes) noexcept;
static_assert(std::is_trivially_copyable_v<JupiterRouteDecodeResult>);
static_assert(sizeof(JupiterRouteDecodeResult) <= 160);
static_assert(std::is_trivially_copyable_v<JupiterDirectRouteDecodeResult>);
static_assert(sizeof(JupiterDirectRouteDecodeResult) <= 160);

}
