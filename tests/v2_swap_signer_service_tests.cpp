#include "v2_swap_signer_test_support.hpp"
#include "velocita/v2/swap_signer_service.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <sys/resource.h>
namespace {
using namespace swap_signer_test;using namespace velocita::v2::swap_signer;
Request request(){Request r;r.operation=Operation::sign;r.effect=EffectId{1};r.sequence=1;r.minimum_index=10;r.sign_by_ms=20100;
    r.handle_size=handle.size();std::copy(handle.begin(),handle.end(),r.handle.begin());r.payload_size=body.size();std::copy(body_bytes().begin(),body_bytes().end(),r.payload.begin());return r;}
void child_result(pid_t child){int status{};require(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"isolated signer process passes its assertions");}
void reopen(Rig&r){r.session.reset();r.store.reset();r.store=std::make_unique<SwapSubmissionStore>(r.scratch.base/"signer-replay",r.binding);
    require(r.store->recover()==SwapSubmissionCode::none,"process signed result recovers");r.session=std::make_unique<SwapSignerSession>(r.link,*r.store,*r.key.protected_key,config(),r.clock.interface());}
void successful_ipc_and_retrieval(){
    Rig r;Server server;const auto path=r.scratch.base/"sign.sock";require(server.listen(path,::geteuid()),"local signer listens");
    auto child=::fork();require(child>=0,"signing child starts");if(child==0){const bool ok=server.serve_once(*r.session,2000);::_exit(ok && r.key.protected_key->signature_attempts()==1?0:1);}
    Client client(path,::geteuid(),r.key.public_key,2000);auto req=request();const auto first=client.request(req);
    require(first.code==ReplyCode::signed_once && !first.stamp.empty(),"peer authenticated IPC returns verified stamp");child_result(child);
    reopen(r);child=::fork();require(child>=0,"recovery child starts");if(child==0){const bool ok=server.serve_once(*r.session,2000);::_exit(ok && r.key.protected_key->signature_attempts()==0?0:1);}
    req.operation=Operation::retrieve;const auto second=client.request(req);child_result(child);
    require(second.code==ReplyCode::retrieved && second.stamp.view()==first.stamp.view(),"separate process retrieves identical persisted stamp without signing");
}
void lost_socket_response(){
    Rig r;Server server;const auto path=r.scratch.base/"lost.sock";require(server.listen(path,::geteuid()),"lost response listener");
    const auto child=::fork();require(child>=0,"lost response child");if(child==0){(void)server.serve_once(*r.session,2000);::_exit(r.key.protected_key->signature_attempts()==1?0:1);}
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);require(fd>=0,"raw client socket");sockaddr_un address{};address.sun_family=AF_UNIX;
    const auto name=path.string();require(name.size()<sizeof(address.sun_path),"socket path fits");std::memcpy(address.sun_path,name.c_str(),name.size()+1);
    require(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"raw client connects");
    std::array<std::byte,kRequestBytes> frame{};const auto n=encode_request(request(),frame);require(::write(fd,frame.data(),n)==static_cast<ssize_t>(n),"raw request sent exactly");require(::shutdown(fd,SHUT_WR)==0,"caller sends no more bytes");child_result(child);::close(fd);
    reopen(r);require(r.session->retrieve(EffectId{1},1,handle,body_bytes(),10,20100) && r.key.protected_key->signature_attempts()==0,
        "lost socket reply recovers durable stamp without repeat crypto");
}
void late_connection_budget(){
    Rig r;Server server;const auto path=r.scratch.base/"late.sock";
    require(server.listen(path,::geteuid()),"late client listener");
    int ready_pipe[2];require(::pipe(ready_pipe)==0,"server readiness pipe");
    const auto child=::fork();require(child>=0,"late client signer child");
    if(child==0){::close(ready_pipe[0]);const char ready='r';
        if(::write(ready_pipe[1],&ready,1)!=1)::_exit(2);::close(ready_pipe[1]);
        const bool ok=server.serve_once(*r.session,2000);
        ::_exit(ok && r.key.protected_key->signature_attempts()==1?0:1);}
    ::close(ready_pipe[1]);char ready{};require(::read(ready_pipe[0],&ready,1)==1,"server ready to enter accept window");::close(ready_pipe[0]);
    ::usleep(1'250'000);
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);require(fd>=0,"late client socket");
#ifdef SO_NOSIGPIPE
    const int one=1;require(::setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one))==0,"late socket ignores SIGPIPE");
#endif
#ifdef MSG_NOSIGNAL
    constexpr int flags=MSG_NOSIGNAL;
#else
    constexpr int flags=0;
#endif
    sockaddr_un address{};address.sun_family=AF_UNIX;const auto name=path.string();
    require(name.size()<sizeof(address.sun_path),"late socket path fits");std::memcpy(address.sun_path,name.c_str(),name.size()+1);
    require(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"late client connects");
    std::array<std::byte,kRequestBytes> frame{};const auto n=encode_request(request(),frame);
    require(::send(fd,frame.data(),8,flags)==8,"late request header sent");
    ::usleep(1'100'000);
    const auto sent=::send(fd,frame.data()+8,n-8,flags);
    int status{};require(::waitpid(child,&status,0)==child,"late signer reaped");
    std::array<std::byte,kReplyBytes> bytes{};std::size_t total{};
    for(;;){const auto got=::read(fd,bytes.data()+total,bytes.size()-total);if(got<=0)break;total+=static_cast<std::size_t>(got);if(total==bytes.size())break;}
    ::close(fd);Reply reply;
    require(sent==static_cast<ssize_t>(n-8) && WIFEXITED(status) && WEXITSTATUS(status)==0 &&
        decode_reply(std::span(bytes).first(total),reply) && reply.code==ReplyCode::signed_once &&
        reply.request_digest==sha256_digest(std::span(frame).first(n)) && reply.public_key==r.key.public_key &&
        r.key.verifies(body_bytes(),std::span(reply.der).first(reply.der_size)),
        "late accepted request keeps full bounded processing budget and returns one verified signature");
}
void incomplete_request_timeout(){
    Rig r;Server server;const auto path=r.scratch.base/"timeout.sock";
    require(server.listen(path,::geteuid()),"incomplete request listener");
    const auto child=::fork();require(child>=0,"incomplete request child");
    if(child==0){const bool ok=server.serve_once(*r.session,500);::_exit(!ok && r.key.protected_key->signature_attempts()==0?0:1);}
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);require(fd>=0,"incomplete socket");
    sockaddr_un address{};address.sun_family=AF_UNIX;const auto name=path.string();
    require(name.size()<sizeof(address.sun_path),"incomplete path fits");std::memcpy(address.sun_path,name.c_str(),name.size()+1);
    require(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"incomplete client connects");
    std::array<std::byte,kRequestBytes> frame{};const auto n=encode_request(request(),frame);
    require(n>8 && ::write(fd,frame.data(),8)==8,"incomplete header sent without body");
    child_result(child);char byte{};require(::read(fd,&byte,1)==0,"incomplete request closes without signing");::close(fd);
}
void process_policy(){
    const auto child=::fork();require(child>=0,"process boundary child");if(child==0){
        if(restrict_process(::geteuid(),::geteuid()) || restrict_process(static_cast<std::uint32_t>(::geteuid()+1),::geteuid()))::_exit(1);
        if(!restrict_process(::geteuid(),static_cast<std::uint32_t>(::geteuid()+1)))::_exit(2);
        rlimit limits{};if(::getrlimit(RLIMIT_CORE,&limits)!=0 || limits.rlim_cur!=0 || limits.rlim_max!=0)::_exit(3);::_exit(0);
    }child_result(child);
}
void peer_and_path_refusals(){
    {Rig r;require(::chmod(r.scratch.base.c_str(),0750)==0,"gateway group traversal fixture");Server server;
    const auto path=r.scratch.base/"deny.sock";require(server.listen(path,static_cast<std::uint32_t>(::geteuid()+1)),"different allowed peer fixture listens");
    const auto child=::fork();require(child>=0,"denied peer child");if(child==0){const bool ok=server.serve_once(*r.session,1000);::_exit(!ok && server.peer_refusals()==1 && r.key.protected_key->signature_attempts()==0?0:1);}
    Client client(path,::geteuid(),r.key.public_key,1000);require(client.request(request()).stamp.empty(),"wrong peer UID obtains no stamp");child_result(child);}
    {Rig r;Server server;const auto path=r.scratch.base/"wrong-server.sock";require(server.listen(path,::geteuid()),"wrong signer UID fixture listens");
    const auto child=::fork();require(child>=0,"server UID child");if(child==0){const bool ok=server.serve_once(*r.session,1000);::_exit(!ok && r.key.protected_key->signature_attempts()==0?0:1);}
    Client client(path,static_cast<std::uint32_t>(::geteuid()+1),r.key.public_key,1000);require(client.request(request()).stamp.empty(),"client verifies actual signer peer UID");child_result(child);}
    {Rig r;Server server;const auto path=r.scratch.base/"wrong-key.sock";require(server.listen(path,::geteuid()),"wrong key pin listener");
    const auto child=::fork();require(child>=0,"key pin child");if(child==0){const bool ok=server.serve_once(*r.session,1000);::_exit(ok?0:1);}
    auto wrong=r.key.public_key;wrong.back()^=std::byte{1};Client client(path,::geteuid(),wrong,1000);
    require(client.request(request()).stamp.empty(),"client pins signer public key independently of peer UID");child_result(child);}
    {Rig r;Server server;require(::chmod(r.scratch.base.c_str(),0777)==0,"public directory fixture");require(!server.listen(r.scratch.base/"bad.sock",::geteuid()),"writable public directory cannot host signer socket");}
    {Rig r;Server server;require(!server.listen(r.scratch.base/"key.pem",::geteuid()) && std::filesystem::is_regular_file(r.scratch.base/"key.pem"),"existing file is never unlinked or replaced for socket");}
}
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()> cases[]{{"late",late_connection_budget},{"incomplete",incomplete_request_timeout},{"success",successful_ipc_and_retrieval},{"lost",lost_socket_response},{"peer",peer_and_path_refusals},{"process",process_policy}};
    unsigned ran{},failed{};for(auto[name,fn]:cases){if(argc==2&&std::string_view(argv[1])!=name)continue;++ran;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return ran&&!failed?0:1;}
