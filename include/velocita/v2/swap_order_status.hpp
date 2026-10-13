#pragma once

#include "velocita/v2/swap_https.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/swap_submission_store.hpp"
#include "velocita/v2/swap_prepare_store.hpp"
#include "velocita/v2/truemarkets_rest.hpp"
#include <memory>

namespace velocita::v2 {

struct SwapOrderSessionIdentity {
    Digest256 account_and_assets{}, authenticated_session{};
    bool operator==(const SwapOrderSessionIdentity&) const = default;
};
class SwapOrderGet {
public:
    virtual ~SwapOrderGet() = default;
    [[nodiscard]] virtual SwapOrderSessionIdentity identity() const noexcept = 0;
    [[nodiscard]] virtual transport::SwapHttpsResult get(std::string_view path) = 0;
};
class SwapOrderReadClock {
public:
    virtual ~SwapOrderReadClock() = default;
    [[nodiscard]] virtual std::uint64_t now_ms() noexcept = 0;
};
class SwapHttpsOrderGet final : public SwapOrderGet {
public:
    SwapHttpsOrderGet(SwapOrderSessionIdentity, std::string bearer, const truemarkets::TmText& handle, long timeout_ms=2000);
    ~SwapHttpsOrderGet() override;
    [[nodiscard]] SwapOrderSessionIdentity identity() const noexcept override { return identity_; }
    [[nodiscard]] transport::SwapHttpsResult get(std::string_view path) override;
#ifdef VELOCITA_SWAP_ORDER_STATUS_TEST_ONLY
    [[nodiscard]] static std::unique_ptr<SwapHttpsOrderGet> local_fixture(SwapOrderSessionIdentity,std::string bearer,
        const truemarkets::TmText&,std::string origin,std::string ca_bundle);
#endif
private:
    SwapHttpsOrderGet(SwapOrderSessionIdentity,std::string,transport::SwapHttpsConfig);
    transport::SwapHttpsClient client_;
    SwapOrderSessionIdentity identity_{};
    std::string bearer_;
};
struct SwapOrderStatusConfig {
    SwapPrepareBinding binding{};
    SwapOrderSessionIdentity http_identity{};
    std::uint64_t timeout_ms{};
};
enum class SwapOrderReadStatus : std::uint8_t {
    not_read, refused, http_refused, identity_conflict, observed_working,
    terminal_needs_finality, storage_failed
};
struct SwapOrderReadResult {
    SwapOrderReadStatus status{SwapOrderReadStatus::not_read};
    truemarkets::RestOrderDetail detail{};
    Digest256 response_digest{};
    SwapSubmissionTransaction transaction{};
};
[[nodiscard]] std::optional<SwapSubmissionTransaction> decode_solana_signature(std::string_view) noexcept;
class SwapOrderStatusReader final {
public:
    SwapOrderStatusReader(SwapSubmissionStore&,const SwapPrepareStore&,SwapOrderGet&,SwapOrderReadClock&,SwapOrderStatusConfig);
    [[nodiscard]] const SwapOrderReadResult& read();
private:
    SwapSubmissionStore& store_;
    const SwapPrepareStore& prepare_;
    SwapOrderGet& http_;
    SwapOrderReadClock& clock_;
    SwapOrderStatusConfig config_;
    SwapOrderReadResult result_{};
};

}
