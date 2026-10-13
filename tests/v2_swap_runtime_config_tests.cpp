#include "velocita/v2/swap_runtime_config.hpp"
#include <cstring>
#include "v2_swap_signer_test_support.hpp"
#include <sys/resource.h>
namespace {
using namespace swap_signer_test;
SwapRuntimeBinding binding(TestKey&key,const Scratch&s){
    SwapRuntimeBinding b;b.allocation=swap_test::opening().binding;b.authority_domain=digest(31);
    b.account_session={digest(32),digest(33)};b.owner_wallet.fill(std::byte{7});b.signer_public_key=key.public_key;
    const auto path=(s.base/"socket").string();require(path.size()<b.socket.size(),"fixture socket fits");
    std::copy(path.begin(),path.end(),b.socket.begin());b.socket_size=path.size();
    b.gateway_uid=::getuid();b.signer_uid=b.gateway_uid+1;b.gateway_caller=CallerId{22};b.signer_caller=CallerId{23};b.ipc_timeout_ms=1000;return b;
}
Digest256 hash(std::string_view s){return sha256_digest(std::as_bytes(std::span(s.data(),s.size())));}
int token(const Scratch&s,std::string_view body,int mode=0600){auto p=s.base/"token";int f=::open(p.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);require(f>=0,"token fixture opens");require(::write(f,body.data(),body.size())==static_cast<ssize_t>(body.size()),"token fixture written");::close(f);require(::chmod(p.c_str(),mode)==0,"fixture permissions set");return ::open(p.c_str(),O_RDONLY|O_CLOEXEC);}
void valid_config(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);
    require(validate_swap_runtime_binding(b)==SwapRuntimeCode::valid,"complete bounded binding validates");
    require(validate_swap_gateway_startup({b,hash("abc.def-123_xyz"),200},100)==SwapRuntimeCode::valid,"future pinned gateway session validates");
    require(validate_swap_signer_startup({b,digest(34),1,100})==SwapRuntimeCode::valid,"dedicated signer startup validates");
}
void invalid_config(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);
    for(unsigned n=0;n<9;++n){auto x=b;if(n==0)x.allocation.custody={};if(n==1)x.account_session.account_and_assets_digest={};if(n==2)x.account_session.session_evidence_digest={};if(n==3)x.owner_wallet={};if(n==4)x.authority_domain={};if(n==5)x.allocation.account=AccountId{};if(n==6)x.allocation.instrument=InstrumentId{};if(n==7)x.allocation.policy=PolicyId{};if(n==8)x.allocation.risk_policy_version=RiskPolicyVersion{};
        require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_binding,"missing identity binding refuses");}
    auto x=b;x.signer_uid=x.gateway_uid;require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_principals,"shared operating system identity refuses");
    x=b;x.signer_caller=x.gateway_caller;require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_principals,"shared Raft caller refuses");
    x=b;x.gateway_uid=0;require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_principals,"root gateway refuses");
    x=b;x.signer_public_key.fill(std::byte{255});x.signer_public_key[0]=std::byte{2};require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_public_key,"invalid curve point refuses");
    for(auto path:{"relative/socket","/a/../s","/a/./s","/a//s","/a/","/"}){x=b;x.socket.fill(0);x.socket_size=std::strlen(path);std::copy_n(path,x.socket_size,x.socket.begin());require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_socket,"noncanonical socket refuses before IO");}
    x=b;x.socket_size=105;require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_socket,"oversized socket checked before view");
    x=b;x.socket[x.socket_size-1]='\0';require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_socket,"embedded socket NUL refuses");
    x=b;x.ipc_timeout_ms=0;require(validate_swap_runtime_binding(x)==SwapRuntimeCode::invalid_timeout,"unbounded IPC timeout refuses");
}
void identity_binding(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);const auto original=swap_runtime_identity_digest(b);require(original!=Digest256{},"valid runtime identity hashes");
    for(unsigned n=0;n<11;++n){auto x=b;if(n==0)x.allocation.custody=digest(66);if(n==1)x.owner_wallet[0]^=std::byte{1};if(n==2)x.account_session.account_and_assets_digest=digest(67);if(n==3)x.account_session.session_evidence_digest=digest(68);if(n==4)x.gateway_uid+=2;if(n==5)x.signer_uid+=3;if(n==6)x.signer_caller=CallerId{24};if(n==7)x.socket[x.socket_size-1]='X';if(n==8)x.authority_domain=digest(69);if(n==9)x.allocation.policy_version=PolicyVersion{2};if(n==10)x.ipc_timeout_ms=1001;
        require(swap_runtime_identity_digest(x)!=original,"runtime identity binds every account and process pin");}
}
void gateway_session(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);constexpr std::string_view raw="abc.def-123_xyz";SwapGatewayStartupConfig c{b,hash(raw),200};SwapRuntimeCode code{};
    int fd=token(s,raw);auto session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(fd),100,code);
    require(session && code==SwapRuntimeCode::valid && session->bearer(101)==raw,"exact pinned bearer loads without private key");
    require(::fcntl(fd,F_GETFD)==-1,"inherited session FD closes after loading");
    require(session->bearer(200).empty() && session->bearer(101).empty(),"expiry permanently erases bearer");
    session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,raw)),100,code);
    require(session && session->bearer(99).empty() && session->bearer(101).empty(),"clock rollback permanently erases bearer");
    session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,"wrong.token")),100,code);
    require(!session && code==SwapRuntimeCode::session_digest_mismatch,"substituted account token refuses");
    session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,raw,0644)),100,code);
    require(!session && code==SwapRuntimeCode::unsafe_descriptor,"readable-by-others session refuses");
    for(auto value:{"abc\nInjected","abc def","abc\r", ""}){c.bearer_digest=hash(value);session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,value)),100,code);require(!session,"header injection and empty bearer refuse");}
    c.bearer_digest=hash(raw);session=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(-1),200,code);
    require(!session && code==SwapRuntimeCode::expired_session,"expired configuration refuses before descriptor IO");
}
void descriptor_bounds(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);SwapRuntimeCode code{};std::string maximum(4096,'A');SwapGatewayStartupConfig c{b,hash(maximum),200};
    auto loaded=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,maximum)),100,code);
    require(loaded && loaded->bearer(100)==maximum,"maximum bounded session fits without truncation");
    maximum.push_back('A');c.bearer_digest=hash(maximum);loaded=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(token(s,maximum)),100,code);
    require(!loaded && code==SwapRuntimeCode::invalid_bearer,"oversized session refuses before reading");
    const auto fd=token(s,"abc");::close(fd);c.bearer_digest=hash("abc");
    loaded=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(::open((s.base/"token").c_str(),O_RDWR)),100,code);
    require(!loaded && code==SwapRuntimeCode::unsafe_descriptor,"writable inherited session refuses");
    require(::link((s.base/"token").c_str(),(s.base/"alias").c_str())==0,"hardlink fixture created");
    loaded=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(::open((s.base/"token").c_str(),O_RDONLY)),100,code);
    require(!loaded && code==SwapRuntimeCode::unsafe_descriptor,"multiply linked inherited session refuses");
    int ends[2];require(::pipe(ends)==0,"pipe fixture created");::close(ends[1]);loaded=SwapGatewayCredentials::load(c,SwapInheritedSecretFd(ends[0]),100,code);
    require(!loaded && code==SwapRuntimeCode::unsafe_descriptor,"nonregular inherited session refuses without blocking");
    const auto invalid=validate_swap_gateway_startup({b,{},200},100);require(invalid==SwapRuntimeCode::invalid_session,"missing session pin refuses");
    auto signer=SwapSignerStartupConfig{b,digest(44),0,100};require(validate_swap_signer_startup(signer)==SwapRuntimeCode::invalid_binding,"missing key version refuses");
    signer.key_version=1;signer.state_timeout=60001;require(validate_swap_signer_startup(signer)==SwapRuntimeCode::invalid_timeout,"oversized state timeout refuses");
}
void protected_startup(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);b.signer_uid=::getuid();b.gateway_uid=b.signer_uid+1;
    const auto pid=::fork();require(pid>=0,"protected child starts");if(pid==0){try{
        require(::chmod(s.base.c_str(),0750)==0,"socket group traversal configured");SwapRuntimeCode code{};SwapSignerStartupConfig c{b,digest(44),1,100};
        auto bad=c;TestKey wrong(s.base/"other-key");bad.binding.signer_public_key=wrong.public_key;
        auto refused=SwapProtectedSignerStartup::open(bad,SwapInheritedSecretFd(::dup(key.fd)),code);
        require(!refused && code==SwapRuntimeCode::key_refused && !std::filesystem::exists(std::string(b.socket_view())),"wrong key refuses before socket creation");
        const auto inherited=::dup(key.fd);auto start=SwapProtectedSignerStartup::open(c,SwapInheritedSecretFd(inherited),code);
        require(::fcntl(inherited,F_GETFD)==-1,"private key descriptor closes before startup returns");
        require(start && code==SwapRuntimeCode::valid,"protected signer loads only after process restriction");
        rlimit limit{};require(::getrlimit(RLIMIT_CORE,&limit)==0 && limit.rlim_cur==0 && limit.rlim_max==0,"startup disables core dumps before key custody");
        require(start->key().signature_attempts()==0 && start->session_config().caller==b.signer_caller && start->session_config().binding==b.allocation,"startup binds session and never signs");
        struct stat st{};require(::lstat(std::string(b.socket_view()).c_str(),&st)==0 && S_ISSOCK(st.st_mode) && (st.st_mode&0777)==0660,"protected startup creates bounded permission socket");
        start.reset();require(!std::filesystem::exists(std::string(b.socket_view())),"startup owner removes its socket");::_exit(0);
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';::_exit(1);}}
    int status{};require(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"protected startup child passes");
}
void refusal_order(){Scratch s;TestKey key(s.base/"key");auto b=binding(key,s);SwapRuntimeCode code{};
    auto c=SwapSignerStartupConfig{b,digest(44),1,100};auto startup=SwapProtectedSignerStartup::open(c,SwapInheritedSecretFd(::dup(key.fd)),code);
    require(!startup && code==SwapRuntimeCode::process_restriction_failed && !std::filesystem::exists(std::string(b.socket_view())),"wrong process identity refuses before key or socket access");
    c.binding.signer_uid=c.binding.gateway_uid;startup=SwapProtectedSignerStartup::open(c,SwapInheritedSecretFd(-1),code);
    require(!startup && code==SwapRuntimeCode::invalid_principals,"invalid config refuses before inherited FD access");
}
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()> cases[]={{"valid",valid_config},{"invalid",invalid_config},{"identity",identity_binding},{"session",gateway_session},{"descriptors",descriptor_bounds},{"startup",protected_startup},{"order",refusal_order}};for(auto [name,run]:cases){if(argc>1 && std::string_view(argv[1])!=name)continue;try{run();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){std::cerr<<name<<": "<<e.what()<<'\n';return 1;}}}
