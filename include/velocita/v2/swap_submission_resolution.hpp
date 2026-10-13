#pragma once

#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_submission_store.hpp"

#include <optional>

namespace velocita::v2 {

struct SwapSubmissionResolutionConfig {
    swap::Binding binding{};
    Digest256 authority_domain{};
    Tick state_timeout{};
};

enum class SwapSubmissionResolutionStatus : std::uint8_t {
    closed, idle, reading_state, resolved, needs_reconciliation,
    refused, storage_failed
};

struct SwapSubmissionResolutionResult {
    SwapSubmissionResolutionStatus status{SwapSubmissionResolutionStatus::closed};
    StateReading reading{};
    swap::Settlement settlement{};
};

class SwapSubmissionResolutionSession final {
public:
    SwapSubmissionResolutionSession(GatewayLink&, SwapSubmissionStore&, SwapSubmissionResolutionConfig);
    ~SwapSubmissionResolutionSession();
    SwapSubmissionResolutionSession(const SwapSubmissionResolutionSession&) = delete;
    SwapSubmissionResolutionSession& operator=(const SwapSubmissionResolutionSession&) = delete;
    [[nodiscard]] bool start(std::int64_t minimum_commit_index, Tick now);
    [[nodiscard]] SwapSubmissionResolutionStatus poll(Tick now);
    [[nodiscard]] const SwapSubmissionResolutionResult& result() const noexcept { return result_; }
    [[nodiscard]] SwapSubmissionResolutionStatus status() const noexcept { return result_.status; }
private:
    [[nodiscard]] SwapSubmissionResolutionStatus finish(SwapSubmissionResolutionStatus) noexcept;
    [[nodiscard]] bool journal_unchanged() const noexcept;
    GatewayLink& link_;
    SwapSubmissionStore& store_;
    SwapSubmissionResolutionConfig config_;
    SwapSubmissionRecord captured_{};
    SwapSubmissionResolutionResult result_{};
    std::int64_t minimum_index_{};
    Tick last_tick_{}, deadline_{};
};

}
