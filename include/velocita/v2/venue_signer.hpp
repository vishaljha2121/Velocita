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

// ADR 0019. The gateway never holds signing material. It asks a Signer to sign
// the payload a venue returned, and the Signer decides by comparing what that
// payload would do with the committed intent below. There is deliberately no
// "sign these bytes" operation: a signer that signs whatever it is handed is a
// remote copy of the key.
//
// This build contains no production Signer. The only implementation is the
// deterministic test signer under tests/, which is not cryptographic.

inline constexpr std::size_t kMaxSignPayloads = 4;
inline constexpr std::size_t kMaxSignatureBytes = 256;

// Built from the committed OrderEffect and configuration, never from a venue
// reply. `quote_bound_atoms` is the worst quote-asset movement the kernel's
// reservation covers: for a buy the maximum total debit (limit notional plus
// the maximum fee), for a sell the minimum net credit (limit notional minus the
// maximum fee). A payload that could move more than this must not be signed.
struct SigningIntent {
    CallerId caller{};
    // Monotone per caller; the signer refuses a sequence it has already seen.
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
    // The signer refuses when its own clock is past this tick.
    sim::Tick not_after{};
};

struct SigningRequest {
    SigningIntent intent{};
    // The venue's name for what is being signed (quote id or order id).
    std::string_view venue_handle{};
    std::size_t payload_count{};
    std::array<std::string_view, kMaxSignPayloads> payloads{};
};

enum class SignerVerdict : std::uint8_t {
    signed_ok = 1,
    // A payload would do something other than the intent: side, asset, amount
    // or bound differ, or it cannot be decoded at all.
    refused_mismatch,
    // The request sequence or the venue handle was signed before.
    refused_replay,
    refused_expired,
    // Outside the signer's own policy (caller, venue, account, size).
    refused_policy,
    // No answer. Nothing was signed that the caller can use.
    unavailable,
};

struct Signature {
    std::array<char, kMaxSignatureBytes> text{};
    std::uint16_t length{};

    [[nodiscard]] std::string_view view() const noexcept { return {text.data(), length}; }
};

struct SignerResult {
    SignerVerdict verdict{SignerVerdict::unavailable};
    // Joins the signer's audit record to the gateway's and the venue's.
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

}  // namespace velocita::v2
