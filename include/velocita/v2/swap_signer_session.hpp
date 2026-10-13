#pragma once
#include "velocita/v2/gateway_link.hpp"
#include "velocita/v2/swap_signing_stamp.hpp"
#include "velocita/v2/swap_submission_store.hpp"
#include <memory>

namespace velocita::v2 {
class SwapSignerSession;
class SwapSignerKey final {
public:
    ~SwapSignerKey();
    SwapSignerKey(const SwapSignerKey&) = delete;
    SwapSignerKey& operator=(const SwapSignerKey&) = delete;
    [[nodiscard]] static std::unique_ptr<SwapSignerKey> load_inherited_fd(int,
        std::span<const std::byte,33> expected_public_key);
    [[nodiscard]] std::uint64_t signature_attempts() const noexcept { return attempts_; }
    [[nodiscard]] const std::array<std::byte,33>& public_key() const noexcept { return public_; }
private:
    friend class SwapSignerSession;
    explicit SwapSignerKey(void*,std::array<std::byte,33>) noexcept;
    [[nodiscard]] bool sign(std::span<const std::byte>,std::span<std::byte>,std::size_t&) noexcept;
    [[nodiscard]] bool verify(std::span<const std::byte>,std::span<const std::byte>) noexcept;
    void* key_{};
    std::uint64_t attempts_{};
    std::array<std::byte,33> public_{};
};
struct SwapSignerClock {
    Tick (*monotonic)(void*) noexcept {};
    std::uint64_t (*event)(void*) noexcept {};
    void* context{};
};
struct SwapSignerConfig {
    swap::Binding binding{};
    Digest256 authority_domain{}, signer_instance{};
    CallerId caller{};
    std::uint64_t key_version{};
    Tick state_timeout{};
    std::uint64_t account_review_expires_at_ms{};
};
[[nodiscard]] SwapSubmissionBinding swap_signer_journal_binding(const SwapSignerConfig&,
    std::span<const std::byte,33> public_key) noexcept;
enum class SwapSignerStatus : std::uint8_t { closed,idle,reading_state,reading_authority,refused,storage_failed,fenced,signed_once };
class SwapSignerSession final {
public:
    SwapSignerSession(GatewayLink&,SwapSubmissionStore&,SwapSignerKey&,SwapSignerConfig,SwapSignerClock);
    ~SwapSignerSession();
    SwapSignerSession(const SwapSignerSession&)=delete;
    SwapSignerSession& operator=(const SwapSignerSession&)=delete;
    [[nodiscard]] bool start(EffectId,std::uint64_t sequence,std::string_view handle,
        std::span<const std::byte> payload,std::int64_t minimum_index,std::uint64_t expected_sign_by_ms);
    [[nodiscard]] bool retrieve(EffectId,std::uint64_t sequence,std::string_view handle,
        std::span<const std::byte> payload,std::int64_t minimum_index,std::uint64_t expected_sign_by_ms);
    void cancel_pending() noexcept;
    [[nodiscard]] SwapSignerStatus poll();
    [[nodiscard]] SwapSignerStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::array<std::byte,33>& public_key() const noexcept { return key_.public_key(); }
    [[nodiscard]] const swap_signing::ApiKeyStamp& stamp() const noexcept { return stamp_; }
    [[nodiscard]] std::span<const std::byte> signature_der() const noexcept { return std::span(der_).first(der_size_); }
private:
    [[nodiscard]] SwapSignerStatus refuse(SwapSignerStatus) noexcept;
    [[nodiscard]] bool clock_valid(const swap::Intent&,std::uint64_t state_event) noexcept;
    void erase() noexcept;
    [[nodiscard]] SwapSignerStatus finish_authorized(std::int64_t);
    GatewayLink& link_;
    SwapSubmissionStore& store_;
    SwapSignerKey& key_;
    SwapSignerConfig config_;
    SwapSignerClock clock_;
    SwapSubmissionIdentity identity_{};
    Digest256 original_handle_digest_{};
    swap::Pending authority_pending_{};
    AuthorityBarrier authority_request_{};
    std::int64_t state_read_index_{};
    std::uint64_t sequence_{};
    std::array<std::byte,8192> payload_{};
    std::size_t size_{};
    std::int64_t minimum_index_{};
    std::uint64_t expected_sign_by_{};
    Tick last_tick_{},deadline_{};
    std::uint64_t last_event_{};
    std::array<std::byte,72> der_{};
    std::size_t der_size_{};
    swap_signing::ApiKeyStamp stamp_{};
    SwapSignerStatus status_{SwapSignerStatus::closed};
};
}
