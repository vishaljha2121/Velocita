#pragma once
#include "velocita/v2/kernel.hpp"

namespace velocita::v2 {
[[nodiscard]] Digest256 swap_record_digest(const swap::Record&) noexcept;
[[nodiscard]] bool swap_catalog_matches(const swap::Binding&, const VerifiedExecutionCatalog&) noexcept;
[[nodiscard]] bool swap_state_bindings_valid(const TradingState&) noexcept;
void synchronize_swap_controls(TradingState&) noexcept;
[[nodiscard]] ApplyCode apply_spot_swap(TradingState&, const swap::Record&,
                                       const VerifiedExecutionCatalog*) noexcept;
}
