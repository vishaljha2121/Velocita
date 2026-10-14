#include "v2_swap_signer_test_support.hpp"
#include "velocita/v2/swap_signing_connection.hpp"

namespace {
using namespace swap_signer_test;
using namespace velocita::v2::swap_signer;
struct FixedLookup final : SwapLookupSource {
    std::optional<truemarkets::JupiterLookupPrefix> value{};
    unsigned reads{};
    FixedLookup(){truemarkets::JupiterLookupPrefix p;p.table[0]=7;p.count=2;p.addresses[0][0]=1;p.addresses[1][0]=2;value=p;}
    std::optional<truemarkets::JupiterLookupPrefix> prefix(std::span<const std::byte>) override {++reads;return value;}
};
struct Setup {
    FixedLookup lookup;
    Rig signer;
    Fault journal_fault;
    SwapSigningConnectionConfig config;
    std::unique_ptr<SwapPrepareStore> preparation;
    std::unique_ptr<SwapSubmissionStore> journal;
    Server server;
    std::unique_ptr<Client> client;
    std::unique_ptr<SwapSigningConnection> connection;
    Setup(){
        config={{swap_test::opening().binding,digest(30),digest(80)},signer.key.public_key};
        preparation=std::make_unique<SwapPrepareStore>(signer.scratch.base/"prepared",config.preparation);
        require(preparation->create({config.preparation,1,digest(91),EffectId{0}})==SwapPrepareStoreCode::none,"prepared bootstrap");
        truemarkets::SwapRestRequest request;
        require(truemarkets::encode_swap_rest_create({Side::buy,1'000'000},request)==truemarkets::SwapRestCode::encoded,"create fixture");
        require(preparation->begin({EffectId{1},digest(92),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),
            {Side::buy,1'000'000}},9,digest(93))==SwapPrepareStoreCode::none,"prepared intent");
        truemarkets::SwapRestPrepared p;p.handle=*truemarkets::tm_text(handle);p.payload_size=body.size();
        std::copy(body.begin(),body.end(),p.payload.begin());p.payload_digest=sha256_digest(body_bytes());
        p.quoted_input=1'000'000;p.quoted_output=9'100'000;p.quoted_fee=2'000;
        require(preparation->complete(p,digest(94))==SwapPrepareStoreCode::none,"prepared response retained");
        journal=std::make_unique<SwapSubmissionStore>(signer.scratch.base/"gateway-sign-request",swap_signing_connection_binding(config),SwapSubmissionFaultHook{Fault::proceed,&journal_fault});
        require(journal->create({swap_signing_connection_binding(config),1,digest(95),EffectId{0}})==SwapSubmissionCode::none,"request bootstrap");
        const auto path=signer.scratch.base/"connection.sock";require(server.listen(path,::geteuid()),"protected fixture listens");
        client=std::make_unique<Client>(path,::geteuid(),signer.key.public_key,500);
        connection=std::make_unique<SwapSigningConnection>(*preparation,*journal,*client,config,lookup);
    }
    pid_t serve(bool sign,bool need_reply=true){const auto child=::fork();require(child>=0,"fork signer");
        if(child==0){const bool ok=server.serve_once(*signer.session,1000);::_exit((ok || !need_reply) && signer.key.protected_key->signature_attempts()==(sign?1U:0U)?0:1);}return child;}
    void joined(pid_t child){int status{};require(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"protected process performed expected operation");}
    void reopen(){
        connection.reset();journal.reset();
        journal=std::make_unique<SwapSubmissionStore>(signer.scratch.base/"gateway-sign-request",swap_signing_connection_binding(config));
        require(journal->recover()==SwapSubmissionCode::none,"gateway request recovers");
        client=std::make_unique<Client>(signer.scratch.base/"connection.sock",::geteuid(),signer.key.public_key,500);
        connection=std::make_unique<SwapSigningConnection>(*preparation,*journal,*client,config,lookup);
        signer.session.reset();signer.store.reset();
        signer.store=std::make_unique<SwapSubmissionStore>(signer.scratch.base/"signer-replay",signer.binding);
        require(signer.store->recover()==SwapSubmissionCode::none,"signer result recovers");
        signer.session=std::make_unique<SwapSignerSession>(signer.link,*signer.store,*signer.key.protected_key,swap_signer_test::config(),signer.clock.interface());
    }
};
void durable_sign_and_restart(){
    Setup s;const auto child=s.serve(true);
    require(s.connection->sign(1,10,20100),"durable request reaches protected signer");s.joined(child);
    const auto* result=s.connection->execution();require(result,"signed execution available");
    const auto original=*result;
    require(result->identity.payload==s.preparation->latest().prepared.payload_digest &&
        result->identity.execution_request==sha256_digest(std::as_bytes(std::span(result->request.body.data(),result->request.body_size))) &&
        result->request.path_view()=="/v1/gateway/orders/synthetic-order-1/execute","exact signed bytes feed canonical execute identity");
    require(!s.connection->sign(2,11,20101),"same process never signs twice");
    s.reopen();require(s.connection->status()==SwapSigningConnectionStatus::unresolved && !s.connection->execution(),"restart needs protected retrieval");
    require(!s.connection->sign(2,11,20101),"restart cannot substitute new sequence index or expiry");
    const auto recovery=s.serve(false);require(s.connection->retrieve(),"restart retrieves original durable request");s.joined(recovery);
    require(s.connection->execution()->identity==original.identity && s.connection->execution()->request.body_view()==original.request.body_view(),
        "retrieved signature reconstructs identical execution request");
}
void rejected_reply_is_not_resigned(){
    Setup s;
    s.connection.reset();auto wrong=s.signer.key.public_key;wrong.back()^=std::byte{1};
    s.client=std::make_unique<Client>(s.signer.scratch.base/"connection.sock",::geteuid(),wrong,500);
    s.connection=std::make_unique<SwapSigningConnection>(*s.preparation,*s.journal,*s.client,s.config,s.lookup);
    const auto first=s.serve(true);
    require(!s.connection->sign(1,10,20100) && s.journal->latest().phase==SwapSubmissionPhase::execute_intent,
        "rejected IPC reply retains durable signing request");s.joined(first);
    s.reopen();require(!s.connection->sign(1,10,20100),"rejected reply cannot resend sign");
    const auto recovery=s.serve(false);require(s.connection->retrieve(),"rejected reply retrieves instead of signing");s.joined(recovery);
}
void crash_before_ipc(){
    Setup s;
    const auto child=::fork();require(child>=0,"gateway crash child starts");
    if(child==0){s.journal_fault.crash=true;s.journal_fault.crash_stage=SwapSubmissionStage::before_publish;
        (void)s.connection->sign(1,10,20100);::_exit(1);}
    int status{};require(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==73,
        "gateway crashes after durable request before IPC");
    s.reopen();require(s.connection->status()==SwapSigningConnectionStatus::unresolved &&
        !s.connection->sign(1,10,20100),"durable pre-IPC crash never grants a new sign attempt");
    const auto recovery=s.serve(false);require(!s.connection->retrieve() && !s.connection->execution(),
        "absent signer result remains unresolved after pre-IPC crash");s.joined(recovery);
}
void signer_result_storage_failure(){
    Setup s;s.signer.fault.writes=0;s.signer.fault.fail_result=true;const auto first=s.serve(true);
    require(!s.connection->sign(1,10,20100) && !s.connection->execution(),
        "signer result fsync failure cannot publish execute request");s.joined(first);
    s.connection.reset();s.journal.reset();
    s.journal=std::make_unique<SwapSubmissionStore>(s.signer.scratch.base/"gateway-sign-request",swap_signing_connection_binding(s.config));
    require(s.journal->recover()==SwapSubmissionCode::none,"uncertain gateway request recovers");
    s.signer.session.reset();s.signer.store.reset();
    s.signer.store=std::make_unique<SwapSubmissionStore>(s.signer.scratch.base/"signer-replay",s.signer.binding);
    require(s.signer.store->recover()==SwapSubmissionCode::temporary_present,"torn signer result quarantines recovery");
    s.signer.session=std::make_unique<SwapSignerSession>(s.signer.link,*s.signer.store,*s.signer.key.protected_key,swap_signer_test::config(),s.signer.clock.interface());
    s.connection=std::make_unique<SwapSigningConnection>(*s.preparation,*s.journal,*s.client,s.config,s.lookup);
    require(!s.connection->sign(1,10,20100),"failed signer persistence never reissues sign");
    const auto recovery=s.serve(false);require(!s.connection->retrieve() && !s.connection->execution(),
        "quarantined signer cannot recover an executable stamp");s.joined(recovery);
}
void expired_recovery(){
    Setup s;const auto first=s.serve(true);require(s.connection->sign(1,10,20100),"expiry fixture initially signs");s.joined(first);
    s.reopen();s.signer.clock.event=20100;const auto recovery=s.serve(false);
    require(!s.connection->retrieve() && !s.connection->execution(),"expired durable stamp never becomes an execute request");s.joined(recovery);
    require(!s.connection->sign(2,11,20101),"expiry cannot grant a replacement signing request");
}
void missing_request_recovery(){
    Setup s;SwapSubmissionStore missing(s.signer.scratch.base/"never-created-request",swap_signing_connection_binding(s.config));
    require(missing.recover()==SwapSubmissionCode::missing,"missing gateway journal is not a fresh baseline");
    SwapSigningConnection connection(*s.preparation,missing,*s.client,s.config,s.lookup);
    require(connection.status()==SwapSigningConnectionStatus::closed && !connection.sign(1,10,20100) &&
        !connection.retrieve() && !connection.execution(),"missing request journal cannot authorize signing or retrieval");
}
void bindings_and_bounds(){
    Setup s;auto changed=s.config;changed.signer_public_key.back()^=std::byte{1};
    SwapSigningConnection wrong(*s.preparation,*s.journal,*s.client,changed,s.lookup);
    require(wrong.status()==SwapSigningConnectionStatus::closed,"different signer pin cannot reuse request journal");
    require(!s.connection->sign(0,10,20100) && !s.connection->sign(1,9,20100) && !s.connection->sign(1,10,0) &&
        s.journal->latest().phase==SwapSubmissionPhase::idle,"invalid signing metadata cannot journal or contact signer");
}
void durability_before_ipc(){
    Setup s;s.journal_fault.armed=true;const auto child=s.serve(false,false);
    require(!s.connection->sign(1,10,20100) && !s.connection->execution(),"gateway fsync failure prevents signing IPC");s.joined(child);
}
void lookup_prefix_bound_to_request(){
    Setup s;s.lookup.value.reset();
    require(!s.connection->sign(1,10,20100) && s.journal->latest().phase==SwapSubmissionPhase::idle && s.lookup.reads==1,
        "unavailable lookup prefix refuses before durable request or signer contact");
    Setup t;const auto first=t.serve(true);require(t.connection->sign(1,10,20100),"prefix request signs");t.joined(first);
    const auto saved=t.journal->latest().identity;
    t.reopen();t.lookup.value->addresses[1][0]=3;
    require(!t.connection->retrieve() && !t.connection->execution(),"changed prefix cannot retrieve original request");
    t.lookup.value->addresses[1][0]=2;const auto recovery=t.serve(false);
    require(t.connection->retrieve() && t.connection->execution() && t.journal->latest().identity==saved,"same append-only prefix retrieves original request");
    t.joined(recovery);
}
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()> cases[]{{"restart",durable_sign_and_restart},{"reply",rejected_reply_is_not_resigned},{"crash_before_ipc",crash_before_ipc},{"signer_storage",signer_result_storage_failure},{"expired_recovery",expired_recovery},{"missing_recovery",missing_request_recovery},{"bounds",bindings_and_bounds},{"durability",durability_before_ipc},{"lookup",lookup_prefix_bound_to_request}};
    unsigned ran{},failed{};for(auto[name,fn]:cases){if(argc==2&&std::string_view(argv[1])!=name)continue;++ran;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return ran&&!failed?0:1;}
