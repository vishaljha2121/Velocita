#pragma once

#include "velocita/v2/action_template.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::uint16_t kFeatureSchemaArtifactSchemaVersion = 1;
inline constexpr std::uint16_t kExecutablePolicyArtifactSchemaVersion = 1;

// Candidate schema capacities. They become accepted V2 wire constants only
// after canonical-size, ARM64, Pi, and NUC gates pass unchanged.
inline constexpr std::size_t kMaxExecutablePolicyArtifacts = 2;
inline constexpr std::size_t kMaxExecutableActionTemplates = 8;

enum class FeatureEconomicKind : std::uint8_t {
    generic = 1,
    price = 2,
    quantity = 3,
};

struct FeatureDescriptor {
    FeatureEconomicKind kind{};
    std::int64_t atoms_per_output_unit{};

    constexpr auto operator<=>(const FeatureDescriptor&) const noexcept = default;
};

struct FeatureSchemaArtifact {
    std::uint16_t schema_version{};
    FeatureSchemaId feature_schema_id{};
    Digest256 artifact_digest{};
    std::uint16_t feature_count{};
    std::array<FeatureDescriptor, kMaxDecisionFeatures> features{};

    constexpr auto operator<=>(const FeatureSchemaArtifact&) const noexcept = default;
};

// Slot zero is the canonical no-action selection. Nonzero slots are one-based
// indices into templates, avoiding a separate sentinel value on the wire.
struct ExecutablePolicyArtifact {
    std::uint16_t schema_version{};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    Digest256 policy_artifact_digest{};
    FeatureSchemaId feature_schema_id{};
    Digest256 feature_schema_digest{};
    Digest256 artifact_digest{};
    std::uint16_t template_count{};
    std::array<ActionTemplate, kMaxExecutableActionTemplates> templates{};
    std::uint16_t rule_binding_count{};
    std::array<ActionTemplateSlot, kMaxPolicyRules> rule_template_slots{};

    constexpr auto operator<=>(
        const ExecutablePolicyArtifact&) const noexcept = default;
};

enum class ExecutablePolicyBindingCode : std::uint8_t {
    bound = 1,
    invalid_policy,
    invalid_feature_schema,
    invalid_executable_policy,
    policy_identity_mismatch,
    policy_digest_mismatch,
    feature_schema_identity_mismatch,
    feature_schema_digest_mismatch,
    rule_count_mismatch,
    no_action_binding_mismatch,
    action_binding_missing,
    directive_mismatch,
    feature_unavailable,
    feature_kind_mismatch,
    feature_conversion_mismatch,
};

namespace executable_policy_detail {

[[nodiscard]] constexpr bool known(const FeatureEconomicKind kind) noexcept {
    return kind == FeatureEconomicKind::generic ||
           kind == FeatureEconomicKind::price ||
           kind == FeatureEconomicKind::quantity;
}

[[nodiscard]] constexpr bool empty(const FeatureDescriptor& value) noexcept {
    return value == FeatureDescriptor{};
}

[[nodiscard]] constexpr bool empty(const ActionTemplate& value) noexcept {
    return value == ActionTemplate{};
}

[[nodiscard]] constexpr ExecutablePolicyBindingCode bind_price_source(
    const PriceSource& source,
    const FeatureSchemaArtifact& schema) noexcept {
    if (source.kind != EconomicValueSourceKind::feature) {
        return ExecutablePolicyBindingCode::bound;
    }
    if (source.feature_index >= schema.feature_count) {
        return ExecutablePolicyBindingCode::feature_unavailable;
    }
    const auto& descriptor = schema.features[source.feature_index];
    if (descriptor.kind != FeatureEconomicKind::price) {
        return ExecutablePolicyBindingCode::feature_kind_mismatch;
    }
    if (descriptor.atoms_per_output_unit != source.feature_atoms_per_tick) {
        return ExecutablePolicyBindingCode::feature_conversion_mismatch;
    }
    return ExecutablePolicyBindingCode::bound;
}

[[nodiscard]] constexpr ExecutablePolicyBindingCode bind_quantity_source(
    const QuantitySource& source,
    const FeatureSchemaArtifact& schema) noexcept {
    if (source.kind != EconomicValueSourceKind::feature) {
        return ExecutablePolicyBindingCode::bound;
    }
    if (source.feature_index >= schema.feature_count) {
        return ExecutablePolicyBindingCode::feature_unavailable;
    }
    const auto& descriptor = schema.features[source.feature_index];
    if (descriptor.kind != FeatureEconomicKind::quantity) {
        return ExecutablePolicyBindingCode::feature_kind_mismatch;
    }
    if (descriptor.atoms_per_output_unit != source.feature_atoms_per_lot) {
        return ExecutablePolicyBindingCode::feature_conversion_mismatch;
    }
    return ExecutablePolicyBindingCode::bound;
}

}  // namespace executable_policy_detail

[[nodiscard]] constexpr ContractError
validate(const FeatureSchemaArtifact& schema) noexcept {
    if (schema.feature_schema_id.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (schema.schema_version != kFeatureSchemaArtifactSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (schema.artifact_digest.is_zero()) return ContractError::missing_digest;
    if (schema.feature_count > schema.features.size()) {
        return ContractError::capacity_exceeded;
    }
    if (schema.feature_count == 0) return ContractError::empty_features;

    for (std::size_t index = 0; index < schema.feature_count; ++index) {
        const auto& feature = schema.features[index];
        if (!executable_policy_detail::known(feature.kind)) {
            return ContractError::invalid_enum;
        }
        if (feature.kind == FeatureEconomicKind::generic) {
            if (feature.atoms_per_output_unit != 0) {
                return ContractError::non_canonical_value;
            }
        } else if (feature.atoms_per_output_unit <= 0) {
            return ContractError::invalid_value_source;
        }
    }
    for (std::size_t index = schema.feature_count;
         index < schema.features.size(); ++index) {
        if (!executable_policy_detail::empty(schema.features[index])) {
            return ContractError::non_canonical_value;
        }
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ContractError
validate(const ExecutablePolicyArtifact& executable) noexcept {
    if (executable.policy_id.value() == 0 ||
        executable.policy_version.value() == 0 ||
        executable.feature_schema_id.value() == 0) {
        return ContractError::invalid_identity;
    }
    if (executable.schema_version != kExecutablePolicyArtifactSchemaVersion) {
        return ContractError::unsupported_schema;
    }
    if (executable.policy_artifact_digest.is_zero() ||
        executable.feature_schema_digest.is_zero() ||
        executable.artifact_digest.is_zero()) {
        return ContractError::missing_digest;
    }
    if (executable.template_count > executable.templates.size() ||
        executable.rule_binding_count > executable.rule_template_slots.size()) {
        return ContractError::capacity_exceeded;
    }
    if (executable.rule_binding_count == 0) {
        return ContractError::empty_rules;
    }

    for (std::size_t index = 0; index < executable.template_count; ++index) {
        if (const auto error = validate(executable.templates[index]);
            error != ContractError::none) {
            return error;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (executable.templates[previous] == executable.templates[index]) {
                return ContractError::non_canonical_value;
            }
        }
    }
    for (std::size_t index = executable.template_count;
         index < executable.templates.size(); ++index) {
        if (!executable_policy_detail::empty(executable.templates[index])) {
            return ContractError::non_canonical_value;
        }
    }

    std::array<bool, kMaxExecutableActionTemplates> referenced{};
    for (std::size_t index = 0; index < executable.rule_binding_count; ++index) {
        const auto slot = executable.rule_template_slots[index].value();
        if (slot > executable.template_count) {
            return ContractError::invalid_action_template;
        }
        if (slot != 0) referenced[slot - 1] = true;
    }
    for (std::size_t index = executable.rule_binding_count;
         index < executable.rule_template_slots.size(); ++index) {
        if (executable.rule_template_slots[index].value() != 0) {
            return ContractError::non_canonical_value;
        }
    }
    for (std::size_t index = 0; index < executable.template_count; ++index) {
        if (!referenced[index]) return ContractError::invalid_action_template;
    }
    return ContractError::none;
}

[[nodiscard]] constexpr ExecutablePolicyBindingCode bind_executable_policy(
    const PolicyArtifact& policy,
    const FeatureSchemaArtifact& schema,
    const ExecutablePolicyArtifact& executable) noexcept {
    if (validate(policy) != ContractError::none) {
        return ExecutablePolicyBindingCode::invalid_policy;
    }
    if (validate(schema) != ContractError::none) {
        return ExecutablePolicyBindingCode::invalid_feature_schema;
    }
    if (validate(executable) != ContractError::none) {
        return ExecutablePolicyBindingCode::invalid_executable_policy;
    }
    if (executable.policy_id != policy.policy_id ||
        executable.policy_version != policy.policy_version) {
        return ExecutablePolicyBindingCode::policy_identity_mismatch;
    }
    if (executable.policy_artifact_digest != policy.artifact_digest) {
        return ExecutablePolicyBindingCode::policy_digest_mismatch;
    }
    if (schema.feature_schema_id != policy.feature_schema_id ||
        executable.feature_schema_id != policy.feature_schema_id) {
        return ExecutablePolicyBindingCode::feature_schema_identity_mismatch;
    }
    if (executable.feature_schema_digest != schema.artifact_digest) {
        return ExecutablePolicyBindingCode::feature_schema_digest_mismatch;
    }
    if (executable.rule_binding_count != policy.rule_count) {
        return ExecutablePolicyBindingCode::rule_count_mismatch;
    }

    for (std::size_t index = 0; index < policy.rule_count; ++index) {
        const auto& rule = policy.rules[index];
        if (rule.feature_index >= schema.feature_count) {
            return ExecutablePolicyBindingCode::feature_unavailable;
        }
        const auto slot = executable.rule_template_slots[index].value();
        if (rule.directive == Directive::no_action) {
            if (slot != 0) {
                return ExecutablePolicyBindingCode::no_action_binding_mismatch;
            }
            continue;
        }
        if (slot == 0) {
            return ExecutablePolicyBindingCode::action_binding_missing;
        }
        const auto& action = executable.templates[slot - 1];
        if (action.directive != rule.directive) {
            return ExecutablePolicyBindingCode::directive_mismatch;
        }

        auto code = executable_policy_detail::bind_price_source(
            action.price_source, schema);
        if (code != ExecutablePolicyBindingCode::bound) return code;
        code = executable_policy_detail::bind_quantity_source(
            action.quantity_source, schema);
        if (code != ExecutablePolicyBindingCode::bound) return code;
        code = executable_policy_detail::bind_price_source(
            action.safety_caps.reference_price, schema);
        if (code != ExecutablePolicyBindingCode::bound) return code;
    }
    return ExecutablePolicyBindingCode::bound;
}

static_assert(std::is_trivially_copyable_v<FeatureDescriptor>);
static_assert(std::is_trivially_copyable_v<FeatureSchemaArtifact>);
static_assert(std::is_trivially_copyable_v<ExecutablePolicyArtifact>);
static_assert(sizeof(FeatureDescriptor) <= kFeatureDescriptorObjectBudgetBytes);
static_assert(sizeof(FeatureSchemaArtifact) <=
              kFeatureSchemaArtifactObjectBudgetBytes);
static_assert(sizeof(ExecutablePolicyArtifact) <=
              kExecutablePolicyArtifactObjectBudgetBytes);

}  // namespace velocita::v2
