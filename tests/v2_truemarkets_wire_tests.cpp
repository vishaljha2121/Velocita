#include "velocita/v2/truemarkets_wire.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

// Expected values are written out by hand from the recorded evidence
// (docs/evidence/truemarkets_markup_2026-09-28/quotes_buy_sell_1631utc.json,
// SOL $25 rows) and the recorded MCP input schema; none is read back from the
// code under test.

namespace {

using namespace velocita::v2;
using namespace velocita::v2::truemarkets;

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

TmText text(const std::string_view value) {
    const auto made = tm_text(value);
    require(made.has_value(), "fixture text refused: " + std::string(value));
    return *made;
}

constexpr std::string_view kSol = "So11111111111111111111111111111111111111112";
constexpr std::string_view kUsdc = "EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v";

// Recorded 2026-09-28 16:31:50 UTC, SOL buy $25 (qty_unit quote). The payloads
// member was omitted from the evidence and is absent here.
constexpr std::string_view kRecordedBuy =
    R"({"base_asset":"So11111111111111111111111111111111111111112",)"
    R"("quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","order_side":"buy",)"
    R"("qty":"25","qty_out":"0.20633117","fee":"0.25",)"
    R"("fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",)"
    R"("quote_id":"2dae2384-b83d-4ea6-ba3d-819b86e5d0e6",)"
    R"("issues":[{"balance":{"actual":"0","expected":"25"},"message":"Insufficient balance"}]})";

// Recorded 2026-09-28 16:31:51 UTC, SOL sell (qty_unit base).
constexpr std::string_view kRecordedSell =
    R"({"base_asset":"So11111111111111111111111111111111111111112",)"
    R"("quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","order_side":"sell",)"
    R"("qty":"0.20839","qty_out":"24.752144","fee":"0.250021",)"
    R"("fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",)"
    R"("quote_id":"6e131038-eebd-4ab4-8a19-6ead1195d400",)"
    R"("issues":[{"balance":{"actual":"0","expected":"0.20839"},"message":"Insufficient balance"}]})";

void test_decimal_parse_is_exact_and_refuses_rounding() {
    require(parse_decimal_atoms("25", 6) == 25'000'000, "25 USDC is 25'000'000 micros");
    require(parse_decimal_atoms("0.20633117", 9) == 206'331'170, "0.20633117 SOL is 206'331'170 lamports");
    require(parse_decimal_atoms("0", 6) == 0, "zero");
    require(parse_decimal_atoms("007", 0) == 7, "leading zeros are digits");
    require(parse_decimal_atoms("9223372036854.775807", 6) == std::numeric_limits<std::int64_t>::max(),
            "the largest representable amount");
    for (const std::string_view bad :
         {"", "1.", ".5", "-1", "+1", "1e3", " 1", "1 ", "0x10", "1.2.3", "0.1234567",
          "9223372036854.775808", "99999999999999999999"}) {
        require(!parse_decimal_atoms(bad, 6).has_value(), "accepted " + std::string(bad));
    }
    // More fractional digits than the asset has is refused, not rounded.
    require(!parse_decimal_atoms("0.2063311701", 9).has_value(), "rounded excess precision");
}

void test_json_validation_refuses_ambiguity_and_garbage() {
    require(validate_json_object(R"({"a":"1","b":{"c":[1,2,{"d":null}]},"e":true})") == JsonError::none,
            "a well-formed object was refused");
    require(validate_json_object(R"({"a":"1","a":"2"})") == JsonError::duplicate_key,
            "a repeated top-level key was accepted");
    require(validate_json_object(R"({"a":1} x)") == JsonError::trailing_bytes, "trailing bytes accepted");
    require(validate_json_object(R"(["a"])") == JsonError::not_object, "an array was accepted as an object");
    require(validate_json_object(R"({"a":)") == JsonError::malformed, "a truncated object was accepted");
    require(validate_json_object(R"({"a":"unterminated})") == JsonError::malformed, "an open string was accepted");
    std::string deep = "{\"a\":";
    for (int i = 0; i < 16; ++i) deep += "[";
    for (int i = 0; i < 16; ++i) deep += "]";
    deep += "}";
    require(validate_json_object(deep) == JsonError::too_deep, "17 levels were accepted");
    require(json_member_string(R"({"a":"x\"y"})", "a") == std::nullopt, "an escaped string was returned raw");
    require(json_member_string(R"({"a":"xy","b":{"a":"z"}})", "a") == std::string_view("xy"),
            "a nested key shadowed the top-level one");
    std::array<std::string_view, 2> elements{};
    require(json_array_elements(R"([{"a":[1,2]},"b"])", elements) == std::size_t{2} &&
                elements[0] == R"({"a":[1,2]})" && elements[1] == R"("b")",
            "array elements were not returned raw");
    require(!json_array_elements(R"([1,2,3])", elements).has_value(), "an over-long array was truncated");
}

void test_trade_call_encodes_every_field_explicitly() {
    std::array<char, 512> out{};
    const TradeArguments arguments{Side::sell, text(kSol), text("0.20839"), QtyUnit::base, Chain::solana};
    const auto result = encode_trade_call(7, TradeTool::get_quote, arguments, out);
    const std::string_view expected =
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_quote","arguments":)"
        R"({"chain":"solana","side":"sell","base_asset":"So11111111111111111111111111111111111111112",)"
        R"("qty":"0.20839","qty_unit":"base"}}})";
    require(result.error == EncodeError::none &&
                std::string_view(out.data(), result.bytes_written) == expected,
            "get_quote body differs from the recorded schema: " +
                std::string(out.data(), result.bytes_written));

    const auto prepare = encode_trade_call(8, TradeTool::trade_prepare,
                                           {Side::buy, text(kSol), text("0.2"), QtyUnit::base, Chain::base}, out);
    const std::string_view expected_prepare =
        R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"trade_prepare","arguments":)"
        R"({"chain":"base","side":"buy","base_asset":"So11111111111111111111111111111111111111112",)"
        R"("qty":"0.2","qty_unit":"base"}}})";
    require(prepare.error == EncodeError::none &&
                std::string_view(out.data(), prepare.bytes_written) == expected_prepare,
            "trade_prepare body differs");

    // D-25 / D-24: an unset chain or unit is refused, never omitted.
    out.fill('#');
    auto no_chain = arguments;
    no_chain.chain = Chain::unset;
    require(encode_trade_call(7, TradeTool::get_quote, no_chain, out).error == EncodeError::unset_field &&
                out[0] == '#',
            "an order without chain was encoded (it would route to CeFi)");
    auto no_unit = arguments;
    no_unit.qty_unit = QtyUnit::unset;
    require(encode_trade_call(7, TradeTool::get_quote, no_unit, out).error == EncodeError::unset_field,
            "an order without qty_unit was encoded (its default depends on side)");

    std::array<char, 40> small{};
    small.fill('#');
    require(encode_trade_call(7, TradeTool::get_quote, arguments, small).error ==
                    EncodeError::output_too_small &&
                small[0] == '#',
            "a short buffer received a partial body");
}

void test_execute_and_status_encoding() {
    std::array<Signature, 2> signatures{};
    const std::string_view first = "abc123";
    const std::string_view second = "DEF-456_x";
    std::copy(first.begin(), first.end(), signatures[0].text.begin());
    signatures[0].length = static_cast<std::uint16_t>(first.size());
    std::copy(second.begin(), second.end(), signatures[1].text.begin());
    signatures[1].length = static_cast<std::uint16_t>(second.size());
    std::array<char, 512> out{};
    const auto result = encode_trade_execute(9, text("ord-1"), signatures, out);
    const std::string_view expected =
        R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"trade_execute","arguments":)"
        R"({"order_id":"ord-1","signatures":"[\"abc123\",\"DEF-456_x\"]"}}})";
    require(result.error == EncodeError::none &&
                std::string_view(out.data(), result.bytes_written) == expected,
            "trade_execute body differs: " + std::string(out.data(), result.bytes_written));

    std::array<Signature, 1> unsafe{};
    const std::string_view quote = "ab\"c";
    std::copy(quote.begin(), quote.end(), unsafe[0].text.begin());
    unsafe[0].length = static_cast<std::uint16_t>(quote.size());
    require(encode_trade_execute(9, text("ord-1"), unsafe, out).error == EncodeError::unsafe_text,
            "a signature that needs escaping was embedded");
    require(encode_trade_execute(9, text("ord-1"), std::span<const Signature>{}, out).error ==
                EncodeError::unrepresentable_value,
            "an execute without signatures was encoded");

    const auto path = encode_order_status_path(text("ord-1"), out);
    require(path.error == EncodeError::none &&
                std::string_view(out.data(), path.bytes_written) == "/v1/conductor/orders/ord-1/status",
            "status path differs");
    require(!tm_text("a/b").has_value() && !tm_text("a b").has_value() && !tm_text("").has_value() &&
                !tm_text("a\"").has_value(),
            "unsafe identifier text was accepted");
}

void test_recorded_quotes_decode_exactly() {
    TradeReply buy;
    require(decode_trade_reply(kRecordedBuy, ReplyShape::quote, {6, 9, 6}, buy) == DecodeError::none,
            "the recorded buy quote did not decode");
    require(buy.handle.view() == "2dae2384-b83d-4ea6-ba3d-819b86e5d0e6" && buy.side == Side::buy &&
                buy.base_asset.view() == kSol && buy.quote_asset.view() == kUsdc &&
                buy.fee_asset.view() == kUsdc,
            "recorded buy identities differ");
    require(buy.qty_atoms == 25'000'000 && buy.qty_out_atoms == 206'331'170 && buy.fee_atoms == 250'000,
            "recorded buy amounts differ");
    require(buy.issue_count == 1 && buy.payload_count == 0, "recorded buy issues/payloads differ");

    TradeReply sell;
    require(decode_trade_reply(kRecordedSell, ReplyShape::quote, {9, 6, 6}, sell) == DecodeError::none,
            "the recorded sell quote did not decode");
    require(sell.side == Side::sell && sell.qty_atoms == 208'390'000 &&
                sell.qty_out_atoms == 24'752'144 && sell.fee_atoms == 250'021,
            "recorded sell amounts differ");

    // A quote is not a prepare: the handle field differs.
    TradeReply wrong;
    require(decode_trade_reply(kRecordedBuy, ReplyShape::prepare, {6, 9, 6}, wrong) ==
                DecodeError::missing_field,
            "a quote decoded as a prepare");
}

void test_reply_refusals() {
    const auto decode = [](const std::string& body) {
        TradeReply out;
        return decode_trade_reply(body, ReplyShape::prepare, {9, 6, 6}, out);
    };
    const std::string good =
        R"({"order_id":"o-1","order_side":"buy","base_asset":"A","quote_asset":"U","qty":"0.2",)"
        R"("qty_out":"24.139","fee":"0.239","fee_asset":"U","issues":[],"route":{"hops":[1,2]},)"
        R"("payloads":[{"unsigned_tx":"x"},{"unsigned_tx":"y"}]})";
    TradeReply parsed;
    require(decode_trade_reply(good, ReplyShape::prepare, {9, 6, 6}, parsed) == DecodeError::none &&
                parsed.payload_count == 2 && parsed.payloads[1] == R"({"unsigned_tx":"y"})" &&
                parsed.qty_atoms == 200'000'000 && parsed.qty_out_atoms == 24'139'000 &&
                parsed.fee_atoms == 239'000,
            "a prepare with an unknown extra member did not decode");
    auto replace = [&](const std::string& from, const std::string& to) {
        auto copy = good;
        copy.replace(copy.find(from), from.size(), to);
        return copy;
    };
    require(decode(replace(R"("order_side":"buy")", R"("order_side":"BUY")")) == DecodeError::bad_side,
            "an unknown side was accepted");
    require(decode(replace(R"("qty":"0.2")", R"("qty":"0.2000000001")")) == DecodeError::bad_amount,
            "excess precision was accepted");
    require(decode(replace(R"("fee":"0.239",)", "")) == DecodeError::missing_field, "a missing fee was accepted");
    require(decode(replace(R"("order_id":"o-1")", R"("order_id":"o\/1")")) == DecodeError::unsafe_text,
            "an escaped id was accepted");
    require(decode(replace(R"("order_id":"o-1")", R"("order_id":7)")) == DecodeError::unsafe_text,
            "a numeric id was accepted as text");
    require(decode(replace(R"([{"unsigned_tx":"x"},{"unsigned_tx":"y"}])", R"(["a","b","c","d","e"])")) ==
                DecodeError::too_many_payloads,
            "payloads beyond the bound were truncated");
    require(decode(replace(R"("issues":[])", R"("issues":[1,2,3,4,5,6,7,8,9])")) == DecodeError::too_many_issues,
            "issues beyond the bound were truncated");
    require(decode(replace(R"("order_id":"o-1")", R"("order_id":"o-1","order_id":"o-2")")) ==
                DecodeError::malformed,
            "two order ids were accepted");
}

void test_fixture_execute_and_status_decode() {
    ExecuteReply execute;
    require(decode_execute_reply(R"({"order_id":"ord-1","status":"submitted"})", execute) == DecodeError::none &&
                execute.order_id.view() == "ord-1" && execute.status.view() == "submitted",
            "execute reply did not decode");
    require(decode_execute_reply(R"({"order_id":"ord-1"})", execute) == DecodeError::missing_field,
            "an execute reply without status was accepted");

    StatusReply status;
    require(decode_status_reply(R"({"order_id":"ord-1","status":"pending"})", {9, 6, 6}, status) ==
                    DecodeError::none &&
                !status.has_execution,
            "pending status did not decode");
    require(decode_status_reply(R"({"order_id":"ord-1","status":"filled","qty":"0.2","qty_out":"24.139",)"
                                R"("fee":"0.239","tx_signature":"5xTx"})",
                                {9, 6, 6}, status) == DecodeError::none &&
                status.has_execution && status.qty_atoms == 200'000'000 && status.qty_out_atoms == 24'139'000 &&
                status.fee_atoms == 239'000 && status.tx_signature.view() == "5xTx",
            "filled status did not decode");
    require(decode_status_reply(R"({"order_id":"ord-1","status":"filled","qty":"0.2"})", {9, 6, 6}, status) ==
                DecodeError::missing_field,
            "a partial execution record was accepted");
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"decimal_parse_is_exact_and_refuses_rounding", test_decimal_parse_is_exact_and_refuses_rounding},
        {"json_validation_refuses_ambiguity_and_garbage", test_json_validation_refuses_ambiguity_and_garbage},
        {"trade_call_encodes_every_field_explicitly", test_trade_call_encodes_every_field_explicitly},
        {"execute_and_status_encoding", test_execute_and_status_encoding},
        {"recorded_quotes_decode_exactly", test_recorded_quotes_decode_exactly},
        {"reply_refusals", test_reply_refusals},
        {"fixture_execute_and_status_decode", test_fixture_execute_and_status_decode},
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (sizeof(tests) / sizeof(tests[0]) - failed) << '/' << sizeof(tests) / sizeof(tests[0])
              << " truemarkets wire tests passed\n";
    return failed == 0 ? 0 : 1;
}
