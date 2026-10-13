#include "velocita/v2/archive_store.hpp"
#include "velocita/v2/wal_arena.hpp"
#include "v2_identity_test_support.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static ArchivePublishResult publish(ArchiveStore& store,const ArchivePublicationContext& context,const ArchiveRecordSource& source) {
        return store.publish(context,source);
    }
};
}
namespace {
using namespace velocita::v2;
void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
struct Scratch {
    std::filesystem::path path;
    Scratch(){auto parent=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/operational-recovery-tests";
        std::filesystem::create_directories(parent);auto name=(parent/"case-XXXXXX").string();require(::mkdtemp(name.data()),"scratch created");path=name;}
    ~Scratch(){std::error_code e;std::filesystem::remove_all(path,e);}
};
const auto& identity(){static auto v=identity_test::verified_identity(identity_test::identity(1,{1}));return v;}
std::vector<std::byte> command_bytes(){Command c{{CallerId{1},RequestSequence{1},{}},AdvanceKillEpoch{KillEpoch{1}}};c.identity.payload_digest.bytes[0]=std::byte{1};
    std::array<std::byte,kMaxKernelCommandBytes> b{};auto e=encode_kernel_command(c,b);require(e.error==KernelCodecError::none,"command encoded");return {b.begin(),b.begin()+e.bytes_written};}
std::vector<std::byte> block(std::uint64_t index){const auto r=encode_bound_wal_record({index,1,1,0,command_bytes()},identity());return frame_block(index,index,r);}
void write(const std::filesystem::path& p,std::span<const std::byte> b){int fd=::open(p.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);require(fd>=0,"fixture file opens");require(::write(fd,b.data(),b.size())==static_cast<ssize_t>(b.size()),"fixture bytes written");require(::fsync(fd)==0,"fixture synced");::close(fd);}
std::vector<std::byte> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::vector<char>b{std::istreambuf_iterator<char>{f},{}};std::vector<std::byte>out;for(char c:b)out.push_back(static_cast<std::byte>(c));return out;}
void wal_refused_preserves(std::vector<std::byte> bytes,const char* oracle){Scratch s;const auto path=s.path/"v2-wal-blocks.bin";write(path,bytes);bool refused=false;
    try{WalSegment segment(s.path,identity());}catch(const BoundWalError&){refused=true;}
    require(refused,oracle);require(read(path)==bytes,"WAL refusal preserves every byte");}
void partial_tail(){auto bytes=block(1),tail=block(2);bytes.insert(bytes.end(),tail.begin(),tail.begin()+173);wal_refused_preserves(bytes,"nonzero partial WAL tail is never silently discarded");}
void zero_gap(){auto bytes=block(1);bytes.resize(2*kWalBlockBytes);auto tail=block(2);bytes.insert(bytes.end(),tail.begin(),tail.end());wal_refused_preserves(bytes,"nonzero WAL block after zero padding is never silently discarded");}
void torn_block(){auto bytes=block(1),tail=block(2);tail[100]^=std::byte{1};bytes.insert(bytes.end(),tail.begin(),tail.end());wal_refused_preserves(bytes,"corrupt final complete block is not guessed uncommitted");}
void zero_padding(){Scratch s;auto bytes=block(1);bytes.resize(3*kWalBlockBytes+71);write(s.path/"v2-wal-blocks.bin",bytes);WalSegment segment(s.path,identity());
    auto records=segment.read_records();require(records.size()==1 && records[0].index==1,"zero padding retains the exact record prefix");WalBlockArena arena;const auto r=encode_bound_wal_record({2,1,1,0,command_bytes()},identity());require(arena.stage(r,2)==StageOutcome::staged,"append staged");arena.seal_open_block();require(segment.flush(arena)==1,"append durable");records=segment.read_records();require(records.size()==2 && records[1].index==2,"append reuses padding without losing records");}
struct ArchiveFixture {
    Scratch scratch;ArchiveStore store;std::array<std::byte,kMaxStaticIdentityBytes> ib{};std::array<std::byte,kMaxExecutionCatalogBytes> eb{};std::size_t in{},en{};std::vector<std::byte> payload{command_bytes()};std::uint64_t index{1};
    static bool source(void* context,std::uint32_t n,ArchiveRecordView& view)noexcept{if(n)return false;auto& f=*static_cast<ArchiveFixture*>(context);view={f.index,1,1,0,f.payload};return true;}
    ArchiveFixture(){auto i=encode_static_identity(identity().value(),ib);in=i.bytes_written;auto e=catalog_test::standard_catalog().encode(eb);en=e.bytes_written;
        require(i.error==StaticIdentityCodecError::none && e.error==ExecutionCatalogError::none,"archive identity encoded");require(open()==ArchiveStoreError::none,"archive seeded");
        require(ArchiveStoreTestAccess::publish(store,{1,1,1,1,1,0,0},{this,1,source}).error==ArchiveStoreError::none,"archive published");store=ArchiveStore{};}
    auto dir()const{return scratch.path/"v2-archive";}
    ArchiveStoreError open(){return ArchiveStore::open(scratch.path,identity(),std::span(ib).first(in),std::span(eb).first(en),catalog_test::standard_catalog(),{},store);}
};
void archive_temporaries(){for(const char* base:{"v2-archive-segment.bin","v2-archive-catalog.bin"}){ArchiveFixture f;const auto temp=f.dir()/(std::string(base)+".tmp"),quarantine=f.dir()/(std::string(base)+".quarantine");
    const std::vector<std::byte> bytes{std::byte{7},std::byte{0},std::byte{9}};write(temp,bytes);struct stat before{};require(::stat(temp.c_str(),&before)==0,"temporary identity captured");const auto catalog=read(f.dir()/"v2-archive-catalog.bin");
    require(f.open()==ArchiveStoreError::none,"verified archive quarantines recognized publication temporary");struct stat after{};require(::stat(quarantine.c_str(),&after)==0 && before.st_ino==after.st_ino && before.st_dev==after.st_dev,"quarantine preserves exact file identity");
    require(!std::filesystem::exists(temp) && read(quarantine)==bytes && read(f.dir()/"v2-archive-catalog.bin")==catalog && f.store.covered_through()==1,"quarantine preserves bytes and published coverage");f.store=ArchiveStore{};require(f.open()==ArchiveStoreError::none,"quarantine restart remains readable");f.index=2;
    require(ArchiveStoreTestAccess::publish(f.store,{2,1,2,2,1,1,0},{&f,1,ArchiveFixture::source}).error==ArchiveStoreError::none && f.store.covered_through()==2 && read(quarantine)==bytes,"quarantined evidence survives subsequent archive publication");}}
void archive_invalid_history(){for(unsigned mode=0;mode<2;++mode){ArchiveFixture f;auto file=f.dir()/"v2-archive-catalog.bin";
    if(mode)for(auto&e:std::filesystem::directory_iterator(f.dir()))if(e.path().filename().string().starts_with("s-"))file=e.path();
    auto damaged=read(file);damaged.back()^=std::byte{1};write(file,damaged);const auto temp=f.dir()/"v2-archive-segment.bin.tmp";const std::vector<std::byte>b{std::byte{5}};write(temp,b);
    require(f.open()!=ArchiveStoreError::none,"invalid published archive refuses recovery");require(read(temp)==b && !std::filesystem::exists(f.dir()/"v2-archive-segment.bin.quarantine"),"archive verification precedes quarantine mutation");}}
void archive_unknown(){ArchiveFixture f;const std::vector<std::byte>b{std::byte{4}};const auto temp=f.dir()/"v2-archive-segment.bin.tmp";write(temp,b);const auto unknown=f.dir()/("s-"+std::string(64,'f')+".bin");write(unknown,b);
    require(f.open()==ArchiveStoreError::unexpected_entry,"unreferenced final segment requires operator recovery");require(read(temp)==b && read(unknown)==b && !std::filesystem::exists(f.dir()/"v2-archive-segment.bin.quarantine"),"unknown history is not adopted or moved");}
void archive_collision(){ArchiveFixture f;const auto temp=f.dir()/"v2-archive-segment.bin.tmp",saved=f.dir()/"v2-archive-segment.bin.quarantine";const std::vector<std::byte>a{std::byte{1}},b{std::byte{2}};write(temp,a);write(saved,b);
    require(f.open()!=ArchiveStoreError::none,"occupied quarantine destination cannot be replaced");require(read(temp)==a && read(saved)==b,"quarantine collision preserves both histories");}
void archive_bad_file(){for(unsigned mode=0;mode<3;++mode){ArchiveFixture f;const auto temp=f.dir()/"v2-archive-segment.bin.tmp";const std::vector<std::byte>b{std::byte{3}};
    if(mode==0)std::filesystem::create_symlink("v2-archive-catalog.bin",temp);else{write(temp,b);if(mode==1)require(::chmod(temp.c_str(),0644)==0,"insecure fixture mode");else std::filesystem::create_hard_link(temp,f.dir()/"alias");}
    require(f.open()!=ArchiveStoreError::none,"nonprivate or aliased temporary refuses quarantine");require(std::filesystem::exists(temp),"refused temporary remains preserved");}}
}
int main(int argc,char**argv){const std::pair<const char*,void(*)()>cases[]={{"partial_tail",partial_tail},{"zero_gap",zero_gap},{"torn_block",torn_block},{"zero_padding",zero_padding},{"archive_temporaries",archive_temporaries},{"archive_invalid_history",archive_invalid_history},{"archive_unknown",archive_unknown},{"archive_collision",archive_collision},{"archive_bad_file",archive_bad_file}};
unsigned ran{},failed{};for(auto[name,test]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return ran&&!failed?0:1;}
