#pragma once

#include "velocita/v2/swap_prepare_session.hpp"
#include <memory>

namespace velocita::v2 {

// One exact unsigned create request per instance. The session persists its intent
// before invoking this port. It has no execute route, refresh or retry operation.
class SwapHttpsPreparePort final : public SwapPreparePort {
public:
    SwapHttpsPreparePort(truemarkets::SwapRestInput, std::string bearer, long timeout_ms);
    ~SwapHttpsPreparePort() override;
    SwapHttpsPreparePort(const SwapHttpsPreparePort&) = delete;
    SwapHttpsPreparePort& operator=(const SwapHttpsPreparePort&) = delete;
    [[nodiscard]] transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&) override;
#ifdef VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY
    [[nodiscard]] static std::unique_ptr<SwapHttpsPreparePort> local_fixture(
        truemarkets::SwapRestInput, std::string bearer, std::string origin, std::string ca);
#endif
private:
    SwapHttpsPreparePort(truemarkets::SwapRestInput, std::string, transport::SwapHttpsConfig);
    transport::SwapHttpsClient client_;
    truemarkets::SwapRestRequest expected_{};
    std::string bearer_;
    bool valid_{}, attempted_{};
};

}
