#pragma once

#include "velocita/v2/swap.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/swap_submission_store.hpp"

namespace velocita::v2 {

inline constexpr std::size_t kSwapPrepareStoreBytes=8768;
static_assert(truemarkets::kMaxSolanaEnvelope==8192 && truemarkets::kMaxTmTextBytes==96);
using SwapPrepareStoreCode=SwapSubmissionCode;
using SwapPrepareStoreStage=SwapSubmissionStage;
using SwapPrepareStoreFaultHook=SwapSubmissionFaultHook;
struct SwapPrepareBinding {
    swap::Binding allocation{};
    Digest256 authority_domain{},mapping_identity{};
    bool operator==(const SwapPrepareBinding&)const=default;
};
struct SwapPrepareOperation {
    EffectId effect{};
    Digest256 intent_digest{},request_digest{};
    truemarkets::SwapRestInput input{};
};
enum class SwapPreparePhase:std::uint8_t { idle=1,create_intent,prepared,released };
struct SwapPrepareRecord {
    SwapPrepareBinding binding{};
    SwapPrepareOperation operation{};
    SwapPreparePhase phase{SwapPreparePhase::idle};
    std::int64_t committed_index{};
    Digest256 state_digest{},response_digest{},checksum{};
    truemarkets::SwapRestPrepared prepared{};
    std::uint64_t ordinal{};
};
struct CallerVerifiedPrepareBootstrap {
    SwapPrepareBinding binding{};
    std::int64_t committed_index{};
    Digest256 state_digest{};
    EffectId last_effect{};
};
struct CallerVerifiedPreparePredecessor {
    swap::Settlement settlement{};
    std::int64_t committed_index{};
    Digest256 state_digest{};
};
class SwapPrepareStore final {
public:
    SwapPrepareStore(std::filesystem::path,SwapPrepareBinding,SwapPrepareStoreFaultHook={});
    ~SwapPrepareStore();
    SwapPrepareStore(const SwapPrepareStore&)=delete;
    SwapPrepareStore& operator=(const SwapPrepareStore&)=delete;
    [[nodiscard]] SwapPrepareStoreCode create(const CallerVerifiedPrepareBootstrap&);
    [[nodiscard]] SwapPrepareStoreCode recover();
    [[nodiscard]] SwapPrepareStoreCode begin(const SwapPrepareOperation&,std::int64_t,Digest256,
                                            const CallerVerifiedPreparePredecessor* =nullptr);
    [[nodiscard]] SwapPrepareStoreCode complete(const truemarkets::SwapRestPrepared&,Digest256 response_digest);
    [[nodiscard]] SwapPrepareStoreCode release(std::int64_t,Digest256);
    [[nodiscard]] SwapPrepareStoreCode status()const noexcept{return status_;}
    [[nodiscard]] const SwapPrepareRecord& latest()const noexcept{return latest_;}
private:
    [[nodiscard]] SwapPrepareStoreCode fail(SwapPrepareStoreCode)noexcept;
    [[nodiscard]] SwapPrepareStoreCode open_directory(bool);
    [[nodiscard]] SwapPrepareStoreCode replace(SwapPrepareRecord);
    [[nodiscard]] bool checkpoint(SwapPrepareStoreStage)noexcept;
    [[nodiscard]] bool identity_current()const noexcept;
    std::filesystem::path directory_path_;
    std::string leaf_;
    SwapPrepareBinding binding_;
    SwapPrepareStoreFaultHook hook_;
    SwapPrepareStoreCode status_{SwapPrepareStoreCode::closed};
    SwapPrepareRecord latest_{};
    int parent_{-1},directory_{-1},file_{-1};
    bool started_{};
};

}
