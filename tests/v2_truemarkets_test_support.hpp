#pragma once

// Test-only True Markets stand-in and signer. Neither is linked by any
// production target. The fake renders and parses its own text and never calls
// the adapter's encoders or decoders, so a test cannot pass by agreeing with
// itself.
//
// MOCK PAYLOAD FORMAT. The real payload is an unsigned transaction for the
// account's custodial signer, and its inner shape was omitted from the evidence.
// Here each payload is {"unsigned_tx":"MOCKTX1;side=S;base=B;bound=Q;handle=H",
// "organization_id":"org-test"}: B base atoms, Q the quote-atom bound the
// transaction would enforce (buy: maximum total debit; sell: minimum net credit).

#include "velocita/v2/digest.hpp"
#include "velocita/v2/truemarkets_venue.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tm_test {

using namespace velocita::v2;
using truemarkets::ApiCall;
using truemarkets::ApiReply;
using truemarkets::ApiStatus;
using sim::Tick;

inline std::string decimal(const std::int64_t atoms, const int decimals) {
    std::string digits = std::to_string(atoms);
    if (decimals == 0) return digits;
    while (static_cast<int>(digits.size()) <= decimals) digits.insert(0, "0");
    std::string text = digits.substr(0, digits.size() - decimals) + "." +
                       digits.substr(digits.size() - decimals);
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    return text;
}

// Exact inverse of `decimal` for the fake's own parsing of requests.
inline std::int64_t atoms_of(const std::string& text, const int decimals) {
    const auto point = text.find('.');
    std::string whole = point == std::string::npos ? text : text.substr(0, point);
    std::string fraction = point == std::string::npos ? "" : text.substr(point + 1);
    while (static_cast<int>(fraction.size()) < decimals) fraction += '0';
    return std::stoll(whole + fraction);
}

inline std::string field(const std::string_view text, const std::string& key) {
    const std::string needle = "\"" + key + "\":\"";
    const auto at = text.find(needle);
    if (at == std::string_view::npos) return {};
    const auto begin = at + needle.size();
    const auto end = text.find('"', begin);
    return std::string(text.substr(begin, end - begin));
}

inline std::string hex(const Digest256& digest) { return to_hex(digest); }

inline std::string sha_hex(const std::string& text) {
    return hex(sha256_digest(std::as_bytes(std::span(text.data(), text.size()))));
}

// ---------------------------------------------------------------------------

struct FakeClock {
    Tick now{0};
    static Tick read(void* self) noexcept { return static_cast<FakeClock*>(self)->now; }
    truemarkets::TrueMarketsClock handle() { return {&FakeClock::read, this}; }
};

enum class Script : std::uint8_t {
    normal,
    tool_error,
    rate_limited,
    // The request never reached the venue.
    lost_request,
    // The venue acted, the reply was lost.
    lost_reply,
    // trade_prepare only: submitted directly, no payloads (D-26).
    direct_submission,
};

enum class Settlement : std::uint8_t { filled, failed, partial, pending_forever };

struct Swap {
    std::string handle;
    std::string order_id;  // assigned at execution
    Side side{};
    std::int64_t base_atoms{};
    std::int64_t gross{};
    std::int64_t fee{};
    std::int64_t qty_out{};
    bool executed{};
    Settlement settlement{Settlement::filled};
    int pending_polls{};
    std::string tx;
    bool booked{};  // applied once to the venue's own ledger
};

struct FakeTrueMarkets {
    // Market: quote micros per whole base unit, and the flat fee.
    std::int64_t price_micros{119'500'000};
    std::int64_t fee_bps{100};
    int base_decimals{9};
    int quote_decimals{6};
    std::string base_asset{"So11111111111111111111111111111111111111112"};
    std::string quote_asset{"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v"};
    bool status_accepts_quote_id{};
    Settlement next_settlement{Settlement::filled};
    int pending_polls{0};
    FakeClock* clock{};
    Tick latency{0};

    // Scripts consumed per call kind; empty means normal.
    std::map<ApiCall, std::deque<Script>> scripts;
    // Quote-reply tampering, applied to the next quote-shaped reply.
    std::string reply_side_override;
    std::string reply_base_override;
    std::string reply_qty_override;
    std::string reply_issue;
    bool reply_without_payloads{};
    std::int64_t payload_bound_delta{};

    std::vector<ApiCall> calls;
    std::vector<std::string> requests;
    std::map<std::string, Swap> swaps;  // by handle
    std::vector<std::string> received_signatures;
    std::int64_t quote_balance{25'000'000};
    std::int64_t base_balance{0};
    int counter{0};

    [[nodiscard]] std::size_t count(const ApiCall call) const {
        std::size_t total = 0;
        for (const auto each : calls) total += each == call ? 1 : 0;
        return total;
    }

    truemarkets::TrueMarketsApi handle() { return {&FakeTrueMarkets::dispatch, this}; }

    static ApiReply dispatch(void* self, const ApiCall call, const std::string_view request,
                             const std::span<char> response, const Tick now) {
        return static_cast<FakeTrueMarkets*>(self)->serve(call, request, response, now);
    }

    Script next_script(const ApiCall call) {
        auto& queue = scripts[call];
        if (queue.empty()) return Script::normal;
        const auto script = queue.front();
        queue.pop_front();
        return script;
    }

    static ApiReply write(const std::string& body, const std::span<char> response,
                          const ApiStatus status = ApiStatus::completed) {
        if (body.size() > response.size()) return {ApiStatus::no_response, 0};
        std::copy(body.begin(), body.end(), response.begin());
        return {status, body.size()};
    }

    void execute(Swap& swap) {
        swap.executed = true;
        swap.settlement = next_settlement;
        swap.pending_polls = pending_polls;
        swap.tx = "tx" + std::to_string(++counter) + "AbC";
        if (swap.order_id.empty()) swap.order_id = "ord-" + std::to_string(++counter);
    }

    // The venue's own economic truth, applied when a swap settles.
    void settle(Swap& swap, const std::int64_t base) {
        if (swap.side == Side::buy) {
            quote_balance -= swap.qty_out;
            base_balance += base;
        } else {
            quote_balance += swap.qty_out;
            base_balance -= base;
        }
    }

    std::string payload_for(const Swap& swap) const {
        const auto bound = swap.qty_out + payload_bound_delta;
        return std::string("{\"unsigned_tx\":\"MOCKTX1;side=") +
               (swap.side == Side::buy ? "buy" : "sell") + ";base=" +
               std::to_string(swap.base_atoms) + ";bound=" + std::to_string(bound) +
               ";handle=" + swap.handle + "\",\"organization_id\":\"org-test\"}";
    }

    std::string trade_reply(const Swap& swap, const bool prepare, const bool payloads) {
        std::string body = "{";
        body += prepare ? "\"order_id\":\"" : "\"quote_id\":\"";
        body += swap.handle + "\",";
        body += "\"base_asset\":\"" + (reply_base_override.empty() ? base_asset : reply_base_override) + "\",";
        body += "\"quote_asset\":\"" + quote_asset + "\",";
        body += "\"order_side\":\"" + (reply_side_override.empty()
                                           ? std::string(swap.side == Side::buy ? "buy" : "sell")
                                           : reply_side_override) + "\",";
        body += "\"qty\":\"" + (reply_qty_override.empty() ? decimal(swap.base_atoms, base_decimals)
                                                           : reply_qty_override) + "\",";
        body += "\"qty_out\":\"" + decimal(swap.qty_out, quote_decimals) + "\",";
        body += "\"fee\":\"" + decimal(swap.fee, quote_decimals) + "\",";
        body += "\"fee_asset\":\"" + quote_asset + "\",";
        body += "\"issues\":[";
        if (!reply_issue.empty()) body += "{\"message\":\"" + reply_issue + "\"}";
        body += "]";
        if (payloads) body += ",\"payloads\":[" + payload_for(swap) + "]";
        body += "}";
        return body;
    }

    ApiReply serve(const ApiCall call, const std::string_view request, const std::span<char> response,
                   const Tick) {
        calls.push_back(call);
        requests.emplace_back(request);
        if (clock) clock->now += latency;
        const auto script = next_script(call);
        if (script == Script::tool_error) return write("{\"message\":\"internal\"}", response, ApiStatus::tool_error);
        if (script == Script::rate_limited) return {ApiStatus::rate_limited, 0};
        if (script == Script::lost_request) return {ApiStatus::no_response, 0};

        if (call == ApiCall::get_quote || call == ApiCall::trade_prepare) {
            const bool prepare = call == ApiCall::trade_prepare;
            Swap swap;
            swap.handle = (prepare ? "ord-p" : "qt-") + std::to_string(++counter);
            if (prepare) swap.order_id = swap.handle;
            swap.side = field(request, "side") == "buy" ? Side::buy : Side::sell;
            swap.base_atoms = atoms_of(field(request, "qty"), base_decimals);
            // Round the gross against the client, then a ceiling fee.
            const auto product = static_cast<__int128>(swap.base_atoms) * price_micros;
            const __int128 scale = [&] { __int128 s = 1; for (int i = 0; i < base_decimals; ++i) s *= 10; return s; }();
            swap.gross = static_cast<std::int64_t>(swap.side == Side::buy ? (product + scale - 1) / scale
                                                                          : product / scale);
            swap.fee = (swap.gross * fee_bps + 9'999) / 10'000;
            swap.qty_out = swap.side == Side::buy ? swap.gross + swap.fee : swap.gross - swap.fee;
            const bool direct = prepare && script == Script::direct_submission;
            if (direct) execute(swap);
            const auto body = trade_reply(swap, prepare, !direct && !reply_without_payloads);
            swaps[swap.handle] = swap;
            if (script == Script::lost_reply) {
                if (prepare) execute(swaps[swap.handle]);
                return {ApiStatus::no_response, 0};
            }
            return write(body, response);
        }
        if (call == ApiCall::trade_execute) {
            const auto handle = field(request, "order_id");
            auto found = swaps.find(handle);
            if (found == swaps.end() || found->second.executed) {
                return write("{\"message\":\"unknown order\"}", response, ApiStatus::tool_error);
            }
            received_signatures.emplace_back(request);
            if (found->second.order_id.empty()) found->second.order_id = "ord-q" + std::to_string(++counter);
            execute(found->second);
            if (script == Script::lost_reply) return {ApiStatus::no_response, 0};
            return write("{\"order_id\":\"" + found->second.order_id + "\",\"status\":\"submitted\"}", response);
        }
        // order_status: /v1/conductor/orders/{id}/status
        const std::string path(request);
        const std::string prefix = "/v1/conductor/orders/";
        const auto id = path.substr(prefix.size(), path.size() - prefix.size() - std::string("/status").size());
        Swap* swap = nullptr;
        for (auto& [handle, each] : swaps) {
            if (each.order_id == id || (status_accepts_quote_id && each.handle == id)) swap = &each;
        }
        if (swap == nullptr) return write("{\"type\":\"not_found\"}", response, ApiStatus::tool_error);
        if (!swap->executed) {
            return write("{\"order_id\":\"" + id + "\",\"status\":\"expired\"}", response);
        }
        if (swap->settlement == Settlement::pending_forever || swap->pending_polls > 0) {
            if (swap->pending_polls > 0) --swap->pending_polls;
            return write("{\"order_id\":\"" + id + "\",\"status\":\"pending\"}", response);
        }
        if (swap->settlement == Settlement::failed) {
            return write("{\"order_id\":\"" + id + "\",\"status\":\"failed\"}", response);
        }
        const auto base = swap->settlement == Settlement::partial ? swap->base_atoms / 2 : swap->base_atoms;
        const auto gross = swap->settlement == Settlement::partial ? swap->gross / 2 : swap->gross;
        const auto fee = swap->settlement == Settlement::partial ? swap->fee / 2 : swap->fee;
        const auto out = swap->side == Side::buy ? gross + fee : gross - fee;
        if (!swap->booked) {
            settle(*swap, base);
            swap->booked = true;
        }
        const std::string& tx = swap->tx;
        return write("{\"order_id\":\"" + id + "\",\"status\":\"filled\",\"qty\":\"" +
                         decimal(base, base_decimals) + "\",\"qty_out\":\"" + decimal(out, quote_decimals) +
                         "\",\"fee\":\"" + decimal(fee, quote_decimals) + "\",\"tx_signature\":\"" + tx + "\"}",
                     response);
    }
};

// ---------------------------------------------------------------------------
// Deterministic TEST signer. Not cryptographic: the "signature" is a SHA-256
// of a fixed label, key label and the payload. It exists to exercise the
// inspect-then-sign contract, replay refusal and audit join.

struct AuditRecord {
    std::uint64_t request_sequence{};
    EffectId effect_id{};
    SignerVerdict verdict{};
    std::uint32_t key_version{};
    PolicyVersion policy_version{};
    std::uint64_t signature_id{};
    std::string payload_digest;
};

struct TestSigner {
    CallerId caller{22};
    VenueId venue{9};
    AccountId account{5};
    std::uint32_t key_version{1};
    std::string key_label{"test-key-1"};
    FakeClock* clock{};
    Tick latency{0};
    bool unavailable{};

    std::uint64_t last_sequence{};
    std::vector<std::string> signed_handles;
    std::vector<AuditRecord> audit;
    std::uint64_t next_signature_id{0};
    std::size_t calls{};
    std::vector<SigningIntent> intents;

    Signer handle() { return {&TestSigner::dispatch, this}; }

    static SignerResult dispatch(void* self, const SigningRequest& request) {
        return static_cast<TestSigner*>(self)->sign(request);
    }

    static std::optional<std::string> tx_field(const std::string& tx, const std::string& key) {
        const auto at = tx.find(key + "=");
        if (at == std::string::npos) return std::nullopt;
        const auto begin = at + key.size() + 1;
        const auto end = tx.find(';', begin);
        return tx.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }

    SignerResult refuse(const SigningRequest& request, const SignerVerdict verdict) {
        audit.push_back({request.intent.request_sequence, request.intent.effect_id, verdict,
                         key_version, request.intent.policy_version, 0, {}});
        return {verdict, 0, key_version, 0, {}};
    }

    SignerResult sign(const SigningRequest& request) {
        ++calls;
        intents.push_back(request.intent);
        if (unavailable) return {SignerVerdict::unavailable, 0, 0, 0, {}};
        const auto& intent = request.intent;
        if (intent.caller != caller || intent.venue_id != venue || intent.account_id != account)
            return refuse(request, SignerVerdict::refused_policy);
        if (intent.request_sequence <= last_sequence) return refuse(request, SignerVerdict::refused_replay);
        for (const auto& seen : signed_handles)
            if (seen == request.venue_handle) return refuse(request, SignerVerdict::refused_replay);
        if (clock && clock->now > intent.not_after) return refuse(request, SignerVerdict::refused_expired);
        if (request.payload_count == 0) return refuse(request, SignerVerdict::refused_mismatch);
        for (std::size_t i = 0; i < request.payload_count; ++i) {
            const auto tx = field(request.payloads[i], "unsigned_tx");
            if (tx.rfind("MOCKTX1;", 0) != 0) return refuse(request, SignerVerdict::refused_mismatch);
            const auto side = tx_field(tx, "side");
            const auto base = tx_field(tx, "base");
            const auto bound = tx_field(tx, "bound");
            const auto handle = tx_field(tx, "handle");
            if (!side || !base || !bound || !handle) return refuse(request, SignerVerdict::refused_mismatch);
            const bool side_ok = *side == (intent.side == Side::buy ? "buy" : "sell");
            const bool base_ok = std::stoll(*base) == intent.base_atoms;
            const auto quote = std::stoll(*bound);
            const bool bound_ok = intent.side == Side::buy ? quote <= intent.quote_bound_atoms
                                                           : quote >= intent.quote_bound_atoms;
            if (!side_ok || !base_ok || !bound_ok || *handle != request.venue_handle)
                return refuse(request, SignerVerdict::refused_mismatch);
        }
        last_sequence = intent.request_sequence;
        signed_handles.emplace_back(request.venue_handle);
        SignerResult result;
        result.verdict = SignerVerdict::signed_ok;
        result.signature_id = ++next_signature_id;
        result.key_version = key_version;
        result.signature_count = request.payload_count;
        for (std::size_t i = 0; i < request.payload_count; ++i) {
            const auto text = sha_hex("velocita-test-signer/v1|" + key_label + "|" +
                                      std::string(request.payloads[i]));
            std::copy(text.begin(), text.end(), result.signatures[i].text.begin());
            result.signatures[i].length = static_cast<std::uint16_t>(text.size());
        }
        audit.push_back({intent.request_sequence, intent.effect_id, SignerVerdict::signed_ok, key_version,
                         intent.policy_version, result.signature_id,
                         sha_hex(std::string(request.payloads[0]))});
        // A slow signer: the time passes after it decided, so only the
        // adapter's own freshness check can notice.
        if (clock) clock->now += latency;
        return result;
    }
};

}  // namespace tm_test
