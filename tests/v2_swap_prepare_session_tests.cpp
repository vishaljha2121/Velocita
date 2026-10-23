#include "velocita/v2/swap_prepare_session.hpp"
#include "v2_swap_test_support.hpp"
#ifdef VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY
#include "velocita/v2/swap_prepare_https.hpp"
#endif
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace velocita::v2;
void require(bool yes, const char* name) { if (!yes) throw std::runtime_error(name); }
Digest256 digest(unsigned n) { return swap_test::digest(n); }
const std::array<std::byte,16> cluster{std::byte{1}}, process{std::byte{2}};
AuthKey key(AuthRole role, int sender, int recipient) {
    std::array<std::byte,32> bytes{}; bytes.fill(std::byte{42});
    auto secret = AuthSecret::from_bytes(bytes);
    return std::move(*AuthKey::derive(*secret,{role,sender,recipient},cluster));
}
std::vector<ClientRoute> routes() {
    std::vector<AuthKey> responses; responses.push_back(key(AuthRole::response,1,22));
    std::vector<ClientRoute> result;
    result.push_back({1,key(AuthRole::ingress,22,1),std::move(responses)}); return result;
}
struct Voter final : ClientTransport {
    TradingState state{catalog_test::state(shadow_catalog::truemarkets_two_row_catalog())};
    std::deque<std::vector<std::byte>> replies;
    Digest256 domain{digest(30)};
    std::uint64_t sequence{};
    std::int64_t index{10};
    bool bad_mac{}, bad_snapshot{}, silent{}, wrong_kind{};
    unsigned requests{};
    Voter() {
        state.kill_epoch = KillEpoch{1}; state.executor_generation = ExecutorGeneration{1};
        ApplyScratch scratch;
        require(apply(state,swap_test::command(9,1,swap_test::opening()),scratch,
                      &shadow_catalog::truemarkets_two_row_catalog()).code == ApplyCode::applied,"fixture funding committed");
    }
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        require(node==1,"request sent to configured voter"); ++requests;
        if(silent) return true;
        auto ingress=key(AuthRole::ingress,22,1); VerifiedAuthEnvelope verified;
        require(verify_auth_envelope(ingress,envelope,verified)==AuthEnvelopeError::none,"actual state request authenticated");
        GatewayRequest request;
        require(decode_gateway_request(verified.body,request)==NodeProtocolError::none &&
                request.kind==GatewayRequestKind::state_read,"coordinator issues actual state read only");
        const auto correlation=gateway_correlation(verified.body);
        std::array<std::byte,kMaxStateSnapshotBytes> snapshot{};
        auto encoded=encode_state_snapshot(state,snapshot);
        require(encoded.error==StateSnapshotError::none,"fixture full snapshot encodes");
        const auto snapshot_bytes=std::span(snapshot).first(encoded.bytes_written);
        const auto count=(encoded.bytes_written+kStateChunkPayloadBytes-1)/kStateChunkPayloadBytes;
        for(std::size_t n=0;n<count;++n) {
            std::array<std::byte,kMaxStateChunkResponseBytes> body{};
            StateChunkHeader h;
            h.correlation=correlation;h.responder_id=1;h.leader_id=1;h.term=1;h.applied_index=index;
            h.authority_domain=domain;h.snapshot_bytes=static_cast<std::uint32_t>(snapshot_bytes.size());
            h.snapshot_digest=bad_snapshot?digest(99):sha256_digest(snapshot_bytes);
            h.chunk_index=static_cast<std::uint16_t>(n);h.chunk_count=static_cast<std::uint16_t>(count);
            const auto offset=n*kStateChunkPayloadBytes;
            auto framed=encode_state_chunk(h,snapshot_bytes.subspan(offset,std::min(kStateChunkPayloadBytes,snapshot_bytes.size()-offset)),body);
            require(framed.error==NodeProtocolError::none,"fixture state chunk encodes");
            if(wrong_kind) body[0]=static_cast<std::byte>(ResponseKind::committed);
            std::vector<std::byte> sealed(kMaxAuthEnvelopeBytes);
            auto response=key(AuthRole::response,1,22);
            const auto auth=encode_auth_envelope(response,1,++sequence,std::span(body).first(framed.bytes_written),sealed);
            require(auth.error==AuthEnvelopeError::none,"fixture state chunk authenticated");
            sealed.resize(auth.bytes_written);if(bad_mac) sealed.back()^=std::byte{1};
            replies.push_back(std::move(sealed));
        }
        return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> out) override {
        if(replies.empty()) return std::nullopt;
        const auto n=replies.front().size(); require(n<=out.size(),"response capacity");
        std::copy(replies.front().begin(),replies.front().end(),out.begin()); replies.pop_front(); return n;
    }
};

std::string reply(){return R"({"order_id":"order-1","status":"initialized","payloads":[{"digest":"3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256","payload":"{\"activity\":\"synthetic\"}\n"}],"quote":{"base_asset":"So11111111111111111111111111111111111111112","quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","qty":"1","qty_out":"0.009191912","fee":"0.002"}})";}
struct Scratch {
    std::filesystem::path base;
    Scratch() {
        const auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/prepare-session-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture scratch created");base=name;
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(base,ignored);}
    auto path() const {return base/"store";}
};
struct Port final:SwapPreparePort {
    unsigned calls{};std::string body,path,document{reply()};unsigned http{201};bool throw_after{};
    transport::SwapHttpsStatus status{transport::SwapHttpsStatus::complete};
    transport::SwapHttpsExposure exposure{transport::SwapHttpsExposure::response_received};
    std::function<void()> during;SwapPrepareRead read{};bool refuse_route{};swap::Pending candidate{};
    bool admit(const swap::Pending& c,std::string_view payload)override{candidate=c;return !refuse_route && !payload.empty();}
    transport::SwapHttpsResult create(const truemarkets::SwapRestRequest& request,const SwapPrepareRead& committed)override{
        ++calls;body=request.body_view();path=request.path_view();read=committed;if(during)during();
        if(throw_after)throw std::runtime_error("synthetic lost create response");
        transport::SwapHttpsResult r;r.status=status;r.exposure=exposure;r.http_status=http;r.body_size=document.size();
        std::copy(document.begin(),document.end(),r.body.begin());return r;
    }
};
struct Fault{bool armed{};static bool proceed(SwapPrepareStoreStage stage,void* p)noexcept{return !(static_cast<Fault*>(p)->armed && stage==SwapPrepareStoreStage::temporary_sync);}};
SwapPrepareSessionConfig config(){return {{swap_test::opening().binding,digest(30),digest(80)},SwapPrepareInputMapping::fixture_principal_and_base_input,20};}
struct Rig {
    Scratch scratch;Voter voter;GatewayLink link{1,process,routes(),voter};Fault fault;Port port;
    std::unique_ptr<SwapPrepareStore> store;
    std::unique_ptr<SwapPrepareSession> session;
    std::uint64_t sequence{3};
    Rig(){
        store=std::make_unique<SwapPrepareStore>(scratch.path(),config().binding,SwapPrepareStoreFaultHook{Fault::proceed,&fault});
        require(store->create({config().binding,1,digest(51),EffectId{0}})==SwapPrepareStoreCode::none,"fixture prepare journal initialized");
        ApplyScratch work;require(apply(voter.state,{{CallerId{9},RequestSequence{2},digest(99)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}},
            work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture mode enabled");
        require(apply(voter.state,swap_test::command(9,sequence++,swap_test::buy(swap_test::opening())),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture exact intent reserved");
        session=std::make_unique<SwapPrepareSession>(link,*store,port,config());
    }
    bool start(std::uint64_t effect=1,std::int64_t index=10){return session->start(EffectId{effect},index,1000,104);}
    auto finish(){(void)session->poll(1000,106);return session->poll(1001,107);}
    void restart(){session.reset();store.reset();store=std::make_unique<SwapPrepareStore>(scratch.path(),config().binding,SwapPrepareStoreFaultHook{Fault::proceed,&fault});
        require(store->recover()==SwapPrepareStoreCode::none,"fixture prepare store recovers");session=std::make_unique<SwapPrepareSession>(link,*store,port,config());}
    void settle_and_reserve_next(bool mismatch=false){
        require(session->prepared()!=nullptr,"fixture retained bytes before kernel Prepare");
        const auto& p=*session->prepared();const auto handle=sha256_digest(std::as_bytes(std::span(p.handle.bytes.data(),p.handle.length)));
        ApplyScratch work;
        for(const auto& c:{swap::Command{swap::Prepare{EffectId{1},p.payload_digest,handle,1}},swap::Command{swap::Attempt{EffectId{1},p.payload_digest}}})
            require(apply(voter.state,swap_test::command(22,sequence++,c,103),work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture original prepared and attempted");
        swap::Settlement x;x.effect=EffectId{1};x.custody=config().binding.allocation.custody;x.payload=p.payload_digest;x.transaction=digest(70);x.evidence=digest(71);
        x.finalized=true;x.outcome=swap::Outcome::succeeded;x.base_credit=swap::BaseAtoms{10'000'000};x.quote_debit=swap::QuoteAtoms{1'002'000};x.fee=swap::QuoteAtoms{2'000};
        require(apply(voter.state,swap_test::command(22,sequence++,x,104),work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture prior settlement committed");
        if(mismatch)voter.state.spot_swap.settlements[0].payload=digest(99);
        require(apply(voter.state,swap_test::command(9,sequence++,swap_test::buy(swap_test::opening(),2),105),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture next intent reserved");voter.index=20;
    }
};
void one_post_and_durable_payload(){
    Rig r;bool was_durable{true};r.port.during=[&]{was_durable=was_durable && r.store->latest().phase==SwapPreparePhase::create_intent &&
        r.store->latest().operation.effect==EffectId{1} && r.store->latest().committed_index==10;};
    require(r.start(),"reserved intent starts authenticated prepare read");
    require(r.finish()==SwapPrepareSessionStatus::prepared && was_durable,"prepare intent is durable before exactly one create invocation");
    require(r.port.calls==1 && r.port.path=="/v1/gateway/orders" && r.port.body==R"({"base_asset":"So11111111111111111111111111111111111111112","chain":"solana","side":"buy","type":"market","qty":"1.000000","qty_unit":"quote"})",
        "fixture mapping sends exactly fixed SOL quote input request once");
    require(r.session->prepared() && r.session->prepared()->payload_view()=="{\"activity\":\"synthetic\"}\n" &&
        r.session->prepared()->handle.view()=="order-1" && r.store->latest().response_digest==sha256_digest(std::as_bytes(std::span(r.port.document.data(),r.port.document.size()))),
        "usable prepared bytes preserve original response payload and response digest");
    r.port.document="mutated HTTP buffer";r.restart();
    require(r.start() && r.session->status()==SwapPrepareSessionStatus::prepared && r.port.calls==1 && r.voter.requests==1 &&
        r.session->prepared()->payload_view()=="{\"activity\":\"synthetic\"}\n","local prepared replay after restart never repeats POST or rereads state");
}
void create_receives_fresh_committed_read(){
    Rig r;require(r.start() && r.finish()==SwapPrepareSessionStatus::prepared,"fresh read fixture prepares");
    const auto& read=r.port.read;
    require(read.reading.applied_index==r.voter.index && read.reading.authority_domain==config().binding.authority_domain && !read.reading.state_digest.is_zero() &&
        read.state.pending.phase==swap::Phase::reserved && read.state.pending.intent.effect==EffectId{1} &&
        read.requested_ms==104 && read.received_ms==107,"create sees the committed read that admitted it and its wall-clock bounds");
}
void unknown_and_bad_response_never_retry(){
    for(unsigned mode=0;mode<6;++mode){Rig r;
        if(mode==5)r.port.refuse_route=true;
        if(mode==0)r.port.throw_after=true;
        if(mode==1)r.port.status=transport::SwapHttpsStatus::timeout;
        if(mode==2)r.port.http=500;
        if(mode==3)r.port.document="{}";
        if(mode==4)r.port.exposure=transport::SwapHttpsExposure::possibly_sent;
        require(r.start(),"ambiguous create fixture starts");const auto got=r.finish();
        require(got==SwapPrepareSessionStatus::needs_reconciliation && r.port.calls==1 &&
            !r.session->prepared() && r.store->latest().phase==SwapPreparePhase::create_intent,"lost timeout malformed unconfirmed or refused-route create remains unresolved");
        if(mode==5)require(r.port.candidate.phase==swap::Phase::prepared && r.port.candidate.intent.effect==EffectId{1} &&
            to_hex(r.port.candidate.payload)=="3a02efff5acd5e623cd99f11791246c191fb38fd52ab29fa4f2bac1fc66f7256" && !r.port.candidate.handle.is_zero(),
            "route check receives the exact payload digest, handle and committed intent");
        r.restart();require(!r.start() && !r.start(2,20) && r.port.calls==1 && r.session->status()==SwapPrepareSessionStatus::needs_reconciliation,
            "unknown prepare restart blocks both repeat and newer create");}
}
void state_and_timing_fences(){
    for(unsigned mode=0;mode<7;++mode){Rig r;
        if(mode==0)r.voter.domain=digest(99);
        if(mode==1)r.voter.index=9;
        if(mode==2)r.voter.bad_mac=true;
        if(mode==3)r.voter.silent=true;
        if(mode==4)r.voter.bad_snapshot=true;
        if(mode==5){ApplyScratch work;require(apply(r.voter.state,swap_test::command(22,900,swap::Prepare{EffectId{1},digest(7),digest(8),1}),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture already prepared state");}
        require(r.start(mode==6?2:1),"state fence fixture starts");(void)r.finish();(void)r.session->poll(1020,120);
        require(r.port.calls==0 && r.store->latest().phase==SwapPreparePhase::idle,"wrong domain index authentication phase or effect never creates order");}
    for(bool backwards:{false,true}){Rig r;require(r.start(),"queued response deadline fixture starts");(void)r.session->poll(1000,101);
        require(r.session->poll(backwards?999:1020,102)==SwapPrepareSessionStatus::refused && !r.link.busy() && !r.port.calls,
            "late queued response or backward clock abandons prepare read without POST");}
}
void durable_write_failures(){
    {Rig r;r.fault.armed=true;require(r.start(),"begin sync failure fixture starts");require(r.finish()==SwapPrepareSessionStatus::storage_failed && !r.port.calls,
        "failed initial journal sync prevents create request");}
    {Rig r;r.port.during=[&]{r.fault.armed=true;};require(r.start(),"response sync failure fixture starts");require(r.finish()==SwapPrepareSessionStatus::storage_failed &&
        r.port.calls==1 && !r.session->prepared() && r.store->latest().phase==SwapPreparePhase::create_intent,"failed response journal sync never exposes usable payload");}
}
void higher_effect_requires_prior_terminal(){
    {Rig r;require(r.start(),"prior prepare fixture starts");require(r.finish()==SwapPrepareSessionStatus::prepared,"prior prepared");r.settle_and_reserve_next();r.restart();
        require(r.start(2,20),"next effect starts fresh state read");require(r.finish()==SwapPrepareSessionStatus::prepared && r.port.calls==2 &&
            r.store->latest().operation.effect==EffectId{2},"exact committed predecessor settlement permits one later create");}
    {Rig r;require(r.start(),"wrong predecessor fixture starts");require(r.finish()==SwapPrepareSessionStatus::prepared,"prior prepared");r.settle_and_reserve_next(true);r.restart();
        require(r.start(2,20),"wrong predecessor state read starts");require(r.finish()==SwapPrepareSessionStatus::needs_reconciliation && r.port.calls==1 &&
            r.store->latest().operation.effect==EffectId{1},"wrong predecessor payload blocks advancing retained prepare operation");}
}
void lifecycle_and_mapping_guards(){
    {Rig r;require(r.start(),"journal mutation fixture starts");(void)r.session->poll(1000,101);
        truemarkets::SwapRestRequest request;require(truemarkets::encode_swap_rest_create({Side::buy,1'000'000},request)==truemarkets::SwapRestCode::encoded,"fixture request encoded");
        require(r.store->begin({EffectId{1},digest(90),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1'000'000}},8,digest(91))==SwapPrepareStoreCode::none,"fixture different caller advances journal");
        require(r.session->poll(1001,102)==SwapPrepareSessionStatus::refused && !r.port.calls && !r.link.busy(),"changed prepare journal invalidates outstanding state read");}

    {Rig r;require(r.start(),"owned read starts");r.session.reset();require(!r.link.busy(),"prepare destructor abandons owned state request");}
    {Rig r;require(r.link.request_state(1,20),"fixture unrelated read");require(!r.start(),"busy unrelated read refuses prepare start");r.session.reset();require(r.link.busy(),"idle prepare destructor preserves unrelated read");r.link.abandon();}
    {Rig r;require(!r.session->start(EffectId{1},10,UINT64_MAX-19,104),"prepare deadline overflow refuses");}
    {Rig r;auto c=config();c.mapping=static_cast<SwapPrepareInputMapping>(99);r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,r.port,c);
        require(!r.start() && !r.port.calls,"unadmitted input mapping refuses prepare session");}
}
void sell_input_and_expired_admission(){
    for(bool executor:{false,true}){
        Rig r;ApplyScratch work;
        Command control=executor?Command{{CallerId{9},RequestSequence{900},digest(94)},SetExecutorGeneration{ExecutorGeneration{2}}}:
            Command{{CallerId{9},RequestSequence{900},digest(94)},AdvanceKillEpoch{KillEpoch{2}}};
        require(apply(r.voter.state,control,work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture current control generation advanced");
        require(apply(r.voter.state,{{CallerId{9},RequestSequence{901},digest(95)},SetStrategyMode{StrategyMode::normal,r.voter.state.kill_epoch,r.voter.state.executor_generation}},
            work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture current mode normal with stale pending authority");
        require(r.start(),"stale pending control fixture starts");
        require(r.finish()==SwapPrepareSessionStatus::refused && !r.port.calls && r.store->latest().phase==SwapPreparePhase::idle,
            "stale reserved kill or executor generation never reaches create port");
    }

    {
        Rig r;auto& state=r.voter.state.spot_swap;
        state={};auto opening=swap_test::opening();opening.base=swap::BaseAtoms{20'000'000};opening.base_cost=swap::QuoteAtoms{2'000'000};
        require(swap::apply(state,opening,swap::Role::operator_role,100)==swap::Code::applied,"fixture explicit funded sell allocation");
        auto intent=swap_test::buy(opening);intent.side=Side::sell;intent.quote_input={};intent.max_quote_debit={};intent.min_base_receive={};
        intent.base_input=swap::BaseAtoms{8'400'000};intent.min_quote_receive=swap::QuoteAtoms{1'000'000};
        require(swap::apply(state,intent,swap::Role::strategy,100)==swap::Code::applied,"fixture exact base sell reserved");
        const auto qty=r.port.document.find("\"qty\":\"1\"");require(qty!=std::string::npos,"fixture quote quantity located");r.port.document.replace(qty,9,"\"qty\":\"0.008400000\"");
        const auto output=r.port.document.find("0.009191912");require(output!=std::string::npos,"fixture output located");r.port.document.replace(output,11,"1.010000");
        require(r.start() && r.finish()==SwapPrepareSessionStatus::prepared && r.port.body==R"({"base_asset":"So11111111111111111111111111111111111111112","chain":"solana","side":"sell","type":"market","qty":"0.008400000","qty_unit":"base"})",
            "fixture sell mapping preserves exact base atoms and decodes USDC output units");
    }
    {
        Rig r;require(r.start(),"expired admission fixture starts");(void)r.session->poll(1000,20'100);
        require(r.session->poll(1001,20'100)==SwapPrepareSessionStatus::refused && !r.port.calls,
            "signing deadline admission refuses unsigned create at expiry");
    }
}

void fee_inclusive_input_mapping(){
    for(unsigned mode=0;mode<4;++mode){
        Rig r;r.session.reset();r.store.reset();
        auto c=config();c.mapping=SwapPrepareInputMapping::fee_inclusive_quote_input;
        c.binding.mapping_identity=fee_inclusive_prepare_mapping_identity();
        require(!c.binding.mapping_identity.is_zero(),"fee_inclusive_mapping_has_distinct_identity");
        if(mode==3)c.binding.mapping_identity=digest(99);
        r.store=std::make_unique<SwapPrepareStore>(r.scratch.base/"inclusive",c.binding);
        require(r.store->create({c.binding,1,digest(51),EffectId{0}})==SwapPrepareStoreCode::none,"inclusive journal created");
        auto& s=r.voter.state.spot_swap;s={};
        const auto opening=swap_test::opening();
        require(swap::apply(s,opening,swap::Role::operator_role,100)==swap::Code::applied,"inclusive opening");
        auto intent=swap_test::buy(opening);intent.quote_input=swap::QuoteAtoms{998'000};
        intent.max_quote_debit=swap::QuoteAtoms{1'000'000};intent.fee_cap=swap::QuoteAtoms{2'000};
        require(swap::apply(s,intent,swap::Role::strategy,100)==swap::Code::applied,"inclusive principal reserved");
        if(mode==1){const auto at=r.port.document.find("0.002");r.port.document.replace(at,5,"0.003");}
        if(mode==2){const auto at=r.port.document.find("0.002");r.port.document.replace(at,5,"0.001");}
        r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,r.port,c);
        if(mode==3){require(!r.start() && !r.port.calls,"inclusive_mapping_identity_mismatch_refuses");continue;}
        require(r.start(),"inclusive prepare starts");const auto result=r.finish();
        require(r.port.calls==1 && r.port.body.find("\"qty\":\"1.000000\"")!=std::string::npos,
            "998000_principal_plus_2000_fee_requests_one_usdc_not_0998");
        if(mode){require(result==SwapPrepareSessionStatus::needs_reconciliation && !r.session->prepared(),
            "fee_mismatch_never_reinterprets_principal_or_retries");continue;}
        require(result==SwapPrepareSessionStatus::prepared && r.session->prepared()->quoted_input==1'000'000 &&
            r.store->latest().operation.input.quantity_atoms==1'000'000,"inclusive_raw_quantity_persisted_for_status_matching");
        r.session.reset();r.store.reset();
        r.store=std::make_unique<SwapPrepareStore>(r.scratch.base/"inclusive",c.binding);
        require(r.store->recover()==SwapPrepareStoreCode::none,"inclusive journal recovers");
        r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,r.port,c);
        require(r.start() && r.session->prepared() && r.port.calls==1,"inclusive_recovery_never_recreates_quote");
        c.mapping=SwapPrepareInputMapping::fixture_principal_and_base_input;
        r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,r.port,c);
        require(r.session->status()==SwapPrepareSessionStatus::closed,"inclusive_journal_cannot_recover_under_legacy_mapping");
    }
    for(bool excess_fee:{false,true}){
        Rig r;r.session.reset();r.store.reset();
        auto c=config();c.mapping=SwapPrepareInputMapping::fee_inclusive_quote_input;
        c.binding.mapping_identity=fee_inclusive_prepare_mapping_identity();
        r.store=std::make_unique<SwapPrepareStore>(r.scratch.base/"inclusive-sell",c.binding);
        require(r.store->create({c.binding,1,digest(51),EffectId{0}})==SwapPrepareStoreCode::none,"inclusive sell journal created");
        auto& s=r.voter.state.spot_swap;s={};auto opening=swap_test::opening();
        opening.base=swap::BaseAtoms{20'000'000};opening.base_cost=swap::QuoteAtoms{2'000'000};
        require(swap::apply(s,opening,swap::Role::operator_role,100)==swap::Code::applied,"inclusive sell opening");
        auto intent=swap_test::buy(opening);intent.side=Side::sell;intent.quote_input={};intent.max_quote_debit={};intent.min_base_receive={};
        intent.base_input=swap::BaseAtoms{8'400'000};intent.min_quote_receive=swap::QuoteAtoms{1'000'000};
        require(swap::apply(s,intent,swap::Role::strategy,100)==swap::Code::applied,"inclusive sell reserves base");
        r.port.document.replace(r.port.document.find("\"qty\":\"1\""),9,"\"qty\":\"0.008400000\"");
        r.port.document.replace(r.port.document.find("0.009191912"),11,"1.010000");
        if(excess_fee)r.port.document.replace(r.port.document.find("0.002"),5,"0.003");
        r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,r.port,c);
        require(r.start(),"inclusive sell starts");const auto result=r.finish();
        require(r.port.body.find("\"qty\":\"0.008400000\"")!=std::string::npos,
            "inclusive_sell_never_adds_quote_fee_to_base_quantity");
        require(result==(excess_fee?SwapPrepareSessionStatus::needs_reconciliation:SwapPrepareSessionStatus::prepared),
            "inclusive_sell_rejects_fee_above_cap");
    }
}

#ifdef VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY
void https_prepare_and_recovery(){
    const auto* origin=std::getenv("PREPARE_FIXTURE_ORIGIN");
    const auto* ca=std::getenv("PREPARE_FIXTURE_CA");
    const auto* mode=std::getenv("PREPARE_FIXTURE_MODE");
    require(origin && ca && mode,"local HTTPS fixture configuration supplied");
    Rig r;r.session.reset();r.store.reset();
    auto c=config();c.mapping=SwapPrepareInputMapping::fee_inclusive_quote_input;
    c.binding.mapping_identity=fee_inclusive_prepare_mapping_identity();
    r.store=std::make_unique<SwapPrepareStore>(r.scratch.base/"https",c.binding);
    require(r.store->create({c.binding,1,digest(51),EffectId{0}})==SwapPrepareStoreCode::none,"HTTPS journal created");
    auto& state=r.voter.state.spot_swap;state={};const auto opening=swap_test::opening();
    require(swap::apply(state,opening,swap::Role::operator_role,100)==swap::Code::applied,"HTTPS opening");
    auto intent=swap_test::buy(opening);intent.quote_input=swap::QuoteAtoms{998'000};
    intent.max_quote_debit=swap::QuoteAtoms{1'000'000};intent.fee_cap=swap::QuoteAtoms{2'000};
    require(swap::apply(state,intent,swap::Role::strategy,100)==swap::Code::applied,"HTTPS principal reserved");
    auto port=SwapHttpsPreparePort::local_fixture({Side::buy,1'000'000},"fixture-token",origin,ca);
    require(bool(port),"HTTPS port is loopback fixture");
    truemarkets::SwapRestRequest request;
    require(truemarkets::encode_swap_rest_create({Side::buy,1'000'000},request)==truemarkets::SwapRestCode::encoded,"HTTPS canonical request");
    if(std::string_view(mode)=="invalid"){
        for(unsigned kind=0;kind<4;++kind){
            auto changed=request;
            if(kind==0)changed.path_size=static_cast<std::uint16_t>(changed.path.size()+1);
            if(kind==1)changed.body_size=static_cast<std::uint16_t>(changed.body.size()+1);
            if(kind==2)changed.path[1]='x';
            if(kind==3)require(truemarkets::encode_swap_rest_create({Side::buy,1'000'001},changed)==truemarkets::SwapRestCode::encoded,"changed quantity encodes");
            require(port->create(changed,{}).status==transport::SwapHttpsStatus::local_refusal,"invalid request refuses before HTTP");
        }
        return;
    }
    r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,*port,c);
    require(r.start(),"HTTPS session requests authenticated reserved state");
    const auto result=r.finish();const bool success=std::string_view(mode)=="success";
    require(result==(success?SwapPrepareSessionStatus::prepared:SwapPrepareSessionStatus::needs_reconciliation),
        "HTTPS result retains prepared bytes or unresolved intent");
    require(r.store->latest().operation.input.quantity_atoms==1'000'000 && r.store->latest().committed_index==10,
        "HTTPS create follows authenticated state and gross input journal");
    require(port->create(request,{}).status==transport::SwapHttpsStatus::local_refusal,"HTTPS port refuses a second invocation");
    r.session.reset();r.store.reset();
    r.store=std::make_unique<SwapPrepareStore>(r.scratch.base/"https",c.binding);
    require(r.store->recover()==SwapPrepareStoreCode::none,"HTTPS journal recovers");
    port=SwapHttpsPreparePort::local_fixture({Side::buy,1'000'000},"fixture-token",origin,ca);
    r.session=std::make_unique<SwapPrepareSession>(r.link,*r.store,*port,c);
    require(success?r.start():!r.start(),"HTTPS restart recovers without creating again");
    require(bool(r.session->prepared())==success,"HTTPS unresolved response never exposes prepared bytes");
}
#endif

}
int main(int argc,char** argv){
    const std::pair<const char*,void(*)()> cases[]{
        {"one_post_and_durable_payload",one_post_and_durable_payload},{"create_receives_fresh_committed_read",create_receives_fresh_committed_read},{"unknown_and_bad_response_never_retry",unknown_and_bad_response_never_retry},
        {"state_and_timing_fences",state_and_timing_fences},{"durable_write_failures",durable_write_failures},
        {"higher_effect_requires_prior_terminal",higher_effect_requires_prior_terminal},{"lifecycle_and_mapping_guards",lifecycle_and_mapping_guards},
        {"sell_input_and_expired_admission",sell_input_and_expired_admission},{"fee_inclusive_input_mapping",fee_inclusive_input_mapping}
#ifdef VELOCITA_SWAP_PREPARE_HTTPS_TEST_ONLY
        ,{"https_prepare_and_recovery",https_prepare_and_recovery}
#endif
    };
    unsigned ran{},failed{};for(const auto& [name,run]:cases)if(argc==1 || std::string(argv[1])==name){++ran;try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
