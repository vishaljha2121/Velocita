#pragma once

#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_https.hpp"
#include "velocita/v2/swap_reconcile.hpp"

#include "velocita/v2/swap_result_watermark.hpp"

namespace velocita::v2 {

struct SwapBalanceSessionIdentity {
    Digest256 account_and_assets_digest{}, session_evidence_digest{};
    bool operator==(const SwapBalanceSessionIdentity&) const = default;
};

class SwapReconcileClock {
public:
    virtual ~SwapReconcileClock() = default;
    [[nodiscard]] virtual std::uint64_t now_ms() noexcept = 0;
};

class SwapBalanceGet {
public:
    virtual ~SwapBalanceGet() = default;
    [[nodiscard]] virtual SwapBalanceSessionIdentity identity() const noexcept = 0;
    [[nodiscard]] virtual transport::SwapHttpsResult get() = 0;
};

class SwapHttpsBalanceGet final : public SwapBalanceGet {
public:
    SwapHttpsBalanceGet(SwapBalanceSessionIdentity, std::string bearer, long timeout_ms = 2000);
    SwapHttpsBalanceGet(const SwapHttpsBalanceGet&) = delete;
    SwapHttpsBalanceGet& operator=(const SwapHttpsBalanceGet&) = delete;
    ~SwapHttpsBalanceGet() override;
    [[nodiscard]] SwapBalanceSessionIdentity identity() const noexcept override { return identity_; }
    [[nodiscard]] transport::SwapHttpsResult get() override;
private:
    transport::SwapHttpsClient client_;
    SwapBalanceSessionIdentity identity_{};
    std::string bearer_;
};

struct SwapReconcileSessionConfig {
    SwapBalanceBinding binding{};
    DeclaredOutsideBalance outside{};
    SwapBalanceSessionIdentity http_identity{};
    Digest256 authority_domain{}, clock_epoch{}, session_nonce{};
    std::uint64_t max_age_ms{}, state_timeout_ms{};
};

enum class SwapReconcileSessionStatus : std::uint8_t {
    closed, idle, reading_state, complete, state_refused, http_refused, identity_refused, comparison_refused
};

struct SwapReconcileSessionResult {
    SwapReconcileSessionStatus status{SwapReconcileSessionStatus::closed};
    StateReading reading{};
    swap::State captured{};
    SwapReconcileResult comparison{};
    std::int64_t end_result_watermark{};
};

class SwapReconcileSession final {
public:
    SwapReconcileSession(GatewayLink&, SwapBalanceGet&, SwapReconcileClock&,
                         SwapResultWatermark&, SwapReconcileSessionConfig);
    SwapReconcileSession(const SwapReconcileSession&) = delete;
    SwapReconcileSession& operator=(const SwapReconcileSession&) = delete;
    [[nodiscard]] bool start();
    [[nodiscard]] SwapReconcileSessionStatus poll();
    [[nodiscard]] const SwapReconcileSessionResult& result() const noexcept { return result_; }
private:
    GatewayLink& link_;
    SwapBalanceGet& http_;
    SwapReconcileClock& clock_;
    SwapResultWatermark& watermark_;
    SwapReconcileSessionConfig config_;
    SwapReconcileSessionResult result_{};
    Digest256 partition_{}, request_id_{};
    std::uint64_t sequence_{}, started_ms_{};
    bool valid_{};
};

}
