#include "velocita/v2/swap_submission_store.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {
using Code = SwapSubmissionCode;
using Stage = SwapSubmissionStage;
constexpr char current_name[]="submission.bin";
constexpr char temporary_name[]="submission.next";
constexpr std::array<std::byte,8> magic{std::byte{'V'},std::byte{'S'},std::byte{'S'},std::byte{'J'},std::byte{1},std::byte{},std::byte{},std::byte{}};
using Bytes=std::array<std::byte,kSwapSubmissionStoreBytes>;
struct Fd {
    int value{-1};
    explicit Fd(int fd):value(fd){}
    ~Fd(){if(value>=0)::close(value);}
    Fd(const Fd&)=delete;
    int release() noexcept{return std::exchange(value,-1);}
};
bool sync_fd(int fd) noexcept {
    int rc;do{rc=::fsync(fd);}while(rc<0 && errno==EINTR);return rc==0;
}
bool write_all(int fd,std::span<const std::byte> data) noexcept {
    while(!data.empty()) {
        const auto n=::write(fd,data.data(),data.size());
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return false;
        data=data.subspan(static_cast<std::size_t>(n));
    }
    return true;
}
bool private_directory(int fd) noexcept {
    struct stat st{};return ::fstat(fd,&st)==0 && S_ISDIR(st.st_mode) &&
        st.st_uid==::geteuid() && (st.st_mode&07777)==0700;
}
bool private_file(int fd) noexcept {
    struct stat st{};return ::fstat(fd,&st)==0 && S_ISREG(st.st_mode) &&
        st.st_uid==::geteuid() && (st.st_mode&07777)==0600 && st.st_nlink==1;
}
bool named_inode(int parent,const char* name,int fd) noexcept {
    struct stat named{},opened{};
    return ::fstat(fd,&opened)==0 && ::fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)==0 &&
        named.st_dev==opened.st_dev && named.st_ino==opened.st_ino &&
        (named.st_mode&S_IFMT)==(opened.st_mode&S_IFMT);
}
Code no_temporary(int directory) noexcept {
    struct stat st{};
    if(::fstatat(directory,temporary_name,&st,AT_SYMLINK_NOFOLLOW)==0)return Code::temporary_present;
    return errno==ENOENT?Code::none:Code::io_failure;
}
void put64(Bytes& bytes,std::size_t offset,std::uint64_t value) noexcept {
    for(unsigned i=0;i<8;++i)bytes[offset+i]=static_cast<std::byte>(value>>(8*i));
}
std::uint64_t get64(const Bytes& bytes,std::size_t offset) noexcept {
    std::uint64_t value{};for(unsigned i=0;i<8;++i)value|=std::uint64_t(std::to_integer<unsigned>(bytes[offset+i]))<<(8*i);
    return value;
}
bool zero_transaction(const SwapSubmissionTransaction& tx) noexcept {
    return std::all_of(tx.begin(),tx.end(),[](auto b){return b==std::byte{};});
}
bool valid_identity(const SwapSubmissionIdentity& identity) noexcept {
    return identity.effect.value()>0 && !identity.payload.is_zero() && !identity.execution_request.is_zero() &&
        identity.handle_size>0 && identity.handle_size<=identity.handle.size() &&
        std::all_of(identity.handle.begin()+static_cast<std::ptrdiff_t>(identity.handle_size),identity.handle.end(),[](auto b){return b==std::byte{};}) &&
        sha256_digest(std::span(identity.handle).first(identity.handle_size))==identity.handle_digest;
}
bool valid_record(const SwapSubmissionRecord& r) noexcept {
    if(r.binding.incarnation.is_zero() || r.binding.authority_domain.is_zero() || r.committed_index<=0 || r.ordinal==0)return false;
    if(r.phase==SwapSubmissionPhase::idle) {
        auto empty=SwapSubmissionIdentity{};empty.effect=r.identity.effect;
        return r.identity==empty && zero_transaction(r.transaction) && !r.evidence.is_zero();
    }
    if(!valid_identity(r.identity))return false;
    if(r.phase==SwapSubmissionPhase::execute_intent)return r.evidence.is_zero();
    return r.phase==SwapSubmissionPhase::resolved && !r.evidence.is_zero();
}
Bytes encode(const SwapSubmissionRecord& record) noexcept {
    Bytes bytes{};std::copy(magic.begin(),magic.end(),bytes.begin());
    put64(bytes,8,record.ordinal);put64(bytes,16,static_cast<std::uint64_t>(record.phase));
    put64(bytes,24,static_cast<std::uint64_t>(record.committed_index));put64(bytes,32,record.identity.effect.value());
    auto digest=[&](std::size_t offset,const Digest256& d){std::copy(d.bytes.begin(),d.bytes.end(),bytes.begin()+static_cast<std::ptrdiff_t>(offset));};
    digest(40,record.binding.incarnation);digest(72,record.binding.authority_domain);digest(104,record.identity.payload);
    digest(136,record.identity.execution_request);digest(168,record.identity.handle_digest);
    std::copy(record.transaction.begin(),record.transaction.end(),bytes.begin()+200);
    put64(bytes,264,record.identity.handle_size);std::copy(record.identity.handle.begin(),record.identity.handle.end(),bytes.begin()+272);
    digest(400,record.evidence);digest(432,sha256_digest(std::span(bytes).first(432)));return bytes;
}
bool decode(const Bytes& bytes,const SwapSubmissionBinding& binding,SwapSubmissionRecord& out) noexcept {
    const auto checksum=sha256_digest(std::span(bytes).first(432));
    if(!std::equal(magic.begin(),magic.end(),bytes.begin()) ||
        !std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+432) ||
        !std::equal(binding.incarnation.bytes.begin(),binding.incarnation.bytes.end(),bytes.begin()+40) ||
        !std::equal(binding.authority_domain.bytes.begin(),binding.authority_domain.bytes.end(),bytes.begin()+72) ||
        get64(bytes,24)>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || get64(bytes,16)>255)return false;
    SwapSubmissionRecord r{};r.binding=binding;r.ordinal=get64(bytes,8);r.phase=static_cast<SwapSubmissionPhase>(get64(bytes,16));
    r.committed_index=static_cast<std::int64_t>(get64(bytes,24));r.identity.effect=EffectId{get64(bytes,32)};
    auto digest=[&](std::size_t offset,Digest256& d){std::copy_n(bytes.begin()+static_cast<std::ptrdiff_t>(offset),32,d.bytes.begin());};
    digest(104,r.identity.payload);digest(136,r.identity.execution_request);digest(168,r.identity.handle_digest);
    std::copy_n(bytes.begin()+200,64,r.transaction.begin());r.identity.handle_size=get64(bytes,264);
    std::copy_n(bytes.begin()+272,128,r.identity.handle.begin());digest(400,r.evidence);r.checksum=checksum;
    if(!valid_record(r))return false;
    out=r;return true;
}
}

SwapSubmissionStore::SwapSubmissionStore(std::filesystem::path path,SwapSubmissionBinding binding,SwapSubmissionFaultHook hook)
    :directory_path_(std::move(path)),leaf_(directory_path_.filename().string()),binding_(binding),hook_(hook){}
SwapSubmissionStore::~SwapSubmissionStore(){
    if(file_>=0)::close(file_);
    if(directory_>=0)::close(directory_);
    if(parent_>=0)::close(parent_);
}
SwapSubmissionCode SwapSubmissionStore::fail(Code code) noexcept {status_=code;started_=true;return code;}
bool SwapSubmissionStore::checkpoint(Stage stage) noexcept {
    return !hook_.proceed || hook_.proceed(stage,hook_.context);
}
bool SwapSubmissionStore::identity_current() const noexcept {
    return parent_>=0 && directory_>=0 && private_directory(directory_) &&
        named_inode(parent_,leaf_.c_str(),directory_) &&
        (file_<0 || (private_file(file_) && named_inode(directory_,current_name,file_)));
}
SwapSubmissionCode SwapSubmissionStore::open_directory(bool create) {
    if(started_)return fail(Code::closed);
    started_=true;
    if(binding_.incarnation.is_zero() || binding_.authority_domain.is_zero() ||
        !directory_path_.is_absolute() || directory_path_.filename().empty() ||
        directory_path_.native().find('\0')!=std::string::npos)return fail(Code::invalid_input);
    for(const auto& component:directory_path_)if(component=="." || component=="..")return fail(Code::invalid_input);
    parent_=::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(parent_<0)return fail(Code::io_failure);
    for(const auto& component:directory_path_.parent_path().relative_path()) {
        const int next=::openat(parent_,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0)return fail(errno==ENOENT?Code::missing:Code::insecure);
        ::close(parent_);parent_=next;
    }
    struct stat parent_info{};
    if(::fstat(parent_,&parent_info)!=0)return fail(Code::io_failure);
    if(parent_info.st_uid!=::geteuid() || (parent_info.st_mode&0022)!=0)return fail(Code::insecure);
    const auto leaf=directory_path_.filename();
    if(create) {
        if(::mkdirat(parent_,leaf.c_str(),0700)!=0 && errno!=EEXIST)return fail(Code::io_failure);
    }
    directory_=::openat(parent_,leaf.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory_<0)return fail(errno==ENOENT?Code::missing:Code::insecure);
    if(!private_directory(directory_) || !named_inode(parent_,leaf.c_str(),directory_))return fail(Code::insecure);
    int locked;do{locked=::flock(directory_,LOCK_EX|LOCK_NB);}while(locked<0 && errno==EINTR);
    if(locked!=0)return fail(errno==EWOULDBLOCK || errno==EAGAIN?Code::locked:Code::io_failure);
    if(create && (!checkpoint(Stage::parent_sync) || !sync_fd(parent_)))return fail(Code::io_failure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    return Code::none;
}
SwapSubmissionCode SwapSubmissionStore::create(const CallerVerifiedSubmissionBootstrap& bootstrap) {
    if(bootstrap.binding!=binding_ || bootstrap.state_digest.is_zero() || bootstrap.committed_index<=0)return fail(Code::invalid_input);
    if(const auto code=open_directory(true);code!=Code::none)return code;
    struct stat st{};
    if(::fstatat(directory_,current_name,&st,AT_SYMLINK_NOFOLLOW)==0)return fail(Code::already_exists);
    if(errno!=ENOENT)return fail(Code::io_failure);
    SwapSubmissionRecord r{};r.binding=binding_;r.identity.effect=bootstrap.last_effect;
    r.committed_index=bootstrap.committed_index;r.evidence=bootstrap.state_digest;
    return replace(r);
}
SwapSubmissionCode SwapSubmissionStore::replace(SwapSubmissionRecord record) {
    if(latest_.ordinal==std::numeric_limits<std::uint64_t>::max())return fail(Code::exhausted);
    record.ordinal=latest_.ordinal+1;
    if(!valid_record(record))return fail(Code::invalid_input);
    if(!identity_current())return fail(Code::insecure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    Fd output(::openat(directory_,temporary_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    if(output.value<0)return fail(Code::io_failure);
    if(!private_file(output.value))return fail(Code::insecure);
    const auto bytes=encode(record);
    if(!write_all(output.value,std::span(bytes).first(232)) || !checkpoint(Stage::partial_write) ||
        !write_all(output.value,std::span(bytes).subspan(232)) ||
        !checkpoint(Stage::temporary_sync) || !sync_fd(output.value))return fail(Code::io_failure);
    if(!identity_current() || !named_inode(directory_,temporary_name,output.value))return fail(Code::insecure);
    if(!checkpoint(Stage::rename) || ::renameat(directory_,temporary_name,directory_,current_name)!=0 ||
        !checkpoint(Stage::directory_sync) || !sync_fd(directory_) ||
        !checkpoint(Stage::before_publish))return fail(Code::io_failure);
    if(!private_file(output.value) || !named_inode(directory_,current_name,output.value))return fail(Code::insecure);
    SwapSubmissionRecord stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    if(file_>=0)::close(file_);
    file_=output.release();latest_=stored;status_=Code::none;return status_;
}
SwapSubmissionCode SwapSubmissionStore::recover() {
    if(const auto code=open_directory(false);code!=Code::none)return code;
    file_=::openat(directory_,current_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(file_<0)return fail(errno==ENOENT?Code::missing:(errno==ELOOP?Code::insecure:Code::io_failure));
    if(!identity_current())return fail(Code::insecure);
    struct stat st{};
    if(::fstat(file_,&st)!=0)return fail(Code::io_failure);
    if(st.st_size!=static_cast<off_t>(kSwapSubmissionStoreBytes))return fail(Code::corrupt);
    Bytes bytes{};std::size_t offset{};
    while(offset<bytes.size()) {
        const auto n=::read(file_,bytes.data()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return fail(Code::io_failure);
        offset+=static_cast<std::size_t>(n);
    }
    SwapSubmissionRecord stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    if(!checkpoint(Stage::recovery_sync) || !sync_fd(file_) || !sync_fd(directory_))return fail(Code::io_failure);
    if(!identity_current())return fail(Code::insecure);
    latest_=stored;status_=Code::none;return status_;
}
SwapSubmissionCode SwapSubmissionStore::begin(const SwapSubmissionIdentity& identity,std::int64_t committed_attempt_index) {
    if(status_!=Code::none)return Code::closed;
    if(!valid_identity(identity))return fail(Code::invalid_input);
    if(latest_.phase==SwapSubmissionPhase::execute_intent || identity.effect.value()<=latest_.identity.effect.value() ||
        committed_attempt_index<=latest_.committed_index)return fail(Code::bad_transition);
    SwapSubmissionRecord record{};record.binding=binding_;record.identity=identity;
    record.phase=SwapSubmissionPhase::execute_intent;record.committed_index=committed_attempt_index;
    return replace(record);
}
SwapSubmissionCode SwapSubmissionStore::observe_transaction(const SwapSubmissionIdentity& identity,const SwapSubmissionTransaction& transaction) {
    if(status_!=Code::none)return Code::closed;
    if(latest_.phase!=SwapSubmissionPhase::execute_intent || identity!=latest_.identity || zero_transaction(transaction))return fail(Code::bad_transition);
    if(!identity_current())return fail(Code::insecure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    if(!zero_transaction(latest_.transaction)) {
        if(latest_.transaction!=transaction)return fail(Code::bad_transition);
        return Code::none;
    }
    auto record=latest_;record.transaction=transaction;return replace(record);
}
SwapSubmissionCode SwapSubmissionStore::resolve(const CallerVerifiedSubmissionResolution& resolution) {
    if(status_!=Code::none)return Code::closed;
    if(latest_.phase!=SwapSubmissionPhase::execute_intent || resolution.identity!=latest_.identity ||
        resolution.committed_index<=latest_.committed_index || resolution.evidence.is_zero())return fail(Code::bad_transition);
    auto record=latest_;record.phase=SwapSubmissionPhase::resolved;
    record.committed_index=resolution.committed_index;record.evidence=resolution.evidence;
    return replace(record);
}
std::optional<Digest256> solana_signature_transaction_digest(const SwapSubmissionTransaction& signature) noexcept {
    if(std::all_of(signature.begin(),signature.end(),[](auto b){return b==std::byte{};}))return std::nullopt;
    constexpr std::string_view domain="velocita.solana.signature.v1";
    std::array<std::byte,domain.size()+64> bytes{};
    for(std::size_t n=0;n<domain.size();++n)bytes[n]=static_cast<std::byte>(domain[n]);
    std::copy(signature.begin(),signature.end(),bytes.begin()+static_cast<std::ptrdiff_t>(domain.size()));
    return sha256_digest(bytes);
}

}
