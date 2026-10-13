#include "velocita/v2/swap_signing_stamp.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace velocita::v2::swap_signing;
namespace {
void require(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> out;
    for(std::size_t i=0;i<text.size();i+=2) out.push_back(static_cast<std::byte>(std::stoul(std::string(text.substr(i,2)),nullptr,16)));
    return out;
}
const auto key=hex("036b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296");
const auto tiny=hex("3006020101020102");
void oracle() {
    ApiKeyStamp out;
    require(encode_api_key_stamp(key,tiny,out)==StampCode::encoded,"valid_der_encodes");
    require(out.view()=="eyJwdWJsaWNLZXkiOiIwMzZiMTdkMWYyZTEyYzQyNDdmOGJjZTZlNTYzYTQ0MGYyNzcwMzdkODEyZGViMzNhMGY0YTEzOTQ1ZDg5OGMyOTYiLCJzY2hlbWUiOiJTSUdOQVRVUkVfU0NIRU1FX1RLX0FQSV9QMjU2Iiwic2lnbmF0dXJlIjoiMzAwNjAyMDEwMTAyMDEwMiJ9", "independent_stamp_bytes");
    auto maximum=hex("3046022100ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550022100ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550");
    require(encode_api_key_stamp(key,maximum,out)==StampCode::encoded && out.view().size()==375,"maximum_der_fits_375");
    require(out.view().find('=')==std::string_view::npos,"stamp_has_no_base64_padding");
    auto short_pair=hex("300702020080020101");
    require(encode_api_key_stamp(key,short_pair,out)==StampCode::encoded && out.view().size()==207,"one_byte_base64_tail");
    short_pair=hex("30080202008002020080");
    require(encode_api_key_stamp(key,short_pair,out)==StampCode::encoded && out.view().size()==210,"two_byte_base64_tail");
}
void public_key() {
    ApiKeyStamp out;
    auto bad=key;bad[0]=std::byte{4};
    require(encode_api_key_stamp(bad,tiny,out)==StampCode::invalid_public_key_encoding && out.empty(),"uncompressed_prefix_refused");
    bad=key;bad.pop_back();
    require(encode_api_key_stamp(bad,tiny,out)==StampCode::invalid_public_key_encoding,"short_public_key_refused");
    bad=key;bad.push_back(std::byte{});
    require(encode_api_key_stamp(bad,tiny,out)==StampCode::invalid_public_key_encoding,"long_public_key_refused");
}
void malformed() {
    ApiKeyStamp out;
    for(auto text:{"", "3000", "3007020101020102", "3106020101020102", "300602010102010200", "308106020101020102", "3006030101020102"})
        require(encode_api_key_stamp(key,hex(text),out)==StampCode::invalid_der_signature && out.empty(),"bad_der_container_refused");
    require(encode_api_key_stamp(key,hex("3006020180020102"),out)==StampCode::invalid_der_signature,"negative_scalar_refused");
    require(encode_api_key_stamp(key,hex("300702020001020102"),out)==StampCode::invalid_der_signature,"redundant_scalar_zero_refused");
    require(encode_api_key_stamp(key,hex("3006020100020102"),out)==StampCode::invalid_der_signature,"zero_scalar_refused");
    require(encode_api_key_stamp(key,hex("3006020101020100"),out)==StampCode::invalid_der_signature,"zero_second_scalar_refused");
    require(encode_api_key_stamp(key,hex("3026022100ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551020101"),out)==StampCode::invalid_der_signature,"curve_order_scalar_refused");
    require(encode_api_key_stamp(key,hex("30260221010000000000000000000000000000000000000000000000000000000000000000020101"),out)==StampCode::invalid_der_signature,"oversized_scalar_refused");
    require(encode_api_key_stamp(key,std::vector<std::byte>(64,std::byte{1}),out)==StampCode::invalid_der_signature,"raw_p1363_refused");
}
void clearing() {
    ApiKeyStamp out;
    require(encode_api_key_stamp(key,tiny,out)==StampCode::encoded && !out.empty(),"setup_stamp_before_refusal");
    require(encode_api_key_stamp(key,{},out)==StampCode::invalid_der_signature && out.empty(),"refusal_clears_previous_stamp");
}
}
int main(int argc,char**argv) {
    int failures{},ran{};
    for(auto [name,fn]:std::array<std::pair<const char*,void(*)()>,4>{{{"oracle",oracle},{"public_key",public_key},{"malformed",malformed},{"clearing",clearing}}}) {
        if(argc>1 && std::string_view(argv[1])!=name)continue;
        ++ran;try {fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    return !ran?2:failures?1:0;
}
