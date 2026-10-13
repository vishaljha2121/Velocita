#pragma once

#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_submission_store.hpp"

namespace velocita::v2 {

inline constexpr std::size_t kSwapExecutionRequestBytes = 16384;

class SwapExecutionPort {
public:
    virtual ~SwapExecutionPort() = default;
    virtual void execute(const SwapSubmissionIdentity&, std::span<const std::byte>) = 0;
};

struct SwapSubmissionSessionConfig {
    swap::Binding binding{};
    Digest256 authority_domain{};
    Tick state_timeout{};
};

enum class SwapSubmissionSessionStatus : std::uint8_t {
    closed, idle, reading_state, refused, storage_failed, needs_reconciliation
};

class SwapSubmissionSession final {
public:
    SwapSubmissionSession(GatewayLink&, SwapSubmissionStore&, SwapExecutionPort&,
                          SwapSubmissionSessionConfig);
    ~SwapSubmissionSession();
    SwapSubmissionSession(const SwapSubmissionSession&) = delete;
    SwapSubmissionSession& operator=(const SwapSubmissionSession&) = delete;
    [[nodiscard]] bool start(const SwapSubmissionIdentity&, std::span<const std::byte>,
                             std::int64_t minimum_commit_index, Tick now);
    [[nodiscard]] SwapSubmissionSessionStatus poll(Tick now, std::uint64_t event_time);
    [[nodiscard]] SwapSubmissionSessionStatus status() const noexcept { return status_; }
private:
    void erase_request() noexcept;
    [[nodiscard]] SwapSubmissionSessionStatus refuse(SwapSubmissionSessionStatus) noexcept;
    GatewayLink& link_;
    SwapSubmissionStore& store_;
    SwapExecutionPort& port_;
    SwapSubmissionSessionConfig config_;
    SwapSubmissionIdentity identity_{};
    std::array<std::byte,kSwapExecutionRequestBytes> request_{};
    std::size_t request_size_{};
    std::int64_t minimum_index_{};
    Tick last_tick_{}, deadline_{};
    SwapSubmissionSessionStatus status_{SwapSubmissionSessionStatus::closed};
};

}
