#pragma once

#include "velocita/v2/swap_result_watermark.hpp"

#include <filesystem>

namespace velocita::v2 {

inline constexpr std::size_t kSwapReconcileStoreBytes = 152;

struct SwapReconcileStoreBinding {
    Digest256 incarnation{}, authority_domain{};
    bool operator==(const SwapReconcileStoreBinding&) const = default;
};

struct CallerVerifiedCommittedResult {
    SwapReconcileStoreBinding binding{};
    std::int64_t index{};
    Digest256 result_digest{};
};

struct SwapReconcileStoredResult {
    CallerVerifiedCommittedResult result{};
    std::uint64_t ordinal{};
    Digest256 checksum{};
};

enum class SwapReconcileStoreCode : std::uint8_t {
    none, closed, invalid_input, missing, already_exists, insecure, locked,
    temporary_present, corrupt, io_failure, nonmonotonic, exhausted, watermark_conflict
};

enum class SwapReconcileStoreStage : std::uint8_t {
    parent_sync, partial_write, temporary_sync, rename, directory_sync, before_publish, recovery_sync
};

struct SwapReconcileStoreFaultHook {
    bool (*proceed)(SwapReconcileStoreStage, void*) noexcept {};
    void* context{};
};

class SwapReconcileStore final {
public:
    SwapReconcileStore(std::filesystem::path, SwapReconcileStoreBinding,
                       SwapResultWatermark&, SwapReconcileStoreFaultHook = {});
    ~SwapReconcileStore();
    SwapReconcileStore(const SwapReconcileStore&) = delete;
    SwapReconcileStore& operator=(const SwapReconcileStore&) = delete;
    [[nodiscard]] SwapReconcileStoreCode create(const CallerVerifiedCommittedResult&);
    [[nodiscard]] SwapReconcileStoreCode recover();
    [[nodiscard]] SwapReconcileStoreCode persist(const CallerVerifiedCommittedResult&);
    [[nodiscard]] SwapReconcileStoreCode status() const noexcept { return status_; }
    [[nodiscard]] const SwapReconcileStoredResult& latest() const noexcept { return latest_; }
private:
    [[nodiscard]] SwapReconcileStoreCode fail(SwapReconcileStoreCode) noexcept;
    [[nodiscard]] SwapReconcileStoreCode open_directory(bool);
    [[nodiscard]] SwapReconcileStoreCode replace(const CallerVerifiedCommittedResult&, std::uint64_t);
    [[nodiscard]] bool checkpoint(SwapReconcileStoreStage) noexcept;
    [[nodiscard]] bool identity_current() const noexcept;
    std::filesystem::path directory_path_;
    std::string leaf_;
    SwapReconcileStoreBinding binding_;
    SwapResultWatermark& watermark_;
    SwapReconcileStoreFaultHook hook_;
    SwapReconcileStoreCode status_{SwapReconcileStoreCode::closed};
    SwapReconcileStoredResult latest_{};
    int parent_{-1}, directory_{-1}, file_{-1};
    bool started_{};
};

}
