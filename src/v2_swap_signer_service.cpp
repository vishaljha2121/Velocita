#include "velocita/v2/swap_signer_service.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/un.h>
#include <unistd.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
namespace velocita::v2::swap_signer {
namespace {
struct Fd{int fd{-1};explicit Fd(int f):fd(f){}~Fd(){if(fd>=0)::close(fd);}Fd(const Fd&)=delete;};
std::int64_t now()noexcept{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool ready(int fd,short events,std::int64_t deadline)noexcept{
    for(;;){const auto left=deadline-now();if(left<=0)return false;pollfd p{fd,events,0};
        const auto n=::poll(&p,1,static_cast<int>(std::min<std::int64_t>(left,1000)));if(n<0&&errno==EINTR)continue;
        if(n<0)return false;if(n)return(p.revents&events)!=0;}
}
bool prepare(int fd)noexcept{
    if(::fcntl(fd,F_SETFD,FD_CLOEXEC)!=0 || ::fcntl(fd,F_SETFL,O_NONBLOCK)!=0)return false;
#ifdef SO_NOSIGPIPE
    const int one=1;if(::setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one))!=0)return false;
#endif
    return true;
}
std::optional<std::uint32_t> peer(int fd)noexcept{
#ifdef __linux__
    ucred c{};socklen_t n=sizeof(c);if(::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&n)!=0||n!=sizeof(c))return {};return static_cast<std::uint32_t>(c.uid);
#else
    uid_t uid{};gid_t gid{};if(::getpeereid(fd,&uid,&gid)!=0)return {};return static_cast<std::uint32_t>(uid);
#endif
}
bool address(const std::filesystem::path&path,sockaddr_un&out)noexcept{
    const auto&s=path.native();if(!path.is_absolute() || s.empty() || s.size()>=sizeof(out.sun_path) || s.find('\0')!=s.npos)return false;
    out={};out.sun_family=AF_UNIX;std::memcpy(out.sun_path,s.c_str(),s.size()+1);return true;
}
bool transfer(int fd,std::span<std::byte>b,bool writing,std::int64_t deadline)noexcept{
    while(!b.empty()){
        if(!ready(fd,writing?POLLOUT:POLLIN,deadline))return false;
#ifdef MSG_NOSIGNAL
        constexpr int flags=MSG_NOSIGNAL;
#else
        constexpr int flags=0;
#endif
        const auto n=writing?::send(fd,b.data(),b.size(),flags) : ::read(fd,b.data(),b.size());
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)return false;b=b.subspan(static_cast<std::size_t>(n));
    }return true;
}
std::size_t read_frame(int fd,std::span<std::byte>b,std::int64_t deadline)noexcept{
    if(!transfer(fd,b.first(8),false,deadline))return 0;const auto n=frame_length(b.first(8));
    if(!n || n>b.size() || !transfer(fd,b.subspan(8,n-8),false,deadline))return 0;return n;
}
bool verify_public(const Reply&r,std::span<const std::byte>bytes)noexcept{
    EVP_PKEY_CTX* creation=EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr);if(!creation)return false;
    char group[]="prime256v1";auto key=r.public_key;
    OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,group,0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,key.data(),key.size()),OSSL_PARAM_construct_end()};
    EVP_PKEY* pub{};const bool imported=EVP_PKEY_fromdata_init(creation)==1 && EVP_PKEY_fromdata(creation,&pub,EVP_PKEY_PUBLIC_KEY,params)==1;
    EVP_PKEY_CTX_free(creation);if(!imported){EVP_PKEY_free(pub);return false;}
    EVP_MD_CTX* context=EVP_MD_CTX_new();const bool valid=context && EVP_DigestVerifyInit(context,nullptr,EVP_sha256(),nullptr,pub)==1 &&
        EVP_DigestVerify(context,reinterpret_cast<const unsigned char*>(r.der.data()),r.der_size,
            reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size())==1;
    EVP_MD_CTX_free(context);EVP_PKEY_free(pub);return valid;
}
int open_directory(const std::filesystem::path&path)noexcept{
    int fd=::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
    for(const auto&c:path.relative_path()){
        if(c=="."||c==".."){::close(fd);return -1;}
        const int next=::openat(fd,c.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);::close(fd);if(next<0)return -1;fd=next;
    }return fd;
}
}
bool restrict_process(std::uint32_t expected_uid,std::uint32_t gateway_uid)noexcept{
    if(!expected_uid || expected_uid==gateway_uid || ::geteuid()!=expected_uid || ::getuid()!=::geteuid() || ::getgid()!=::getegid())return false;
    const rlimit limit{0,0};if(::setrlimit(RLIMIT_CORE,&limit)!=0)return false;
#ifdef __linux__
    if(::prctl(PR_SET_DUMPABLE,0)!=0)return false;
#endif
    return true;
}
Server::~Server(){
    if(fd_>=0){::close(fd_);struct stat st{};if(::lstat(path_.c_str(),&st)==0 && static_cast<std::uint64_t>(st.st_dev)==device_ && static_cast<std::uint64_t>(st.st_ino)==inode_)::unlink(path_.c_str());}
    if(directory_>=0)::close(directory_);
}
bool Server::listen(const std::filesystem::path&path,std::uint32_t allowed)noexcept{
    try{
        sockaddr_un addr{};if(fd_>=0 || !address(path,addr))return false;
        Fd parent(open_directory(path.parent_path()));struct stat st{};
        if(parent.fd<0 || ::fstat(parent.fd,&st)!=0 || st.st_uid!=::geteuid() ||
            ((st.st_mode&07777)!=0700 && (st.st_mode&07777)!=0750) ||
            (allowed!=::geteuid() && ((st.st_mode&07777)!=0750 || st.st_gid!=::getegid())) ||
            ::flock(parent.fd,LOCK_EX|LOCK_NB)!=0)return false;
        struct stat old{};if(::fstatat(parent.fd,path.filename().c_str(),&old,AT_SYMLINK_NOFOLLOW)==0 || errno!=ENOENT)return false;
        Fd sock(::socket(AF_UNIX,SOCK_STREAM,0));if(sock.fd<0 || !prepare(sock.fd))return false;
        const auto mask=::umask(0117);const bool bound=::bind(sock.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0;::umask(mask);
        if(!bound)return false;
        if(::listen(sock.fd,4)!=0 || ::lstat(path.c_str(),&st)!=0 || !S_ISSOCK(st.st_mode) || st.st_uid!=::geteuid() || (st.st_mode&0777)!=0660){::unlink(path.c_str());return false;}
        fd_=sock.fd;sock.fd=-1;directory_=parent.fd;parent.fd=-1;path_=path;client_uid_=allowed;device_=st.st_dev;inode_=st.st_ino;return true;
    }catch(...){return false;}
}
bool Server::serve_once(SwapSignerSession&session,int timeout){
    if(fd_<0 || timeout<=0 || timeout>30000)return false;const auto deadline=now()+timeout;
    if(!ready(fd_,POLLIN,deadline))return false;Fd client(::accept(fd_,nullptr,nullptr));if(client.fd<0 || !prepare(client.fd))return false;
    const auto uid=peer(client.fd);if(!uid || *uid!=client_uid_){++peer_refusals_;return false;}
    std::array<std::byte,kRequestBytes> bytes{};const auto n=read_frame(client.fd,bytes,deadline);Request req;
    if(!n || !decode_request(std::span(bytes).first(n),req))return false;
    Reply reply;reply.request_digest=sha256_digest(std::span(bytes).first(n));
    if(req.operation==Operation::authenticate){
        std::size_t size{};if(session.authenticate(req.payload_view(),reply.der,size)){reply.code=ReplyCode::authenticated;reply.public_key=session.public_key();reply.der_size=static_cast<std::uint16_t>(size);}
    }else if(req.operation==Operation::retrieve){
        if(session.retrieve(req.effect,req.sequence,req.handle_view(),req.payload_view(),req.minimum_index,req.sign_by_ms))reply.code=ReplyCode::retrieved;
    }else if(session.start(req.effect,req.sequence,req.handle_view(),req.payload_view(),req.minimum_index,req.sign_by_ms,req.lookup)){
        while(now()<deadline && (session.status()==SwapSignerStatus::reading_state || session.status()==SwapSignerStatus::reading_authority)){(void)session.poll();if((session.status()==SwapSignerStatus::reading_state || session.status()==SwapSignerStatus::reading_authority))::usleep(1000);}
        session.cancel_pending();if(session.status()==SwapSignerStatus::signed_once)reply.code=ReplyCode::signed_once;
        else if(session.status()==SwapSignerStatus::fenced || session.status()==SwapSignerStatus::storage_failed)reply.code=ReplyCode::fenced;
    }
    if(reply.code==ReplyCode::signed_once || reply.code==ReplyCode::retrieved){reply.public_key=session.public_key();
        const auto der=session.signature_der();reply.der_size=static_cast<std::uint16_t>(der.size());std::copy(der.begin(),der.end(),reply.der.begin());}
    std::array<std::byte,kReplyBytes> frame{};const auto size=encode_reply(reply,frame);
    return size && transfer(client.fd,std::span(frame).first(size),true,deadline);
}
Client::Client(std::filesystem::path path,std::uint32_t uid,std::array<std::byte,33> key,int timeout)
    :path_(std::move(path)),signer_uid_(uid),public_key_(key),timeout_ms_(timeout){}
Reply Client::request(const Request&req){
    Reply denied;std::array<std::byte,kRequestBytes> bytes{};const auto size=encode_request(req,bytes);sockaddr_un addr{};
    if(!size || timeout_ms_<=0 || timeout_ms_>30000 || !address(path_,addr))return denied;const auto deadline=now()+timeout_ms_;
    Fd fd(::socket(AF_UNIX,SOCK_STREAM,0));if(fd.fd<0 || !prepare(fd.fd))return denied;
    if(::connect(fd.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0){
        if(errno!=EINPROGRESS || !ready(fd.fd,POLLOUT,deadline))return denied;
        int error{};socklen_t length=sizeof(error);if(::getsockopt(fd.fd,SOL_SOCKET,SO_ERROR,&error,&length)!=0 || error)return denied;
    }
    const auto uid=peer(fd.fd);if(!uid || *uid!=signer_uid_)return denied;
    if(!transfer(fd.fd,std::span(bytes).first(size),true,deadline))return denied;
    std::array<std::byte,kReplyBytes> response{};const auto n=read_frame(fd.fd,response,deadline);Reply result;
    if(!n || !decode_reply(std::span(response).first(n),result) || result.request_digest!=sha256_digest(std::span(bytes).first(size)))return denied;
    if(result.code==ReplyCode::signed_once || result.code==ReplyCode::retrieved || result.code==ReplyCode::authenticated){
        if(result.public_key!=public_key_ || !verify_public(result,req.payload_view()))return denied;
    }
    return result;
}
}
