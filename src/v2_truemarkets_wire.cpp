#include "velocita/v2/truemarkets_wire.hpp"

#include <algorithm>
#include <limits>

namespace velocita::v2::truemarkets {
namespace {

[[nodiscard]] constexpr bool digit(const char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] constexpr bool identifier_char(const char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || digit(c) || c == '.' || c == '_' ||
           c == ':' || c == '-';
}

// Signatures may be base64 or base64url as well as hex.
[[nodiscard]] constexpr bool signature_char(const char c) noexcept {
    return identifier_char(c) || c == '+' || c == '/' || c == '=';
}

// ---------------------------------------------------------------------------
// JSON scanning over a view. Every function advances `at` past what it read
// and returns false on the first byte that is not valid JSON.

struct Cursor {
    std::string_view text;
    std::size_t at{};

    [[nodiscard]] bool done() const noexcept { return at >= text.size(); }
    [[nodiscard]] char peek() const noexcept { return done() ? '\0' : text[at]; }
};

void skip_space(Cursor& cursor) noexcept {
    while (!cursor.done()) {
        const char c = cursor.peek();
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
        ++cursor.at;
    }
}

[[nodiscard]] bool hex_digit(const char c) noexcept {
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// At an opening quote. `content` is the raw bytes between the quotes.
[[nodiscard]] bool scan_string(Cursor& cursor, std::string_view& content, bool& escaped) noexcept {
    if (cursor.peek() != '"') return false;
    const auto begin = ++cursor.at;
    escaped = false;
    while (!cursor.done()) {
        const auto c = static_cast<unsigned char>(cursor.text[cursor.at]);
        if (c == '"') {
            content = cursor.text.substr(begin, cursor.at - begin);
            ++cursor.at;
            return true;
        }
        if (c < 0x20) return false;
        if (c == '\\') {
            escaped = true;
            ++cursor.at;
            if (cursor.done()) return false;
            const char kind = cursor.text[cursor.at];
            if (kind == 'u') {
                for (int i = 0; i < 4; ++i) {
                    ++cursor.at;
                    if (cursor.done() || !hex_digit(cursor.text[cursor.at])) return false;
                }
            } else if (kind != '"' && kind != '\\' && kind != '/' && kind != 'b' && kind != 'f' &&
                       kind != 'n' && kind != 'r' && kind != 't') {
                return false;
            }
        }
        ++cursor.at;
    }
    return false;
}

[[nodiscard]] bool scan_number(Cursor& cursor) noexcept {
    if (cursor.peek() == '-') ++cursor.at;
    if (cursor.peek() == '0') {
        ++cursor.at;
    } else if (digit(cursor.peek())) {
        while (digit(cursor.peek())) ++cursor.at;
    } else {
        return false;
    }
    if (cursor.peek() == '.') {
        ++cursor.at;
        if (!digit(cursor.peek())) return false;
        while (digit(cursor.peek())) ++cursor.at;
    }
    if (cursor.peek() == 'e' || cursor.peek() == 'E') {
        ++cursor.at;
        if (cursor.peek() == '+' || cursor.peek() == '-') ++cursor.at;
        if (!digit(cursor.peek())) return false;
        while (digit(cursor.peek())) ++cursor.at;
    }
    return true;
}

[[nodiscard]] bool scan_literal(Cursor& cursor, const std::string_view literal) noexcept {
    if (cursor.text.substr(cursor.at, literal.size()) != literal) return false;
    cursor.at += literal.size();
    return true;
}

// Scans one value at `depth` (the enclosing container count). Recursion is
// bounded by kMaxJsonDepth.
[[nodiscard]] JsonError scan_value(Cursor& cursor, const std::size_t depth) noexcept {
    skip_space(cursor);
    const char c = cursor.peek();
    if (c == '{' || c == '[') {
        if (depth + 1 > kMaxJsonDepth) return JsonError::too_deep;
        const char close = c == '{' ? '}' : ']';
        ++cursor.at;
        skip_space(cursor);
        if (cursor.peek() == close) {
            ++cursor.at;
            return JsonError::none;
        }
        while (true) {
            skip_space(cursor);
            if (c == '{') {
                std::string_view key;
                bool escaped = false;
                if (!scan_string(cursor, key, escaped)) return JsonError::malformed;
                skip_space(cursor);
                if (cursor.peek() != ':') return JsonError::malformed;
                ++cursor.at;
            }
            if (const auto error = scan_value(cursor, depth + 1); error != JsonError::none) return error;
            skip_space(cursor);
            if (cursor.peek() == ',') {
                ++cursor.at;
                continue;
            }
            if (cursor.peek() != close) return JsonError::malformed;
            ++cursor.at;
            return JsonError::none;
        }
    }
    if (c == '"') {
        std::string_view content;
        bool escaped = false;
        return scan_string(cursor, content, escaped) ? JsonError::none : JsonError::malformed;
    }
    if (c == 't') return scan_literal(cursor, "true") ? JsonError::none : JsonError::malformed;
    if (c == 'f') return scan_literal(cursor, "false") ? JsonError::none : JsonError::malformed;
    if (c == 'n') return scan_literal(cursor, "null") ? JsonError::none : JsonError::malformed;
    return scan_number(cursor) ? JsonError::none : JsonError::malformed;
}

struct Member {
    std::string_view key;
    bool key_escaped{};
    std::string_view raw;
};

// Walks the top-level members of a validated object. `visit` returns false
// to stop early.
template <typename Visit>
[[nodiscard]] bool each_member(const std::string_view object, Visit&& visit) noexcept {
    Cursor cursor{object, 0};
    skip_space(cursor);
    if (cursor.peek() != '{') return false;
    ++cursor.at;
    skip_space(cursor);
    if (cursor.peek() == '}') return true;
    while (true) {
        skip_space(cursor);
        Member member;
        if (!scan_string(cursor, member.key, member.key_escaped)) return false;
        skip_space(cursor);
        if (cursor.peek() != ':') return false;
        ++cursor.at;
        skip_space(cursor);
        const auto begin = cursor.at;
        if (scan_value(cursor, 1) != JsonError::none) return false;
        member.raw = object.substr(begin, cursor.at - begin);
        if (!visit(member)) return true;
        skip_space(cursor);
        if (cursor.peek() == ',') {
            ++cursor.at;
            continue;
        }
        return cursor.peek() == '}';
    }
}

// ---------------------------------------------------------------------------

struct Writer {
    std::span<char> out;
    std::size_t length{};
    bool overflow{};

    void put(const std::string_view text) noexcept {
        if (overflow || text.size() > out.size() - length) {
            overflow = true;
            return;
        }
        std::copy(text.begin(), text.end(), out.begin() + static_cast<std::ptrdiff_t>(length));
        length += text.size();
    }
    void put_unsigned(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        std::reverse(digits.begin(), digits.begin() + static_cast<std::ptrdiff_t>(count));
        put({digits.data(), count});
    }
};

// Builds in scratch and copies only a complete result, so a refusal never
// leaves a prefix in `out`.
template <typename Build>
[[nodiscard]] EncodeResult build_into(const std::span<char> out, Build&& build) noexcept {
    std::array<char, kMaxTmRequestBytes> scratch{};
    Writer writer{scratch, 0, false};
    build(writer);
    if (writer.overflow || writer.length > out.size()) return {EncodeError::output_too_small, 0};
    std::copy(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(writer.length), out.begin());
    return {EncodeError::none, writer.length};
}

[[nodiscard]] bool plain_decimal(const std::string_view text) noexcept {
    if (text.empty() || !digit(text.front()) || !digit(text.back())) return false;
    std::size_t points = 0;
    for (const char c : text) {
        if (c == '.') {
            ++points;
        } else if (!digit(c)) {
            return false;
        }
    }
    return points <= 1;
}

[[nodiscard]] std::string_view side_word(const Side side) noexcept {
    return side == Side::buy ? "buy" : side == Side::sell ? "sell" : "";
}

// A required text member: missing_field if absent, unsafe_text if present but
// not a plain identifier string.
[[nodiscard]] DecodeError required_text(const std::string_view object, const std::string_view key,
                                        TmText& out) noexcept {
    const auto raw = json_member_raw(object, key);
    if (!raw) return DecodeError::missing_field;
    const auto content = json_member_string(object, key);
    if (!content) return DecodeError::unsafe_text;
    const auto text = tm_text(*content);
    if (!text) return DecodeError::unsafe_text;
    out = *text;
    return DecodeError::none;
}

[[nodiscard]] DecodeError required_amount(const std::string_view object, const std::string_view key,
                                          const std::uint8_t decimals, std::int64_t& out) noexcept {
    if (!json_member_raw(object, key)) return DecodeError::missing_field;
    const auto content = json_member_string(object, key);
    if (!content) return DecodeError::bad_amount;
    const auto atoms = parse_decimal_atoms(*content, decimals);
    if (!atoms) return DecodeError::bad_amount;
    out = *atoms;
    return DecodeError::none;
}

}  // namespace

std::optional<TmText> tm_text(const std::string_view text) noexcept {
    if (text.empty() || text.size() > kMaxTmTextBytes) return std::nullopt;
    TmText value;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!identifier_char(text[i])) return std::nullopt;
        value.bytes[i] = text[i];
    }
    value.length = static_cast<std::uint8_t>(text.size());
    return value;
}

std::optional<std::int64_t> parse_decimal_atoms(const std::string_view text,
                                                const std::uint8_t decimals) noexcept {
    if (!plain_decimal(text) || decimals > 18) return std::nullopt;
    const auto point = text.find('.');
    const auto whole = text.substr(0, point);
    const auto fraction = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    if (fraction.size() > decimals) return std::nullopt;
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    std::int64_t value = 0;
    const auto push = [&](const char c) {
        const std::int64_t d = c - '0';
        if (value > (kMax - d) / 10) return false;
        value = value * 10 + d;
        return true;
    };
    for (const char c : whole)
        if (!push(c)) return std::nullopt;
    for (const char c : fraction)
        if (!push(c)) return std::nullopt;
    for (std::size_t i = fraction.size(); i < decimals; ++i)
        if (!push('0')) return std::nullopt;
    return value;
}

JsonError validate_json_object(const std::string_view document) noexcept {
    if (document.size() > kMaxTmResponseBytes) return JsonError::too_large;
    Cursor cursor{document, 0};
    skip_space(cursor);
    if (cursor.peek() != '{') return JsonError::not_object;
    if (const auto error = scan_value(cursor, 0); error != JsonError::none) return error;
    skip_space(cursor);
    if (!cursor.done()) return JsonError::trailing_bytes;

    // Top-level keys: no escapes (they would hide a duplicate) and no repeats.
    constexpr std::size_t kMaxKeys = 64;
    std::array<std::string_view, kMaxKeys> keys{};
    std::size_t count = 0;
    JsonError verdict = JsonError::none;
    const bool walked = each_member(document, [&](const Member& member) {
        if (member.key_escaped) {
            verdict = JsonError::malformed;
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (keys[i] == member.key) {
                verdict = JsonError::duplicate_key;
                return false;
            }
        }
        if (count == kMaxKeys) {
            verdict = JsonError::too_large;
            return false;
        }
        keys[count++] = member.key;
        return true;
    });
    if (!walked) return JsonError::malformed;
    return verdict;
}

std::optional<std::string_view> json_member_raw(const std::string_view object,
                                                const std::string_view key) noexcept {
    std::optional<std::string_view> found;
    (void)each_member(object, [&](const Member& member) {
        if (member.key_escaped || member.key != key) return true;
        found = member.raw;
        return false;
    });
    return found;
}

std::optional<std::string_view> json_member_string(const std::string_view object,
                                                   const std::string_view key) noexcept {
    const auto raw = json_member_raw(object, key);
    if (!raw) return std::nullopt;
    Cursor cursor{*raw, 0};
    std::string_view content;
    bool escaped = false;
    if (!scan_string(cursor, content, escaped) || escaped || !cursor.done()) return std::nullopt;
    return content;
}

std::optional<std::size_t> json_array_elements(const std::string_view array,
                                               const std::span<std::string_view> out) noexcept {
    Cursor cursor{array, 0};
    skip_space(cursor);
    if (cursor.peek() != '[') return std::nullopt;
    ++cursor.at;
    skip_space(cursor);
    std::size_t count = 0;
    if (cursor.peek() == ']') return count;
    while (true) {
        skip_space(cursor);
        const auto begin = cursor.at;
        if (scan_value(cursor, 1) != JsonError::none) return std::nullopt;
        if (count == out.size()) return std::nullopt;
        out[count++] = array.substr(begin, cursor.at - begin);
        skip_space(cursor);
        if (cursor.peek() == ',') {
            ++cursor.at;
            continue;
        }
        if (cursor.peek() != ']') return std::nullopt;
        return count;
    }
}

EncodeResult encode_trade_call(const std::uint64_t rpc_id, const TradeTool tool,
                               const TradeArguments& arguments, const std::span<char> out) noexcept {
    if (arguments.chain == Chain::unset || arguments.qty_unit == QtyUnit::unset) {
        return {EncodeError::unset_field, 0};
    }
    if (side_word(arguments.side).empty() || arguments.base_asset.empty() ||
        !plain_decimal(arguments.qty.view()) ||
        (tool != TradeTool::get_quote && tool != TradeTool::trade_prepare)) {
        return {EncodeError::unrepresentable_value, 0};
    }
    return build_into(out, [&](Writer& w) {
        w.put(R"({"jsonrpc":"2.0","id":)");
        w.put_unsigned(rpc_id);
        w.put(R"(,"method":"tools/call","params":{"name":")");
        w.put(tool == TradeTool::get_quote ? "get_quote" : "trade_prepare");
        w.put(R"(","arguments":{"chain":")");
        w.put(arguments.chain == Chain::solana ? "solana" : "base");
        w.put(R"(","side":")");
        w.put(side_word(arguments.side));
        w.put(R"(","base_asset":")");
        w.put(arguments.base_asset.view());
        w.put(R"(","qty":")");
        w.put(arguments.qty.view());
        w.put(R"(","qty_unit":")");
        w.put(arguments.qty_unit == QtyUnit::base ? "base" : "quote");
        w.put(R"("}}})");
    });
}

EncodeResult encode_trade_execute(const std::uint64_t rpc_id, const TmText& order_id,
                                  const std::span<const Signature> signatures,
                                  const std::span<char> out) noexcept {
    if (order_id.empty() || signatures.empty() || signatures.size() > kMaxTmPayloads) {
        return {EncodeError::unrepresentable_value, 0};
    }
    for (const auto& signature : signatures) {
        if (signature.length == 0 || signature.length > kMaxSignatureBytes) {
            return {EncodeError::unrepresentable_value, 0};
        }
        for (const char c : signature.view()) {
            if (!signature_char(c)) return {EncodeError::unsafe_text, 0};
        }
    }
    return build_into(out, [&](Writer& w) {
        w.put(R"({"jsonrpc":"2.0","id":)");
        w.put_unsigned(rpc_id);
        w.put(R"(,"method":"tools/call","params":{"name":"trade_execute","arguments":{"order_id":")");
        w.put(order_id.view());
        w.put(R"(","signatures":"[)");
        for (std::size_t i = 0; i < signatures.size(); ++i) {
            if (i != 0) w.put(",");
            w.put(R"(\")");
            w.put(signatures[i].view());
            w.put(R"(\")");
        }
        w.put(R"(]"}}})");
    });
}

EncodeResult encode_order_status_path(const TmText& order_id, const std::span<char> out) noexcept {
    if (order_id.empty()) return {EncodeError::unrepresentable_value, 0};
    return build_into(out, [&](Writer& w) {
        w.put("/v1/conductor/orders/");
        w.put(order_id.view());
        w.put("/status");
    });
}

DecodeError decode_trade_reply(const std::string_view document, const ReplyShape shape,
                               const AmountDecimals& decimals, TradeReply& out) noexcept {
    if (validate_json_object(document) != JsonError::none) return DecodeError::malformed;
    TradeReply reply;
    const auto handle_key = shape == ReplyShape::quote ? "quote_id" : "order_id";
    if (const auto e = required_text(document, handle_key, reply.handle); e != DecodeError::none) return e;

    if (!json_member_raw(document, "order_side")) return DecodeError::missing_field;
    const auto side = json_member_string(document, "order_side");
    if (!side) return DecodeError::unsafe_text;
    if (*side == "buy") {
        reply.side = Side::buy;
    } else if (*side == "sell") {
        reply.side = Side::sell;
    } else {
        return DecodeError::bad_side;
    }
    for (const auto& [key, target] : {std::pair{"base_asset", &reply.base_asset},
                                      std::pair{"quote_asset", &reply.quote_asset},
                                      std::pair{"fee_asset", &reply.fee_asset}}) {
        if (const auto e = required_text(document, key, *target); e != DecodeError::none) return e;
    }
    if (const auto e = required_amount(document, "qty", decimals.qty, reply.qty_atoms); e != DecodeError::none)
        return e;
    if (const auto e = required_amount(document, "qty_out", decimals.qty_out, reply.qty_out_atoms);
        e != DecodeError::none)
        return e;
    if (const auto e = required_amount(document, "fee", decimals.fee, reply.fee_atoms); e != DecodeError::none)
        return e;

    if (const auto issues = json_member_raw(document, "issues")) {
        std::array<std::string_view, kMaxTmIssues + 1> elements{};
        const auto count = json_array_elements(*issues, elements);
        if (!count) return issues->front() == '[' ? DecodeError::too_many_issues : DecodeError::malformed;
        if (*count > kMaxTmIssues) return DecodeError::too_many_issues;
        reply.issue_count = *count;
    }
    if (const auto payloads = json_member_raw(document, "payloads")) {
        std::array<std::string_view, kMaxTmPayloads + 1> elements{};
        const auto count = json_array_elements(*payloads, elements);
        if (!count) return payloads->front() == '[' ? DecodeError::too_many_payloads : DecodeError::malformed;
        if (*count > kMaxTmPayloads) return DecodeError::too_many_payloads;
        reply.payload_count = *count;
        std::copy(elements.begin(), elements.begin() + static_cast<std::ptrdiff_t>(*count), reply.payloads.begin());
    }
    out = reply;
    return DecodeError::none;
}

DecodeError decode_execute_reply(const std::string_view document, ExecuteReply& out) noexcept {
    if (validate_json_object(document) != JsonError::none) return DecodeError::malformed;
    ExecuteReply reply;
    if (const auto e = required_text(document, "order_id", reply.order_id); e != DecodeError::none) return e;
    if (const auto e = required_text(document, "status", reply.status); e != DecodeError::none) return e;
    out = reply;
    return DecodeError::none;
}

DecodeError decode_status_reply(const std::string_view document, const AmountDecimals& decimals,
                                StatusReply& out) noexcept {
    if (validate_json_object(document) != JsonError::none) return DecodeError::malformed;
    StatusReply reply;
    if (const auto e = required_text(document, "order_id", reply.order_id); e != DecodeError::none) return e;
    if (const auto e = required_text(document, "status", reply.status); e != DecodeError::none) return e;
    const std::size_t present = (json_member_raw(document, "qty") ? 1U : 0U) +
                                (json_member_raw(document, "qty_out") ? 1U : 0U) +
                                (json_member_raw(document, "fee") ? 1U : 0U) +
                                (json_member_raw(document, "tx_signature") ? 1U : 0U);
    if (present != 0 && present != 4) return DecodeError::missing_field;
    if (present == 4) {
        if (const auto e = required_amount(document, "qty", decimals.qty, reply.qty_atoms); e != DecodeError::none)
            return e;
        if (const auto e = required_amount(document, "qty_out", decimals.qty_out, reply.qty_out_atoms);
            e != DecodeError::none)
            return e;
        if (const auto e = required_amount(document, "fee", decimals.fee, reply.fee_atoms); e != DecodeError::none)
            return e;
        if (const auto e = required_text(document, "tx_signature", reply.tx_signature); e != DecodeError::none)
            return e;
        reply.has_execution = true;
    }
    out = reply;
    return DecodeError::none;
}

}  // namespace velocita::v2::truemarkets
