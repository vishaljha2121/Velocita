// The strategy's submitter: sends ticks, decisions and authorizations to the cluster.
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

struct ShadowProgramFile {
    std::optional<ShadowProgram> program;
    SourceId source{};
    std::string error;
};

[[nodiscard]] ShadowProgramFile parse_shadow_program(std::string_view text, const VerifiedExecutionCatalog& catalog);

struct SubmitterStart {
    std::string error;
    std::uint64_t generation{};
    std::uint64_t first_sequence{};
};

[[nodiscard]] SubmitterStart begin_submitter_state(const std::filesystem::path& file,
                                                   const std::array<std::byte, 16>& incarnation,
                                                   std::uint64_t reserve);

[[nodiscard]] std::string extend_reservation(const std::filesystem::path& file,
                                             const std::array<std::byte, 16>& incarnation,
                                             std::uint64_t generation, std::uint64_t reserved_through);

enum class SubmitterRole : std::uint8_t { setup, ticks };
[[nodiscard]] std::string submitter_role_error(SubmitterRole role, CallerId caller);

[[nodiscard]] std::optional<bool> parse_exits_only(const std::optional<std::string_view>& value) noexcept;

[[nodiscard]] std::string_view apply_code_name(ApplyCode code) noexcept;

[[nodiscard]] std::optional<StrategyMode> parse_strategy_mode(std::string_view name) noexcept;

struct RiskLimitsVerb {
    std::optional<ConfigureRiskLimits> limits;
    std::string error;
};
[[nodiscard]] RiskLimitsVerb parse_risk_limits_verb(std::span<const std::string> words);

[[nodiscard]] std::string risk_limits_canonical(const ConfigureRiskLimits& limits);

[[nodiscard]] std::string describe_risk_limits(const ConfigureRiskLimits& limits);

}
