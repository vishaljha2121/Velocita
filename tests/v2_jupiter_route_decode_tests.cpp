#include "velocita/v2/jupiter_route_decode.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace velocita::v2::truemarkets;
namespace {
using Status = JupiterRouteStatus;
void require(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
}
std::vector<std::uint8_t> literal() {
    constexpr std::string_view hex = "c1209b3341d69c810602000000a100010000006400018d0064010240420f0000000000e8418c0000000000f40114";
    std::vector<std::uint8_t> out;
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(16 * digit(hex[i]) + digit(hex[i+1])));
    return out;
}
void refuse(const JupiterRouteDecodeResult& result, Status status, const char* name) {
    require(result.status == status && result.data == JupiterSharedRouteData{}, name);
}
void captured_oracle() {
    auto bytes = literal();
    auto result = decode_jupiter_shared_route_data(bytes);
    require(result.status == Status::decoded_data_only, "captured_route_decodes");
    const auto& d = result.data;
    require(d.authority_id == 6 && d.step_count == 2, "authority_and_route_count");
    require(d.steps[0] == JupiterRouteStep{JupiterSwapKind::deriverse,0,100,0,1,1}, "deriverse_fields_in_order");
    require(d.steps[1] == JupiterRouteStep{JupiterSwapKind::bison_fi_v2,0,100,1,2,0}, "bison_fields_in_order");
    require(d.input_atoms == 1000000 && d.quoted_output_atoms == 9191912, "captured_input_and_quote");
    require(d.slippage_bps == 500 && d.platform_fee_bps == 20, "captured_slippage_and_fee");
    const auto saved = result;
    bytes.assign(bytes.size(),255);
    require(result == saved, "owned_result_survives_input_change");
}
void exact_shapes() {
    auto bytes = literal();
    for (std::size_t n = 0; n < bytes.size(); ++n)
        refuse(decode_jupiter_shared_route_data({bytes.data(),n}), Status::invalid, "all_truncations_refused");
    bytes.push_back(0);
    refuse(decode_jupiter_shared_route_data(bytes),Status::invalid,"trailing_bytes_refused");
    bytes = literal(); bytes[9] = 0;
    bytes.erase(bytes.begin()+13,bytes.begin()+27);
    refuse(decode_jupiter_shared_route_data(bytes),Status::invalid,"empty_route_refused");
    bytes = literal(); bytes[12] = 255;
    refuse(decode_jupiter_shared_route_data(bytes),Status::invalid,"route_count_capacity");
    bytes = literal();
    std::vector<std::uint8_t> eight(bytes.begin(),bytes.begin()+13);
    eight[9] = 8;
    for (int i = 0; i < 8; ++i) eight.insert(eight.end(),bytes.begin()+22,bytes.begin()+27);
    eight.insert(eight.end(),bytes.begin()+27,bytes.end());
    require(decode_jupiter_shared_route_data(eight).status == Status::decoded_data_only, "eight_steps_fit");
    eight[9] = 9;
    refuse(decode_jupiter_shared_route_data(eight),Status::invalid,"ninth_step_refused");
    bytes = literal(); bytes[14] = 2;
    refuse(decode_jupiter_shared_route_data(bytes),Status::invalid,"invalid_side_refused");
    bytes = literal(); bytes[23] = 2;
    refuse(decode_jupiter_shared_route_data(bytes),Status::invalid,"invalid_bool_refused");
}
void unsupported_and_boundaries() {
    auto bytes = literal(); bytes[0] ^= 1;
    refuse(decode_jupiter_shared_route_data(bytes),Status::unsupported,"other_instruction_refused");
    bytes = literal(); bytes[13] = 160;
    refuse(decode_jupiter_shared_route_data(bytes),Status::unsupported,"unknown_first_variant_refused");
    bytes = literal(); bytes[22] = 140;
    refuse(decode_jupiter_shared_route_data(bytes),Status::unsupported,"unknown_later_variant_clears_partial");
    bytes = literal(); bytes[14] = 1; bytes[23] = 1;
    for (std::size_t i = 15; i < 19; ++i) bytes[i] = 255;
    for (std::size_t i = 27; i < bytes.size(); ++i) bytes[i] = 255;
    const auto result = decode_jupiter_shared_route_data(bytes);
    require(result.status == Status::decoded_data_only && result.data.steps[0].side_or_direction == 1 &&
            result.data.steps[1].side_or_direction == 1, "enum_and_bool_one");
    require(result.data.steps[0].instruction_id == std::numeric_limits<std::uint32_t>::max(), "instruction_id_unsigned");
    require(result.data.input_atoms == std::numeric_limits<std::uint64_t>::max() &&
            result.data.quoted_output_atoms == std::numeric_limits<std::uint64_t>::max(), "amounts_no_signed_narrowing");
    require(result.data.slippage_bps == 65535 && result.data.platform_fee_bps == 255,
            "structural_decoder_does_not_invent_economic_approval");
}
void variant_orders() {
    const auto bytes = literal();
    for (const auto range : {std::pair{13,22},std::pair{22,27}}) {
        std::vector<std::uint8_t> one(bytes.begin(),bytes.begin()+13);
        one[9] = 1;
        one.insert(one.end(),bytes.begin()+range.first,bytes.begin()+range.second);
        one.insert(one.end(),bytes.begin()+27,bytes.end());
        const auto result = decode_jupiter_shared_route_data(one);
        require(result.status == Status::decoded_data_only && result.data.step_count == 1 &&
                result.data.input_atoms == 1000000 && result.data.quoted_output_atoms == 9191912,
                "single_variant_preserves_suffix_offset");
    }
    std::vector<std::uint8_t> reverse(bytes.begin(),bytes.begin()+13);
    reverse.insert(reverse.end(),bytes.begin()+22,bytes.begin()+27);
    reverse.insert(reverse.end(),bytes.begin()+13,bytes.begin()+22);
    reverse.insert(reverse.end(),bytes.begin()+27,bytes.end());
    const auto result = decode_jupiter_shared_route_data(reverse);
    require(result.status == Status::decoded_data_only &&
            result.data.steps[0] == JupiterRouteStep{JupiterSwapKind::bison_fi_v2,0,100,1,2,0} &&
            result.data.steps[1] == JupiterRouteStep{JupiterSwapKind::deriverse,0,100,0,1,1} &&
            result.data.slippage_bps == 500 && result.data.platform_fee_bps == 20,
            "reversed_variants_are_data_not_economics");
}
std::vector<std::uint8_t> direct_literal() {
    // Independent byte oracle from program-owned IDL: 8 discriminator bytes,
    // u32 count=1, enum=177, bool=false, percent=100, indices=0/1,
    // LE u64 input=1,000,000 and quote=9,364,365, u16 slippage=500, u8 fee=20.
    return {0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,
        177,0,100,0,1,0x40,0x42,0x0f,0,0,0,0,0,
        0x8d,0xe3,0x8e,0,0,0,0,0,0xf4,1,20};
}
void direct_capture() {
    auto bytes=direct_literal();const auto r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::decoded_data_only && r.data.step_count==1,"direct_route_count_without_authority_byte");
    require(r.data.steps[0]==JupiterRouteStep{JupiterSwapKind::bison_fi_market_backed,0,100,0,1,0},
        "market_backed_bool_and_route_fields");
    require(r.data.input_atoms==1'000'000 && r.data.quoted_output_atoms==9'364'365,
        "direct_route_exact_amounts");
    require(r.data.slippage_bps==500 && r.data.platform_fee_bps==20,"direct_route_slippage_and_fee");
    const auto saved=r;bytes.assign(bytes.size(),255);
    require(r==saved,"direct_result_owns_fields");
    refuse(decode_jupiter_shared_route_data(direct_literal()),Status::unsupported,"direct_not_shared_route");
    const auto shared=decode_jupiter_direct_route_data(literal());
    require(shared.status==Status::unsupported && shared.data==JupiterDirectRouteData{},"shared_not_direct_route");
}
void direct_boundaries() {
    const auto clean=direct_literal();
    for(std::size_t n=0;n<clean.size();++n){const auto r=decode_jupiter_direct_route_data({clean.data(),n});
        require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_truncation_clears_result");}
    auto bytes=clean;bytes.push_back(0);auto r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_trailing_bytes_refused");
    bytes=clean;bytes[13]=2;r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_invalid_bool_refused");
    bytes=clean;bytes[12]=178;r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::unsupported && r.data==JupiterDirectRouteData{},"direct_unknown_variant_refused");
    bytes=clean;bytes[8]=0;r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_empty_route_refused");
    bytes=clean;bytes[11]=255;r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_large_count_refused");
    std::vector<std::uint8_t> eight(clean.begin(),clean.begin()+12);eight[8]=8;
    for(unsigned i=0;i<8;++i)eight.insert(eight.end(),clean.begin()+12,clean.begin()+17);
    eight.insert(eight.end(),clean.begin()+17,clean.end());
    r=decode_jupiter_direct_route_data(eight);
    require(r.status==Status::decoded_data_only && r.data.step_count==8 && r.data.input_atoms==1'000'000,
        "direct_eight_steps_fit");
    eight[8]=9;r=decode_jupiter_direct_route_data(eight);
    require(r.status==Status::invalid && r.data==JupiterDirectRouteData{},"direct_ninth_step_refused");
    bytes=clean;bytes[13]=1;std::fill(bytes.begin()+17,bytes.end(),255);r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::decoded_data_only && r.data.steps[0].side_or_direction==1 &&
        r.data.input_atoms==std::numeric_limits<std::uint64_t>::max() &&
        r.data.quoted_output_atoms==std::numeric_limits<std::uint64_t>::max() &&
        r.data.slippage_bps==65535 && r.data.platform_fee_bps==255,"direct_unsigned_fields_do_not_grant_admission");
}
void direct_raydium() {
    // Pinned program-owned IDL: variant 26 is RaydiumClmm with NO fields.
    // Hand-decoded fresh capture: 26,100,0,1 then LE amounts; no bool byte.
    std::vector<std::uint8_t> bytes{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,
        26,100,0,1,0x40,0x42,0x0f,0,0,0,0,0,0xc3,0xa8,0x92,0,0,0,0,0,0xf4,1,20};
    const auto r=decode_jupiter_direct_route_data(bytes);
    require(r.status==Status::decoded_data_only && r.data.step_count==1 &&
        static_cast<unsigned>(r.data.steps[0].kind)==26 && r.data.steps[0].side_or_direction==0 &&
        r.data.steps[0].percent==100 && r.data.steps[0].input_index==0 && r.data.steps[0].output_index==1,
        "raydium_no_variant_payload");
    require(r.data.input_atoms==1'000'000 && r.data.quoted_output_atoms==9'611'459 &&
        r.data.slippage_bps==500 && r.data.platform_fee_bps==20,"raydium_exact_suffix_offset");
    for(std::size_t n=0;n<bytes.size();++n){const auto short_r=decode_jupiter_direct_route_data({bytes.data(),n});
        require(short_r.status==Status::invalid && short_r.data==JupiterDirectRouteData{},"raydium_truncated_refused");}
    auto extra=bytes;extra.insert(extra.begin()+13,0);
    require(decode_jupiter_direct_route_data(extra).status==Status::invalid,"raydium_spurious_bool_refused");
    auto mixed=bytes; mixed[8]=2;
    const auto bison=direct_literal();mixed.insert(mixed.begin()+16,bison.begin()+12,bison.begin()+17);
    const auto two=decode_jupiter_direct_route_data(mixed);
    require(two.status==Status::decoded_data_only && two.data.step_count==2 &&
        two.data.steps[1].kind==JupiterSwapKind::bison_fi_market_backed && two.data.quoted_output_atoms==9'611'459,
        "mixed_variant_offsets");
}
void direct_current_variants() {
    // Hand-written Borsh oracle: Obsidian (109) has no fields; KipseliV3
    // (181) has one bool. Suffix: 1,000,000 input, 9,410,623 quote, 500bp,20bp.
    const std::vector<std::uint8_t> obsidian{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,
        109,100,0,1,0x40,0x42,0x0f,0,0,0,0,0,0x3f,0x98,0x8f,0,0,0,0,0,0xf4,1,20};
    const std::vector<std::uint8_t> kipseli{0xe5,0x17,0xcb,0x97,0x7a,0xe3,0xad,0x2a,1,0,0,0,
        181,0,100,0,1,0x40,0x42,0x0f,0,0,0,0,0,0x3f,0x98,0x8f,0,0,0,0,0,0xf4,1,20};
    for (const auto& bytes : {obsidian,kipseli}) {
        const auto r=decode_jupiter_direct_route_data(bytes);
        require(r.status==Status::decoded_data_only && r.data.step_count==1,
            "current_variants_decode");
        require(static_cast<unsigned>(r.data.steps[0].kind)==bytes[12] &&
            r.data.steps[0].percent==100 && r.data.steps[0].input_index==0 && r.data.steps[0].output_index==1,
            "current_variant_fields");
        require(r.data.input_atoms==1'000'000 && r.data.quoted_output_atoms==9'410'623 &&
            r.data.slippage_bps==500 && r.data.platform_fee_bps==20,"current_variant_suffix_offsets");
        for(std::size_t n=0;n<bytes.size();++n) {
            const auto short_r=decode_jupiter_direct_route_data({bytes.data(),n});
            require(short_r.status==Status::invalid && short_r.data==JupiterDirectRouteData{},
                "current_variant_truncation");
        }
    }
    auto one=kipseli;one[13]=1;
    require(decode_jupiter_direct_route_data(one).data.steps[0].side_or_direction==1,"kipseli_bool_one");
    one[13]=2;const auto bad=decode_jupiter_direct_route_data(one);
    require(bad.status==Status::invalid && bad.data==JupiterDirectRouteData{},"kipseli_bool_two_refused");
    auto extra=obsidian;extra.insert(extra.begin()+13,0);
    require(decode_jupiter_direct_route_data(extra).status==Status::invalid,"obsidian_spurious_field_refused");
}

}
int main(int argc, char** argv) {
    const std::pair<const char*,void(*)()> tests[] = {{"captured",captured_oracle},{"shapes",exact_shapes},
        {"boundaries",unsupported_and_boundaries},{"orders",variant_orders},
        {"direct",direct_capture},{"direct_boundaries",direct_boundaries},{"raydium",direct_raydium},{"current_variants",direct_current_variants}};
    int failures{}, selected{};
    for (const auto& [name,fn] : tests) {
        if (argc > 1 && std::string_view(argv[1]) != name) continue;
        ++selected;
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    if (!selected || argc > 2) { std::cerr << "Unknown test selection\n"; return 2; }
    return failures ? 1 : 0;
}
