#include "velocita/v2/swap_order_status.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::truemarkets;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Digest256 hash(unsigned n){Digest256 d;d.bytes.fill(static_cast<std::byte>(n));return d;}
constexpr std::string_view signature="1GMkH3brNXiNNs1tiFZHu4yZSRrzJwxi5wB9bHFtMinfCXNnR1adh8Vo8NTheK4evneedH4qmvjeqcBBNAefgS";
std::string fixture(){return std::string(R"({"order_id":"abc","asset_id":"495e07ac-fa3e-4179-85b7-b1e8cece3dc4","base_asset":"SOL","quote_asset":"USDC","asset_type":"spot","venue":"defi","status":"complete","type":"market","side":"buy","qty_unit":"quote","qty":"1","executed_qty":"0.01","leaves_qty":"0","executed_vwap":"100","fee":"0.002","tx_hash":")")+std::string(signature)+"\"}";}
std::string changed(std::string input,std::string_view from,std::string_view to){auto at=input.find(from);require(at!=input.npos,"fixture target exists");input.replace(at,from.size(),to);return input;}
struct Clock final:SwapOrderReadClock {std::uint64_t tick{100};std::uint64_t now_ms()noexcept override{return tick;}};
struct Port final:SwapOrderGet {
    SwapOrderSessionIdentity id{hash(3),hash(4)};unsigned calls{};std::string path;
    std::string response{fixture()};unsigned status{200};bool oversized{},throws{};
    transport::SwapHttpsExposure exposure{transport::SwapHttpsExposure::response_received};
    transport::SwapHttpsStatus transport_status{transport::SwapHttpsStatus::complete};
    std::function<void()> during;
    SwapOrderSessionIdentity identity()const noexcept override{return id;}
    transport::SwapHttpsResult get(std::string_view p)override{
        ++calls;path=p;if(during)during();if(throws)throw std::runtime_error("fixture read failed");
        transport::SwapHttpsResult out;out.status=transport_status;out.exposure=exposure;
        out.http_status=status;out.body_size=oversized?out.body.size()+1:response.size();
        std::copy(response.begin(),response.end(),out.body.begin());return out;
    }
};
struct Scratch {
    std::filesystem::path root;
    Scratch(){auto base=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/order-status-tests";
        std::filesystem::create_directories(base);auto p=(base/"case-XXXXXX").string();require(mkdtemp(p.data()),"scratch created");root=p;}
    ~Scratch(){std::error_code e;std::filesystem::remove_all(root,e);}
};
struct Rig {
    Scratch scratch;Clock clock;Port port;bool fail_sync{};
    SwapSubmissionStore store{scratch.root/"store",{hash(1),hash(2)},
        {+[](SwapSubmissionStage s,void* p)noexcept{return !(s==SwapSubmissionStage::temporary_sync && *static_cast<bool*>(p));},&fail_sync}};
    SwapPrepareBinding preparation_binding{{hash(1),hash(10),hash(11),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},hash(2),hash(12)};
    SwapPrepareStore preparation{scratch.root/"prepare",preparation_binding};
    SwapOrderStatusConfig config{preparation_binding,{hash(3),hash(4)},20};
    SwapSubmissionIdentity identity;
    explicit Rig(unsigned mismatch=0){
        constexpr std::string_view bytes="{\"activity\":\"synthetic\"}\n";
        SwapRestPrepared p;p.handle=*tm_text("abc");std::copy(bytes.begin(),bytes.end(),p.payload.begin());p.payload_size=bytes.size();
        p.payload_digest=sha256_digest(std::as_bytes(std::span(bytes.data(),bytes.size())));p.quoted_input=1'000'000;p.quoted_output=10'000'000;p.quoted_fee=2'000;
        SwapRestRequest request;require(encode_swap_rest_create({Side::buy,1'000'000},request)==SwapRestCode::encoded,"fixture request encoded");
        require(preparation.create({preparation_binding,1,hash(13),EffectId{0}})==SwapPrepareStoreCode::none,"fixture preparation created");
        require(preparation.begin({EffectId{1},hash(14),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1'000'000}},2,hash(15))==SwapPrepareStoreCode::none,"fixture preparation begun");
        require(preparation.complete(p,hash(16))==SwapPrepareStoreCode::none,"fixture payload retained before execute");
        identity.effect=EffectId{1};identity.payload=p.payload_digest;identity.execution_request=hash(6);
        identity.handle_size=3;identity.handle[0]=std::byte{'a'};identity.handle[1]=std::byte{'b'};identity.handle[2]=std::byte{'c'};
        identity.handle_digest=*parse_digest256("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        require(store.create({{hash(1),hash(2)},1,hash(7),EffectId{0}})==SwapSubmissionCode::none,"journal created");
        if(mismatch==1)identity.payload=hash(99);
        if(mismatch==2)identity.effect=EffectId{2};
        if(mismatch==3){identity.handle[2]=std::byte{'d'};identity.handle_digest=sha256_digest(std::span(identity.handle).first(3));}
        require(store.begin(identity,mismatch==4?2:3)==SwapSubmissionCode::none,"execute intent retained");}
};
void signature_oracle(){
    const auto decoded=decode_solana_signature(signature);require(decoded.has_value(),"independent base58 oracle decodes");
    for(std::size_t n=0;n<64;++n)require((*decoded)[n]==static_cast<std::byte>(n),"all raw signature bytes match independent oracle");
    for(const auto& bad:{std::string{},std::string(64,'1'),std::string("1")+std::string(signature),std::string(signature.substr(1)),std::string("0")+std::string(signature.substr(1)),std::string(88,'z')})
        require(!decode_solana_signature(bad),"noncanonical zero invalid digit overflow or wrong length signature refuses");
}
void exact_status_and_persistence(){
    Rig r;SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);const auto& out=reader.read();
    require(out.status==SwapOrderReadStatus::terminal_needs_finality && out.detail.executed_base_atoms==10'000'000 &&
        out.detail.fee_quote_atoms==2'000 && !out.response_digest.is_zero(),"matched terminal remains observational pending chain finality");
    require(r.port.calls==1 && r.port.path=="/v1/gateway/orders/abc","one exact order GET route used");
    require(r.store.latest().transaction==*decode_solana_signature(signature) && r.store.latest().phase==SwapSubmissionPhase::execute_intent,
        "transaction identity persists while execution stays unresolved");
    (void)reader.read();require(r.port.calls==1,"same reader never automatically retries");
}
void status_refusals(){
    for(unsigned mode=0;mode<11;++mode){Rig r;
        auto expected=SwapOrderReadStatus::identity_conflict;
        if(mode==0)r.port.response=changed(fixture(),"\"abc\"","\"other\"");
        if(mode==1)r.port.response=changed(fixture(),"\"qty\":\"1\"","\"qty\":\"2\"");
        if(mode==2)r.port.response=changed(fixture(),"\"buy\"","\"sell\"");
        if(mode==3){r.port.status=404;expected=SwapOrderReadStatus::http_refused;}
        if(mode==4){r.port.oversized=true;expected=SwapOrderReadStatus::http_refused;}
        if(mode==5){r.port.throws=true;expected=SwapOrderReadStatus::http_refused;}
        if(mode==6){r.port.transport_status=transport::SwapHttpsStatus::tls_failure;expected=SwapOrderReadStatus::http_refused;}
        if(mode==7)r.port.response=changed(fixture(),signature,"invalid0");
        if(mode==8)r.port.response=changed(fixture(),"\"SOL\"","\"BTC\"");
        if(mode==9){r.port.exposure=transport::SwapHttpsExposure::possibly_sent;expected=SwapOrderReadStatus::http_refused;}
        if(mode==10){r.port.response=fixture()+std::string(kMaxTmResponseBytes,' ');expected=SwapOrderReadStatus::http_refused;}
        SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);const auto& out=reader.read();
        require(out.status==expected && out.detail.order_id.empty() && r.store.latest().transaction==SwapSubmissionTransaction{},
            "wrong request identity HTTP transport or transaction cannot publish evidence");
    }
}
void identity_time_and_journal_fences(){
    for(unsigned mode=0;mode<7;++mode){Rig r;
        if(mode==0)r.port.id.authenticated_session=hash(99);
        if(mode==1)r.config.binding.authority_domain=hash(99);
        if(mode==2)r.port.during=[&]{r.clock.tick=120;};
        if(mode==3)r.port.during=[&]{r.clock.tick=99;};
        if(mode==4)r.port.during=[&]{r.port.id.account_and_assets=hash(99);};
        if(mode==5)r.port.during=[&]{SwapSubmissionTransaction tx{};tx[0]=std::byte{9};require(r.store.observe_transaction(r.identity,tx)==SwapSubmissionCode::none,"fixture journal changes during HTTP");};
        if(mode==6)r.clock.tick=std::numeric_limits<std::uint64_t>::max()-5;
        SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);const auto& out=reader.read();
        require(out.status==SwapOrderReadStatus::refused && out.detail.order_id.empty(),"session binding journal or exact timeout fence refuses stale HTTP evidence");
        if(mode<2 || mode==6)require(r.port.calls==0,"invalid initial scope refuses before GET");
    }
}
void preparation_changes_during_get(){
    for(unsigned n=0;n<2;++n){Rig r;
        r.port.during=[&]{
            const auto previous=r.preparation.latest();
            if(n==0){
                auto next=previous.operation;next.effect=EffectId{2};
                CallerVerifiedPreparePredecessor prior;
                prior.committed_index=4;prior.state_digest=hash(20);
                prior.settlement.effect=previous.operation.effect;prior.settlement.payload=previous.prepared.payload_digest;
                prior.settlement.custody=previous.binding.allocation.custody;prior.settlement.finalized=true;
                prior.settlement.transaction=hash(21);prior.settlement.evidence=hash(22);
                require(r.preparation.begin(next,4,hash(20),&prior)==SwapPrepareStoreCode::none,"synthetic new preparation replaces retained record during GET");
            }else require(r.preparation.complete(previous.prepared,hash(88))==SwapPrepareStoreCode::bad_transition,"synthetic prepare store becomes unavailable during GET");
        };
        SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);const auto& out=reader.read();
        require(out.status==SwapOrderReadStatus::refused && out.detail.order_id.empty() && out.response_digest.is_zero() &&
            r.store.latest().transaction==SwapSubmissionTransaction{},"changed or failed prepare journal cannot publish old order evidence");
        (void)reader.read();require(r.port.calls==1,"refusal never triggers another GET");
    }
}
void retained_prepare_binding(){
    for(unsigned mode=1;mode<=4;++mode){Rig r(mode);SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);
        require(reader.read().status==SwapOrderReadStatus::refused && r.port.calls==0,"retained prepare effect payload handle and causal index must match execute journal");}
    Rig r;r.config.binding.allocation.risk_policy_version=RiskPolicyVersion{99};SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);
    require(reader.read().status==SwapOrderReadStatus::refused && r.port.calls==0,"full preparation allocation binding is required before GET");
}
void known_signature_conflict_and_storage_failure(){
    {Rig r;SwapSubmissionTransaction old{};old[0]=std::byte{9};require(r.store.observe_transaction(r.identity,old)==SwapSubmissionCode::none,"known transaction fixture");
    SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);
    require(reader.read().status==SwapOrderReadStatus::identity_conflict && r.store.latest().transaction==old,"different authenticated transaction cannot overwrite retained signature");}
    {Rig r;r.port.during=[&]{r.fail_sync=true;};SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);
    require(reader.read().status==SwapOrderReadStatus::storage_failed && r.store.latest().transaction==SwapSubmissionTransaction{},"failed durable signature write publishes no usable status evidence");}
    {Rig r;r.port.response=changed(fixture(),"\"complete\"","\"pending\"");SwapOrderStatusReader reader(r.store,r.preparation,r.port,r.clock,r.config);
    require(reader.read().status==SwapOrderReadStatus::observed_working && r.store.latest().phase==SwapSubmissionPhase::execute_intent,"working order never releases uncertain execution");}
}
}
int main(int argc,char** argv){
    const std::pair<const char*,void(*)()> cases[]{{"signature",signature_oracle},{"exact",exact_status_and_persistence},{"refusals",status_refusals},
        {"fences",identity_time_and_journal_fences},{"conflict",known_signature_conflict_and_storage_failure},{"prepare",retained_prepare_binding},{"prepare_changes",preparation_changes_during_get}};
    unsigned ran{},failed{};for(auto[name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
