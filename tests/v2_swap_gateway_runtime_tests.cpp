// Reuse the real three-voter fixture without changing its existing test registry.
#define main existing_gateway_suite_main
#include "v2_gateway_node_tests.cpp"
#undef main
#include "velocita/v2/swap_gateway_runtime.hpp"

namespace {
struct RuntimePrepare final:SwapPreparePort {
    SwapPrepareStore* store{};unsigned calls{};bool lose_reply{};
    transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&)override{
        ++calls;require(store->latest().phase==SwapPreparePhase::create_intent,"create fence durable before network");
        if(lose_reply)throw std::runtime_error("lost create");
        constexpr std::string_view body=R"({"order_id":"synthetic-tm-order","status":"initialized","payloads":[{"digest":"a71e361bce0c479f89741f87a0248cf21cae63ee745a5d6623d0e5760aabebcc","payload":"VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n"}],"quote":{"base_asset":"So11111111111111111111111111111111111111112","quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","qty":"1","qty_out":"0.009191912","fee":"0.002"}})";
        transport::SwapHttpsResult r;r.status=transport::SwapHttpsStatus::complete;r.exposure=transport::SwapHttpsExposure::response_received;r.http_status=201;
        r.body_size=body.size();std::copy(body.begin(),body.end(),r.body.begin());return r;
    }
};
struct RuntimeSigner final:SwapGatewaySigningPort {
    SwapPrepareStore* store{};unsigned calls{};bool refuse{},oversized{};
    std::optional<SwapSignedExecution> sign_or_retrieve(std::uint64_t,std::int64_t index,std::uint64_t)override{
        ++calls;require(index>store->latest().committed_index,"sign only after kernel prepare committed");if(refuse)return {};
        const auto& saved=store->latest();SwapSignedExecution r;auto& id=r.identity;
        id.effect=saved.operation.effect;id.payload=saved.prepared.payload_digest;id.handle_size=saved.prepared.handle.length;
        std::copy_n(reinterpret_cast<const std::byte*>(saved.prepared.handle.bytes.data()),id.handle_size,id.handle.begin());
        id.handle_digest=sha256_digest(std::span(id.handle).first(id.handle_size));
        std::array<std::byte,33> pub{};pub[0]=std::byte{2};
        const std::array<std::byte,8> der{std::byte{0x30},std::byte{6},std::byte{2},std::byte{1},std::byte{1},std::byte{2},std::byte{1},std::byte{2}};
        swap_signing::ApiKeyStamp stamp;
        require(swap_signing::encode_api_key_stamp(pub,der,stamp)==swap_signing::StampCode::encoded,"fake stamp formats");
        require(truemarkets::encode_swap_rest_execute(saved.prepared.handle,stamp,r.request)==truemarkets::SwapRestCode::encoded,"canonical fake execute");
        id.execution_request=sha256_digest(std::as_bytes(std::span(r.request.body.data(),r.request.body_size)));
        if(oversized)r.request.body_size=65535;return r;
    }
};
struct RuntimeExecute final:SwapExecutionPort {
    SwapSubmissionStore* store{};unsigned calls{};
    void execute(const SwapSubmissionIdentity& id,std::span<const std::byte> bytes)override{
        ++calls;require(store->latest().phase==SwapSubmissionPhase::execute_intent && store->latest().identity==id && sha256_digest(bytes)==id.execution_request,"execute fence durable before network");
        throw std::runtime_error("venue accepted but response lost");
    }
};
struct RuntimeSettlement final:SwapGatewaySettlementPort {
    SwapSubmissionStore* store{};bool prove{};unsigned calls{};
    std::optional<VerifiedSwapSettlement> reconcile(const swap::Pending& p)override{
        ++calls;if(!prove)return {};
        SwapSubmissionTransaction signature{};for(std::size_t n=0;n<signature.size();++n)signature[n]=static_cast<std::byte>(n);
        require(store->observe_transaction(store->latest().identity,signature)==SwapSubmissionCode::none,"fake authenticated order lookup persists signature");
        swap::Settlement s;s.effect=p.intent.effect;s.custody=p.intent.binding.custody;s.payload=p.payload;
        s.transaction=*solana_signature_transaction_digest(signature);s.evidence=digest(210);s.finalized=true;s.outcome=swap::Outcome::succeeded;
        s.base_credit=swap::BaseAtoms{10'000'000};s.quote_debit=swap::QuoteAtoms{1'000'000};s.fee=swap::QuoteAtoms{2'000};
        return settlement_test_fixture::make_verified(s);
    }
};
void runtime_drill(unsigned mode){
    g_swap=true;struct Reset{~Reset(){g_swap=false;}}reset;
    Base base("v2-runtime");Cluster c(base.path,{1,2,3});Caller op(kOperatorKey,kOperator);c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"epoch");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"generation");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"catalog");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"gateway role");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"funded");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"normal");
    auto intent=swap_test::buy(opening);intent.quote_input=swap::QuoteAtoms{998000};intent.max_quote_debit=swap::QuoteAtoms{1000000};
    GatewayClient client;client.network=&c.network;
    GatewayLink link(1,process_incarnation(0x91),gateway_routes(c.voters,caller_seed(kGatewayKey)),client);
    require(link.request_state(steady_ms(),3000),"bootstrap read");auto baseline=settle(c,link);require(baseline.status==LinkStatus::done,"bootstrap");
    const auto domain=baseline.reading.authority_domain;
    SwapPrepareBinding binding{opening.binding,domain,fee_inclusive_prepare_mapping_identity()};
    auto prepared=std::make_unique<SwapPrepareStore>(base.path/"prepared",binding);require(prepared->create({binding,baseline.reading.applied_index,baseline.reading.state_digest,EffectId{0}})==SwapPrepareStoreCode::none,"prepare bootstrap");
    auto submission=std::make_unique<SwapSubmissionStore>(base.path/"submission",SwapSubmissionBinding{opening.binding.incarnation,domain});require(submission->create({{opening.binding.incarnation,domain},baseline.reading.applied_index,baseline.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"submission bootstrap");
    auto watermark=std::make_unique<SwapResultWatermark>(opening.binding.incarnation);
    auto recovery=std::make_unique<SwapReconcileStore>(base.path/"recovery",SwapReconcileStoreBinding{opening.binding.incarnation,domain},*watermark);
    require(recovery->create({{opening.binding.incarnation,domain},baseline.reading.applied_index,baseline.reading.state_digest})==SwapReconcileStoreCode::none,"watermark bootstrap");
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,intent}}).code==ApplyCode::applied,"reserve all-in one dollar");
    RuntimePrepare prepare;prepare.store=prepared.get();prepare.lose_reply=mode==2;
    RuntimeSigner signer;signer.store=prepared.get();signer.refuse=mode==3;signer.oversized=mode==4;
    RuntimeExecute execute;execute.store=submission.get();RuntimeSettlement settlement;settlement.store=submission.get();settlement.prove=mode==0;
    SwapGatewayRuntimeConfig config{binding,CallerId{kGateway},1,10000,3000,1};
    config.recovery_only=mode==5;
    auto runtime=std::make_unique<SwapGatewayRuntime>(link,*recovery,*prepared,*submission,prepare,signer,execute,settlement,config);
    c.on_round=[&]{runtime->step(steady_ms(),110);};c.run_until([&]{return runtime->stopped();},"runtime reaches bounded terminal");c.on_round={};
    if(mode==5){require(prepare.calls==0 && signer.calls==0 && execute.calls==0 && runtime->status()==SwapGatewayRuntimeStatus::blocked,
        "recovery-only reserved intent cannot prepare sign or execute");return;}
    require(prepare.calls==1,"one create per effect status="+std::to_string(int(runtime->status()))+" cause="+std::to_string(int(runtime->cause())));
    if(mode==0){require(runtime->status()==SwapGatewayRuntimeStatus::complete && execute.calls==1 && submission->latest().phase==SwapSubmissionPhase::resolved,"joined lifecycle completes once");
        c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](auto& n){return n.second->applied_state().spot_swap.settlement_count==1;});},"all voters settle");
        for(auto& [id,node]:c.nodes)require(node->applied_state().spot_swap.quote.value()==4000000 && node->applied_state().spot_swap.base.value()==10000000 && node->applied_state().spot_swap.reserved_quote.value()==0,"exact final economics replicated");
    }else{
        require(execute.calls==((mode==1 || mode==6)?1u:0u),"refusal never sends extra execute");
        auto next=runtime->next_sequence();runtime.reset();config.first_sequence=next;
        const auto signing_calls=signer.calls;
        if(mode==1 || mode==6)config.recovery_only=true;
        if(mode==6)settlement.prove=true;
        prepared.reset();submission.reset();recovery.reset();watermark=std::make_unique<SwapResultWatermark>(opening.binding.incarnation);
        prepared=std::make_unique<SwapPrepareStore>(base.path/"prepared",binding);
        submission=std::make_unique<SwapSubmissionStore>(base.path/"submission",SwapSubmissionBinding{opening.binding.incarnation,domain});
        recovery=std::make_unique<SwapReconcileStore>(base.path/"recovery",SwapReconcileStoreBinding{opening.binding.incarnation,domain},*watermark);
        require(prepared->recover()==SwapPrepareStoreCode::none && submission->recover()==SwapSubmissionCode::none && recovery->recover()==SwapReconcileStoreCode::none,"all restart journals recover from disk");
        prepare.store=prepared.get();signer.store=prepared.get();execute.store=submission.get();settlement.store=submission.get();
        runtime=std::make_unique<SwapGatewayRuntime>(link,*recovery,*prepared,*submission,prepare,signer,execute,settlement,config);
        c.on_round=[&]{runtime->step(steady_ms(),111);};c.run_until([&]{return runtime->stopped();},"restart remains bounded");c.on_round={};
        require(prepare.calls==1 && execute.calls==((mode==1 || mode==6)?1u:0u),"restart never repeats ambiguous POST");
        if(mode==1)require(signer.calls==signing_calls && runtime->cause()==SwapGatewayRuntimeCause::settlement_unproven,"restart of sent request only reconciles without signing");
        if(mode==4)require(runtime->cause()==SwapGatewayRuntimeCause::identity,"oversized signed request refused before span construction");
        if(mode==6){require(runtime->status()==SwapGatewayRuntimeStatus::complete && signer.calls==signing_calls && execute.calls==1 &&
            submission->latest().phase==SwapSubmissionPhase::resolved,"recovery-only finalized receipt resolves without signing or resubmission");return;}
        const auto& s=c.state_of(c.elect()).spot_swap;require(s.reserved_quote.value()==1000000 && s.quote.value()==5000000 && s.settlement_count==0,"unknown retains full funds and hold");
    }
}
}
int main(int argc,char** argv){try{for(unsigned mode=0;mode<7;++mode){if(argc==2 && mode!=static_cast<unsigned>(std::atoi(argv[1])))continue;runtime_drill(mode);std::cout<<"PASS runtime mode "<<mode<<'\n';}return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
