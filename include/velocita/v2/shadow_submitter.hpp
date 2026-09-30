#pragma once

#include "velocita/v2/execution_catalog.hpp"
#include "velocita/v2/shadow_program.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace velocita::v2 {

// The shadow program file: `key = value` lines, `#` comments, every key
// required exactly once, rules numbered rule.1..rule.N without gaps:
//   rule.N = FEATURE OP THRESHOLD ACTION   (OP lt|le|eq|ge|gt, ACTION enter_long|exit|hold)
// The catalog row is looked up in the node's own verified catalog, whose root
// must equal catalog.root: units are the catalog's, never the file's.
struct ShadowProgramFile {
    std::optional<ShadowProgram> program;
    SourceId source{};
    std::string error;  // "line N: ..." or "key K: ..."; empty on success
};

[[nodiscard]] ShadowProgramFile parse_shadow_program(std::string_view text, const VerifiedExecutionCatalog& catalog);

// Durable submitter state: cluster incarnation, source generation, and the
// request-sequence high-water mark reserved before use. Every start moves to
// the next generation (the source restarts at sequence 1) and to the first
// request sequence past anything a previous run may have used; a different
// incarnation (a new cluster) starts again at generation 1. A torn or corrupt
// file is refused, never replaced: that is how sequences would be reused.
struct SubmitterStart {
    std::string error;
    std::uint64_t generation{};
    std::uint64_t first_sequence{};
};

[[nodiscard]] SubmitterStart begin_submitter_state(const std::filesystem::path& file,
                                                   const std::array<std::byte, 16>& incarnation,
                                                   std::uint64_t reserve);

// Raises the reserved high-water mark to RESERVED_THROUGH (exclusive) before
// sequences below it are used. Empty string on success.
[[nodiscard]] std::string extend_reservation(const std::filesystem::path& file,
                                             const std::array<std::byte, 16>& incarnation,
                                             std::uint64_t generation, std::uint64_t reserved_through);

// Split authority: the one-shot setup (policy, executable bundle, strategy
// mode) needs an operator caller; the tick loop must never be one, so a
// strategy process cannot change controls. Empty string when CALLER may act.
enum class SubmitterRole : std::uint8_t { setup, ticks };
[[nodiscard]] std::string submitter_role_error(SubmitterRole role, CallerId caller);

// The TICK line's optional `exits_only` token: absent or 0 = buys allowed, 1 =
// exits only. Any other value is a bad tick, never a guess.
[[nodiscard]] std::optional<bool> parse_exits_only(const std::optional<std::string_view>& value) noexcept;

// Stable snake_case name of every ApplyCode (metrics labels, operator output).
[[nodiscard]] std::string_view apply_code_name(ApplyCode code) noexcept;

[[nodiscard]] std::optional<StrategyMode> parse_strategy_mode(std::string_view name) noexcept;

// The operator's `risk-limits` verb: KEY=VALUE words, every key exactly once:
//   account instrument risk_policy_version expected_risk_version kill_epoch
//   executor_generation max_order_lots max_reserved_lots max_order_notional_micros
//   max_reserved_exposure_micros initial_margin_ppm maximum_fee_ppm
//   max_position_lots allow_short(0|1) reason_code(non-zero)
struct RiskLimitsVerb {
    std::optional<ConfigureRiskLimits> limits;
    std::string error;
};
[[nodiscard]] RiskLimitsVerb parse_risk_limits_verb(std::span<const std::string> words);

// Every field in a fixed order: the payload digest is taken over this, so the
// same limits typed in another order are the same command.
[[nodiscard]] std::string risk_limits_canonical(const ConfigureRiskLimits& limits);

// Lots and dollars, for the operator to read before the command is sent.
[[nodiscard]] std::string describe_risk_limits(const ConfigureRiskLimits& limits);

}  // namespace velocita::v2
