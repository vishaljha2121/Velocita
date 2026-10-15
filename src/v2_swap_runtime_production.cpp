#include "velocita/v2/swap_runtime_production.hpp"
#include "velocita/v2/swap_prepare_https.hpp"
#include <algorithm>
#include <charconv>
#include <map>

namespace velocita::v2 {
std::optional<SwapRuntimeProfile> decode_swap_runtime_profile(std::string_view document)noexcept{
    try{
        if(document.empty() || document.size()>8192)return {};
        std::map<std::string_view,std::string_view> fields;
        while(!document.empty()){
            const auto end=document.find('\n');const auto line=document.substr(0,end);
            document=end==std::string_view::npos?std::string_view{}:document.substr(end+1);
            if(line.empty())return {};const auto equal=line.find('=');
            if(equal==std::string_view::npos || !equal || equal+1==line.size() || !fields.emplace(line.substr(0,equal),line.substr(equal+1)).second)return {};
        }
        auto get=[&](std::string_view name){const auto it=fields.find(name);if(it==fields.end())throw 0;const auto value=it->second;fields.erase(it);return value;};
        auto number=[&](std::string_view name){const auto s=get(name);std::uint64_t n{};auto r=std::from_chars(s.data(),s.data()+s.size(),n);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw 0;return n;};
        auto hex=[&](std::string_view name,std::span<std::byte> bytes){auto s=get(name);if(s.size()!=bytes.size()*2)throw 0;
            for(std::size_t i=0;i<bytes.size();++i){unsigned n{};auto r=std::from_chars(s.data()+2*i,s.data()+2*i+2,n,16);if(r.ec!=std::errc{}||r.ptr!=s.data()+2*i+2)throw 0;bytes[i]=static_cast<std::byte>(n);}};
        if(get("schema")!="1")return {};
        SwapRuntimeProfile p;auto& b=p.gateway.binding;auto& a=b.allocation;
        hex("incarnation",a.incarnation.bytes);hex("catalog",a.catalog.bytes);hex("custody",a.custody.bytes);hex("authority_domain",b.authority_domain.bytes);
        a.account=AccountId{number("account")};a.instrument=InstrumentId{number("instrument")};a.policy=PolicyId{number("policy")};
        a.policy_version=PolicyVersion{number("policy_version")};a.risk_policy_version=RiskPolicyVersion{number("risk_policy_version")};
        hex("account_assets",b.account_session.account_and_assets_digest.bytes);hex("session_evidence",b.account_session.session_evidence_digest.bytes);
        hex("owner_wallet",b.owner_wallet);hex("signer_public_key",b.signer_public_key);
        if(fields.contains("max_buy_output_deduction_lamports"))
            b.max_buy_output_deduction_lamports=number("max_buy_output_deduction_lamports");
        if(fields.contains("transaction_review_digest")){
            hex("transaction_review_digest",b.transaction_review_digest.bytes);
            if(b.transaction_review_digest.is_zero())return {};
        }
        hex("account_review_digest",p.account_review_digest.bytes);if(p.account_review_digest.is_zero())return {};
        hex("failure_fee_payer",std::as_writable_bytes(std::span(p.failure_policy.expected_external_fee_payer)));
        const auto& payer=p.failure_policy.expected_external_fee_payer;
        if(std::all_of(payer.begin(),payer.end(),[](auto v){return v==0;}) ||
            std::equal(payer.begin(),payer.end(),b.owner_wallet.begin(),[](auto x,auto y){return x==std::to_integer<std::uint8_t>(y);}))return {};
        const auto socket=get("socket");if(socket.size()>=b.socket.size())return {};b.socket_size=socket.size();std::copy(socket.begin(),socket.end(),b.socket.begin());
        const auto gateway_uid=number("gateway_uid"),signer_uid=number("signer_uid"),timeout=number("ipc_timeout_ms");
        if(gateway_uid>UINT32_MAX||signer_uid>UINT32_MAX||timeout>60000)return {};
        b.gateway_uid=gateway_uid;b.signer_uid=signer_uid;b.ipc_timeout_ms=timeout;
        b.gateway_caller=CallerId{number("gateway_caller")};b.signer_caller=CallerId{number("signer_caller")};
        hex("bearer_digest",p.gateway.bearer_digest.bytes);p.gateway.expires_at_ms=number("expires_at_ms");
        p.signer.binding=b;hex("signer_instance",p.signer.signer_instance.bytes);p.signer.key_version=number("key_version");p.signer.state_timeout=number("state_timeout_ms");
        const auto organization=truemarkets::tm_text(get("organization")),signer=truemarkets::tm_text(get("signer_id")),user=truemarkets::tm_text(get("user_id"));
        if(!organization||!signer||!user||!swap_account_user_id_valid(user->view())||!fields.empty()||p.gateway.expires_at_ms<=1 || validate_swap_gateway_startup(p.gateway,1)!=SwapRuntimeCode::valid||validate_swap_signer_startup(p.signer)!=SwapRuntimeCode::valid)return {};
        p.organization=*organization;p.signer_id=*signer;p.user_id=*user;return p;
    }catch(...){return {};}
}
bool swap_runtime_live_contract_admitted(const SwapRuntimeProfile& profile,std::uint64_t now)noexcept{
    const auto& b=profile.gateway.binding;
    return validate_swap_gateway_startup(profile.gateway,now)==SwapRuntimeCode::valid &&
        validate_swap_signer_startup(profile.signer)==SwapRuntimeCode::valid &&
        swap_runtime_identity_digest(b)==swap_runtime_identity_digest(profile.signer.binding) &&
        !b.transaction_review_digest.is_zero() && b.max_buy_output_deduction_lamports<=2'039'280 &&
        !profile.account_review_digest.is_zero();
}
namespace {
class SwapHttpsTokenPort final:public SwapTokenPort {
public:
    explicit SwapHttpsTokenPort(long timeout):timeout_(timeout){}
    transport::SwapHttpsResult post(std::string_view body)override{
        transport::SwapHttpsConfig cfg;cfg.origin="https://api.truemarkets.co";
        cfg.timeout_ms=timeout_;cfg.max_response_bytes=truemarkets::kMaxTmResponseBytes;
        cfg.routes[0]={transport::SwapHttpsMethod::post,"/v1/auth/api-key/token"};cfg.route_count=1;
        transport::SwapHttpsClient client(cfg);
        return client.exchange({transport::SwapHttpsMethod::post,"/v1/auth/api-key/token",{},body});
    }
private:
    long timeout_{};
};
}
std::unique_ptr<SwapTokenPort> make_swap_token_port(long timeout_ms){return std::make_unique<SwapHttpsTokenPort>(timeout_ms);}
SwapAccountSetupResult read_swap_account_setup(SwapGatewayCredentials& credentials,
    SwapFinalizedReadClock& clock,std::string_view user,std::uint64_t timeout){
    if(!swap_account_user_id_valid(user) || !timeout || timeout>30'000)return {};
    const auto bearer=credentials.bearer(clock.now_ms());const auto start=clock.now_ms();
    if(!start || bearer.empty())return {};
    transport::SwapHttpsConfig cfg;cfg.origin="https://api.truemarkets.co";
    cfg.timeout_ms=static_cast<long>(timeout);cfg.max_response_bytes=truemarkets::kMaxTmResponseBytes;
    cfg.routes[0]={transport::SwapHttpsMethod::get,"/v1/account/me/trading"};cfg.route_count=1;
    transport::SwapHttpsClient client(cfg);transport::SwapHttpsResult response;
    try{response=client.exchange({transport::SwapHttpsMethod::get,"/v1/account/me/trading",bearer,{}});}catch(...){return {};}
    const auto end=clock.now_ms();
    if(end<start || end-start>timeout || credentials.bearer(end).empty() ||
        response.status!=transport::SwapHttpsStatus::complete || response.exposure!=transport::SwapHttpsExposure::response_received ||
        response.http_status!=200 || response.body_size>response.body.size())return {};
    truemarkets::SolanaAccountKey owner{};
    for(std::size_t i=0;i<owner.size();++i)owner[i]=std::to_integer<std::uint8_t>(credentials.binding().owner_wallet[i]);
    return verify_swap_account_setup({response.body.data(),response.body_size},user,owner);
}
bool swap_admission_funded(const SwapAccountStartupAdmission& admission,const SwapRuntimeBinding& binding,std::uint64_t now) noexcept {
    return swap_account_history_funding(admission.history,admission.review,binding,admission.funding_state,now);
}
bool read_swap_reviewed_balance(SwapGatewayCredentials& credentials,SwapFinalizedReadClock& clock,
    const SwapAccountStartupAdmission& admission,std::uint64_t timeout){
    if(admission.recovery_only || !timeout || timeout>30'000)return false;
    const auto bearer=credentials.bearer(clock.now_ms());const auto start=clock.now_ms();
    if(bearer.empty() || !swap_admission_funded(admission,credentials.binding(),start))return false;
    transport::SwapHttpsConfig cfg;cfg.origin="https://api.truemarkets.co";
    cfg.timeout_ms=static_cast<long>(timeout);cfg.max_response_bytes=truemarkets::kMaxTmResponseBytes;
    cfg.routes[0]={transport::SwapHttpsMethod::get,"/v1/gateway/balances"};cfg.route_count=1;
    transport::SwapHttpsClient client(cfg);transport::SwapHttpsResult response;
    try{response=client.exchange({transport::SwapHttpsMethod::get,"/v1/gateway/balances",bearer,{}});}catch(...){return false;}
    const auto end=clock.now_ms();
    return end>=start && end-start<=timeout && !credentials.bearer(end).empty() &&
        swap_admission_funded(admission,credentials.binding(),end) &&
        response.status==transport::SwapHttpsStatus::complete && response.exposure==transport::SwapHttpsExposure::response_received &&
        response.http_status==200 && response.body_size<=response.body.size() &&
        swap_reviewed_balance_matches(admission.review,credentials.binding(),admission.state,admission.barrier,
            {response.body.data(),response.body_size},start,end);
}
std::optional<swap::State> swap_unsent_funding_state(const swap::State& state) noexcept {
    if(state.pending.phase==swap::Phase::none)return state;
    if(state.pending.phase!=swap::Phase::reserved)return {};
    auto funded=state;funded.pending={};funded.reserved_base={};funded.reserved_quote={};
    if(!swap::valid_state(funded))return {};
    return funded;
}
bool swap_startup_recovery_only(const swap::State& state,SwapPreparePhase prepared,EffectId prepared_effect,SwapSubmissionPhase submitted) noexcept {
    const auto& pending=state.pending;
    if(submitted!=SwapSubmissionPhase::idle || (pending.phase!=swap::Phase::none && pending.phase!=swap::Phase::reserved) ||
        prepared==SwapPreparePhase::prepared)return true;
    return prepared==SwapPreparePhase::create_intent && (pending.phase!=swap::Phase::reserved || pending.intent.effect!=prepared_effect);
}
SwapCreateTrace swap_create_trace(const transport::SwapHttpsResult& r) noexcept {
    SwapCreateTrace t;t.stage=SwapCreateStage::exchanged;t.transport=r.status;t.exposure=r.exposure;t.http_status=r.http_status;
    t.body_size=r.body_size;const auto body=r.body_view().substr(0,std::min(r.body_size,r.body.size()));
    t.body_digest=sha256_digest(std::as_bytes(std::span(body.data(),body.size())));
    if(r.http_status<200 || r.http_status>=300){t.error_size=std::min(body.size(),t.error.size());std::copy_n(body.begin(),t.error_size,t.error.begin());}
    return t;
}
std::string render_swap_create_trace(const SwapCreateTrace& t){
    return "velocita_swap_create_stage "+std::to_string(static_cast<unsigned>(t.stage))+"\nvelocita_swap_create_transport "+
        std::to_string(static_cast<unsigned>(t.transport))+"\nvelocita_swap_create_exposure "+std::to_string(static_cast<unsigned>(t.exposure))+
        "\nvelocita_swap_create_http_status "+std::to_string(t.http_status)+"\nvelocita_swap_create_body_bytes "+std::to_string(t.body_size)+"\n";
}
std::optional<SwapAccountStartupAdmission> swap_prepare_admission(const SwapAccountStartupAdmission& startup,const SwapPrepareRead& read) noexcept {
    const auto funded=swap_unsent_funding_state(read.state);
    if(!funded || read.reading.applied_index<0 || static_cast<std::uint64_t>(read.reading.applied_index)<startup.barrier.applied_index ||
        read.reading.state_digest.is_zero() || !read.requested_ms || read.received_ms<read.requested_ms)return {};
    auto admission=startup;admission.state=*funded;
    admission.barrier={startup.barrier.incarnation,startup.barrier.clock_epoch,read.reading.state_digest,
        static_cast<std::uint64_t>(read.reading.applied_index),read.requested_ms,read.received_ms};
    return admission;
}
SwapProductionPorts::SwapProductionPorts(SwapGatewayCredentials& credentials,SwapPrepareStore& p,SwapSubmissionStore& s,SwapPrepareBinding binding,
    SwapFinalizedReadClock& clock,truemarkets::TmText org,truemarkets::TmText signer,std::uint64_t timeout,SwapAccountStartupAdmission admission,SwapFailureAccountingPolicy failure_policy)
    :credentials_(credentials),preparation_(p),submission_(s),binding_(binding),clock_(clock),organization_(org),signer_(signer),timeout_(timeout),failure_policy_(failure_policy),admission_(std::move(admission)){
    if(credentials.binding().allocation!=binding.allocation || credentials.binding().authority_domain!=binding.authority_domain || timeout<1 || timeout>60000)
        throw std::invalid_argument("production port binding refused");
}
std::optional<truemarkets::JupiterLookupPrefix> SwapProductionPorts::prefix(std::span<const std::byte> payload){
    const auto need=truemarkets::jupiter_lookup_requirement(std::string_view(reinterpret_cast<const char*>(payload.data()),payload.size()),
        organization_.view(),signer_.view());
    if(!need)return {};
    SwapRuntimeFinalizedHttps rpc(timeout_);
    return read_jupiter_lookup_prefix(rpc,rpc.provider_identity(),timeout_,1,*need);
}
transport::SwapHttpsResult SwapProductionPorts::create(const truemarkets::SwapRestRequest& request,const SwapPrepareRead& read){
    transport::SwapHttpsResult refused;refused.status=transport::SwapHttpsStatus::local_refusal;
    trace_.stage=SwapCreateStage::latched;
    if(created_ || admission_.recovery_only)return refused;created_=true;
    trace_.stage=SwapCreateStage::journal;
    if(preparation_.status()!=SwapPrepareStoreCode::none || preparation_.latest().binding!=binding_ || preparation_.latest().phase!=SwapPreparePhase::create_intent)return refused;
    trace_.stage=SwapCreateStage::admission;
    const auto fresh=swap_prepare_admission(admission_,read);if(!fresh)return refused;
    trace_.stage=SwapCreateStage::balance;
    if(!read_swap_reviewed_balance(credentials_,clock_,*fresh,timeout_))return refused;
    trace_.stage=SwapCreateStage::bearer;
    const auto bearer=credentials_.bearer(clock_.now_ms());if(bearer.empty())return refused;
    SwapHttpsPreparePort port(preparation_.latest().operation.input,std::string(bearer),static_cast<long>(timeout_));
    const auto result=port.create(request,read);trace_=swap_create_trace(result);return result;
}
void SwapProductionPorts::execute(const SwapSubmissionIdentity& id,std::span<const std::byte> bytes){
    if(admission_.recovery_only || !swap_admission_funded(admission_,credentials_.binding(),clock_.now_ms()))return;
    if(executed_)return;executed_=true;
    if(submission_.status()!=SwapSubmissionCode::none || submission_.latest().phase!=SwapSubmissionPhase::execute_intent || submission_.latest().identity!=id ||
        preparation_.status()!=SwapPrepareStoreCode::none || preparation_.latest().binding!=binding_ || preparation_.latest().phase!=SwapPreparePhase::prepared ||
        id.effect!=preparation_.latest().operation.effect || id.payload!=preparation_.latest().prepared.payload_digest || bytes.empty() || bytes.size()>1024)return;
    truemarkets::SwapRestRequest request;const auto path="/v1/gateway/orders/"+std::string(preparation_.latest().prepared.handle.view())+"/execute";
    if(path.size()>request.path.size())return;request.path_size=path.size();std::copy(path.begin(),path.end(),request.path.begin());
    request.body_size=bytes.size();std::copy(bytes.begin(),bytes.end(),reinterpret_cast<std::byte*>(request.body.data()));
    SwapRuntimeExecutionHttps port(credentials_,clock_,id,request,timeout_);port.execute(id,bytes);
}
std::optional<VerifiedSwapSettlement> SwapProductionPorts::reconcile(const swap::Pending& pending){
    if(++reconcile_attempts_>64)return {};
    const auto bearer=credentials_.bearer(clock_.now_ms());if(bearer.empty())return {};
    const auto& session=credentials_.binding().account_session;
    SwapOrderSessionIdentity identity{session.account_and_assets_digest,session.session_evidence_digest};
    SwapHttpsOrderGet get(identity,std::string(bearer),preparation_.latest().prepared.handle,timeout_);
    struct Clock final:SwapOrderReadClock{SwapFinalizedReadClock& source;explicit Clock(SwapFinalizedReadClock&s):source(s){}std::uint64_t now_ms()noexcept override{return source.now_ms();}} clock(clock_);
    SwapOrderStatusReader order(submission_,preparation_,get,clock,{binding_,identity,timeout_});
    if(order.read().status!=SwapOrderReadStatus::terminal_needs_finality)return {};
    const auto payload=preparation_.latest().prepared.payload_view();
    const auto lookup=prefix(std::as_bytes(std::span(payload.data(),payload.size())));if(!lookup)return {};
    const auto evidence=truemarkets::jupiter_lookup_evidence(*lookup);
    SwapRuntimeFinalizedHttps rpc(timeout_);SwapFinalizedReader reader(rpc,clock_,rpc.provider_identity(),timeout_);
    return admit_retained_swap_settlement(preparation_,submission_,binding_,pending.intent,reader,1,1,
        organization_.view(),signer_.view(),std::span(&evidence,1),[&]{truemarkets::SolanaAccountKey owner{};
            for(std::size_t n=0;n<owner.size();++n)owner[n]=std::to_integer<std::uint8_t>(credentials_.binding().owner_wallet[n]);return owner;}(),failure_policy_,
            {swap::BaseAtoms{credentials_.binding().max_buy_output_deduction_lamports}}).verified;
}
SwapProductionGateway::SwapProductionGateway(GatewayLink& link,SwapReconcileStore& recovery,SwapPrepareStore& prepared,SwapSubmissionStore& submitted,
    SwapSubmissionStore& signing_requests,SwapGatewayCredentials& credentials,SwapFinalizedReadClock& clock,const SwapRuntimeProfile& profile,
    SwapGatewayRuntimeConfig config,SwapAccountStartupAdmission admission)
    :client_(credentials.signer_client()),
    ports_(credentials,prepared,submitted,config.preparation,clock,profile.organization,profile.signer_id,config.timeout,admission,profile.failure_policy),
    signing_(prepared,signing_requests,*client_,{config.preparation,credentials.binding().signer_public_key},ports_),
    runtime_(link,recovery,prepared,submitted,ports_,signing_,ports_,ports_,config){
    if(!swap_account_history_identity(admission.history,admission.review,credentials.binding()) ||
        config.recovery_only!=admission.recovery_only || profile.account_review_digest!=admission.review.document_digest ||
        !swap_account_review_matches(admission.review,credentials.binding(),profile.user_id.view(),admission.state) ||
        config.caller!=credentials.binding().gateway_caller || swap_runtime_identity_digest(profile.gateway.binding)!=swap_runtime_identity_digest(credentials.binding()))
        throw std::invalid_argument("production gateway caller/profile mismatch");
}
}
