#include "velocita/v2/swap_runtime_config.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>
namespace velocita::v2 {
namespace {
bool public_key_valid(const std::array<std::byte,33>&key)noexcept{
    if(key[0]!=std::byte{2} && key[0]!=std::byte{3})return false;
    auto copy=key;char group[]="prime256v1";OSSL_PARAM params[]{
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,group,0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,copy.data(),copy.size()),OSSL_PARAM_construct_end()};
    EVP_PKEY_CTX* create=EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr);if(!create)return false;
    EVP_PKEY* pub{};const bool loaded=EVP_PKEY_fromdata_init(create)==1 && EVP_PKEY_fromdata(create,&pub,EVP_PKEY_PUBLIC_KEY,params)==1;
    EVP_PKEY_CTX_free(create);EVP_PKEY_CTX* check=loaded?EVP_PKEY_CTX_new(pub,nullptr):nullptr;
    const bool valid=check && EVP_PKEY_public_check(check)==1;EVP_PKEY_CTX_free(check);EVP_PKEY_free(pub);return valid;
}
bool socket_valid(const SwapRuntimeBinding&b)noexcept{
    if(b.socket_size<2 || b.socket_size>=b.socket.size() || b.socket[0]!='/' || b.socket[b.socket_size-1]=='/')return false;
    const auto path=b.socket_view();std::size_t begin=1;
    for(std::size_t n=1;n<=path.size();++n){
        if(n==path.size() || path[n]=='/'){
            const auto part=path.substr(begin,n-begin);if(part.empty() || part=="." || part=="..")return false;begin=n+1;
        }else{const auto c=static_cast<unsigned char>(path[n]);if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;}
    }
    return std::all_of(b.socket.begin()+b.socket_size,b.socket.end(),[](char c){return c==0;});
}
void add64(Sha256&h,std::uint64_t n)noexcept{std::array<std::byte,8>b{};for(unsigned i=0;i<8;++i)b[i]=static_cast<std::byte>(n>>(i*8));h.update(b);}
bool safe_fd(int fd,std::uint32_t owner,struct stat&st)noexcept{
    if(fd<3 || ::fstat(fd,&st)!=0)return false;
    const auto flags=::fcntl(fd,F_GETFL);
    return flags>=0 && (flags&O_ACCMODE)==O_RDONLY && S_ISREG(st.st_mode) && st.st_uid==owner &&
        (st.st_mode&07777)==0600 && st.st_nlink==1 && ::fcntl(fd,F_SETFD,FD_CLOEXEC)==0;
}
bool token_char(unsigned char c)noexcept{
    return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~' || c=='+' || c=='/' || c=='=';
}
}
SwapRuntimeCode validate_swap_runtime_binding(const SwapRuntimeBinding&b)noexcept{
    const auto&a=b.allocation;
    if(a.incarnation.is_zero() || a.catalog.is_zero() || a.custody.is_zero() || !a.account.value() || !a.instrument.value() ||
       !a.policy.value() || !a.policy_version.value() || !a.risk_policy_version.value() || b.authority_domain.is_zero() ||
       b.account_session.account_and_assets_digest.is_zero() || b.account_session.session_evidence_digest.is_zero() ||
       std::all_of(b.owner_wallet.begin(),b.owner_wallet.end(),[](std::byte x){return x==std::byte{};}))return SwapRuntimeCode::invalid_binding;
    if(b.max_buy_output_deduction_lamports>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return SwapRuntimeCode::invalid_binding;
    if(!b.gateway_uid || !b.signer_uid || b.gateway_uid==b.signer_uid || !b.gateway_caller.value() || !b.signer_caller.value() ||
       b.gateway_caller==b.signer_caller)return SwapRuntimeCode::invalid_principals;
    if(!socket_valid(b))return SwapRuntimeCode::invalid_socket;
    if(b.ipc_timeout_ms<1 || b.ipc_timeout_ms>60000)return SwapRuntimeCode::invalid_timeout;
    if(!public_key_valid(b.signer_public_key))return SwapRuntimeCode::invalid_public_key;
    return SwapRuntimeCode::valid;
}
SwapRuntimeCode validate_swap_gateway_startup(const SwapGatewayStartupConfig&c,std::uint64_t now)noexcept{
    const auto result=validate_swap_runtime_binding(c.binding);if(result!=SwapRuntimeCode::valid)return result;
    if(c.bearer_digest.is_zero() || !c.expires_at_ms)return SwapRuntimeCode::invalid_session;
    if(!now || now>=c.expires_at_ms)return SwapRuntimeCode::expired_session;
    return SwapRuntimeCode::valid;
}
SwapRuntimeCode validate_swap_signer_startup(const SwapSignerStartupConfig&c)noexcept{
    const auto result=validate_swap_runtime_binding(c.binding);if(result!=SwapRuntimeCode::valid)return result;
    if(c.signer_instance.is_zero() || !c.key_version)return SwapRuntimeCode::invalid_binding;
    if(c.state_timeout<1 || c.state_timeout>60000)return SwapRuntimeCode::invalid_timeout;
    return SwapRuntimeCode::valid;
}
Digest256 swap_runtime_identity_digest(const SwapRuntimeBinding&b)noexcept{
    if(validate_swap_runtime_binding(b)!=SwapRuntimeCode::valid)return {};
    Sha256 h;constexpr std::string_view domain="velocita-swap-runtime-v1";h.update(std::as_bytes(std::span(domain.data(),domain.size())));
    const auto&a=b.allocation;h.update(a.incarnation.bytes);h.update(a.catalog.bytes);h.update(a.custody.bytes);
    add64(h,a.account.value());add64(h,a.instrument.value());add64(h,a.policy.value());add64(h,a.policy_version.value());add64(h,a.risk_policy_version.value());
    h.update(b.authority_domain.bytes);h.update(b.account_session.account_and_assets_digest.bytes);h.update(b.account_session.session_evidence_digest.bytes);
    h.update(b.owner_wallet);h.update(b.signer_public_key);add64(h,b.gateway_uid);add64(h,b.signer_uid);add64(h,b.gateway_caller.value());add64(h,b.signer_caller.value());
    add64(h,b.ipc_timeout_ms);add64(h,b.socket_size);h.update(std::as_bytes(std::span(b.socket.data(),b.socket_size)));
    // Preserve existing zero-cost identities. Nonzero cost authority changes the
    // reviewed identity and cannot reuse a prior zero-cost account review.
    if(b.max_buy_output_deduction_lamports){
        constexpr std::string_view costs="accepted-buy-output-deduction-v1";
        h.update(std::as_bytes(std::span(costs.data(),costs.size())));
        add64(h,b.max_buy_output_deduction_lamports);
    }
    if(!b.transaction_review_digest.is_zero()){
        constexpr std::string_view review="transaction-review-v1";
        h.update(std::as_bytes(std::span(review)));h.update(b.transaction_review_digest.bytes);
    }
    return h.finish();
}
SwapInheritedSecretFd::~SwapInheritedSecretFd(){if(fd_>=3)::close(fd_);}
SwapInheritedSecretFd::SwapInheritedSecretFd(SwapInheritedSecretFd&&o)noexcept:fd_(o.fd_){o.fd_=-1;}
SwapGatewayCredentials::~SwapGatewayCredentials(){OPENSSL_cleanse(bearer_.data(),bearer_.size());}
std::unique_ptr<SwapGatewayCredentials> SwapGatewayCredentials::load(SwapGatewayStartupConfig c,SwapInheritedSecretFd fd,std::uint64_t now,SwapRuntimeCode&code){
    code=validate_swap_gateway_startup(c,now);if(code!=SwapRuntimeCode::valid)return {};
    if(fd.get()<3){code=SwapRuntimeCode::invalid_descriptor;return {};}
    struct stat st{};if(::geteuid()!=c.binding.gateway_uid || ::getuid()!=::geteuid() || !safe_fd(fd.get(),c.binding.gateway_uid,st)){
        code=SwapRuntimeCode::unsafe_descriptor;return {};}
    auto result=std::unique_ptr<SwapGatewayCredentials>(new SwapGatewayCredentials(c));
    if(st.st_size<1 || static_cast<std::uint64_t>(st.st_size)>result->bearer_.size()){code=SwapRuntimeCode::invalid_bearer;return {};}
    const auto size=static_cast<std::size_t>(st.st_size);std::size_t read{};
    while(read<size){const auto n=::pread(fd.get(),result->bearer_.data()+read,size-read,static_cast<off_t>(read));if(n<0&&errno==EINTR)continue;
        if(n<=0){code=SwapRuntimeCode::invalid_bearer;return {};}read+=static_cast<std::size_t>(n);}
    struct stat after{};if(::fstat(fd.get(),&after)!=0 || after.st_size!=st.st_size || after.st_nlink!=1 || after.st_uid!=st.st_uid || after.st_mode!=st.st_mode){code=SwapRuntimeCode::unsafe_descriptor;return {};}
    if(!std::all_of(result->bearer_.begin(),result->bearer_.begin()+size,[](unsigned char ch){return token_char(ch);})){code=SwapRuntimeCode::invalid_bearer;return {};}
    if(sha256_digest(std::as_bytes(std::span(result->bearer_.data(),size)))!=c.bearer_digest){code=SwapRuntimeCode::session_digest_mismatch;return {};}
    result->bearer_size_=size;result->last_time_=now;code=SwapRuntimeCode::valid;return result;
}
std::string_view SwapGatewayCredentials::bearer(std::uint64_t now)noexcept{
    if(now<last_time_ || now>=config_.expires_at_ms){OPENSSL_cleanse(bearer_.data(),bearer_.size());bearer_size_=0;return {};}
    last_time_=now;return {bearer_.data(),bearer_size_};
}
std::unique_ptr<swap_signer::Client> SwapGatewayCredentials::signer_client()const{
    const auto&b=config_.binding;return std::make_unique<swap_signer::Client>(std::string(b.socket_view()),b.signer_uid,b.signer_public_key,b.ipc_timeout_ms);
}
std::unique_ptr<SwapProtectedSignerStartup> SwapProtectedSignerStartup::open(SwapSignerStartupConfig c,SwapInheritedSecretFd fd,SwapRuntimeCode&code){
    code=validate_swap_signer_startup(c);if(code!=SwapRuntimeCode::valid)return {};
    if(fd.get()<3){code=SwapRuntimeCode::invalid_descriptor;return {};}
    if(!swap_signer::restrict_process(c.binding.signer_uid,c.binding.gateway_uid)){code=SwapRuntimeCode::process_restriction_failed;return {};}
    struct stat metadata{};if(!safe_fd(fd.get(),c.binding.signer_uid,metadata)){code=SwapRuntimeCode::unsafe_descriptor;return {};}
    auto result=std::unique_ptr<SwapProtectedSignerStartup>(new SwapProtectedSignerStartup(c));
    result->key_=SwapSignerKey::load_inherited_fd(fd.get(),c.binding.signer_public_key);
    if(!result->key_){code=SwapRuntimeCode::key_refused;return {};}
    if(!result->server_.listen(std::string(c.binding.socket_view()),c.binding.gateway_uid)){code=SwapRuntimeCode::socket_refused;return {};}
    code=SwapRuntimeCode::valid;return result;
}
SwapSignerConfig SwapProtectedSignerStartup::session_config()const noexcept{
    return {config_.binding.allocation,config_.binding.authority_domain,config_.signer_instance,config_.binding.signer_caller,config_.key_version,config_.state_timeout};
}
}
