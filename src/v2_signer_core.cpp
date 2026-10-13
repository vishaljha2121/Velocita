#include "velocita/v2/signer_service.hpp"

#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace velocita::v2::signer {
namespace {

constexpr std::array<std::byte, 4> kRequestMagic{std::byte{'V'}, std::byte{'S'}, std::byte{'R'}, std::byte{'1'}};
constexpr std::array<std::byte, 4> kResponseMagic{std::byte{'V'}, std::byte{'S'}, std::byte{'P'}, std::byte{'1'}};
constexpr std::size_t kDigest = 32;

std::span<const std::byte> bytes_of(const std::string_view text) noexcept {
    return std::as_bytes(std::span(text.data(), text.size()));
}

class Writer final {
public:
    explicit Writer(const std::span<std::byte> out) noexcept : out_(out) {}
    template <typename T>
    void integral(const T value) noexcept {
        const auto raw = static_cast<std::make_unsigned_t<T>>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) byte(static_cast<std::byte>((raw >> (8 * i)) & 0xff));
    }
    void byte(const std::byte value) noexcept {
        if (at_ < out_.size()) out_[at_] = value;
        ++at_;
    }
    void raw(const std::span<const std::byte> bytes) noexcept {
        for (const auto value : bytes) byte(value);
    }
    void text(const std::string_view value) noexcept {
        integral(static_cast<std::uint16_t>(value.size()));
        raw(bytes_of(value));
    }
    [[nodiscard]] std::size_t size() const noexcept { return at_; }
    [[nodiscard]] bool fits() const noexcept { return at_ <= out_.size(); }

private:
    std::span<std::byte> out_;
    std::size_t at_{};
};

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> in) noexcept : in_(in) {}
    template <typename T>
    [[nodiscard]] T integral() noexcept {
        std::make_unsigned_t<T> raw{};
        for (std::size_t i = 0; i < sizeof(T); ++i) raw |= static_cast<std::make_unsigned_t<T>>(next()) << (8 * i);
        return static_cast<T>(raw);
    }
    [[nodiscard]] std::span<const std::byte> take(const std::size_t count) noexcept {
        if (count > in_.size() - std::min(at_, in_.size())) {
            failed_ = true;
            return {};
        }
        const auto view = in_.subspan(at_, count);
        at_ += count;
        return view;
    }
    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return in_.size() - std::min(at_, in_.size()); }

private:
    [[nodiscard]] unsigned next() noexcept {
        if (at_ >= in_.size()) {
            failed_ = true;
            return 0;
        }
        return std::to_integer<unsigned>(in_[at_++]);
    }
    std::span<const std::byte> in_;
    std::size_t at_{};
    bool failed_{};
};

EncodeResult seal(Writer& writer, const std::span<std::byte> output) noexcept {
    const auto total = writer.size() + kDigest;
    if (total > output.size()) return {WireError::too_large, 0};
    for (std::size_t i = 0; i < 4; ++i) output[4 + i] = static_cast<std::byte>((total >> (8 * i)) & 0xff);
    const auto digest = sha256_digest(output.first(writer.size()));
    std::copy(digest.bytes.begin(), digest.bytes.end(), output.begin() + static_cast<std::ptrdiff_t>(writer.size()));
    return {WireError::none, total};
}

WireError open_frame(const std::span<const std::byte> input, const std::array<std::byte, 4>& magic,
                     const std::size_t maximum, std::span<const std::byte>& body) noexcept {
    if (input.size() < 8 + kDigest) return WireError::truncated;
    if (!std::equal(magic.begin(), magic.end(), input.begin())) return WireError::invalid_magic;
    std::size_t total = 0;
    for (std::size_t i = 0; i < 4; ++i) total |= std::to_integer<std::size_t>(input[4 + i]) << (8 * i);
    if (total > maximum) return WireError::too_large;
    if (total != input.size()) return WireError::length_mismatch;
    const auto covered = input.first(total - kDigest);
    const auto digest = sha256_digest(covered);
    if (!std::equal(digest.bytes.begin(), digest.bytes.end(), input.begin() + static_cast<std::ptrdiff_t>(total - kDigest)))
        return WireError::digest_mismatch;
    body = covered.subspan(8);
    return WireError::none;
}

bool known_verdict(const std::uint8_t value) noexcept {
    return value >= static_cast<std::uint8_t>(SignerVerdict::signed_ok) &&
           value <= static_cast<std::uint8_t>(SignerVerdict::unavailable);
}

bool consume(std::string_view& text, const std::string_view prefix) noexcept {
    if (text.substr(0, prefix.size()) != prefix) return false;
    text.remove_prefix(prefix.size());
    return true;
}

std::optional<std::int64_t> consume_amount(std::string_view& text) noexcept {
    std::size_t digits = 0;
    std::int64_t value = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        if (digits == 18) return std::nullopt;
        value = value * 10 + (text[digits] - '0');
        ++digits;
    }
    if (digits == 0 || (digits > 1 && text[0] == '0') || value == 0) return std::nullopt;
    text.remove_prefix(digits);
    return value;
}

std::optional<std::string_view> consume_word(std::string_view& text) noexcept {
    std::size_t length = 0;
    while (length < text.size() && text[length] != '"') {
        const auto c = text[length];
        if (c == ';' || c == '\\' || c < 0x21 || c > 0x7e) return std::nullopt;
        ++length;
    }
    if (length == 0 || length == text.size()) return std::nullopt;
    const auto word = text.substr(0, length);
    text.remove_prefix(length);
    return word;
}

Digest256 hmac(const std::span<const std::byte> key, const std::string_view domain,
               const std::string_view message) noexcept {
    std::array<std::byte, 64> pad{};
    std::copy(key.begin(), key.end(), pad.begin());
    Sha256 inner;
    for (auto& value : pad) value ^= std::byte{0x36};
    inner.update(pad);
    inner.update(bytes_of(domain));
    inner.update(bytes_of(message));
    const auto inner_digest = inner.finish();
    for (auto& value : pad) value ^= std::byte{0x36} ^ std::byte{0x5c};
    Sha256 outer;
    outer.update(pad);
    outer.update(inner_digest.bytes);
    pad.fill(std::byte{0});
    return outer.finish();
}

}

std::optional<DecodedSwap> decode_mock_swap_payload(std::string_view payload) noexcept {
    DecodedSwap swap;
    if (payload.size() > kMaxPayloadBytes || !consume(payload, "{\"unsigned_tx\":\"MOCKTX1;side=")) return std::nullopt;
    if (consume(payload, "buy")) swap.side = Side::buy;
    else if (consume(payload, "sell")) swap.side = Side::sell;
    else return std::nullopt;
    if (!consume(payload, ";base=")) return std::nullopt;
    const auto base = consume_amount(payload);
    if (!base || !consume(payload, ";bound=")) return std::nullopt;
    const auto bound = consume_amount(payload);
    if (!bound || !consume(payload, ";handle=")) return std::nullopt;
    const auto handle = consume_word(payload);
    if (!handle || handle->size() > kMaxHandleBytes || !consume(payload, "\",\"organization_id\":\"")) return std::nullopt;
    const auto organization = consume_word(payload);
    if (!organization || payload != "\"}") return std::nullopt;
    swap.base_atoms = *base;
    swap.quote_bound_atoms = *bound;
    swap.handle = *handle;
    return swap;
}

SignerCore::SignerCore(const SignerPolicy& policy, const PayloadDecoder decoder, const KeyHolder key,
                       const AuditSink audit, const SequenceStore store, const SignerClock clock,
                       const std::uint64_t sequence_floor) noexcept
    : policy_(policy), decoder_(decoder), key_(key), audit_(audit), store_(store), clock_(clock),
      floor_(sequence_floor) {}

bool SignerCore::already_signed(const std::string_view handle) const noexcept {
    const auto digest = sha256_digest(bytes_of(handle));
    return std::find(handles_.begin(), handles_.begin() + static_cast<std::ptrdiff_t>(handle_count_), digest) !=
           handles_.begin() + static_cast<std::ptrdiff_t>(handle_count_);
}

SignerResult SignerCore::refuse(const SigningRequest& request, const SignerVerdict verdict) noexcept {
    AuditRecord record{request.intent.request_sequence, request.intent.effect_id, verdict, key_.key_version,
                       request.intent.policy_version, 0, {}};
    if (request.payload_count != 0 && request.payload_count <= kMaxSignPayloads)
        record.payload_digest = sha256_digest(bytes_of(request.payloads[0]));
    if (audit_.append_fn) (void)audit_.append_fn(audit_.context, record);
    SignerResult result;
    result.verdict = verdict;
    result.key_version = key_.key_version;
    return result;
}

SignerResult SignerCore::sign(const SigningRequest& request) noexcept {
    const auto& intent = request.intent;
    if (!decoder_ || !key_.sign_fn || !audit_.append_fn || !store_.persist_fn || !clock_.now_fn)
        return {};
    if (intent.caller != policy_.caller || intent.venue_id != policy_.venue_id ||
        intent.account_id != policy_.account_id || intent.instrument_id != policy_.instrument_id ||
        policy_.caller.value() == 0 || intent.effect_id.value() == 0 || intent.cluster_order_id.value() == 0 ||
        intent.effect_payload_digest.is_zero()) {
        return refuse(request, SignerVerdict::refused_policy);
    }
    if (intent.request_sequence <= floor_ || request.venue_handle.empty() ||
        request.venue_handle.size() > kMaxHandleBytes || already_signed(request.venue_handle)) {
        return refuse(request, request.venue_handle.empty() || request.venue_handle.size() > kMaxHandleBytes
                                   ? SignerVerdict::refused_mismatch : SignerVerdict::refused_replay);
    }
    if (clock_.now_fn(clock_.context) > intent.not_after) return refuse(request, SignerVerdict::refused_expired);
    if (request.payload_count != 1 || intent.base_atoms <= 0 ||
        intent.quote_bound_atoms <= 0 || (intent.side != Side::buy && intent.side != Side::sell)) {
        return refuse(request, SignerVerdict::refused_mismatch);
    }
    for (std::size_t index = 0; index < request.payload_count; ++index) {
        const auto swap = request.payloads[index].size() <= kMaxPayloadBytes ? decoder_(request.payloads[index])
                                                                             : std::nullopt;
        const bool inside = swap && swap->side == intent.side && swap->base_atoms == intent.base_atoms &&
                            swap->handle == request.venue_handle &&
                            (intent.side == Side::buy ? swap->quote_bound_atoms <= intent.quote_bound_atoms
                                                      : swap->quote_bound_atoms >= intent.quote_bound_atoms);
        if (!inside) return refuse(request, SignerVerdict::refused_mismatch);
    }

    if (!store_.persist_fn(store_.context, intent.request_sequence)) return {};
    floor_ = intent.request_sequence;
    handles_[handle_next_] = sha256_digest(bytes_of(request.venue_handle));
    handle_next_ = (handle_next_ + 1) % handles_.size();
    handle_count_ = std::min(handle_count_ + 1, handles_.size());

    SignerResult result;
    for (std::size_t index = 0; index < request.payload_count; ++index) {
        if (!key_.sign_fn(key_.context, request.payloads[index], result.signatures[index]) ||
            result.signatures[index].length == 0) {
            return {};
        }
    }
    const AuditRecord record{intent.request_sequence, intent.effect_id, SignerVerdict::signed_ok, key_.key_version,
                             intent.policy_version, intent.request_sequence,
                             sha256_digest(bytes_of(request.payloads[0]))};
    if (!audit_.append_fn(audit_.context, record)) return {};
    result.verdict = SignerVerdict::signed_ok;
    result.signature_id = intent.request_sequence;
    result.key_version = key_.key_version;
    result.signature_count = request.payload_count;
    return result;
}

EncodeResult encode_request(const SigningRequest& request, const std::span<std::byte> output) noexcept {
    if (request.payload_count > kMaxSignPayloads || request.venue_handle.size() > kMaxHandleBytes)
        return {WireError::invalid_value, 0};
    for (std::size_t index = 0; index < request.payload_count; ++index)
        if (request.payloads[index].size() > kMaxPayloadBytes) return {WireError::too_large, 0};
    Writer writer(output);
    writer.raw(kRequestMagic);
    writer.integral(std::uint32_t{0});
    const auto& intent = request.intent;
    writer.integral(intent.caller.value());
    writer.integral(intent.request_sequence);
    writer.integral(intent.venue_id.value());
    writer.integral(intent.account_id.value());
    writer.integral(intent.instrument_id.value());
    writer.integral(intent.effect_id.value());
    writer.integral(intent.cluster_order_id.value());
    writer.raw(intent.effect_payload_digest.bytes);
    writer.integral(intent.policy_id.value());
    writer.integral(intent.policy_version.value());
    writer.integral(static_cast<std::uint8_t>(intent.side));
    writer.integral(intent.base_atoms);
    writer.integral(intent.quote_bound_atoms);
    writer.integral(static_cast<std::uint64_t>(intent.not_after));
    writer.text(request.venue_handle);
    writer.integral(static_cast<std::uint8_t>(request.payload_count));
    for (std::size_t index = 0; index < request.payload_count; ++index) writer.text(request.payloads[index]);
    if (!writer.fits()) return {WireError::too_large, 0};
    return seal(writer, output);
}

WireError decode_request(const std::span<const std::byte> input, OwnedSigningRequest& destination) {
    std::span<const std::byte> body;
    if (const auto error = open_frame(input, kRequestMagic, kMaxRequestBytes, body); error != WireError::none)
        return error;
    Reader reader(body);
    OwnedSigningRequest owned;
    owned.text.reserve(body.size());
    auto& intent = owned.request.intent;
    intent.caller = CallerId{reader.integral<std::uint64_t>()};
    intent.request_sequence = reader.integral<std::uint64_t>();
    intent.venue_id = VenueId{reader.integral<std::uint64_t>()};
    intent.account_id = AccountId{reader.integral<std::uint64_t>()};
    intent.instrument_id = InstrumentId{reader.integral<std::uint64_t>()};
    intent.effect_id = EffectId{reader.integral<std::uint64_t>()};
    intent.cluster_order_id = ClusterOrderId{reader.integral<std::uint64_t>()};
    const auto digest = reader.take(kDigest);
    if (!reader.ok()) return WireError::truncated;
    std::copy(digest.begin(), digest.end(), intent.effect_payload_digest.bytes.begin());
    intent.policy_id = PolicyId{reader.integral<std::uint64_t>()};
    intent.policy_version = PolicyVersion{reader.integral<std::uint64_t>()};
    const auto side = reader.integral<std::uint8_t>();
    intent.base_atoms = reader.integral<std::int64_t>();
    intent.quote_bound_atoms = reader.integral<std::int64_t>();
    intent.not_after = reader.integral<std::uint64_t>();
    std::array<std::pair<std::size_t, std::size_t>, 1 + kMaxSignPayloads> pieces{};
    const auto text = [&](const std::size_t slot, const std::size_t maximum) {
        const auto length = reader.integral<std::uint16_t>();
        if (!reader.ok() || length > maximum) return false;
        const auto bytes = reader.take(length);
        if (!reader.ok()) return false;
        pieces[slot] = {owned.text.size(), length};
        for (const auto value : bytes) owned.text.push_back(static_cast<char>(value));
        return true;
    };
    if (!text(0, kMaxHandleBytes)) return reader.ok() ? WireError::invalid_value : WireError::truncated;
    const auto count = reader.integral<std::uint8_t>();
    if (!reader.ok()) return WireError::truncated;
    if (count > kMaxSignPayloads || (side != static_cast<std::uint8_t>(Side::buy) &&
                                     side != static_cast<std::uint8_t>(Side::sell)))
        return WireError::invalid_value;
    intent.side = static_cast<Side>(side);
    for (std::size_t index = 0; index < count; ++index)
        if (!text(1 + index, kMaxPayloadBytes)) return reader.ok() ? WireError::invalid_value : WireError::truncated;
    if (reader.remaining() != 0) return WireError::length_mismatch;
    owned.request.venue_handle = {owned.text.data() + pieces[0].first, pieces[0].second};
    owned.request.payload_count = count;
    for (std::size_t index = 0; index < count; ++index)
        owned.request.payloads[index] = {owned.text.data() + pieces[1 + index].first, pieces[1 + index].second};
    destination = std::move(owned);
    return WireError::none;
}

EncodeResult encode_response(const SignerResult& result, const std::span<std::byte> output) noexcept {
    if (result.signature_count > kMaxSignPayloads || !known_verdict(static_cast<std::uint8_t>(result.verdict)) ||
        (result.verdict != SignerVerdict::signed_ok && result.signature_count != 0))
        return {WireError::invalid_value, 0};
    Writer writer(output);
    writer.raw(kResponseMagic);
    writer.integral(std::uint32_t{0});
    writer.integral(static_cast<std::uint8_t>(result.verdict));
    writer.integral(result.signature_id);
    writer.integral(result.key_version);
    writer.integral(static_cast<std::uint8_t>(result.signature_count));
    for (std::size_t index = 0; index < result.signature_count; ++index) {
        if (result.signatures[index].length == 0 || result.signatures[index].length > kMaxSignatureBytes)
            return {WireError::invalid_value, 0};
        writer.text(result.signatures[index].view());
    }
    if (!writer.fits()) return {WireError::too_large, 0};
    return seal(writer, output);
}

WireError decode_response(const std::span<const std::byte> input, SignerResult& destination) noexcept {
    std::span<const std::byte> body;
    if (const auto error = open_frame(input, kResponseMagic, kMaxResponseBytes, body); error != WireError::none)
        return error;
    Reader reader(body);
    SignerResult result;
    const auto verdict = reader.integral<std::uint8_t>();
    result.signature_id = reader.integral<std::uint64_t>();
    result.key_version = reader.integral<std::uint32_t>();
    const auto count = reader.integral<std::uint8_t>();
    if (!reader.ok()) return WireError::truncated;
    if (!known_verdict(verdict) || count > kMaxSignPayloads ||
        (verdict != static_cast<std::uint8_t>(SignerVerdict::signed_ok) && count != 0))
        return WireError::invalid_value;
    result.verdict = static_cast<SignerVerdict>(verdict);
    result.signature_count = count;
    for (std::size_t index = 0; index < count; ++index) {
        const auto length = reader.integral<std::uint16_t>();
        if (!reader.ok()) return WireError::truncated;
        if (length == 0 || length > kMaxSignatureBytes) return WireError::invalid_value;
        const auto bytes = reader.take(length);
        if (!reader.ok()) return WireError::truncated;
        for (std::size_t at = 0; at < length; ++at) result.signatures[index].text[at] = static_cast<char>(bytes[at]);
        result.signatures[index].length = length;
    }
    if (reader.remaining() != 0) return WireError::length_mismatch;
    destination = result;
    return WireError::none;
}

std::optional<std::size_t> frame_length(const std::span<const std::byte> prefix) noexcept {
    if (prefix.size() < 8) return std::nullopt;
    const bool request = std::equal(kRequestMagic.begin(), kRequestMagic.end(), prefix.begin());
    const bool response = std::equal(kResponseMagic.begin(), kResponseMagic.end(), prefix.begin());
    if (!request && !response) return std::nullopt;
    std::size_t total = 0;
    for (std::size_t i = 0; i < 4; ++i) total |= std::to_integer<std::size_t>(prefix[4 + i]) << (8 * i);
    if (total < 8 + kDigest || total > (request ? kMaxRequestBytes : kMaxResponseBytes)) return std::nullopt;
    return total;
}

std::string TestKey::file_text(const std::span<const std::byte, kTestKeyBytes> key) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text(kTestKeyHeader);
    for (const auto value : key) {
        text.push_back(digits[std::to_integer<unsigned>(value) >> 4]);
        text.push_back(digits[std::to_integer<unsigned>(value) & 15]);
    }
    text.push_back('\n');
    return text;
}

std::optional<TestKey> TestKey::load(const std::filesystem::path& file) noexcept {
    const int fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    std::array<char, kTestKeyHeader.size() + 2 * kTestKeyBytes + 2> text{};
    std::size_t length = 0;
    while (length < text.size()) {
        const auto got = ::read(fd, text.data() + length, text.size() - length);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        length += static_cast<std::size_t>(got);
    }
    ::close(fd);
    const std::string_view view(text.data(), length);
    if (length != text.size() - 1 || view.substr(0, kTestKeyHeader.size()) != kTestKeyHeader || view.back() != '\n')
        return std::nullopt;
    const auto digest = parse_digest256(view.substr(kTestKeyHeader.size(), 2 * kTestKeyBytes));
    if (!digest || digest->is_zero()) return std::nullopt;
    TestKey key;
    key.key_ = digest->bytes;
    return key;
}

bool TestKey::sign(void* context, const std::string_view payload, Signature& out) noexcept {
    const auto& self = *static_cast<TestKey*>(context);
    const auto hex = to_hex(hmac(self.key_, kTestSignatureDomain, payload));
    std::copy(hex.begin(), hex.end(), out.text.begin());
    out.length = static_cast<std::uint16_t>(hex.size());
    return true;
}

KeyHolder TestKey::holder(const std::uint32_t key_version) noexcept { return {&TestKey::sign, this, key_version}; }

}
