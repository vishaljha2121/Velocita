#include "velocita/v2/swap_prepare_store.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace velocita::v2 {
namespace {
using Code = SwapPrepareStoreCode;
using Stage = SwapPrepareStoreStage;
constexpr char current_name[]="prepare.bin";
constexpr char temporary_name[]="prepare.next";
constexpr std::array<std::byte,8> magic{std::byte{'V'},std::byte{'P'},std::byte{'R'},std::byte{'P'},std::byte{1},std::byte{},std::byte{},std::byte{}};
using Bytes=std::array<std::byte,kSwapPrepareStoreBytes>;
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
bool valid_binding(const SwapPrepareBinding& b) noexcept {
    const auto& a=b.allocation;
    return !a.incarnation.is_zero() && !a.catalog.is_zero() && !a.custody.is_zero() && a.account.value() && a.instrument.value() &&
        a.policy.value() && a.policy_version.value() && a.risk_policy_version.value() && !b.authority_domain.is_zero() && !b.mapping_identity.is_zero();
}
bool valid_operation(const SwapPrepareOperation& operation) noexcept {
    truemarkets::SwapRestRequest request;
    return operation.effect.value()>0 && !operation.intent_digest.is_zero() &&
        truemarkets::encode_swap_rest_create(operation.input,request)==truemarkets::SwapRestCode::encoded &&
        operation.request_digest==sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size)));
}
bool empty_prepared(const truemarkets::SwapRestPrepared& p) noexcept {
    return p.handle.length==0 && p.payload_size==0 && p.payload_digest.is_zero() && p.quoted_input==0 && p.quoted_output==0 && p.quoted_fee==0 &&
        std::all_of(p.handle.bytes.begin(),p.handle.bytes.end(),[](char c){return c==0;}) &&
        std::all_of(p.payload.begin(),p.payload.end(),[](char c){return c==0;});
}
bool valid_prepared(const truemarkets::SwapRestPrepared& p,const SwapPrepareOperation& operation) noexcept {
    if(!p.handle.length || p.handle.length>p.handle.bytes.size() || !p.payload_size || p.payload_size>p.payload.size() ||
        p.quoted_input!=operation.input.quantity_atoms || !p.quoted_output || p.quoted_output>INT64_MAX || p.quoted_fee>INT64_MAX)return false;
    return std::all_of(p.handle.bytes.begin(),p.handle.bytes.begin()+p.handle.length,[](unsigned char c){return
            (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_';}) &&
        std::all_of(p.handle.bytes.begin()+p.handle.length,p.handle.bytes.end(),[](char c){return c==0;}) &&
        std::all_of(p.payload.begin(),p.payload.begin()+p.payload_size,[](unsigned char c){return c>0 && c<128;}) &&
        std::all_of(p.payload.begin()+p.payload_size,p.payload.end(),[](char c){return c==0;}) &&
        p.payload_digest==sha256_digest(std::as_bytes(std::span(p.payload.data(),p.payload_size)));
}
bool valid_record(const SwapPrepareRecord& r) noexcept {
    if(!valid_binding(r.binding) || !r.ordinal || r.committed_index<=0 || r.state_digest.is_zero())return false;
    if(r.phase==SwapPreparePhase::idle)return r.operation.intent_digest.is_zero() && r.operation.request_digest.is_zero() &&
        r.operation.input.side==Side{} && !r.operation.input.quantity_atoms && r.response_digest.is_zero() && empty_prepared(r.prepared);
    if(!valid_operation(r.operation))return false;
    if(r.phase==SwapPreparePhase::create_intent)return r.response_digest.is_zero() && empty_prepared(r.prepared);
    if(r.phase==SwapPreparePhase::released)return !r.response_digest.is_zero() && empty_prepared(r.prepared);
    return r.phase==SwapPreparePhase::prepared && !r.response_digest.is_zero() && valid_prepared(r.prepared,r.operation);
}
Bytes encode(const SwapPrepareRecord& r) noexcept {
    Bytes bytes{};std::copy(magic.begin(),magic.end(),bytes.begin());
    auto digest=[&](std::size_t o,const Digest256& d){std::copy(d.bytes.begin(),d.bytes.end(),bytes.begin()+static_cast<std::ptrdiff_t>(o));};
    put64(bytes,8,r.ordinal);put64(bytes,16,static_cast<std::uint64_t>(r.phase));put64(bytes,24,r.committed_index);put64(bytes,32,r.operation.effect.value());
    const auto& b=r.binding.allocation;
    digest(40,b.incarnation);digest(72,b.catalog);digest(104,b.custody);
    put64(bytes,136,b.account.value());put64(bytes,144,b.instrument.value());put64(bytes,152,b.policy.value());
    put64(bytes,160,b.policy_version.value());put64(bytes,168,b.risk_policy_version.value());
    digest(176,r.binding.authority_domain);digest(208,r.binding.mapping_identity);digest(240,r.operation.intent_digest);
    digest(272,r.operation.request_digest);digest(304,r.state_digest);digest(336,r.response_digest);digest(368,r.prepared.payload_digest);
    put64(bytes,400,static_cast<std::uint64_t>(r.operation.input.side));put64(bytes,408,r.operation.input.quantity_atoms);
    put64(bytes,416,r.prepared.quoted_output);put64(bytes,424,r.prepared.quoted_fee);
    put64(bytes,432,r.prepared.handle.length);put64(bytes,440,r.prepared.payload_size);
    for(std::size_t n=0;n<96;++n)bytes[448+n]=static_cast<std::byte>(r.prepared.handle.bytes[n]);
    for(std::size_t n=0;n<8192;++n)bytes[544+n]=static_cast<std::byte>(r.prepared.payload[n]);
    digest(8736,sha256_digest(std::span(bytes).first(8736)));return bytes;
}
bool decode(const Bytes& bytes,const SwapPrepareBinding& binding,SwapPrepareRecord& out) noexcept {
    const auto checksum=sha256_digest(std::span(bytes).first(8736));
    if(!std::equal(magic.begin(),magic.end(),bytes.begin()) ||
        !std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+8736) ||
        get64(bytes,24)>INT64_MAX || get64(bytes,16)>255 || get64(bytes,400)>255 || get64(bytes,432)>96 || get64(bytes,440)>8192)return false;
    SwapPrepareRecord r;
    auto digest=[&](std::size_t o,Digest256& d){std::copy_n(bytes.begin()+static_cast<std::ptrdiff_t>(o),32,d.bytes.begin());};
    r.ordinal=get64(bytes,8);r.phase=static_cast<SwapPreparePhase>(get64(bytes,16));r.committed_index=static_cast<std::int64_t>(get64(bytes,24));
    r.operation.effect=EffectId{get64(bytes,32)};auto& b=r.binding.allocation;
    digest(40,b.incarnation);digest(72,b.catalog);digest(104,b.custody);
    b.account=AccountId{get64(bytes,136)};b.instrument=InstrumentId{get64(bytes,144)};b.policy=PolicyId{get64(bytes,152)};
    b.policy_version=PolicyVersion{get64(bytes,160)};b.risk_policy_version=RiskPolicyVersion{get64(bytes,168)};
    digest(176,r.binding.authority_domain);digest(208,r.binding.mapping_identity);digest(240,r.operation.intent_digest);
    digest(272,r.operation.request_digest);digest(304,r.state_digest);digest(336,r.response_digest);digest(368,r.prepared.payload_digest);
    r.operation.input={static_cast<Side>(get64(bytes,400)),get64(bytes,408)};
    r.prepared.quoted_input=r.phase==SwapPreparePhase::prepared?r.operation.input.quantity_atoms:0;
    r.prepared.quoted_output=get64(bytes,416);r.prepared.quoted_fee=get64(bytes,424);
    r.prepared.handle.length=static_cast<std::uint16_t>(get64(bytes,432));r.prepared.payload_size=static_cast<std::uint16_t>(get64(bytes,440));
    for(std::size_t n=0;n<96;++n)r.prepared.handle.bytes[n]=static_cast<char>(bytes[448+n]);
    for(std::size_t n=0;n<8192;++n)r.prepared.payload[n]=static_cast<char>(bytes[544+n]);
    r.checksum=checksum;
    if(r.binding!=binding || !valid_record(r))return false;
    out=r;return true;
}
}

SwapPrepareStore::SwapPrepareStore(std::filesystem::path path,SwapPrepareBinding binding,SwapPrepareStoreFaultHook hook)
    :directory_path_(std::move(path)),leaf_(directory_path_.filename().string()),binding_(binding),hook_(hook){}
SwapPrepareStore::~SwapPrepareStore(){
    if(file_>=0)::close(file_);
    if(directory_>=0)::close(directory_);
    if(parent_>=0)::close(parent_);
}
SwapPrepareStoreCode SwapPrepareStore::fail(Code code) noexcept {status_=code;started_=true;return code;}
bool SwapPrepareStore::checkpoint(Stage stage) noexcept {
    return !hook_.proceed || hook_.proceed(stage,hook_.context);
}
bool SwapPrepareStore::identity_current() const noexcept {
    return parent_>=0 && directory_>=0 && private_directory(directory_) &&
        named_inode(parent_,leaf_.c_str(),directory_) &&
        (file_<0 || (private_file(file_) && named_inode(directory_,current_name,file_)));
}
SwapPrepareStoreCode SwapPrepareStore::open_directory(bool create) {
    if(started_)return fail(Code::closed);
    started_=true;
    if(!valid_binding(binding_) ||
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
SwapPrepareStoreCode SwapPrepareStore::create(const CallerVerifiedPrepareBootstrap& bootstrap) {
    if(bootstrap.binding!=binding_ || bootstrap.state_digest.is_zero() || bootstrap.committed_index<=0)return fail(Code::invalid_input);
    if(const auto code=open_directory(true);code!=Code::none)return code;
    struct stat st{};
    if(::fstatat(directory_,current_name,&st,AT_SYMLINK_NOFOLLOW)==0)return fail(Code::already_exists);
    if(errno!=ENOENT)return fail(Code::io_failure);
    SwapPrepareRecord r;r.binding=binding_;r.operation.effect=bootstrap.last_effect;
    r.committed_index=bootstrap.committed_index;r.state_digest=bootstrap.state_digest;return replace(r);
}
SwapPrepareStoreCode SwapPrepareStore::replace(SwapPrepareRecord record) {
    if(latest_.ordinal==std::numeric_limits<std::uint64_t>::max())return fail(Code::exhausted);
    record.ordinal=latest_.ordinal+1;
    if(!valid_record(record))return fail(Code::invalid_input);
    if(!identity_current())return fail(Code::insecure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    Fd output(::openat(directory_,temporary_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    if(output.value<0)return fail(Code::io_failure);
    if(!private_file(output.value))return fail(Code::insecure);
    const auto bytes=encode(record);
    if(!write_all(output.value,std::span(bytes).first(4384)) || !checkpoint(Stage::partial_write) ||
        !write_all(output.value,std::span(bytes).subspan(4384)) ||
        !checkpoint(Stage::temporary_sync) || !sync_fd(output.value))return fail(Code::io_failure);
    if(!identity_current() || !named_inode(directory_,temporary_name,output.value))return fail(Code::insecure);
    if(!checkpoint(Stage::rename) || ::renameat(directory_,temporary_name,directory_,current_name)!=0 ||
        !checkpoint(Stage::directory_sync) || !sync_fd(directory_) ||
        !checkpoint(Stage::before_publish))return fail(Code::io_failure);
    if(!private_file(output.value) || !named_inode(directory_,current_name,output.value))return fail(Code::insecure);
    SwapPrepareRecord stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    if(file_>=0)::close(file_);
    file_=output.release();latest_=stored;status_=Code::none;return status_;
}
SwapPrepareStoreCode SwapPrepareStore::recover() {
    if(const auto code=open_directory(false);code!=Code::none)return code;
    file_=::openat(directory_,current_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(file_<0)return fail(errno==ENOENT?Code::missing:(errno==ELOOP?Code::insecure:Code::io_failure));
    if(!identity_current())return fail(Code::insecure);
    struct stat st{};
    if(::fstat(file_,&st)!=0)return fail(Code::io_failure);
    if(st.st_size!=static_cast<off_t>(kSwapPrepareStoreBytes))return fail(Code::corrupt);
    Bytes bytes{};std::size_t offset{};
    while(offset<bytes.size()) {
        const auto n=::read(file_,bytes.data()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return fail(Code::io_failure);
        offset+=static_cast<std::size_t>(n);
    }
    SwapPrepareRecord stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    if(!checkpoint(Stage::recovery_sync) || !sync_fd(file_) || !sync_fd(directory_))return fail(Code::io_failure);
    if(!identity_current())return fail(Code::insecure);
    latest_=stored;status_=Code::none;return status_;
}
SwapPrepareStoreCode SwapPrepareStore::begin(const SwapPrepareOperation& operation,std::int64_t index,Digest256 state_digest,
    const CallerVerifiedPreparePredecessor* prior) {
    if(status_!=Code::none)return Code::closed;
    if(!valid_operation(operation) || state_digest.is_zero())return fail(Code::invalid_input);
    if(latest_.phase==SwapPreparePhase::create_intent || operation.effect.value()<=latest_.operation.effect.value() ||
        index<=latest_.committed_index)return fail(Code::bad_transition);
    if(latest_.phase==SwapPreparePhase::prepared) {
        if(!prior || prior->committed_index!=index || prior->state_digest!=state_digest ||
            prior->settlement.effect!=latest_.operation.effect || prior->settlement.payload!=latest_.prepared.payload_digest ||
            prior->settlement.custody!=binding_.allocation.custody || !prior->settlement.finalized ||
            prior->settlement.evidence.is_zero() ||
            prior->settlement.transaction.is_zero()!=(prior->settlement.outcome==swap::Outcome::expired_unexecuted))return fail(Code::bad_transition);
    }else if(prior)return fail(Code::bad_transition);
    SwapPrepareRecord next;next.binding=binding_;next.operation=operation;next.phase=SwapPreparePhase::create_intent;
    next.committed_index=index;next.state_digest=state_digest;return replace(next);
}
SwapPrepareStoreCode SwapPrepareStore::complete(const truemarkets::SwapRestPrepared& prepared,Digest256 response_digest) {
    if(status_!=Code::none)return Code::closed;
    if(latest_.phase!=SwapPreparePhase::create_intent)return fail(Code::bad_transition);
    if(response_digest.is_zero() || !valid_prepared(prepared,latest_.operation))return fail(Code::invalid_input);
    auto record=latest_;record.prepared=prepared;record.response_digest=response_digest;record.phase=SwapPreparePhase::prepared;
    return replace(record);
}
SwapPrepareStoreCode SwapPrepareStore::release(std::int64_t index,Digest256 state_digest) {
    if(status_!=Code::none)return Code::closed;
    if(latest_.phase!=SwapPreparePhase::create_intent)return fail(Code::bad_transition);
    if(state_digest.is_zero() || index<latest_.committed_index)return fail(Code::invalid_input);
    auto record=latest_;record.phase=SwapPreparePhase::released;record.response_digest=latest_.checksum;
    record.committed_index=index;record.state_digest=state_digest;return replace(record);
}
}
