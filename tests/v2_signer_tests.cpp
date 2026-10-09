#include "velocita/v2/signer_service.hpp"

#include "velocita/v2/digest.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace velocita::v2;
using namespace velocita::v2::signer;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const std::string kPayload =
    "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\"}";
const std::string kPayloadSignature = "322acb0c3db0b52a76c3b42bcd38bab3f5f54f5ea5a49825552e81f703630055";
const std::string kPayloadSha256 = "11203f2da21210af33b8e848a8973fa66adcf83cbcf217289fdd366a0528d3a4";

std::string payload(const std::string& side, const std::int64_t base, const std::int64_t bound,
                    const std::string& handle) {
    return "{\"unsigned_tx\":\"MOCKTX1;side=" + side + ";base=" + std::to_string(base) + ";bound=" +
           std::to_string(bound) + ";handle=" + handle + "\",\"organization_id\":\"org-test\"}";
}

std::filesystem::path fresh_directory(const char* leaf) {
    auto pattern = (std::filesystem::temp_directory_path() / (std::string(leaf) + "-XXXXXX")).string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    require(::mkdtemp(bytes.data()) != nullptr, "mkdtemp failed");
    return std::filesystem::canonical(bytes.data());
}

struct Directory {
    std::filesystem::path path;
    explicit Directory(const char* leaf) : path(fresh_directory(leaf)) {}
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

std::array<std::byte, kTestKeyBytes> test_key_bytes() {
    std::array<std::byte, kTestKeyBytes> key{};
    for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::byte>(i + 1);
    return key;
}

std::filesystem::path write_test_key(const std::filesystem::path& directory) {
    const auto file = directory / "test.key";
    std::ofstream(file, std::ios::binary) << TestKey::file_text(test_key_bytes());
    return file;
}

struct Rig {
    Directory directory{"v2-signer-core"};
    TestKey key = *TestKey::load(write_test_key(directory.path));
    std::vector<AuditRecord> audit;
    std::vector<std::string> order;
    std::uint64_t persisted{};
    sim::Tick now{1'000};
    bool audit_fails{}, store_fails{}, key_fails{};
    SignerPolicy policy{CallerId{22}, VenueId{9}, AccountId{5}, InstrumentId{4}};
    std::unique_ptr<SignerCore> core;

    static bool append(void* self, const AuditRecord& record) noexcept {
        auto& rig = *static_cast<Rig*>(self);
        rig.order.emplace_back("audit");
        if (rig.audit_fails) return false;
        rig.audit.push_back(record);
        return true;
    }
    static bool persist(void* self, const std::uint64_t floor) noexcept {
        auto& rig = *static_cast<Rig*>(self);
        rig.order.emplace_back("persist");
        if (rig.store_fails) return false;
        rig.persisted = floor;
        return true;
    }
    static sim::Tick clock(void* self) noexcept { return static_cast<Rig*>(self)->now; }
    static bool sign(void* self, const std::string_view text, Signature& out) noexcept {
        auto& rig = *static_cast<Rig*>(self);
        rig.order.emplace_back("sign");
        if (rig.key_fails) return false;
        const auto real = rig.key.holder(7);
        return real.sign_fn(real.context, text, out);
    }

    explicit Rig(const std::uint64_t floor = 10) {
        core = std::make_unique<SignerCore>(policy, &decode_mock_swap_payload, KeyHolder{&Rig::sign, this, 7},
                                            AuditSink{&Rig::append, this}, SequenceStore{&Rig::persist, this},
                                            SignerClock{&Rig::clock, this}, floor);
    }
};

SigningRequest request_for(const std::string& text, const std::uint64_t sequence = 11,
                           const std::string_view handle = "q-1") {
    SigningRequest request;
    request.intent.caller = CallerId{22};
    request.intent.request_sequence = sequence;
    request.intent.venue_id = VenueId{9};
    request.intent.account_id = AccountId{5};
    request.intent.instrument_id = InstrumentId{4};
    request.intent.effect_id = EffectId{77};
    request.intent.cluster_order_id = ClusterOrderId{88};
    request.intent.effect_payload_digest.bytes[0] = std::byte{0x5a};
    request.intent.policy_id = PolicyId{129};
    request.intent.policy_version = PolicyVersion{3};
    request.intent.side = Side::buy;
    request.intent.base_atoms = 120;
    request.intent.quote_bound_atoms = 2'500;
    request.intent.not_after = 1'000;
    request.venue_handle = handle;
    request.payload_count = 1;
    request.payloads[0] = text;
    return request;
}

void a_request_inside_its_intent_is_signed_persisted_and_audited() {
    Rig rig;
    const auto result = rig.core->sign(request_for(kPayload));
    require(result.verdict == SignerVerdict::signed_ok && result.signature_count == 1 &&
                result.signatures[0].view() == kPayloadSignature && result.signature_id == 11 &&
                result.key_version == 7,
            "a valid request was not signed with the expected test signature");
    require(rig.persisted == 11 && rig.core->sequence_floor() == 11, "the sequence floor did not advance");
    require(rig.order == std::vector<std::string>{"persist", "sign", "audit"},
            "the floor was not made durable before signing, or the audit did not follow the signature");
    require(rig.audit.size() == 1 && rig.audit[0].request_sequence == 11 && rig.audit[0].effect_id == EffectId{77} &&
                rig.audit[0].verdict == SignerVerdict::signed_ok && rig.audit[0].key_version == 7 &&
                rig.audit[0].policy_version == PolicyVersion{3} && rig.audit[0].signature_id == 11 &&
                to_hex(rig.audit[0].payload_digest) == kPayloadSha256,
            "the audit record does not describe the signature");
}

void every_departure_from_the_intent_is_refused_and_nothing_is_signed() {
    struct Case {
        const char* name;
        SignerVerdict verdict;
        std::function<void(SigningRequest&, Rig&)> change;
    };
    static const std::string wrong_side = payload("sell", 120, 2'500, "q-1");
    static const std::string wrong_base = payload("buy", 121, 2'500, "q-1");
    static const std::string over_bound = payload("buy", 120, 2'501, "q-1");
    static const std::string other_handle = payload("buy", 120, 2'500, "q-2");
    static const std::string not_mock = "{\"unsigned_tx\":\"AQAAAA==\",\"organization_id\":\"org-test\"}";
    static const std::string long_handle(kMaxHandleBytes + 1, 'h');
    const std::vector<Case> cases{
        {"another caller", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.caller = CallerId{21}; }},
        {"another venue", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.venue_id = VenueId{8}; }},
        {"another account", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.account_id = AccountId{6}; }},
        {"another instrument", SignerVerdict::refused_policy,
         [](auto& r, auto&) { r.intent.instrument_id = InstrumentId{3}; }},
        {"no effect", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.effect_id = {}; }},
        {"no order", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.cluster_order_id = {}; }},
        {"no effect digest", SignerVerdict::refused_policy, [](auto& r, auto&) { r.intent.effect_payload_digest = {}; }},
        {"the floor sequence", SignerVerdict::refused_replay, [](auto& r, auto&) { r.intent.request_sequence = 10; }},
        {"a sequence below the floor", SignerVerdict::refused_replay,
         [](auto& r, auto&) { r.intent.request_sequence = 9; }},
        {"no handle", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.venue_handle = {}; }},
        {"a handle too long", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.venue_handle = long_handle; }},
        {"one tick past expiry", SignerVerdict::refused_expired, [](auto&, auto& rig) { rig.now = 1'001; }},
        {"no payload", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.payload_count = 0; }},
        {"too many payloads", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.payload_count = kMaxSignPayloads + 1; }},
        {"the other side", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.payloads[0] = wrong_side; }},
        {"another quantity", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.payloads[0] = wrong_base; }},
        {"a debit one atom over the bound", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.payloads[0] = over_bound; }},
        {"a payload for another quote", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.payloads[0] = other_handle; }},
        {"a payload it cannot decode", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.payloads[0] = not_mock; }},
        {"a second payload outside the intent", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.payload_count = 2; r.payloads[1] = over_bound; }},
        {"no quantity in the intent", SignerVerdict::refused_mismatch, [](auto& r, auto&) { r.intent.base_atoms = 0; }},
        {"no bound in the intent", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.intent.quote_bound_atoms = 0; }},
        {"an unknown side", SignerVerdict::refused_mismatch,
         [](auto& r, auto&) { r.intent.side = static_cast<Side>(3); }},
    };
    for (const auto& item : cases) {
        Rig rig;
        auto request = request_for(kPayload);
        item.change(request, rig);
        const auto result = rig.core->sign(request);
        require(result.verdict == item.verdict, std::string(item.name) + ": verdict " +
                    std::to_string(static_cast<int>(result.verdict)));
        require(result.signature_count == 0 && result.signatures[0].length == 0 && result.signature_id == 0,
                std::string(item.name) + ": a refusal carried a signature");
        require(rig.persisted == 0 && rig.core->sequence_floor() == 10 &&
                    rig.order == std::vector<std::string>{"audit"},
                std::string(item.name) + ": a refusal moved the floor, used the key or was not audited");
        require(rig.audit.size() == 1 && rig.audit[0].verdict == item.verdict && rig.audit[0].signature_id == 0,
                std::string(item.name) + ": the refusal's audit record is wrong");
        rig.now = 1'000;
        require(rig.core->sign(request_for(kPayload)).verdict == SignerVerdict::signed_ok,
                std::string(item.name) + ": a refusal blocked the next valid request");
    }
}

void bounds_are_inclusive_on_the_safe_side() {
    {
        Rig rig;
        const auto exact = payload("buy", 120, 2'500, "q-1");
        const auto cheaper = payload("buy", 120, 2'499, "q-9");
        require(rig.core->sign(request_for(exact)).verdict == SignerVerdict::signed_ok,
                "a buy debiting exactly the bound was refused");
        require(rig.core->sign(request_for(cheaper, 12, "q-9")).verdict == SignerVerdict::signed_ok,
                "a buy debiting less than the bound was refused");
    }
    {
        Rig rig;
        auto sell = request_for("");
        sell.intent.side = Side::sell;
        const auto exact = payload("sell", 120, 2'500, "q-1");
        const auto richer = payload("sell", 120, 2'501, "q-1");
        const auto poorer = payload("sell", 120, 2'499, "q-1");
        sell.payloads[0] = poorer;
        require(rig.core->sign(sell).verdict == SignerVerdict::refused_mismatch,
                "a sell crediting one atom under the bound was signed");
        sell.payloads[0] = exact;
        require(rig.core->sign(sell).verdict == SignerVerdict::signed_ok, "a sell crediting exactly the bound was refused");
        sell.intent.request_sequence = 12;
        sell.payloads[0] = richer;
        require(rig.core->sign(sell).verdict == SignerVerdict::refused_replay,
                "the same quote was signed twice under a new sequence");
    }
    {
        Rig rig;
        rig.now = 1'000;
        require(rig.core->sign(request_for(kPayload)).verdict == SignerVerdict::signed_ok,
                "a request exactly at its expiry was refused");
    }
}

void a_sequence_or_a_quote_is_never_signed_twice() {
    Rig rig;
    require(rig.core->sign(request_for(kPayload)).verdict == SignerVerdict::signed_ok, "fixture: first signature refused");
    require(rig.core->sign(request_for(kPayload)).verdict == SignerVerdict::refused_replay,
            "the same request was signed twice");
    const auto other = payload("buy", 120, 2'500, "q-2");
    require(rig.core->sign(request_for(other, 11, "q-2")).verdict == SignerVerdict::refused_replay,
            "a used sequence signed another quote");
    require(rig.core->sign(request_for(kPayload, 12)).verdict == SignerVerdict::refused_replay,
            "a signed quote was signed again under the next sequence");
    require(rig.core->sign(request_for(other, 13, "q-2")).verdict == SignerVerdict::signed_ok && rig.persisted == 13,
            "a new quote under a new sequence was refused, or a skipped sequence was not accepted");
    require(rig.core->sign(request_for(kPayload, 12)).verdict == SignerVerdict::refused_replay,
            "a sequence below the floor was accepted after a later one");

    Rig restarted(13);
    require(restarted.core->sign(request_for(kPayload, 13)).verdict == SignerVerdict::refused_replay &&
                restarted.core->sign(request_for(kPayload, 14)).verdict == SignerVerdict::signed_ok,
            "a restarted signer did not start from its durable floor");

    Rig many(0);
    std::vector<std::string> texts;
    for (std::size_t index = 0; index < kSignedHandleCache; ++index) {
        const auto handle = "h" + std::to_string(index);
        texts.push_back(payload("buy", 120, 2'500, handle));
        require(many.core->sign(request_for(texts.back(), index + 1, handle)).verdict == SignerVerdict::signed_ok,
                "fixture: could not fill the handle cache");
    }
    const std::string last = "h" + std::to_string(kSignedHandleCache - 1);
    require(many.core->sign(request_for(texts.back(), kSignedHandleCache + 1, last)).verdict ==
                SignerVerdict::refused_replay,
            "the newest of many signed quotes was signed again");
    require(many.core->sign(request_for(texts.front(), kSignedHandleCache + 1, "h0")).verdict ==
                SignerVerdict::refused_replay,
            "the oldest quote still in the cache was signed again");
}

void no_signature_leaves_without_a_durable_floor_and_an_audit_record() {
    {
        Rig rig;
        rig.store_fails = true;
        const auto result = rig.core->sign(request_for(kPayload));
        require(result.verdict == SignerVerdict::unavailable && result.signature_count == 0 &&
                    result.signatures[0].length == 0 && rig.core->sequence_floor() == 10 &&
                    rig.order == std::vector<std::string>{"persist"},
                "a signature was made although the floor could not be made durable");
        rig.store_fails = false;
        require(rig.core->sign(request_for(kPayload)).verdict == SignerVerdict::signed_ok,
                "the request could not be retried after the floor store recovered");
    }
    {
        Rig rig;
        rig.audit_fails = true;
        const auto result = rig.core->sign(request_for(kPayload));
        require(result.verdict == SignerVerdict::unavailable && result.signature_count == 0 &&
                    result.signatures[0].length == 0 && result.signature_id == 0,
                "a signature left the signer although it could not be audited");
        require(rig.core->sequence_floor() == 11, "a signature that was computed did not consume its sequence");
    }
    {
        Rig rig;
        rig.key_fails = true;
        const auto result = rig.core->sign(request_for(kPayload));
        require(result.verdict == SignerVerdict::unavailable && result.signature_count == 0 && rig.audit.empty(),
                "a failed key operation was reported as a signature");
    }
    {
        Rig rig;
        SignerCore unwired(rig.policy, nullptr, KeyHolder{&Rig::sign, &rig, 7}, AuditSink{&Rig::append, &rig},
                           SequenceStore{&Rig::persist, &rig}, SignerClock{&Rig::clock, &rig}, 10);
        require(unwired.sign(request_for(kPayload)).verdict == SignerVerdict::unavailable && rig.order.empty(),
                "a signer with no payload decoder signed");
    }
}

void the_mock_payload_decoder_accepts_one_exact_shape() {
    const auto decoded = decode_mock_swap_payload(kPayload);
    require(decoded && decoded->side == Side::buy && decoded->base_atoms == 120 && decoded->quote_bound_atoms == 2'500 &&
                decoded->handle == "q-1",
            "the fixture payload did not decode");
    const auto sell_text = payload("sell", 1, 999'999'999'999'999'999, "A.b-c_9");
    const auto sell = decode_mock_swap_payload(sell_text);
    require(sell && sell->side == Side::sell && sell->base_atoms == 1 &&
                sell->quote_bound_atoms == 999'999'999'999'999'999 && sell->handle == "A.b-c_9",
            "an 18-digit bound or a sell did not decode");
    const std::vector<std::string> refused{
        "",
        "{}",
        kPayload + " ",
        " " + kPayload,
        kPayload.substr(0, kPayload.size() - 1),
        "{\"unsigned_tx\":\"MOCKTX2;side=buy;base=120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=hold;base=120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\"}",
        payload("buy", 0, 2'500, "q-1"),
        payload("buy", 120, 0, "q-1"),
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=0120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=-120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=1000000000000000000;handle=q-1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q;1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q 1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q\\\\1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q-1;bound=1\",\"organization_id\":\"org-test\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q-1\",\"organization_id\":\"\"}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q-1\",\"organization_id\":\"org-test\",\"x\":1}",
        "{\"unsigned_tx\":\"MOCKTX1;side=buy;base=120;bound=2500;handle=q-1\"}",
        payload("buy", 120, 2'500, std::string(kMaxHandleBytes + 1, 'h')),
    };
    for (const auto& text : refused)
        require(!decode_mock_swap_payload(text), "a malformed payload decoded: " + text.substr(0, 90));
    require(decode_mock_swap_payload(payload("buy", 120, 2'500, std::string(kMaxHandleBytes, 'h'))).has_value(),
            "a handle of the maximum length was refused");
}

void requests_and_responses_round_trip_and_reject_damage() {
    auto request = request_for(kPayload);
    const auto second = payload("buy", 120, 2'400, "q-1");
    request.payload_count = 2;
    request.payloads[1] = second;
    request.intent.side = Side::sell;
    request.intent.base_atoms = 0x0102030405060708;
    request.intent.quote_bound_atoms = 0x1112131415161718;
    request.intent.not_after = 0x2122232425262728;
    std::vector<std::byte> bytes(kMaxRequestBytes);
    const auto encoded = encode_request(request, bytes);
    require(encoded.error == WireError::none && frame_length(std::span(bytes).first(8)) == encoded.bytes_written,
            "a request did not encode with a readable length");
    OwnedSigningRequest back;
    require(decode_request(std::span(bytes).first(encoded.bytes_written), back) == WireError::none,
            "an encoded request did not decode");
    const auto& a = request.intent;
    const auto& b = back.request.intent;
    require(a.caller == b.caller && a.request_sequence == b.request_sequence && a.venue_id == b.venue_id &&
                a.account_id == b.account_id && a.instrument_id == b.instrument_id && a.effect_id == b.effect_id &&
                a.cluster_order_id == b.cluster_order_id && a.effect_payload_digest == b.effect_payload_digest &&
                a.policy_id == b.policy_id && a.policy_version == b.policy_version && a.side == b.side &&
                a.base_atoms == b.base_atoms && a.quote_bound_atoms == b.quote_bound_atoms && a.not_after == b.not_after &&
                back.request.venue_handle == "q-1" && back.request.payload_count == 2 &&
                back.request.payloads[0] == kPayload && back.request.payloads[1] == second,
            "a request changed in transit");

    for (std::size_t at = 0; at < encoded.bytes_written; ++at) {
        auto damaged = bytes;
        damaged[at] ^= std::byte{0x40};
        OwnedSigningRequest refused;
        require(decode_request(std::span(damaged).first(encoded.bytes_written), refused) != WireError::none,
                "a request with byte " + std::to_string(at) + " changed still decoded");
    }
    OwnedSigningRequest refused;
    require(decode_request(std::span(bytes).first(encoded.bytes_written - 1), refused) != WireError::none &&
                decode_request(std::span(bytes).first(encoded.bytes_written + 1), refused) != WireError::none &&
                decode_request(std::span(bytes).first(8), refused) == WireError::truncated,
            "a truncated or padded request decoded");
    const auto reseal = [](std::vector<std::byte> frame) {
        const auto total = frame.size() + 32;
        for (std::size_t i = 0; i < 4; ++i) frame[4 + i] = static_cast<std::byte>((total >> (8 * i)) & 0xff);
        const auto digest = sha256_digest(frame);
        frame.insert(frame.end(), digest.bytes.begin(), digest.bytes.end());
        return frame;
    };
    const std::vector<std::byte> body(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written - 32));
    {
        auto padded = body;
        padded.push_back(std::byte{0});
        require(decode_request(reseal(padded), refused) == WireError::length_mismatch,
                "a request with a byte after its last payload decoded");
        auto other_magic = body;
        other_magic[2] = std::byte{'P'};
        require(decode_request(reseal(other_magic), refused) == WireError::invalid_magic,
                "a frame that is not a request was read as one");
        auto huge = body;
        huge.resize(kMaxRequestBytes + 1 - 32, std::byte{0});
        require(decode_request(reseal(huge), refused) == WireError::too_large,
                "a frame larger than the declared maximum was hashed and parsed");
        auto bad_side = request;
        bad_side.intent.side = static_cast<Side>(3);
        std::vector<std::byte> side_bytes(kMaxRequestBytes);
        const auto side_encoded = encode_request(bad_side, side_bytes);
        require(side_encoded.error == WireError::none &&
                    decode_request(std::span(side_bytes).first(side_encoded.bytes_written), refused) ==
                        WireError::invalid_value,
                "a request with an unknown side decoded");
    }
    const std::string oversize(kMaxPayloadBytes + 1, 'x');
    auto large = request_for(oversize);
    require(encode_request(large, bytes).error == WireError::too_large, "an oversize payload was encoded");
    const std::string largest(kMaxPayloadBytes, 'x');
    const std::string longest(kMaxHandleBytes, 'h');
    auto full = request_for(largest, 11, longest);
    full.payload_count = kMaxSignPayloads;
    for (auto& text : full.payloads) text = largest;
    const auto full_encoded = encode_request(full, bytes);
    require(full_encoded.error == WireError::none && full_encoded.bytes_written == kMaxRequestBytes &&
                decode_request(bytes, refused) == WireError::none && refused.request.payloads[3] == largest,
            "the largest allowed request does not fit the declared maximum exactly");
    std::vector<std::byte> small(encoded.bytes_written - 1);
    require(encode_request(request, small).error == WireError::too_large, "a request was written past its buffer");

    SignerResult result;
    result.verdict = SignerVerdict::signed_ok;
    result.signature_id = 0x3132333435363738;
    result.key_version = 0x41424344;
    result.signature_count = 2;
    for (std::size_t index = 0; index < 2; ++index) {
        const std::string text = index == 0 ? kPayloadSignature : std::string(kMaxSignatureBytes, 's');
        std::copy(text.begin(), text.end(), result.signatures[index].text.begin());
        result.signatures[index].length = static_cast<std::uint16_t>(text.size());
    }
    std::array<std::byte, kMaxResponseBytes> reply{};
    const auto reply_encoded = encode_response(result, reply);
    SignerResult reply_back;
    require(reply_encoded.error == WireError::none &&
                decode_response(std::span(reply).first(reply_encoded.bytes_written), reply_back) == WireError::none &&
                reply_back.verdict == SignerVerdict::signed_ok && reply_back.signature_id == result.signature_id &&
                reply_back.key_version == result.key_version && reply_back.signature_count == 2 &&
                reply_back.signatures[0].view() == kPayloadSignature &&
                reply_back.signatures[1].view() == std::string(kMaxSignatureBytes, 's'),
            "a response changed in transit");
    for (std::size_t at = 0; at < reply_encoded.bytes_written; ++at) {
        auto damaged = reply;
        damaged[at] ^= std::byte{0x40};
        SignerResult ignored;
        require(decode_response(std::span(damaged).first(reply_encoded.bytes_written), ignored) != WireError::none,
                "a response with byte " + std::to_string(at) + " changed still decoded");
    }
    SignerResult refusal;
    refusal.verdict = SignerVerdict::refused_mismatch;
    refusal.signature_count = 1;
    refusal.signatures[0] = result.signatures[0];
    require(encode_response(refusal, reply).error == WireError::invalid_value, "a refusal carrying a signature was encoded");
    refusal.signature_count = 0;
    refusal.verdict = static_cast<SignerVerdict>(0);
    require(encode_response(refusal, reply).error == WireError::invalid_value, "an unknown verdict was encoded");
    require(!frame_length(std::span(bytes).first(7)) && !frame_length(std::span(reply).first(4)),
            "a frame length was read from too few bytes");
}

void the_key_file_must_be_a_test_key() {
    Directory directory("v2-signer-key");
    const auto good = write_test_key(directory.path);
    auto key = TestKey::load(good);
    require(key.has_value(), "a test key file did not load");
    Signature signature;
    const auto holder = key->holder(3);
    require(holder.key_version == 3 && holder.sign_fn(holder.context, kPayload, signature) &&
                signature.view() == kPayloadSignature,
            "the loaded test key does not produce the known signature");
    const auto text = TestKey::file_text(test_key_bytes());
    const std::vector<std::pair<const char*, std::string>> refused{
        {"raw bytes", std::string(32, '\x01')},
        {"no header", text.substr(kTestKeyHeader.size())},
        {"another header", "velocita-live-signing-key-v1\n" + text.substr(kTestKeyHeader.size())},
        {"short key", text.substr(0, text.size() - 3) + "\n"},
        {"trailing bytes", text + "x"},
        {"a second newline", text + "\n"},
        {"no newline", text.substr(0, text.size() - 1)},
        {"not hex", std::string(kTestKeyHeader) + std::string(64, 'z') + "\n"},
        {"zero key", std::string(kTestKeyHeader) + std::string(64, '0') + "\n"},
        {"empty", ""},
    };
    for (const auto& [name, content] : refused) {
        const auto file = directory.path / "bad.key";
        std::ofstream(file, std::ios::binary | std::ios::trunc) << content;
        require(!TestKey::load(file), std::string("a key file was accepted: ") + name);
    }
    require(!TestKey::load(directory.path / "absent.key"), "a missing key file loaded");
}

void the_floor_file_survives_and_never_goes_back() {
    Directory directory("v2-signer-floor");
    const auto file = directory.path / "floor.bin";
    FloorFile missing;
    require(FloorFile::open(file, missing) == FloorError::missing, "a missing floor file opened");
    require(FloorFile::create(file) == FloorError::none && FloorFile::create(file) == FloorError::already_exists,
            "a floor file was not created once");
    FloorFile floor;
    require(FloorFile::open(file, floor) == FloorError::none && floor.floor() == 0, "a new floor is not zero");
    const auto store = floor.store();
    require(store.persist_fn(store.context, 41) && floor.floor() == 41, "the floor did not advance");
    require(!store.persist_fn(store.context, 41) && !store.persist_fn(store.context, 40) && floor.floor() == 41,
            "the floor went back or stood still");
    FloorFile reopened;
    require(FloorFile::open(file, reopened) == FloorError::none && reopened.floor() == 41,
            "the floor did not survive a reopen");
    require(!std::filesystem::exists(directory.path / "floor.bin.tmp"), "the floor's temporary file was left behind");
    struct stat info {};
    require(::stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "the floor file is not private");

    std::string bytes;
    {
        std::ifstream input(file, std::ios::binary);
        std::ostringstream all;
        all << input.rdbuf();
        bytes = all.str();
    }
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        auto damaged = bytes;
        damaged[at] = static_cast<char>(damaged[at] ^ 0x10);
        std::ofstream(file, std::ios::binary | std::ios::trunc) << damaged;
        FloorFile refused;
        require(FloorFile::open(file, refused) == FloorError::invalid_file,
                "a floor file with byte " + std::to_string(at) + " changed opened");
    }
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes << 'x';
    FloorFile refused;
    require(FloorFile::open(file, refused) == FloorError::invalid_file, "a floor file with a trailing byte opened");
}

void the_audit_file_only_grows() {
    Directory directory("v2-signer-audit");
    const auto file = directory.path / "audit.log";
    AuditRecord record{11, EffectId{77}, SignerVerdict::signed_ok, 7, PolicyVersion{3}, 11, {}};
    record.payload_digest = *parse_digest256(kPayloadSha256);
    const std::string expected = "sequence=11 effect=77 verdict=1 key_version=7 policy_version=3 signature_id=11 "
                                 "payload_sha256=" + kPayloadSha256 + "\n";
    require(AuditFile::line(record) == expected, "the audit line changed: " + AuditFile::line(record));
    {
        AuditFile audit;
        require(audit.sink().append_fn(audit.sink().context, record) == false, "an unopened audit file accepted a record");
        require(audit.open(file) && !audit.open(file), "the audit file did not open exactly once");
        const auto sink = audit.sink();
        require(sink.append_fn(sink.context, record), "an audit record was not written");
    }
    {
        AuditFile again;
        require(again.open(file), "the audit file did not reopen");
        auto refusal = record;
        refusal.verdict = SignerVerdict::refused_replay;
        refusal.signature_id = 0;
        const auto sink = again.sink();
        require(sink.append_fn(sink.context, refusal), "a second audit record was not written");
    }
    std::ifstream input(file);
    std::ostringstream all;
    all << input.rdbuf();
    require(all.str() == expected + "sequence=11 effect=77 verdict=3 key_version=7 policy_version=3 signature_id=0 "
                                    "payload_sha256=" + kPayloadSha256 + "\n",
            "reopening the audit file did not append: " + all.str());
    struct stat info {};
    require(::stat(file.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "the audit file is not private");
    AuditFile directory_target;
    require(!directory_target.open(directory.path), "a directory was opened as the audit file");
}

struct Service {
    Directory directory{"v2-signer-socket"};
    std::filesystem::path socket = directory.path / "s.sock";
    Rig rig;
    SignerServer server;
    std::atomic<bool> stop{false};
    std::thread thread;

    explicit Service(const std::uint32_t allowed_uid) {
        require(server.listen(socket, allowed_uid), "the signer socket could not be created");
        thread = std::thread([this] {
            while (!stop.load()) (void)server.serve_once(*rig.core, 20);
        });
    }
    ~Service() {
        stop.store(true);
        thread.join();
    }
};

void a_client_of_the_allowed_user_gets_a_signature_over_the_socket() {
    Service service(static_cast<std::uint32_t>(::geteuid()));
    struct stat info {};
    require(::stat(service.socket.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) && (info.st_mode & 0777) == 0660,
            "the socket is not a socket readable by its owner and group only");
    SocketSigner client(service.socket, 2'000);
    const auto handle = client.handle();
    const auto signed_result = handle.sign(request_for(kPayload));
    require(signed_result.verdict == SignerVerdict::signed_ok && signed_result.signature_count == 1 &&
                signed_result.signatures[0].view() == kPayloadSignature && signed_result.signature_id == 11 &&
                signed_result.key_version == 7,
            "the socket client did not receive the signature");
    const auto replayed = handle.sign(request_for(kPayload));
    require(replayed.verdict == SignerVerdict::refused_replay && replayed.signature_count == 0,
            "a replay over the socket was not refused");
    const auto mismatch = handle.sign(request_for(payload("buy", 121, 2'500, "q-7"), 12, "q-7"));
    require(mismatch.verdict == SignerVerdict::refused_mismatch, "a mismatch over the socket was not refused");
    require(service.server.counters().served == 3 && service.server.counters().peer_refusals == 0,
            "the server did not count three served requests");
}

void another_user_is_refused_before_any_byte_is_read() {
    Service service(static_cast<std::uint32_t>(::geteuid()) + 1);
    SocketSigner client(service.socket, 500);
    const auto result = client.sign(request_for(kPayload));
    require(result.verdict == SignerVerdict::unavailable && result.signature_count == 0,
            "a peer that is not the allowed user received an answer");
    for (int waited = 0; waited < 200 && service.server.counters().peer_refusals == 0; ++waited)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(service.server.counters().peer_refusals == 1 && service.server.counters().served == 0 &&
                service.rig.order.empty(),
            "the refused peer reached the signer core");
}

void a_missing_or_silent_signer_is_unavailable() {
    Directory directory("v2-signer-absent");
    SocketSigner nobody(directory.path / "none.sock", 200);
    require(nobody.sign(request_for(kPayload)).verdict == SignerVerdict::unavailable,
            "a missing socket produced a verdict other than unavailable");
    SignerServer silent;
    const auto socket = directory.path / "silent.sock";
    require(silent.listen(socket, static_cast<std::uint32_t>(::geteuid())), "fixture: silent socket not created");
    SocketSigner waiting(socket, 150);
    const auto started = std::chrono::steady_clock::now();
    require(waiting.sign(request_for(kPayload)).verdict == SignerVerdict::unavailable,
            "a signer that never answered produced a verdict");
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    require(waited >= 100 && waited < 2'000, "the client did not honour its timeout: " + std::to_string(waited) + " ms");
    std::ofstream(directory.path / "file.sock") << "not a socket";
    SignerServer refused;
    require(!refused.listen(directory.path / "file.sock", 1), "the server replaced a regular file with its socket");
    require(!refused.listen(directory.path / std::string(200, 'p'), 1), "a socket path too long was accepted");
}

void a_reply_with_the_wrong_number_of_signatures_is_unavailable() {
    Directory directory("v2-signer-fake");
    const auto socket = directory.path / "fake.sock";
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket.c_str(), socket.native().size() + 1);
    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    require(listener >= 0 && ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
                ::listen(listener, 2) == 0,
            "fixture: fake signer socket");
    for (const std::size_t count : {std::size_t{2}, std::size_t{1}}) {
        std::thread fake([&] {
            const int connection = ::accept(listener, nullptr, nullptr);
            std::array<std::byte, 8> prefix{};
            std::size_t got = 0;
            while (got < prefix.size()) {
                const auto n = ::read(connection, prefix.data() + got, prefix.size() - got);
                if (n <= 0) break;
                got += static_cast<std::size_t>(n);
            }
            SignerResult result;
            result.verdict = SignerVerdict::signed_ok;
            result.signature_id = 5;
            result.key_version = 1;
            result.signature_count = count;
            for (std::size_t index = 0; index < count; ++index) {
                std::copy(kPayloadSignature.begin(), kPayloadSignature.end(), result.signatures[index].text.begin());
                result.signatures[index].length = static_cast<std::uint16_t>(kPayloadSignature.size());
            }
            std::array<std::byte, kMaxResponseBytes> reply{};
            const auto encoded = encode_response(result, reply);
            (void)::write(connection, reply.data(), encoded.bytes_written);
            ::close(connection);
        });
        SocketSigner client(socket, 2'000);
        const auto answer = client.sign(request_for(kPayload));
        fake.join();
        if (count == 1)
            require(answer.verdict == SignerVerdict::signed_ok && answer.signature_count == 1,
                    "fixture: the fake signer's well-formed reply was not accepted");
        else
            require(answer.verdict == SignerVerdict::unavailable && answer.signature_count == 0,
                    "a reply with two signatures for one payload was accepted");
    }
    ::close(listener);
}

struct Process {
    pid_t pid{-1};
    int out{-1};

    Process(const std::string& binary, const std::vector<std::string>& args) {
        int pipes[2];
        require(::pipe(pipes) == 0, "pipe failed");
        pid = ::fork();
        require(pid >= 0, "fork failed");
        if (pid == 0) {
            ::dup2(pipes[1], STDOUT_FILENO);
            ::dup2(pipes[1], STDERR_FILENO);
            ::close(pipes[0]);
            ::close(pipes[1]);
            std::vector<char*> argv{const_cast<char*>(binary.c_str())};
            for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            ::execv(binary.c_str(), argv.data());
            ::_exit(127);
        }
        ::close(pipes[1]);
        out = pipes[0];
    }
    std::string line() {
        std::string text;
        char c{};
        while (::read(out, &c, 1) == 1 && c != '\n') text.push_back(c);
        return text;
    }
    int finish(const bool terminate) {
        if (terminate) ::kill(pid, SIGTERM);
        int status = 0;
        bool exited = false;
        for (int waited = 0; waited < 500 && !exited; ++waited) {
            exited = ::waitpid(pid, &status, WNOHANG) == pid;
            if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!exited) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        }
        ::close(out);
        return exited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
};

void the_process_signs_stops_and_restarts_above_its_floor(const std::string& binary) {
    Directory directory("v2-signer-process");
    const auto key = write_test_key(directory.path);
    const auto socket = directory.path / "s.sock";
    const auto floor = directory.path / "floor.bin";
    const auto audit = directory.path / "audit.log";
    std::vector<std::string> args{"--socket", socket.string(), "--allowed-uid", std::to_string(::geteuid()),
                                  "--test-key-file", key.string(), "--floor-file", floor.string(), "--audit-file",
                                  audit.string(), "--caller-id", "22", "--venue-id", "9", "--account-id", "5",
                                  "--instrument-id", "4", "--key-version", "7"};
    {
        Process refused(binary, args);
        const auto message = refused.line();
        require(refused.finish(false) == 64 && message.find("--init-floor") != std::string::npos,
                "a first start without --init-floor was not refused: " + message);
    }
    auto first_args = args;
    first_args.emplace_back("--init-floor");
    auto request = request_for(kPayload, 1);
    request.intent.not_after = ~sim::Tick{0};
    {
        Process first(binary, first_args);
        const auto ready = first.line();
        require(ready.rfind("READY signer test_key_only=1 payloads=mock sequence_floor=0", 0) == 0,
                "the signer did not report ready with a test key: " + ready);
        SocketSigner client(socket, 2'000);
        const auto result = client.sign(request);
        require(result.verdict == SignerVerdict::signed_ok && result.signatures[0].view() == kPayloadSignature &&
                    result.key_version == 7,
                "the signer process did not sign");
        const int code = first.finish(true);
        require(code == 0, "the signer did not stop cleanly on SIGTERM: " + std::to_string(code));
    }
    require(!std::filesystem::exists(socket), "the socket was left behind after a clean stop");
    {
        Process again(binary, first_args);
        const auto message = again.line();
        require(again.finish(false) == 64 && message.find("never replaces") != std::string::npos,
                "--init-floor replaced an existing floor: " + message);
    }
    {
        Process second(binary, args);
        const auto ready = second.line();
        require(ready.find("sequence_floor=1") != std::string::npos, "the restart lost its floor: " + ready);
        SocketSigner client(socket, 2'000);
        require(client.sign(request).verdict == SignerVerdict::refused_replay,
                "a restarted signer signed a sequence it had already used");
        const auto next = payload("buy", 120, 2'500, "q-2");
        auto fresh = request_for(next, 2, "q-2");
        fresh.intent.not_after = ~sim::Tick{0};
        require(client.sign(fresh).verdict == SignerVerdict::signed_ok, "a restarted signer refused the next sequence");
        auto stale = request_for(payload("buy", 120, 2'500, "q-3"), 3, "q-3");
        stale.intent.not_after = 1;
        require(client.sign(stale).verdict == SignerVerdict::refused_expired,
                "the process clock did not expire a request dated at tick 1");
        require(second.finish(true) == 0, "the second signer did not stop cleanly");
    }
    std::ifstream input(audit);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(input, line)) lines.push_back(line);
    require(lines.size() == 4 && lines[0].rfind("sequence=1 effect=77 verdict=1 key_version=7", 0) == 0 &&
                lines[1].rfind("sequence=1 effect=77 verdict=3", 0) == 0 &&
                lines[2].rfind("sequence=2 effect=77 verdict=1", 0) == 0 &&
                lines[3].rfind("sequence=3 effect=77 verdict=4", 0) == 0,
            "the audit log across two processes is not the four decisions in order");
    {
        const auto bad_key = directory.path / "bad.key";
        std::ofstream(bad_key, std::ios::binary) << std::string(32, '\x07');
        auto bad = args;
        bad[5] = bad_key.string();
        Process refused(binary, bad);
        const auto message = refused.line();
        require(refused.finish(false) == 64 && message.find("test signing key") != std::string::npos,
                "a key file that is not a test key was accepted: " + message);
    }
}

}

int main(int argc, char** argv) {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"a_request_inside_its_intent_is_signed_persisted_and_audited",
         a_request_inside_its_intent_is_signed_persisted_and_audited},
        {"every_departure_from_the_intent_is_refused_and_nothing_is_signed",
         every_departure_from_the_intent_is_refused_and_nothing_is_signed},
        {"bounds_are_inclusive_on_the_safe_side", bounds_are_inclusive_on_the_safe_side},
        {"a_sequence_or_a_quote_is_never_signed_twice", a_sequence_or_a_quote_is_never_signed_twice},
        {"no_signature_leaves_without_a_durable_floor_and_an_audit_record",
         no_signature_leaves_without_a_durable_floor_and_an_audit_record},
        {"the_mock_payload_decoder_accepts_one_exact_shape", the_mock_payload_decoder_accepts_one_exact_shape},
        {"requests_and_responses_round_trip_and_reject_damage", requests_and_responses_round_trip_and_reject_damage},
        {"the_key_file_must_be_a_test_key", the_key_file_must_be_a_test_key},
        {"the_floor_file_survives_and_never_goes_back", the_floor_file_survives_and_never_goes_back},
        {"the_audit_file_only_grows", the_audit_file_only_grows},
        {"a_client_of_the_allowed_user_gets_a_signature_over_the_socket",
         a_client_of_the_allowed_user_gets_a_signature_over_the_socket},
        {"another_user_is_refused_before_any_byte_is_read", another_user_is_refused_before_any_byte_is_read},
        {"a_missing_or_silent_signer_is_unavailable", a_missing_or_silent_signer_is_unavailable},
        {"a_reply_with_the_wrong_number_of_signatures_is_unavailable",
         a_reply_with_the_wrong_number_of_signatures_is_unavailable},
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try {
            run();
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    if (argc > 1) {
        try {
            the_process_signs_stops_and_restarts_above_its_floor(argv[1]);
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL the_process_signs_stops_and_restarts_above_its_floor: " << error.what() << '\n';
        }
    }
    if (failures == 0) std::cout << "all V2 signer tests passed\n";
    return failures == 0 ? 0 : 1;
}
