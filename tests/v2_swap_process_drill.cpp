// Process drills: real gateway and signer processes against three voters, with fixture venue and settlement.
#include <cstring>
#define main existing_gateway_suite_main
#include "v2_gateway_node_tests.cpp"
#undef main
#include "velocita/v2/swap_gateway_runtime.hpp"
#include "v2_swap_signer_test_support.hpp"
#include <csignal>
#include <cerrno>
#include <fstream>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
void journal_line(const std::filesystem::path& root,std::string_view line){
    const int fd=::open((root/"venue.log").c_str(),O_WRONLY|O_CREAT|O_APPEND,0600);
    require(fd>=0,"transcript opens");const std::string bytes=std::string(line)+"\n";
    require(::write(fd,bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()) && ::fsync(fd)==0,"transcript durable");::close(fd);
}
unsigned count_line(const std::filesystem::path& root,std::string_view needle){
    std::ifstream f(root/"venue.log");std::string line;unsigned n=0;while(std::getline(f,line))if(line==needle)++n;return n;
}
[[noreturn]] void die(){::kill(::getpid(),SIGKILL);::_exit(99);}
struct Wire {
    int fd;Bytes incoming,outgoing;bool observe_child_exit{},peer_closed{};
    explicit Wire(int f,bool observe_exit=false):fd(f),observe_child_exit(observe_exit){require(::fcntl(fd,F_SETFL,::fcntl(fd,F_GETFL)|O_NONBLOCK)==0,"bridge nonblocking");}
    void send(std::int32_t node,std::span<const std::byte> body){
        if(peer_closed)return;
        require(body.size()<=kMaxAuthEnvelopeBytes,"bridge frame bounded");
        const auto size=static_cast<std::uint32_t>(body.size());
        auto add=[&](const auto& v){const auto b=std::as_bytes(std::span(&v,1));outgoing.insert(outgoing.end(),b.begin(),b.end());};
        add(size);add(node);outgoing.insert(outgoing.end(),body.begin(),body.end());
        require(outgoing.size()<8*1024*1024,"bridge outbound bounded");flush();
    }
    void flush(){while(!outgoing.empty()){
        const auto n=::write(fd,outgoing.data(),outgoing.size());
        if(n>0)outgoing.erase(outgoing.begin(),outgoing.begin()+n);
        else if(n<0 && errno==EINTR)continue;
        else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))return;
        else if(n<0 && observe_child_exit && (errno==EPIPE || errno==ECONNRESET)){
            peer_closed=true;outgoing.clear();return;
        }else throw std::runtime_error("bridge write closed errno="+std::to_string(errno));
    }}
    std::optional<std::pair<std::int32_t,Bytes>> receive(){
        std::array<std::byte,16384> buf;
        for(;!peer_closed;){const auto n=::read(fd,buf.data(),buf.size());if(n>0){incoming.insert(incoming.end(),buf.begin(),buf.begin()+n);require(incoming.size()<8*1024*1024,"bridge inbound bounded");}
            else if(n<0 && errno==EINTR)continue;else if(n==0){if(observe_child_exit){peer_closed=true;outgoing.clear();}break;}
            else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))break;
            else if(n<0 && errno==ECONNRESET && observe_child_exit){peer_closed=true;outgoing.clear();break;}
            else throw std::runtime_error("bridge read errno="+std::to_string(errno));}
        if(incoming.size()<8)return {};std::uint32_t size;std::int32_t node;
        std::memcpy(&size,incoming.data(),4);std::memcpy(&node,incoming.data()+4,4);require(size<=kMaxAuthEnvelopeBytes,"bridge received bound");
        if(incoming.size()<8+size)return {};Bytes body(incoming.begin()+8,incoming.begin()+8+size);incoming.erase(incoming.begin(),incoming.begin()+8+size);return std::pair{node,std::move(body)};
    }
};
void dead_peer_bridge_oracle(){
    int pair[2];require(::socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"reset bridge fixture");
    Wire parent(pair[0],true);const char unread='x';
    require(::write(pair[0],&unread,1)==1,"reset fixture unread byte");::close(pair[1]);
    require(!parent.receive() && parent.peer_closed,"dead child reset is observed before waitpid");
    parent.send(1,{});require(parent.outgoing.empty(),"dead child receives no queued reply");::close(pair[0]);
    require(::socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"strict bridge fixture");
    Wire worker(pair[0]);::close(pair[1]);bool refused=false;
    try{worker.send(1,{});}catch(const std::runtime_error&){refused=true;}
    require(refused,"worker transport failure must remain fatal");::close(pair[0]);
}
struct ProcessClient final:ClientTransport {
    Wire wire;explicit ProcessClient(int fd):wire(fd){}
    bool offer_ingress(std::int32_t node,std::span<const std::byte> frame)override{wire.send(node,frame);return true;}
    std::optional<std::size_t> receive_response(std::span<std::byte> out)override{
        wire.flush();auto r=wire.receive();if(!r)return {};require(r->first==0 && r->second.size()<=out.size(),"worker response framing");
        std::copy(r->second.begin(),r->second.end(),out.begin());return r->second.size();
    }
};
struct Ports final:SwapPreparePort,SwapExecutionPort,SwapGatewaySettlementPort {
    std::filesystem::path root;std::string stage;SwapPrepareStore& prepared;SwapSubmissionStore& submitted;
    Ports(std::filesystem::path r,std::string s,SwapPrepareStore& p,SwapSubmissionStore& u):root(std::move(r)),stage(std::move(s)),prepared(p),submitted(u){}
    transport::SwapHttpsResult create(const truemarkets::SwapRestRequest&,const SwapPrepareRead&)override{
        require(prepared.latest().phase==SwapPreparePhase::create_intent,"create fenced");journal_line(root,"create");
        constexpr std::string_view body=R"({"order_id":"synthetic-tm-order","status":"initialized","payloads":[{"digest":"a71e361bce0c479f89741f87a0248cf21cae63ee745a5d6623d0e5760aabebcc","payload":"VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n"}],"quote":{"base_asset":"So11111111111111111111111111111111111111112","quote_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","fee_asset":"EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v","qty":"1","qty_out":"0.009191912","fee":"0.002"}})";
        transport::SwapHttpsResult r;r.status=transport::SwapHttpsStatus::complete;r.exposure=transport::SwapHttpsExposure::response_received;r.http_status=201;
        r.body_size=body.size();std::copy(body.begin(),body.end(),r.body.begin());return r;
    }
    void execute(const SwapSubmissionIdentity& id,std::span<const std::byte> bytes)override{
        require(submitted.latest().phase==SwapSubmissionPhase::execute_intent && submitted.latest().identity==id && sha256_digest(bytes)==id.execution_request,"execute durable fence");
        if(stage=="before-send")die();journal_line(root,"execute");
        if(stage=="accepted")die();throw std::runtime_error("fixture accepted response lost");
    }
    std::optional<VerifiedSwapSettlement> reconcile(const swap::Pending& p)override{
        journal_line(root,"read-reconcile");if(stage=="unknown")return {};
        SwapSubmissionTransaction signature{};for(std::size_t n=0;n<signature.size();++n)signature[n]=static_cast<std::byte>(n);
        require(submitted.observe_transaction(submitted.latest().identity,signature)==SwapSubmissionCode::none,"observed fixture transaction persists");
        if(stage=="receipt-before-commit")die();
        swap::Settlement s;s.effect=p.intent.effect;s.custody=p.intent.binding.custody;s.payload=p.payload;s.transaction=*solana_signature_transaction_digest(signature);
        s.evidence=digest(210);s.finalized=true;s.outcome=swap::Outcome::succeeded;s.base_credit=swap::BaseAtoms{9'000'000};s.quote_debit=swap::QuoteAtoms{1'000'000};s.fee=swap::QuoteAtoms{2'000};
        return settlement_test_fixture::make_verified(s);
    }
};
std::array<std::byte,33> public_key(const std::filesystem::path& root){
    std::array<std::byte,33> value{};std::ifstream f(root/"public-key",std::ios::binary);f.read(reinterpret_cast<char*>(value.data()),value.size());require(f.gcount()==33,"fixture public key read");return value;
}
struct CrashSignerResult {
    std::filesystem::path root;SwapSignerKey* key{};unsigned writes{};bool armed{};
    static bool proceed(SwapSubmissionStage stage,void* raw)noexcept{
        auto& self=*static_cast<CrashSignerResult*>(raw);if(!self.armed)return true;
        if(stage==SwapSubmissionStage::partial_write)++self.writes;
        if(stage==SwapSubmissionStage::temporary_sync && self.writes==2 && self.key->signature_attempts()==1){
            const int fd=::open((self.root/"venue.log").c_str(),O_WRONLY|O_APPEND);constexpr char line[]="signature\n";
            if(fd<0 || ::write(fd,line,sizeof(line)-1)!=sizeof(line)-1 || ::fsync(fd)!=0)::_exit(125);
            ::close(fd);::kill(::getpid(),SIGKILL);
        }
        return true;
    }
};
int signer_worker(const std::filesystem::path& root,unsigned epoch,int fd){
    ProcessClient client(fd);GatewayLink link(epoch,process_incarnation(static_cast<std::uint8_t>(0xA0+epoch)),gateway_routes({1,2,3},caller_seed(kSignerKey),kSignerKey),client);
    require(link.request_state(steady_ms(),5000),"signer independent startup read");LinkOutcome out;const auto until=steady_ms()+10000;
    do{out=link.pump(steady_ms());if(out.status==LinkStatus::done)break;::usleep(500);}while(steady_ms()<until);require(out.status==LinkStatus::done,"signer bootstrap authenticates");
    auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    const auto pub=public_key(root);const auto keyfd=::open((root/"fixture-key.pem").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(keyfd>=0,"fixture signer key fd");
    auto key=SwapSignerKey::load_inherited_fd(keyfd,pub);::close(keyfd);require(bool(key),"protected generated key loaded");
    SwapSignerConfig config{opening.binding,out.reading.authority_domain,digest(221),CallerId{kSigner},1,5000};
    const auto binding=swap_signer_journal_binding(config,pub);CrashSignerResult crash{root,key.get()};SwapSubmissionStore store(root/"signer-result",binding,{CrashSignerResult::proceed,&crash});
    if(epoch==1)require(store.create({binding,out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"signer journal created");
    else {const auto recovered=store.recover();
        if(std::filesystem::exists(root/"expect-quarantined")){require(recovered==SwapSubmissionCode::temporary_present,"torn signer result stays quarantined");journal_line(root,"signer-quarantined");return 0;}
        require(recovered==SwapSubmissionCode::none,"signer process recovers result journal");}
    crash.armed=std::filesystem::exists(root/"crash-signer-result");
    const SwapSignerClock clock{[](void*)noexcept{return steady_ms();},[](void*)noexcept{return std::uint64_t{111};},nullptr};
    SwapSignerSession session(link,store,*key,config,clock);swap_signer::Server server;
    require(server.listen(root/"signer.sock",::geteuid()),"signer protected Unix socket listens");
    while(!std::filesystem::exists(root/"stop-signer")){
        const auto before=key->signature_attempts();(void)server.serve_once(session,2000);
        if(key->signature_attempts()>before)journal_line(root,"signature");
    }
    return 0;
}
struct CrashSigningRequest {
    bool armed{};
    static bool proceed(SwapSubmissionStage stage,void* p)noexcept{
        if(static_cast<CrashSigningRequest*>(p)->armed && stage==SwapSubmissionStage::before_publish)::kill(::getpid(),SIGKILL);
        return true;
    }
};
struct CrashSigningPort final:SwapGatewaySigningPort {
    SwapDurableSigningPort& real;std::string_view stage;
    CrashSigningPort(SwapDurableSigningPort& r,std::string_view s):real(r),stage(s){}
    std::optional<SwapSignedExecution> sign_or_retrieve(std::uint64_t seq,std::int64_t index,std::uint64_t expiry)override{
        auto result=real.sign_or_retrieve(seq,index,expiry);if(result && stage=="signed-before-attempt")die();return result;
    }
};
int worker(const std::filesystem::path& root,std::string stage,unsigned incarnation,int fd){
    ProcessClient client(fd);GatewayLink link(incarnation+1,process_incarnation(static_cast<std::uint8_t>(0x90+incarnation)),gateway_routes({1,2,3},caller_seed(kGatewayKey)),client);
    require(link.request_state(steady_ms(),5000),"worker startup read");LinkOutcome outcome;
    const auto until=steady_ms()+10000;do{outcome=link.pump(steady_ms());if(outcome.status==LinkStatus::done)break;::usleep(500);}while(steady_ms()<until);
    require(outcome.status==LinkStatus::done,"worker authenticated bootstrap");auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    const SwapPrepareBinding binding{opening.binding,outcome.reading.authority_domain,fee_inclusive_prepare_mapping_identity()};
    SwapPrepareStore prepared(root/"prepared",binding);SwapSubmissionStore submitted(root/"submission",{opening.binding.incarnation,binding.authority_domain});
    SwapResultWatermark watermark(opening.binding.incarnation);SwapReconcileStore recovery(root/"recovery",{opening.binding.incarnation,binding.authority_domain},watermark);
    require(prepared.recover()==SwapPrepareStoreCode::none && submitted.recover()==SwapSubmissionCode::none && recovery.recover()==SwapReconcileStoreCode::none,"process restart journals recover");
    const auto pub=public_key(root);SwapSigningConnectionConfig signing_config{binding,pub};
    CrashSigningRequest crash;SwapSubmissionStore sign_requests(root/"sign-requests",swap_signing_connection_binding(signing_config),{CrashSigningRequest::proceed,&crash});
    if(incarnation==1)require(sign_requests.create({swap_signing_connection_binding(signing_config),outcome.reading.applied_index,outcome.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"signing request journal created");
    else require(sign_requests.recover()==SwapSubmissionCode::none,"signing request journal recovered");
    crash.armed=stage=="sign-request";
    const auto wait_until=steady_ms()+5000;while(!std::filesystem::exists(root/"signer.sock") && steady_ms()<wait_until)::usleep(500);
    swap_signer::Client signer_client(root/"signer.sock",::geteuid(),pub,10000);
    struct DrillLookup final:SwapLookupSource{
        std::optional<truemarkets::JupiterLookupPrefix> prefix(std::span<const std::byte>)override{
            truemarkets::JupiterLookupPrefix p;p.table[0]=7;p.count=1;p.addresses[0][0]=1;return p;}
    } lookup;
    SwapDurableSigningPort signing(prepared,sign_requests,signer_client,signing_config,lookup);
    CrashSigningPort signing_boundary(signing,stage);
    Ports ports(root,stage,prepared,submitted);SwapGatewayRuntimeConfig config{binding,CallerId{kGateway},incarnation*1000,incarnation*1000+900,5000,1};config.recovery_only=incarnation>1;
    SwapGatewayRuntime runtime(link,recovery,prepared,submitted,ports,signing_boundary,ports,ports,config);
    const auto deadline=steady_ms()+25000;while(!runtime.stopped() && steady_ms()<deadline){runtime.step(steady_ms(),110+incarnation);::usleep(500);}
    if(stage=="signer-result-crash")require(runtime.status()==SwapGatewayRuntimeStatus::blocked && runtime.cause()==SwapGatewayRuntimeCause::signer_refused,"signer death cannot publish executable request");
    else if(stage=="sign-fenced")require(runtime.status()==SwapGatewayRuntimeStatus::blocked && runtime.cause()==SwapGatewayRuntimeCause::state,"recovery-only restart blocks pre-Attempt execution");
    else if(stage=="unknown")require(runtime.status()==SwapGatewayRuntimeStatus::blocked && runtime.cause()==SwapGatewayRuntimeCause::settlement_unproven,"unknown restart blocks status="+std::to_string(int(runtime.status()))+" cause="+std::to_string(int(runtime.cause()))+" phase="+std::to_string(int(link.state().spot_swap.pending.phase))+" seen="+std::to_string(recovery.latest().result.index)+" prepared="+std::to_string(prepared.latest().committed_index)+" submitted="+std::to_string(submitted.latest().committed_index));
    else require(runtime.status()==SwapGatewayRuntimeStatus::complete && submitted.latest().phase==SwapSubmissionPhase::resolved,"finalized restart resolves exactly once status="+std::to_string(int(runtime.status()))+" cause="+std::to_string(int(runtime.cause())));
    return 0;
}
struct ChildGuard{pid_t pid;~ChildGuard(){if(pid>0){::kill(pid,SIGKILL);::waitpid(pid,nullptr,0);}}};
int run_worker(Cluster& c,const std::filesystem::path& executable,const std::filesystem::path& root,std::string stage,unsigned epoch,bool kill_after_commit=false){
    std::filesystem::remove(root/"stop-signer");
    int signer_pair[2];require(::socketpair(AF_UNIX,SOCK_STREAM,0,signer_pair)==0,"signer bridge socketpair");
    const auto signer_fd=std::to_string(signer_pair[1]),signer_epoch=std::to_string(epoch);
    const auto signer_pid=::fork();require(signer_pid>=0,"independent signer fork");
    if(signer_pid==0){::close(signer_pair[0]);::execl(executable.c_str(),executable.c_str(),"--signer",root.c_str(),signer_epoch.c_str(),signer_fd.c_str(),nullptr);::_exit(126);}
    ::close(signer_pair[1]);ChildGuard signer_guard{signer_pid};Wire signer_wire(signer_pair[0],true);
    int pair[2];require(::socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"worker socketpair");
    const auto fd=std::to_string(pair[1]),ep=std::to_string(epoch);const auto child=::fork();require(child>=0,"worker fork");
    if(child==0){::close(pair[0]);::execl(executable.c_str(),executable.c_str(),"--worker",root.c_str(),stage.c_str(),ep.c_str(),fd.c_str(),nullptr);::_exit(126);}
    ::close(pair[1]);ChildGuard guard{child};Wire wire(pair[0],true);bool killed=false;int status{};const auto deadline=steady_ms()+30000;
    for(;;){const auto reaped=::waitpid(child,&status,WNOHANG);if(reaped==child){guard.pid=0;break;}require(reaped==0 && steady_ms()<deadline,"worker bounded lifetime");
        while(auto packet=wire.receive()){require(std::find(c.voters.begin(),c.voters.end(),packet->first)!=c.voters.end(),"worker configured node");if(c.nodes.contains(packet->first))c.network.at(packet->first).ingress_inbox.push_back(std::move(packet->second));}
        while(auto packet=signer_wire.receive()){require(std::find(c.voters.begin(),c.voters.end(),packet->first)!=c.voters.end(),"signer configured node");if(c.nodes.contains(packet->first))c.network.at(packet->first).ingress_inbox.push_back(std::move(packet->second));}
        c.run(1);
        auto& signer_queue=c.network.responses[kSignerKey];while(!signer_queue.empty()){signer_wire.send(0,signer_queue.front());signer_queue.pop_front();}signer_wire.flush();
        if(kill_after_commit && !killed && c.state_of(c.elect()).spot_swap.settlement_count==1){require(::kill(child,SIGKILL)==0,"kill gateway after committed settlement");killed=true;}
        if(!killed){auto& queue=c.network.responses[kGatewayKey];while(!queue.empty()){wire.send(0,queue.front());queue.pop_front();}wire.flush();}
    }
    ::close(pair[0]);c.network.responses[kGatewayKey].clear();
    {std::ofstream stop(root/"stop-signer");stop<<"stop";}
    int signer_status{};const auto sign_start=steady_ms(),sign_end=sign_start+10000;
    unsigned shutdown_turns{};
    while(::waitpid(signer_pid,&signer_status,WNOHANG)==0){
        require(steady_ms()<sign_end,"signer bounded shutdown");
        while(auto packet=signer_wire.receive()){
            require(std::find(c.voters.begin(),c.voters.end(),packet->first)!=c.voters.end(),"shutdown signer configured node");
            if(c.nodes.contains(packet->first))c.network.at(packet->first).ingress_inbox.push_back(std::move(packet->second));
        }
        c.run(1);++shutdown_turns;
        auto& queue=c.network.responses[kSignerKey];
        while(!queue.empty()){signer_wire.send(0,queue.front());queue.pop_front();}
        signer_wire.flush();
    }
    c.network.responses[kGatewayKey].clear();
    {std::ofstream log(root/"shutdown.jsonl",std::ios::app);
        log<<"{\"stage\":\""<<stage<<"\",\"epoch\":"<<epoch<<",\"elapsed_ms\":"<<steady_ms()-sign_start
           <<",\"voter_turns\":"<<shutdown_turns<<"}\n";}
    signer_guard.pid=0;::close(signer_pair[0]);
    if(stage=="signer-result-crash"){
        require(WIFSIGNALED(signer_status) && WTERMSIG(signer_status)==SIGKILL,"actual signer SIGKILL after signature before result fsync");
        std::filesystem::remove(root/"signer.sock");std::filesystem::remove(root/"crash-signer-result");
        std::ofstream marker(root/"expect-quarantined");marker<<"quarantine";
    }else require(WIFEXITED(signer_status) && WEXITSTATUS(signer_status)==0,"independent signer exits normally");
    std::ofstream out(root/"processes.jsonl",std::ios::app);out<<"{\"role\":\"signer\",\"pid\":"<<signer_pid<<",\"epoch\":"<<epoch<<",\"signal\":"<<(WIFSIGNALED(signer_status)?WTERMSIG(signer_status):0)<<",\"exit\":"<<(WIFEXITED(signer_status)?WEXITSTATUS(signer_status):-1)<<"}\n";out<<"{\"role\":\"gateway\",\"pid\":"<<child<<",\"stage\":\""<<stage<<"\",\"epoch\":"<<epoch<<",\"signal\":"<<(WIFSIGNALED(status)?WTERMSIG(status):0)<<",\"exit\":"<<(WIFEXITED(status)?WEXITSTATUS(status):-1)<<"}\n";
    return status;
}
void require_killed(int status){require(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"actual gateway SIGKILL observed");}
void require_ok(int status){require(WIFEXITED(status) && WEXITSTATUS(status)==0,"fresh gateway process exits successfully");}
void assert_hold(Cluster& c){c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](auto& n){return n.second->applied_state().spot_swap.reserved_quote.value()==1000000;});},"hold replicated");
    for(auto& [id,n]:c.nodes){const auto& s=n->applied_state().spot_swap;require(s.quote.value()==5000000 && s.base.value()==0 && s.reserved_quote.value()==1000000 && s.settlement_count==0 && s.fees.value()==0 && s.purchases.value()==0,"unknown holds exact pretrade cash on every voter");}}
void record_result(Cluster& c,const std::filesystem::path& root,unsigned mode){
    const auto& s=c.state_of(c.elect()).spot_swap;std::ofstream f(root/"result.json");
    f<<"{\"case\":"<<mode<<",\"passed\":true,\"cash_atoms\":"<<s.quote.value()<<",\"base_atoms\":"<<s.base.value()
     <<",\"reserved_quote_atoms\":"<<s.reserved_quote.value()<<",\"fee_atoms\":"<<s.fees.value()<<",\"purchase_atoms\":"<<s.purchases.value()
     <<",\"settlements\":"<<static_cast<unsigned>(s.settlement_count)<<",\"pending_phase\":"<<int(s.pending.phase)<<",\"creates\":"<<count_line(root,"create")
     <<",\"signatures\":"<<count_line(root,"signature")<<",\"executes\":"<<count_line(root,"execute")<<"}\n";
}
void record_replicas(Cluster& c,const std::filesystem::path& root,std::string_view cut){
    std::ofstream f(root/"replicas.jsonl",std::ios::app);for(auto& [id,node]:c.nodes){const auto r=node->status().replica;
        f<<"{\"cut\":\""<<cut<<"\",\"node\":"<<id<<",\"term\":"<<r.term<<",\"role\":\""<<r.role<<"\",\"current\":"<<r.current_index
         <<",\"committed\":"<<r.commit_index<<",\"applied\":"<<r.applied_index<<",\"phase\":"<<int(node->applied_state().spot_swap.pending.phase)<<"}\n";}
}
void scenario(const std::filesystem::path& exe,const std::filesystem::path& root,unsigned mode){
    std::filesystem::create_directories(root);std::filesystem::permissions(root,std::filesystem::perms::owner_all);
    {swap_signer_test::TestKey key(root/"fixture-key.pem");std::ofstream f(root/"public-key",std::ios::binary);f.write(reinterpret_cast<const char*>(key.public_key.data()),key.public_key.size());}
    Cluster c(root/"voters",{1,2,3});Caller op(kOperatorKey,kOperator);c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"epoch");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"generation");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"catalog");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"gateway role");auto opening=swap_test::opening();opening.binding.incarnation=sha256_digest(kIncarnation);
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening}}).code==ApplyCode::applied,"funded");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"normal");
    {
        GatewayClient bootstrap;bootstrap.network=&c.network;GatewayLink link(1,process_incarnation(0x80),gateway_routes(c.voters,caller_seed(kGatewayKey)),bootstrap);
        require(link.request_state(steady_ms(),5000),"virgin bootstrap requested");const auto out=settle(c,link);require(out.status==LinkStatus::done,"virgin bootstrap authenticated");
        const SwapPrepareBinding binding{opening.binding,out.reading.authority_domain,fee_inclusive_prepare_mapping_identity()};
        SwapPrepareStore prepared(root/"prepared",binding);SwapSubmissionStore submitted(root/"submission",{opening.binding.incarnation,binding.authority_domain});
        SwapResultWatermark watermark(opening.binding.incarnation);SwapReconcileStore recovery(root/"recovery",{opening.binding.incarnation,binding.authority_domain},watermark);
        require(prepared.create({binding,out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapPrepareStoreCode::none,"virgin preparation bootstrap");
        require(submitted.create({{opening.binding.incarnation,binding.authority_domain},out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"virgin submission bootstrap");
        require(recovery.create({{opening.binding.incarnation,binding.authority_domain},out.reading.applied_index,out.reading.state_digest})==SwapReconcileStoreCode::none,"virgin watermark bootstrap");
    }
    auto intent=swap_test::buy(opening);intent.quote_input=swap::QuoteAtoms{998000};intent.max_quote_debit=swap::QuoteAtoms{1000000};
    require(c.commit(op,ApplySpotSwap{{swap::Role::strategy,100,intent}}).code==ApplyCode::applied,"reserved");
    if(mode==7){
        {std::ofstream flag(root/"crash-signer-result");flag<<"armed";}
        require_ok(run_worker(c,exe,root,"signer-result-crash",1));assert_hold(c);
        require_ok(run_worker(c,exe,root,"sign-fenced",2));assert_hold(c);
        require(count_line(root,"signature")==1 && count_line(root,"execute")==0 && count_line(root,"signer-quarantined")==1,"torn signer result never executes or signs again");
        std::cout<<"PASS F04 signer SIGKILL after signature: partial result quarantined, zero executes\n";record_result(c,root,mode);return;
    }
    const auto initial=mode==0?"before-send":mode==4?"sign-request":mode==5?"signed-before-attempt":"accepted";
    require_killed(run_worker(c,exe,root,initial,1));assert_hold(c);
    if(mode==4){require_ok(run_worker(c,exe,root,"sign-fenced",2));assert_hold(c);
        require(count_line(root,"create")==1 && count_line(root,"signature")==0 && count_line(root,"execute")==0,"durable pre-IPC request never permits a fresh signature");
        std::cout<<"PASS F03 durable signing request crash: zero signatures, zero sends, hold retained\n";record_result(c,root,mode);return;}
    if(mode==5){require(count_line(root,"signature")==1 && count_line(root,"execute")==0,"durable signature precedes Attempt and execution");
        require_ok(run_worker(c,exe,root,"sign-fenced",2));assert_hold(c);
        require(count_line(root,"signature")==1 && count_line(root,"execute")==0,"recovery-only pre-Attempt crash cannot execute retained signature");
        std::cout<<"PASS F05 durable signature crash: no execute permission after restart, hold retained\n";record_result(c,root,mode);return;}


    if(mode==6){record_replicas(c,root,"before-stop");const auto old=c.elect();c.stop(old);require(c.elect()!=old,"replacement quorum elects another leader");assert_hold(c);record_replicas(c,root,"after-election");}
    require_ok(run_worker(c,exe,root,"unknown",mode==5?3:2));assert_hold(c);
    require(count_line(root,"create")==1 && count_line(root,"signature")==1 && count_line(root,"execute")== (mode==0?0U:1U),"restart never repeats create sign or execute");
    if(mode==0){std::cout<<"PASS F06 before send: uncertain intent holds; zero sends\n";record_result(c,root,mode);return;}
    unsigned epoch=mode==5?4:3;
    if(mode==2){require_killed(run_worker(c,exe,root,"receipt-before-commit",epoch++));assert_hold(c);}
    if(mode==3)require_killed(run_worker(c,exe,root,"success",epoch++,true));
    require_ok(run_worker(c,exe,root,"success",epoch++));require_ok(run_worker(c,exe,root,"success",epoch));
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](auto& n){return n.second->applied_state().spot_swap.settlement_count==1;});},"all voters final");
    for(auto& [id,n]:c.nodes){const auto& s=n->applied_state().spot_swap;require(s.quote.value()==4000000 && s.base.value()==9000000 && s.reserved_quote.value()==0 && s.fees.value()==2000 && s.purchases.value()==1000000 && s.settlement_count==1 && s.pending.phase==swap::Phase::none,"exact final economics once on every voter");}
    require(count_line(root,"create")==1 && count_line(root,"signature")==1 && count_line(root,"execute")==1,"all process restarts preserve exactly one venue send");
    record_result(c,root,mode);
    std::cout<<"PASS F06/F08 mode "<<mode<<": one send; cash=4000000 base=9000000 fees=2000 purchases=1000000 hold=0 settlements=1\n";
}
}
int main(int argc,char** argv){try{::signal(SIGPIPE,SIG_IGN);g_swap=true;
    if(argc==5 && std::string_view(argv[1])=="--signer")return signer_worker(argv[2],std::stoul(argv[3]),std::stoi(argv[4]));
    if(argc==6 && std::string_view(argv[1])=="--worker")return worker(argv[2],argv[3],std::stoul(argv[4]),std::stoi(argv[5]));
    dead_peer_bridge_oracle();
    const auto root=argc==4 && std::string_view(argv[1])=="--case"?std::filesystem::path(argv[3]):argc==2?std::filesystem::path(argv[1]):std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/process-drill-20261013"/("run-"+std::to_string(::getpid()));
    require(!std::filesystem::exists(root),"refuse existing drill evidence directory");std::filesystem::create_directories(root);
    const auto exe=std::filesystem::canonical(argv[0]);unsigned failures=0;
    for(unsigned mode=0;mode<8;++mode){if(argc==4 && mode!=std::stoul(argv[2]))continue;
        try{scenario(exe,root/("case-"+std::to_string(mode)),mode);}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL case "<<mode<<": "<<error.what()<<'\n';}}
    std::cout<<"Evidence: "<<root<<'\n';return failures?1:0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
