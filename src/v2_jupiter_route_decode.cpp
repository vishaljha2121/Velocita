#include "velocita/v2/jupiter_route_decode.hpp"

namespace velocita::v2::truemarkets {
namespace {
using Status = JupiterRouteStatus;
struct Cursor {
    std::span<const std::uint8_t> bytes;
    std::size_t offset{};
    bool integer(std::size_t size, std::uint64_t& value) noexcept {
        if (size > bytes.size() - offset) return false;
        value = 0;
        for (std::size_t i = 0; i < size; ++i)
            value |= static_cast<std::uint64_t>(bytes[offset++]) << (8U * i);
        return true;
    }
};
JupiterRouteDecodeResult invalid() noexcept { return {Status::invalid,{}}; }
}
JupiterRouteDecodeResult decode_jupiter_shared_route_data(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::array<std::uint8_t,8> discriminator{193,32,155,51,65,214,156,129};
    if (bytes.size() < discriminator.size()) return invalid();
    for (std::size_t i = 0; i < discriminator.size(); ++i)
        if (bytes[i] != discriminator[i]) return {};
    Cursor cursor{bytes,discriminator.size()};
    JupiterSharedRouteData data;
    std::uint64_t value{};
    if (!cursor.integer(1,value)) return invalid();
    data.authority_id = static_cast<std::uint8_t>(value);
    if (!cursor.integer(4,value) || value == 0 || value > kJupiterRouteSteps) return invalid();
    data.step_count = static_cast<std::uint8_t>(value);
    for (std::size_t i = 0; i < data.step_count; ++i) {
        auto& step = data.steps[i];
        if (!cursor.integer(1,value)) return invalid();
        if (value != 141 && value != 161) return {};
        step.kind = static_cast<JupiterSwapKind>(value);
        if (!cursor.integer(1,value) || value > 1) return invalid();
        step.side_or_direction = static_cast<std::uint8_t>(value);
        if (step.kind == JupiterSwapKind::deriverse) {
            if (!cursor.integer(4,value)) return invalid();
            step.instruction_id = static_cast<std::uint32_t>(value);
        }
        if (!cursor.integer(1,value)) return invalid();
        step.percent = static_cast<std::uint8_t>(value);
        if (!cursor.integer(1,value)) return invalid();
        step.input_index = static_cast<std::uint8_t>(value);
        if (!cursor.integer(1,value)) return invalid();
        step.output_index = static_cast<std::uint8_t>(value);
    }
    if (!cursor.integer(8,data.input_atoms) || !cursor.integer(8,data.quoted_output_atoms) ||
        !cursor.integer(2,value)) return invalid();
    data.slippage_bps = static_cast<std::uint16_t>(value);
    if (!cursor.integer(1,value)) return invalid();
    data.platform_fee_bps = static_cast<std::uint8_t>(value);
    if (cursor.offset != bytes.size()) return invalid();
    return {Status::decoded_data_only,data};
}
JupiterDirectRouteDecodeResult decode_jupiter_direct_route_data(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::array<std::uint8_t,8> direct_tag{229,23,203,151,122,227,173,42};
    if(bytes.size()<direct_tag.size())return {Status::invalid,{}};
    for(std::size_t i=0;i<direct_tag.size();++i)if(bytes[i]!=direct_tag[i])return {};
    Cursor cursor{bytes,direct_tag.size()};
    JupiterDirectRouteData route;
    std::uint64_t number{};
    if(!cursor.integer(4,number) || number==0 || number>kJupiterRouteSteps)return {Status::invalid,{}};
    route.step_count=static_cast<std::uint8_t>(number);
    for(std::size_t i=0;i<route.step_count;++i){
        auto& step=route.steps[i];
        if(!cursor.integer(1,number))return {Status::invalid,{}};
        // Program-owned IDL: RaydiumClmm/Obsidian/ByrealDynamicV3 have no payload;
        // Bison/KipseliV3 have a bool.
        // Decoding either variant remains data only, never economic authority.
        if(number!=177 && number!=26 && number!=109 && number!=181 && number!=156)return {};
        step.kind=static_cast<JupiterSwapKind>(number);
        if(step.kind==JupiterSwapKind::bison_fi_market_backed || step.kind==JupiterSwapKind::kipseli_v3){
            if(!cursor.integer(1,number) || number>1)return {Status::invalid,{}};
            step.side_or_direction=static_cast<std::uint8_t>(number);
        }
        if(!cursor.integer(1,number))return {Status::invalid,{}};
        step.percent=static_cast<std::uint8_t>(number);
        if(!cursor.integer(1,number))return {Status::invalid,{}};
        step.input_index=static_cast<std::uint8_t>(number);
        if(!cursor.integer(1,number))return {Status::invalid,{}};
        step.output_index=static_cast<std::uint8_t>(number);
    }
    if(!cursor.integer(8,route.input_atoms) || !cursor.integer(8,route.quoted_output_atoms) ||
        !cursor.integer(2,number))return {Status::invalid,{}};
    route.slippage_bps=static_cast<std::uint16_t>(number);
    if(!cursor.integer(1,number))return {Status::invalid,{}};
    route.platform_fee_bps=static_cast<std::uint8_t>(number);
    if(cursor.offset!=bytes.size())return {Status::invalid,{}};
    return {Status::decoded_data_only,route};
}
}
