#pragma once

#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_https.hpp"
#include "velocita/v2/swap_prepare_store.hpp"

namespace velocita::v2 {

enum class SwapPrepareInputMapping:std::uint8_t {
    fixture_principal_and_base_input=1,
    fee_inclusive_quote_input=2
};
// Versioned identity prevents recovery under a different quantity convention.
[[nodiscard]] Digest256 fee_inclusive_prepare_mapping_identity() noexcept;
class SwapPreparePort {
public:
    virtual ~SwapPreparePort()=default;
    [[nodiscard]] virtual transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&)=0;
};
struct SwapPrepareSessionConfig {
    SwapPrepareBinding binding{};
    SwapPrepareInputMapping mapping{};
    Tick state_timeout{};
};
enum class SwapPrepareSessionStatus:std::uint8_t {
    closed,idle,reading_state,prepared,needs_reconciliation,refused,storage_failed
};
class SwapPrepareSession final {
public:
    SwapPrepareSession(GatewayLink&,SwapPrepareStore&,SwapPreparePort&,SwapPrepareSessionConfig);
    ~SwapPrepareSession();
    SwapPrepareSession(const SwapPrepareSession&)=delete;
    SwapPrepareSession& operator=(const SwapPrepareSession&)=delete;
    [[nodiscard]] bool start(EffectId,std::int64_t minimum_commit_index,Tick now);
    [[nodiscard]] SwapPrepareSessionStatus poll(Tick now,std::uint64_t event_time);
    [[nodiscard]] SwapPrepareSessionStatus status()const noexcept{return status_;}
    [[nodiscard]] const truemarkets::SwapRestPrepared* prepared()const noexcept;
private:
    [[nodiscard]] SwapPrepareSessionStatus finish(SwapPrepareSessionStatus)noexcept;
    GatewayLink& link_;
    SwapPrepareStore& store_;
    SwapPreparePort& port_;
    SwapPrepareSessionConfig config_;
    SwapPrepareSessionStatus status_{SwapPrepareSessionStatus::closed};
    EffectId effect_{};
    std::int64_t minimum_index_{};
    std::uint64_t ordinal_{};
    Digest256 checksum_{};
    Tick last_tick_{},deadline_{};
};

}
