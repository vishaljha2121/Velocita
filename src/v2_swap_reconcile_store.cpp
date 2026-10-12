#include "velocita/v2/swap_reconcile_store.hpp"
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
using Code = SwapReconcileStoreCode;
using Stage = SwapReconcileStoreStage;
constexpr char current_name[]="result.bin";
constexpr char temporary_name[]="result.next";
constexpr std::array<std::byte,8> magic{std::byte{'V'},std::byte{'S'},std::byte{'R'},std::byte{'W'},std::byte{1},std::byte{},std::byte{},std::byte{}};
using Bytes=std::array<std::byte,kSwapReconcileStoreBytes>;
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
Bytes encode(const CallerVerifiedCommittedResult& result,std::uint64_t ordinal) noexcept {
    Bytes bytes{};std::copy(magic.begin(),magic.end(),bytes.begin());
    put64(bytes,8,ordinal);put64(bytes,16,static_cast<std::uint64_t>(result.index));
    std::copy(result.binding.incarnation.bytes.begin(),result.binding.incarnation.bytes.end(),bytes.begin()+24);
    std::copy(result.binding.authority_domain.bytes.begin(),result.binding.authority_domain.bytes.end(),bytes.begin()+56);
    std::copy(result.result_digest.bytes.begin(),result.result_digest.bytes.end(),bytes.begin()+88);
    const auto checksum=sha256_digest(std::span(bytes).first(120));
    std::copy(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+120);return bytes;
}
bool decode(const Bytes& bytes,const SwapReconcileStoreBinding& binding,SwapReconcileStoredResult& out) noexcept {
    const auto ordinal=get64(bytes,8),index=get64(bytes,16);
    const auto checksum=sha256_digest(std::span(bytes).first(120));
    if(!std::equal(magic.begin(),magic.end(),bytes.begin()) || ordinal==0 || index==0 ||
        index>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !std::equal(checksum.bytes.begin(),checksum.bytes.end(),bytes.begin()+120) ||
        !std::equal(binding.incarnation.bytes.begin(),binding.incarnation.bytes.end(),bytes.begin()+24) ||
        !std::equal(binding.authority_domain.bytes.begin(),binding.authority_domain.bytes.end(),bytes.begin()+56))return false;
    Digest256 result_digest{};std::copy_n(bytes.begin()+88,32,result_digest.bytes.begin());
    if(result_digest.is_zero())return false;
    out={{binding,static_cast<std::int64_t>(index),result_digest},ordinal,checksum};return true;
}
bool valid(const CallerVerifiedCommittedResult& result,const SwapReconcileStoreBinding& binding) noexcept {
    return !binding.incarnation.is_zero() && !binding.authority_domain.is_zero() &&
        result.binding==binding && result.index>0 && !result.result_digest.is_zero();
}
}

SwapReconcileStore::SwapReconcileStore(std::filesystem::path path,SwapReconcileStoreBinding binding,
    SwapResultWatermark& watermark,SwapReconcileStoreFaultHook hook)
    :directory_path_(std::move(path)),leaf_(directory_path_.filename().string()),binding_(binding),watermark_(watermark),hook_(hook){}
SwapReconcileStore::~SwapReconcileStore(){
    if(file_>=0)::close(file_);
    if(directory_>=0)::close(directory_);
    if(parent_>=0)::close(parent_);
}
SwapReconcileStoreCode SwapReconcileStore::fail(Code code) noexcept {
    status_=code;
    if(!binding_.incarnation.is_zero() && watermark_.incarnation()==binding_.incarnation)
        (void)watermark_.publish(0);
    return code;
}
bool SwapReconcileStore::checkpoint(Stage stage) noexcept {
    return !hook_.proceed || hook_.proceed(stage,hook_.context);
}
bool SwapReconcileStore::identity_current() const noexcept {
    return parent_>=0 && directory_>=0 && private_directory(directory_) &&
        named_inode(parent_,leaf_.c_str(),directory_) &&
        (file_<0 || (private_file(file_) && named_inode(directory_,current_name,file_)));
}
SwapReconcileStoreCode SwapReconcileStore::open_directory(bool create) {
    if(started_)return fail(Code::closed);
    started_=true;
    if(binding_.incarnation.is_zero() || binding_.authority_domain.is_zero() ||
        watermark_.incarnation()!=binding_.incarnation || watermark_.index()!=0 ||
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
SwapReconcileStoreCode SwapReconcileStore::create(const CallerVerifiedCommittedResult& result) {
    if(!valid(result,binding_))return fail(Code::invalid_input);
    if(const auto code=open_directory(true);code!=Code::none)return code;
    struct stat st{};
    if(::fstatat(directory_,current_name,&st,AT_SYMLINK_NOFOLLOW)==0)return fail(Code::already_exists);
    if(errno!=ENOENT)return fail(Code::io_failure);
    return replace(result,1);
}
SwapReconcileStoreCode SwapReconcileStore::replace(const CallerVerifiedCommittedResult& result,std::uint64_t ordinal) {
    if(!identity_current())return fail(Code::insecure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    Fd output(::openat(directory_,temporary_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    if(output.value<0)return fail(Code::io_failure);
    if(!private_file(output.value))return fail(Code::insecure);
    const auto bytes=encode(result,ordinal);
    if(!write_all(output.value,std::span(bytes).first(76)) || !checkpoint(Stage::partial_write) ||
        !write_all(output.value,std::span(bytes).subspan(76)) ||
        !checkpoint(Stage::temporary_sync) || !sync_fd(output.value))return fail(Code::io_failure);
    if(!identity_current() || !named_inode(directory_,temporary_name,output.value))return fail(Code::insecure);
    if(!checkpoint(Stage::rename) || ::renameat(directory_,temporary_name,directory_,current_name)!=0 ||
        !checkpoint(Stage::directory_sync) || !sync_fd(directory_) ||
        !checkpoint(Stage::before_publish))return fail(Code::io_failure);
    if(!private_file(output.value) || !named_inode(directory_,current_name,output.value))return fail(Code::insecure);
    SwapReconcileStoredResult stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    const bool published=latest_.ordinal==0?
        watermark_.restore({binding_.incarnation,stored.checksum,result.index}):watermark_.publish(result.index);
    if(!published)return fail(Code::watermark_conflict);
    if(file_>=0)::close(file_);
    file_=output.release();latest_=stored;status_=Code::none;return status_;
}
SwapReconcileStoreCode SwapReconcileStore::recover() {
    if(const auto code=open_directory(false);code!=Code::none)return code;
    file_=::openat(directory_,current_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(file_<0)return fail(errno==ENOENT?Code::missing:(errno==ELOOP?Code::insecure:Code::io_failure));
    if(!identity_current())return fail(Code::insecure);
    struct stat st{};
    if(::fstat(file_,&st)!=0)return fail(Code::io_failure);
    if(st.st_size!=static_cast<off_t>(kSwapReconcileStoreBytes))return fail(Code::corrupt);
    Bytes bytes{};std::size_t offset{};
    while(offset<bytes.size()) {
        const auto n=::read(file_,bytes.data()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return fail(Code::io_failure);
        offset+=static_cast<std::size_t>(n);
    }
    SwapReconcileStoredResult stored{};
    if(!decode(bytes,binding_,stored))return fail(Code::corrupt);
    if(!checkpoint(Stage::recovery_sync) || !sync_fd(file_) || !sync_fd(directory_))return fail(Code::io_failure);
    if(!identity_current())return fail(Code::insecure);
    if(!watermark_.restore({binding_.incarnation,stored.checksum,stored.result.index}))return fail(Code::watermark_conflict);
    latest_=stored;status_=Code::none;return status_;
}
SwapReconcileStoreCode SwapReconcileStore::persist(const CallerVerifiedCommittedResult& result) {
    if(status_!=Code::none)return Code::closed;
    if(!valid(result,binding_))return fail(Code::invalid_input);
    if(watermark_.index()!=latest_.result.index)return fail(Code::watermark_conflict);
    if(!identity_current())return fail(Code::insecure);
    if(const auto code=no_temporary(directory_);code!=Code::none)return fail(code);
    if(result.index<latest_.result.index ||
        (result.index==latest_.result.index && result.result_digest!=latest_.result.result_digest))return fail(Code::nonmonotonic);
    if(result.index==latest_.result.index)return Code::none;
    if(latest_.ordinal==std::numeric_limits<std::uint64_t>::max())return fail(Code::exhausted);
    return replace(result,latest_.ordinal+1);
}
}
