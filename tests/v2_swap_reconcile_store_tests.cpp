#include "velocita/v2/swap_reconcile_store.hpp"
#include "velocita/v2/digest.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace velocita::v2;
using Code = SwapReconcileStoreCode;
using Stage = SwapReconcileStoreStage;
void require(bool yes, const char* name) { if (!yes) throw std::runtime_error(name); }
Digest256 digest(unsigned n) { Digest256 d; d.bytes.fill(static_cast<std::byte>(n)); return d; }
const SwapReconcileStoreBinding binding{digest(1),digest(2)};
CallerVerifiedCommittedResult result(std::int64_t index=7) { return {binding,index,digest(static_cast<unsigned>(index))}; }
struct Scratch {
    std::filesystem::path base;
    Scratch() {
        const char* home=std::getenv("HOME"); require(home,"fixture home exists");
        auto root=std::filesystem::path(home)/".cache/velocita-build/swap-reconcile-store-tests";
        std::filesystem::create_directories(root);
        auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture temporary directory created"); base=name;
    }
    ~Scratch() { std::error_code ignored; std::filesystem::remove_all(base,ignored); }
    auto path() const { return base/"store"; }
    auto file() const { return path()/"result.bin"; }
};
std::vector<std::byte> bytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); std::vector<std::byte> out;
    char c; while(in.get(c)) out.push_back(static_cast<std::byte>(c)); return out;
}
void write(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size())); out.close();
    require(::chmod(path.c_str(),0600)==0,"fixture mode private");
}
void put64(std::vector<std::byte>& b,std::size_t offset,std::uint64_t value) {
    for(unsigned n=0;n<8;++n)b[offset+n]=static_cast<std::byte>(value>>(8*n));
}
void checksum(std::vector<std::byte>& b) {
    const auto hash=sha256_digest(std::span(b).first(120));
    std::copy(hash.bytes.begin(),hash.bytes.end(),b.begin()+120);
}
void bootstrap(const Scratch& s) {
    SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
    require(store.create(result())==Code::none,"fixture bootstrap persisted");
}
void create_recover_and_exact_bytes() {
    Scratch s;
    {
        SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
        require(store.create(result())==Code::none && w.index()==7,"bootstrap persists before publishing index seven");
        require(store.latest().ordinal==1,"bootstrap ordinal is one");
        auto b=bytes(s.file());
        require(b.size()==152,"canonical record has exactly 152 bytes");
        const std::array<std::byte,8> magic{std::byte{'V'},std::byte{'S'},std::byte{'R'},std::byte{'W'},std::byte{1},std::byte{},std::byte{},std::byte{}};
        require(std::equal(magic.begin(),magic.end(),b.begin()),"magic and version oracle");
        require(b[8]==std::byte{1} && b[16]==std::byte{7} &&
            std::all_of(b.begin()+9,b.begin()+16,[](auto v){return v==std::byte{};}) &&
            std::all_of(b.begin()+17,b.begin()+24,[](auto v){return v==std::byte{};}),"ordinal and index little endian oracle");
        require(std::all_of(b.begin()+24,b.begin()+56,[](auto v){return v==std::byte{1};}) &&
            std::all_of(b.begin()+56,b.begin()+88,[](auto v){return v==std::byte{2};}) &&
            std::all_of(b.begin()+88,b.begin()+120,[](auto v){return v==std::byte{7};}),"binding and result digest offsets oracle");
        const auto expected_checksum=parse_digest256("3e710aefaccbbbbbda872c6ff4cc69b3fbde4d1a56890ffd1ef065eb72e47a66");
        require(expected_checksum && std::equal(expected_checksum->bytes.begin(),expected_checksum->bytes.end(),b.begin()+120),"independent hashlib checksum oracle");
        require(store.persist(result(11))==Code::none && w.index()==11 && store.latest().ordinal==2,"higher result advances durable and visible index");
    }
    SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
    require(store.recover()==Code::none && w.index()==11 && store.latest().ordinal==2 &&
        store.latest().result.result_digest==digest(11),"fresh watermark restores validated durable result");
}
void missing_torn_corrupt_and_binding() {
    {
        Scratch s; SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
        require(store.recover()==Code::missing && w.index()==-1 && !std::filesystem::exists(s.path()),"missing recovery closes without bootstrap");
    }
    for(unsigned mutation=0;mutation<6;++mutation) {
        Scratch s; bootstrap(s); auto b=bytes(s.file());
        if(mutation==0)b.pop_back();
        if(mutation==1)b[119]^=std::byte{1};
        if(mutation==2){b[24]=std::byte{3};checksum(b);}
        if(mutation==3){b[56]=std::byte{3};checksum(b);}
        if(mutation==4){put64(b,16,0);checksum(b);}
        if(mutation==5)b.push_back(std::byte{});
        write(s.file(),b);
        SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
        require(store.recover()==Code::corrupt && w.index()==-1,"torn corrupt wrong binding invalid index and trailing bytes refuse");
    }
}
void duplicate_monotonic_and_exhaustion() {
    {
        Scratch s; SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
        require(store.create(result())==Code::none,"fixture create");auto original=bytes(s.file());
        require(store.persist(result())==Code::none && store.latest().ordinal==1 && bytes(s.file())==original,"identical result duplicate is idempotent");
        auto conflicting=result();conflicting.result_digest=digest(9);
        require(store.persist(conflicting)==Code::nonmonotonic && w.index()==-1 && bytes(s.file())==original,"same index different digest closes without overwrite");
    }
    {
        Scratch s; SwapResultWatermark w(binding.incarnation); SwapReconcileStore store(s.path(),binding,w);
        require(store.create(result())==Code::none,"fixture create");
        require(store.persist(result(6))==Code::nonmonotonic && w.index()==-1,"lower committed index closes");
    }
    {
        Scratch s;bootstrap(s);auto b=bytes(s.file());put64(b,8,UINT64_MAX);checksum(b);write(s.file(),b);
        SwapResultWatermark w(binding.incarnation);SwapReconcileStore store(s.path(),binding,w);
        require(store.recover()==Code::none,"fixture maximum ordinal recovers");
        require(store.persist(result(8))==Code::exhausted && w.index()==-1,"ordinal exhaustion closes without wrap");
    }
}
struct Fault {
    Stage target;bool armed{};SwapResultWatermark* watermark{};std::int64_t expected{};bool early_publication{};
    static bool proceed(Stage stage,void* context) noexcept {
        auto& f=*static_cast<Fault*>(context);
        if(f.armed && f.watermark->index()!=f.expected)f.early_publication=true;
        return !f.armed || stage!=f.target;
    }
};
void fault_boundaries_never_publish() {
    for(Stage stage:{Stage::partial_write,Stage::temporary_sync,Stage::rename,Stage::directory_sync,Stage::before_publish}) {
        Scratch s;
        {
            SwapResultWatermark w(binding.incarnation);Fault f{stage,false,&w,7,false};
            SwapReconcileStore store(s.path(),binding,w,{Fault::proceed,&f});
            require(store.create(result())==Code::none,"fixture before fault");f.armed=true;
            require(store.persist(result(11))==Code::io_failure && w.index()==-1 && !f.early_publication,"failed durability never publishes higher index and closes");
            require(store.persist(result(12))==Code::closed && w.index()==-1,"failed store never retries automatically");
        }
        SwapResultWatermark recovered(binding.incarnation);SwapReconcileStore store(s.path(),binding,recovered);
        if(stage==Stage::directory_sync || stage==Stage::before_publish)
            require(store.recover()==Code::none && recovered.index()==11,"renamed but unpublished higher state recovers conservatively");
        else require(store.recover()==Code::temporary_present && recovered.index()==-1,"leftover temporary refuses recovery without cleanup");
    }
    {
        Scratch s;SwapResultWatermark w(binding.incarnation);Fault f{Stage::parent_sync,true,&w,0,false};
        SwapReconcileStore store(s.path(),binding,w,{Fault::proceed,&f});
        require(store.create(result())==Code::io_failure && w.index()==-1 && !f.early_publication,"new directory parent durability failure closes");
    }
    {
        Scratch s;bootstrap(s);SwapResultWatermark w(binding.incarnation);Fault f{Stage::recovery_sync,true,&w,0,false};
        SwapReconcileStore store(s.path(),binding,w,{Fault::proceed,&f});
        require(store.recover()==Code::io_failure && w.index()==-1,"recovery fsync failure never restores watermark");
    }
}
struct Crash {
    Stage target;bool armed{};
    static bool proceed(Stage stage,void* context) noexcept {
        auto& crash=*static_cast<Crash*>(context);
        if(crash.armed && stage==crash.target)::_exit(77);
        return true;
    }
};
void abrupt_exit_recovery() {
    for(Stage stage:{Stage::partial_write,Stage::rename,Stage::directory_sync,Stage::before_publish}) {
        Scratch s;const auto child=::fork();require(child>=0,"fixture fork starts");
        if(child==0) {
            SwapResultWatermark w(binding.incarnation);Crash crash{stage,false};
            SwapReconcileStore store(s.path(),binding,w,{Crash::proceed,&crash});
            if(store.create(result())!=Code::none)::_exit(79);
            crash.armed=true;(void)store.persist(result(11));::_exit(80);
        }
        int status{};require(::waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77,
            "child exits at selected persistence boundary without destructors");
        SwapResultWatermark w(binding.incarnation);SwapReconcileStore recovered(s.path(),binding,w);
        if(stage==Stage::partial_write || stage==Stage::rename)
            require(recovered.recover()==Code::temporary_present && w.index()==-1,"crashed temporary never silently repaired");
        else require(recovered.recover()==Code::none && w.index()==11,"crashed renamed higher record restores after validation");
    }
}
void filesystem_safety_and_single_writer() {
    for(unsigned variant=0;variant<5;++variant) {
        Scratch s;bootstrap(s);
        if(variant==0)require(::chmod(s.file().c_str(),0644)==0,"fixture public file");
        if(variant==1)require(::chmod(s.path().c_str(),0755)==0,"fixture public directory");
        if(variant==2){std::filesystem::rename(s.file(),s.path()/"elsewhere");std::filesystem::create_symlink("elsewhere",s.file());}
        if(variant==3){std::filesystem::remove(s.file());require(::mkfifo(s.file().c_str(),0600)==0,"fixture fifo");}
        if(variant==4)std::filesystem::create_hard_link(s.file(),s.path()/"alias");
        SwapResultWatermark w(binding.incarnation);SwapReconcileStore store(s.path(),binding,w);
        require(store.recover()==Code::insecure && w.index()==-1,"insecure symlink fifo hardlink and permissions refuse");
    }
    {
        Scratch s;bootstrap(s);std::filesystem::create_directory_symlink(s.path(),s.base/"link");
        SwapResultWatermark w(binding.incarnation);SwapReconcileStore store(s.base/"link",binding,w);
        require(store.recover()==Code::insecure && w.index()==-1,"symlink directory refuses");
    }
    {
        Scratch s;SwapResultWatermark a(binding.incarnation),b(binding.incarnation);
        SwapReconcileStore first(s.path(),binding,a),second(s.path(),binding,b);
        require(first.create(result())==Code::none,"fixture first owner");
        require(second.recover()==Code::locked && b.index()==-1 && a.index()==7,"second owner fails without invalidating first watermark");
    }
}
void invalid_input_and_replaced_file() {
    {
        Scratch s;SwapResultWatermark w(binding.incarnation);auto bad=result();bad.binding.authority_domain=digest(99);
        SwapReconcileStore store(s.path(),binding,w);
        require(store.create(bad)==Code::invalid_input && w.index()==-1 && !std::filesystem::exists(s.path()),"wrong authenticated input binding fails before disk creation");
    }
    {
        Scratch s;SwapResultWatermark w(binding.incarnation);SwapReconcileStore store(s.path(),binding,w);
        require(store.create(result())==Code::none,"fixture create");
        auto b=bytes(s.file());std::filesystem::rename(s.file(),s.path()/"old");write(s.file(),b);
        require(store.persist(result(11))==Code::insecure && w.index()==-1,"file replacement while owned refuses");
    }
    {
        Scratch s;SwapResultWatermark w(binding.incarnation);SwapReconcileStore store(s.path(),binding,w);
        require(store.create(result())==Code::none && w.publish(9),"fixture unauthorized external watermark update");
        require(store.persist(result(11))==Code::watermark_conflict && w.index()==-1,"external watermark update cannot bypass durable owner");
    }
}
}

int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> cases[]{
        {"create_recover_and_exact_bytes",create_recover_and_exact_bytes},
        {"missing_torn_corrupt_and_binding",missing_torn_corrupt_and_binding},
        {"duplicate_monotonic_and_exhaustion",duplicate_monotonic_and_exhaustion},
        {"fault_boundaries_never_publish",fault_boundaries_never_publish},
        {"abrupt_exit_recovery",abrupt_exit_recovery},
        {"filesystem_safety_and_single_writer",filesystem_safety_and_single_writer},
        {"invalid_input_and_replaced_file",invalid_input_and_replaced_file}};
    for(const auto& [name,run]:cases)if(argc==1 || std::string(argv[1])==name) {
        try {run();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}
    }
}
