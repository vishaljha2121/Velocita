#include "velocita/v2/swap_order_status.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
Digest256 hash(unsigned n){Digest256 d;d.bytes.fill(static_cast<std::byte>(n));return d;}
struct Clock final:SwapOrderReadClock {
    std::uint64_t now_ms()noexcept override{return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();}
};
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    const std::filesystem::path root=argv[3];
    const SwapPrepareBinding binding{{hash(1),hash(10),hash(11),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},hash(2),hash(12)};
    const SwapOrderSessionIdentity http_identity{hash(3),hash(4)};
    SwapPrepareStore preparation(root/"prepare",binding);
    SwapSubmissionStore submission(root/"submit",{hash(1),hash(2)});
    constexpr std::string_view payload="SYNTHETIC STATUS FIXTURE\n";
    SwapRestPrepared p;p.handle=*tm_text("abc");std::copy(payload.begin(),payload.end(),p.payload.begin());p.payload_size=payload.size();
    p.payload_digest=sha256_digest(std::as_bytes(std::span(payload.data(),payload.size())));
    p.quoted_input=1'000'000;p.quoted_output=10'000'000;p.quoted_fee=2'000;
    SwapRestRequest request;
    if(encode_swap_rest_create({Side::buy,1'000'000},request)!=SwapRestCode::encoded ||
        preparation.create({binding,1,hash(13),EffectId{0}})!=SwapPrepareStoreCode::none ||
        preparation.begin({EffectId{1},hash(14),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1'000'000}},2,hash(15))!=SwapPrepareStoreCode::none ||
        preparation.complete(p,hash(16))!=SwapPrepareStoreCode::none)return 3;
    SwapSubmissionIdentity identity;identity.effect=EffectId{1};identity.payload=p.payload_digest;identity.execution_request=hash(6);
    identity.handle_size=3;identity.handle[0]=std::byte{'a'};identity.handle[1]=std::byte{'b'};identity.handle[2]=std::byte{'c'};
    identity.handle_digest=sha256_digest(std::span(identity.handle).first(3));
    if(submission.create({{hash(1),hash(2)},1,hash(7),EffectId{0}})!=SwapSubmissionCode::none ||
        submission.begin(identity,3)!=SwapSubmissionCode::none)return 4;
    auto port=SwapHttpsOrderGet::local_fixture(http_identity,"fixture-token",p.handle,argv[1],argv[2]);
    if(!port)return 5;
    if(std::string_view(argv[4])=="wrong-path"){
        const auto response=port->get("/v1/gateway/orders/other");
        std::cout<<"local_refusal="<<(response.status==transport::SwapHttpsStatus::local_refusal)<<'\n';return 0;
    }
    Clock clock;SwapOrderStatusReader reader(submission,preparation,*port,clock,{binding,http_identity,3000});
    const auto first=reader.read().status;
    (void)reader.read();
    std::cout<<"terminal="<<(first==SwapOrderReadStatus::terminal_needs_finality)
        <<" retained="<<(submission.latest().transaction!=SwapSubmissionTransaction{})
        <<" unresolved="<<(submission.latest().phase==SwapSubmissionPhase::execute_intent)<<'\n';
    return 0;
}
