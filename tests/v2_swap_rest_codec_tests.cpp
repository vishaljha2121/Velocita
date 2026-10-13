#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
constexpr std::string_view payload="{\"activity\":\"synthetic\"}\n";
constexpr std::string_view payload_hash="3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256";
std::string fixture(){return R"({"order_id":"order-1","status":"initialized","payloads":[{"digest":"3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256","payload":"{\"activity\":\"synthetic\"}\n"}],"quote":{"base_asset":"So11111111111111111111111111111111111111112","quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","qty":"1","qty_out":"0.009191912","fee":"0.002"}})";}
std::string replace(std::string text,std::string_view from,std::string_view to){const auto at=text.find(from);require(at!=std::string::npos,"fixture replacement exists");text.replace(at,from.size(),to);return text;}
swap_signing::ApiKeyStamp stamp(){
    std::array<std::byte,33> key{};key[0]=std::byte{2};
    const std::array<std::byte,8> der{std::byte{0x30},std::byte{6},std::byte{2},std::byte{1},std::byte{1},std::byte{2},std::byte{1},std::byte{2}};
    swap_signing::ApiKeyStamp out;
    require(swap_signing::encode_api_key_stamp(key,der,out)==swap_signing::StampCode::encoded,"fixture stamp encoded");return out;
}
void exact_create_requests(){
    SwapRestRequest out;
    require(encode_swap_rest_create({Side::buy,1'000'000},out)==SwapRestCode::encoded && out.path_view()=="/v1/gateway/orders" &&
        out.body_view()==R"({"base_asset":"So11111111111111111111111111111111111111112","chain":"solana","side":"buy","type":"market","qty":"1.000000","qty_unit":"quote"})",
        "buy uses exact quote micros and pinned SOL market route");
    require(encode_swap_rest_create({Side::sell,8'400'000},out)==SwapRestCode::encoded &&
        out.body_view()==R"({"base_asset":"So11111111111111111111111111111111111111112","chain":"solana","side":"sell","type":"market","qty":"0.008400000","qty_unit":"base"})",
        "sell uses exact base atoms without price or limit conversion");
    require(encode_swap_rest_create({Side::buy,1},out)==SwapRestCode::encoded && out.body_view().find("0.000001")!=std::string_view::npos,
        "one USDC atom is encoded without floating point rounding");
    require(encode_swap_rest_create({Side::sell,9'223'372'036'854'775'807ULL},out)==SwapRestCode::encoded &&
        out.body_view().find("9223372036.854775807")!=std::string_view::npos,"maximum signed atom quantity stays exact");
}
void create_refusals_clear_output(){
    for(const auto input:{SwapRestInput{Side::buy,0},SwapRestInput{static_cast<Side>(99),1},SwapRestInput{Side::buy,std::numeric_limits<std::uint64_t>::max()}}){
        SwapRestRequest out;out.body.fill('x');out.path.fill('x');out.body_size=out.path_size=1;
        require(encode_swap_rest_create(input,out)==SwapRestCode::invalid_input && out.body_size==0 && out.path_size==0 &&
            std::all_of(out.body.begin(),out.body.end(),[](char c){return c==0;}),"invalid input clears stale executable request");
    }
}
void preserve_original_payload(){
    SwapRestPrepared out;auto document=fixture();
    require(decode_swap_rest_prepare(201,document,{Side::buy,1'000'000},out)==SwapRestCode::decoded_unsigned_only &&
        out.payload_view()==payload && out.payload_digest==*parse_digest256(payload_hash) && out.handle.view()=="order-1" &&
        out.quoted_input==1'000'000 && out.quoted_output==9'191'912 && out.quoted_fee==2'000,
        "nested payload unescapes once preserves newline and matches independent digest");
    std::fill(document.begin(),document.end(),'x');
    require(out.payload_view()==payload,"decoded envelope owns bytes after HTTP buffer reuse");
    const auto unicode=replace(fixture(),"{\\\"activity","\\u007b\\\"activity");
    require(decode_swap_rest_prepare(201,unicode,{Side::buy,1'000'000},out)==SwapRestCode::decoded_unsigned_only && out.payload_view()==payload,
        "equivalent outer JSON escape preserves original signed UTF8 bytes");
}
void quote_and_payload_refusals(){
    constexpr const char* failures[]{"non201 HTTP status refuses with empty output", "noninitialized lifecycle refuses with empty output",
        "payload hash mismatch refuses with empty output", "quoted input mismatch refuses with empty output",
        "quote warning refuses with empty output", "outer warning refuses with empty output", "wrong fee mint refuses with empty output"};
    for(unsigned mode=0;mode<7;++mode){
        auto text=fixture();auto http=201U;auto expected=SwapRestCode::unsupported;
        if(mode==0){http=200;expected=SwapRestCode::invalid_http;}
        if(mode==1)text=replace(text,"initialized","complete");
        if(mode==2){text=replace(text,"3a02efff","4a02efff");expected=SwapRestCode::payload_digest_mismatch;}
        if(mode==3){text=replace(text,"\"qty\":\"1\"","\"qty\":\"2\"");expected=SwapRestCode::quote_mismatch;}
        if(mode==4){text=replace(text,"\"quote\":{","\"quote\":{\"issues\":[\"minimum\"],");expected=SwapRestCode::issue;}
        if(mode==5){text=replace(text,"\"quote\":{","\"issues\":[\"failure\"],\"quote\":{");expected=SwapRestCode::issue;}
        if(mode==6){text=replace(text,"\"fee_asset\":\"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v\"","\"fee_asset\":\"other\"");expected=SwapRestCode::quote_mismatch;}
        SwapRestPrepared out;out.payload[0]='x';out.payload_size=1;
        require(decode_swap_rest_prepare(http,text,{Side::buy,1'000'000},out)==expected && out.payload_size==0 && out.handle.empty(),
            failures[mode]);
    }
}
void unsafe_handle_and_malformed_json(){
    for(const auto handle:{"../other","other/execute","other?x=1","","order%2fother"}){
        SwapRestPrepared out;require(decode_swap_rest_prepare(201,replace(fixture(),"order-1",handle),{Side::buy,1'000'000},out)==SwapRestCode::unsafe_handle,
            "unsafe or missing handles cannot select another REST route");
    }
    for(auto text:{fixture()+"{}",replace(fixture(),"\"status\":","\"status\":\"initialized\",\"status\":"),
        replace(fixture(),"synthetic","synthetic\\u0000")}){
        SwapRestPrepared out;const auto code=decode_swap_rest_prepare(201,text,{Side::buy,1'000'000},out);
        require(code==SwapRestCode::malformed && out.payload_size==0,"ambiguous JSON and NUL payload refuse before usable result");
    }
}
void nested_payload_ambiguity(){
    auto duplicate=replace(fixture(),"\"digest\":", "\"digest\":\"3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256\",\"digest\":");
    SwapRestPrepared out;
    require(decode_swap_rest_prepare(201,duplicate,{Side::buy,1'000'000},out)==SwapRestCode::malformed && out.payload_size==0,
        "duplicate nested payload digest refuses before usable bytes");
    duplicate=replace(fixture(),"\"payload\":", "\"payload\":\"{\\\"activity\\\":\\\"synthetic\\\"}\\n\",\"payload\":");
    require(decode_swap_rest_prepare(201,duplicate,{Side::buy,1'000'000},out)==SwapRestCode::malformed,
        "duplicate nested payload text refuses");
    duplicate=replace(fixture(),"\"digest\":", "\"\\u0064igest\":\"ignored\",\"digest\":");
    require(decode_swap_rest_prepare(201,duplicate,{Side::buy,1'000'000},out)==SwapRestCode::malformed,
        "escaped nested payload key refuses");
}
void asset_and_amount_bounds(){
    SwapRestPrepared out;
    auto text=replace(fixture(),"So11111111111111111111111111111111111111112","other");
    require(decode_swap_rest_prepare(201,text,{Side::buy,1'000'000},out)==SwapRestCode::quote_mismatch,"wrong base mint refused");
    text=replace(fixture(),"\"quote_asset\":\"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v\"","\"quote_asset\":\"other\"");
    require(decode_swap_rest_prepare(201,text,{Side::buy,1'000'000},out)==SwapRestCode::quote_mismatch,"wrong quote mint refused");
    text=replace(fixture(),"\"qty_out\":\"0.009191912\"","\"qty_out\":\"0\"");
    require(decode_swap_rest_prepare(201,text,{Side::buy,1'000'000},out)==SwapRestCode::quote_mismatch,"zero output refused");
    for(auto bad:{"-1","1e0","1.0000001","9223372036854.775808"}){
        text=replace(fixture(),"\"qty\":\"1\"",std::string("\"qty\":\"")+bad+"\"");
        require(decode_swap_rest_prepare(201,text,{Side::buy,1'000'000},out)==SwapRestCode::quote_mismatch,"nonexact or overflowing quote decimal refused");
    }
    text=replace(fixture(),"\"qty\":\"1\"","\"qty\":\"0.0084\"");
    text=replace(text,"\"qty_out\":\"0.009191912\"","\"qty_out\":\"1.006989\"");
    require(decode_swap_rest_prepare(201,text,{Side::sell,8'400'000},out)==SwapRestCode::decoded_unsigned_only &&
        out.quoted_input==8'400'000 && out.quoted_output==1'006'989 && out.quoted_fee==2'000,"sell quote uses SOL input and USDC output units");
}
void payload_cardinality_and_capacity(){
    SwapRestPrepared out;
    const auto text=fixture();const auto begin=text.find("[{"),end=text.find("}],\"quote\"")+2;
    require(begin!=std::string::npos && end>begin,"payload fixture array boundaries");
    auto no_payload=text;no_payload.replace(begin,end-begin,"[]");
    require(decode_swap_rest_prepare(201,no_payload,{Side::buy,1'000'000},out)==SwapRestCode::unsupported,"empty payload collection refused");
    const auto element=text.substr(begin+1,end-begin-2);
    auto doubled=text;doubled.replace(begin,end-begin,"["+element+","+element+"]");
    require(decode_swap_rest_prepare(201,doubled,{Side::buy,1'000'000},out)==SwapRestCode::unsupported,"multiple signing payloads refused");
    auto exact=replace(text,R"("payload":"{\"activity\":\"synthetic\"}\n")",std::string("\"payload\":\"")+std::string(8192,'a')+"\"");
    exact=replace(exact,payload_hash,"dd4e6730520932767ec0a9e33fe19c4ce24399d6eba4ff62f13013c9ed30ef87");
    require(decode_swap_rest_prepare(201,exact,{Side::buy,1'000'000},out)==SwapRestCode::decoded_unsigned_only &&
        out.payload_size==8192 && out.payload_view()==std::string(8192,'a'),"exact payload capacity is retained without truncation");
    auto oversized=replace(exact,std::string(8192,'a'),std::string(8193,'a'));
    require(decode_swap_rest_prepare(201,oversized,{Side::buy,1'000'000},out)==SwapRestCode::malformed && out.payload_size==0,"one byte beyond payload capacity refused");
}
void exact_execute_and_stamp_gate(){
    auto valid=stamp();SwapRestRequest out;
    const auto expected=std::string("{\"auth_type\":\"api_key\",\"signatures\":[\"")+std::string(valid.view())+"\"]}";
    require(encode_swap_rest_execute(*tm_text("order-1"),valid,out)==SwapRestCode::encoded &&
        out.path_view()=="/v1/gateway/orders/order-1/execute" && out.body_view()==expected,
        "REST execute carries one full API key stamp without MCP wrapping");
    const std::string big_der="3046022100ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550022100ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550";
    std::array<std::byte,72> signature{};std::array<std::byte,33> public_key{};public_key[0]=std::byte{2};
    for(std::size_t i=0;i<signature.size();++i)signature[i]=static_cast<std::byte>(std::stoul(big_der.substr(i*2,2),nullptr,16));
    swap_signing::ApiKeyStamp maximum;
    require(swap_signing::encode_api_key_stamp(public_key,signature,maximum)==swap_signing::StampCode::encoded && maximum.view().size()==375,"maximum synthetic stamp fixture");
    const auto maximum_body=std::string("{\"auth_type\":\"api_key\",\"signatures\":[\"")+std::string(maximum.view())+"\"]}";
    require(encode_swap_rest_execute(*tm_text(std::string(96,'a')),maximum,out)==SwapRestCode::encoded && out.body_view()==maximum_body &&
        out.path_view()==std::string("/v1/gateway/orders/")+std::string(96,'a')+"/execute","maximum stamp and handle preserved without truncation");
    swap_signing::ApiKeyStamp empty;
    require(encode_swap_rest_execute(*tm_text("order-1"),empty,out)==SwapRestCode::invalid_input && out.body_size==0,
        "empty stamp cannot become execute body");
    TmText malformed;malformed.length=255;
    require(encode_swap_rest_execute(malformed,valid,out)==SwapRestCode::unsafe_handle && out.body_size==0,
        "malformed bounded handle length refuses before view construction");
}
}
int main(int argc,char** argv){
    if(argc==3 && std::string_view(argv[1])=="--private-fixture"){
        std::ifstream in(argv[2],std::ios::binary);std::string bytes(kMaxTmResponseBytes+1,'\0');in.read(bytes.data(),bytes.size());bytes.resize(static_cast<std::size_t>(in.gcount()));
        SwapRestPrepared out;const auto code=decode_swap_rest_prepare(201,bytes,{Side::buy,1'000'000},out);
        std::cout<<"decoded_unsigned_only="<<(code==SwapRestCode::decoded_unsigned_only)<<" payload_size="<<out.payload_size<<" hash_verified="<<(!out.payload_digest.is_zero())<<'\n';
        return code==SwapRestCode::decoded_unsigned_only?0:1;
    }
    const std::pair<const char*,void(*)()> cases[]{{"exact_create_requests",exact_create_requests},{"create_refusals_clear_output",create_refusals_clear_output},
        {"preserve_original_payload",preserve_original_payload},{"quote_and_payload_refusals",quote_and_payload_refusals},
        {"unsafe_handle_and_malformed_json",unsafe_handle_and_malformed_json},{"exact_execute_and_stamp_gate",exact_execute_and_stamp_gate},
        {"nested_payload_ambiguity",nested_payload_ambiguity},{"asset_and_amount_bounds",asset_and_amount_bounds},
        {"payload_cardinality_and_capacity",payload_cardinality_and_capacity}};
    unsigned ran{},failed{};for(const auto&[name,test]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
