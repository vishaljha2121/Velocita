// The request a gateway sends to an external signer; the gateway holds no key.
#pragma once

#include "velocita/v2/contracts.hpp"
#include "velocita/v2/venue_simulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace velocita::v2 {

inline constexpr std::size_t kMaxSignPayloads = 4;
inline constexpr std::size_t kMaxSignatureBytes = 256;

struct SigningIntent {
    CallerId caller{};
    std::uint64_t request_sequence{};
    VenueId venue_id{};
    AccountId account_id{};
    InstrumentId instrument_id{};
    EffectId effect_id{};
    ClusterOrderId cluster_order_id{};
    Digest256 effect_payload_digest{};
    PolicyId policy_id{};
    PolicyVersion policy_version{};
    Side side{};
    std::int64_t base_atoms{};
    std::int64_t quote_bound_atoms{};
    sim::Tick not_after{};
};

struct SigningRequest {
    SigningIntent intent{};
    std::string_view venue_handle{};
    std::size_t payload_count{};
    std::array<std::string_view, kMaxSignPayloads> payloads{};
};

enum class SignerVerdict : std::uint8_t {
    signed_ok = 1,
    refused_mismatch,
    refused_replay,
    refused_expired,
    refused_policy,
    unavailable,
};

struct Signature {
    std::array<char, kMaxSignatureBytes> text{};
    std::uint16_t length{};

    [[nodiscard]] std::string_view view() const noexcept { return {text.data(), length}; }
};

struct SignerResult {
    SignerVerdict verdict{SignerVerdict::unavailable};
    std::uint64_t signature_id{};
    std::uint32_t key_version{};
    std::size_t signature_count{};
    std::array<Signature, kMaxSignPayloads> signatures{};
};

struct Signer {
    SignerResult (*sign_fn)(void* context, const SigningRequest& request){};
    void* context{};

    [[nodiscard]] SignerResult sign(const SigningRequest& request) const {
        return sign_fn(context, request);
    }
};

static_assert(std::is_trivially_copyable_v<Signer>);

}
