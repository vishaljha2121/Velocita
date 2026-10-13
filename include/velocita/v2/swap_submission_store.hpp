#pragma once

#include "velocita/v2/contracts.hpp"

#include <filesystem>
#include <optional>

namespace velocita::v2 {

inline constexpr std::size_t kSwapSubmissionHandleBytes = 128;
inline constexpr std::size_t kSwapSubmissionStoreBytes = 464;
using SwapSubmissionTransaction = std::array<std::byte,64>;

// Shared by finalized settlement admission and durable journal resolution.
[[nodiscard]] std::optional<Digest256> solana_signature_transaction_digest(const SwapSubmissionTransaction&) noexcept;

struct SwapSubmissionBinding {
    Digest256 incarnation{}, authority_domain{};
    bool operator==(const SwapSubmissionBinding&) const = default;
};

struct SwapSubmissionIdentity {
    EffectId effect{};
    Digest256 payload{}, execution_request{}, handle_digest{};
    std::array<std::byte,kSwapSubmissionHandleBytes> handle{};
    std::uint64_t handle_size{};
    bool operator==(const SwapSubmissionIdentity&) const = default;
};

struct CallerVerifiedSubmissionBootstrap {
    SwapSubmissionBinding binding{};
    std::int64_t committed_index{};
    Digest256 state_digest{};
    EffectId last_effect{};
};

struct CallerVerifiedSubmissionResolution {
    SwapSubmissionIdentity identity{};
    std::int64_t committed_index{};
    Digest256 evidence{};
};

enum class SwapSubmissionPhase : std::uint8_t { idle = 1, execute_intent, resolved };
enum class SwapSubmissionCode : std::uint8_t {
    none, closed, invalid_input, missing, already_exists, insecure, locked,
    temporary_present, corrupt, io_failure, bad_transition, exhausted
};
enum class SwapSubmissionStage : std::uint8_t {
    parent_sync, partial_write, temporary_sync, rename, directory_sync, before_publish, recovery_sync
};
struct SwapSubmissionFaultHook {
    bool (*proceed)(SwapSubmissionStage, void*) noexcept {};
    void* context{};
};
struct SwapSubmissionRecord {
    SwapSubmissionBinding binding{};
    SwapSubmissionIdentity identity{};
    SwapSubmissionTransaction transaction{};
    SwapSubmissionPhase phase{SwapSubmissionPhase::idle};
    std::int64_t committed_index{};
    Digest256 evidence{}, checksum{};
    std::uint64_t ordinal{};
};

class SwapSubmissionStore final {
public:
    SwapSubmissionStore(std::filesystem::path, SwapSubmissionBinding, SwapSubmissionFaultHook = {});
    ~SwapSubmissionStore();
    SwapSubmissionStore(const SwapSubmissionStore&) = delete;
    SwapSubmissionStore& operator=(const SwapSubmissionStore&) = delete;
    [[nodiscard]] SwapSubmissionCode create(const CallerVerifiedSubmissionBootstrap&);
    [[nodiscard]] SwapSubmissionCode recover();
    [[nodiscard]] SwapSubmissionCode begin(const SwapSubmissionIdentity&, std::int64_t committed_attempt_index);
    [[nodiscard]] SwapSubmissionCode observe_transaction(const SwapSubmissionIdentity&, const SwapSubmissionTransaction&);
    [[nodiscard]] SwapSubmissionCode resolve(const CallerVerifiedSubmissionResolution&);
    [[nodiscard]] SwapSubmissionCode status() const noexcept { return status_; }
    [[nodiscard]] const SwapSubmissionRecord& latest() const noexcept { return latest_; }
private:
    [[nodiscard]] SwapSubmissionCode fail(SwapSubmissionCode) noexcept;
    [[nodiscard]] SwapSubmissionCode open_directory(bool);
    [[nodiscard]] SwapSubmissionCode replace(SwapSubmissionRecord);
    [[nodiscard]] bool checkpoint(SwapSubmissionStage) noexcept;
    [[nodiscard]] bool identity_current() const noexcept;
    std::filesystem::path directory_path_;
    std::string leaf_;
    SwapSubmissionBinding binding_;
    SwapSubmissionFaultHook hook_;
    SwapSubmissionCode status_{SwapSubmissionCode::closed};
    SwapSubmissionRecord latest_{};
    int parent_{-1}, directory_{-1}, file_{-1};
    bool started_{};
};

}
