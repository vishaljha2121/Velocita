#include "velocita/v2/archive_store.hpp"
#include "v2_identity_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <unistd.h>

namespace velocita::v2 {
struct ArchiveStoreTestAccess {
    static ArchivePublishResult publish(ArchiveStore& store,
        const ArchivePublicationContext& context, const ArchiveRecordSource& source) {
        return store.publish(context, source);
    }
};
}

namespace {
using namespace velocita::v2;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<std::byte> hex(std::string_view text){std::vector<std::byte> out;for(std::size_t i=0;i<text.size();i+=2)out.push_back(std::byte(std::stoul(std::string{text.substr(i,2)},nullptr,16)));return out;}
struct Temp { std::filesystem::path path; Temp(){std::array<char,64> p{};std::strcpy(p.data(),"/tmp/v2-archive-store-XXXXXX");path=::mkdtemp(p.data());} ~Temp(){std::error_code e;std::filesystem::remove_all(path,e);} };
struct Source { std::vector<std::byte> payload; static bool get(void* p,std::uint32_t n,ArchiveRecordView& out) noexcept {if(n)return false;auto& s=*static_cast<Source*>(p);out={1,3,7,0,s.payload};return true;} };
std::vector<std::byte> maximum_command(){
    auto digest=[] { Digest256 d;d.bytes[0]=std::byte{1};return d; };
    StageExecutablePolicy stage;stage.feature_schema.schema_version=kFeatureSchemaArtifactSchemaVersion;stage.feature_schema.feature_schema_id=FeatureSchemaId{3};stage.feature_schema.feature_count=kMaxDecisionFeatures;
    for(auto& f:stage.feature_schema.features)f={FeatureEconomicKind::price,1};stage.feature_schema.artifact_digest=compute_artifact_digest(stage.feature_schema).digest;
    auto& p=stage.executable_policy;p.schema_version=kExecutablePolicyArtifactSchemaVersion;p.policy_id=PolicyId{17};p.policy_version=PolicyVersion{2};p.policy_artifact_digest=digest();p.feature_schema_id=FeatureSchemaId{3};p.feature_schema_digest=stage.feature_schema.artifact_digest;p.template_count=kMaxExecutableActionTemplates;
    for(std::size_t i=0;i<p.template_count;++i){auto&a=p.templates[i];a.schema_version=kActionTemplateSchemaVersion;a.directive=Directive::place_limit;a.side=Side::buy;a.time_in_force=TimeInForce::good_til_cancel;a.post_only=PostOnlyMode::required;a.order_slot=LogicalOrderSlot{static_cast<std::uint16_t>(i+1)};a.instrument_spec_version=InstrumentSpecVersion{6};a.venue_capabilities_version=VenueCapabilitiesVersion{7};a.price_source={EconomicValueSourceKind::feature,static_cast<std::uint16_t>(i),RoundingMode::toward_zero,{},1,{}};a.quantity_source={EconomicValueSourceKind::fixed,0,RoundingMode::toward_zero,QuantityLots::from_atoms(1),0};a.safety_caps={QuantityLots::from_atoms(2),MoneyMicros::from_atoms(1000),PriceTicks::from_atoms(2),PriceTicks::from_atoms(1),100,a.price_source};}
    p.rule_binding_count=kMaxPolicyRules;for(std::size_t i=0;i<p.rule_binding_count;++i)p.rule_template_slots[i]=ActionTemplateSlot{static_cast<std::uint8_t>(i%kMaxExecutableActionTemplates+1)};p.artifact_digest=compute_artifact_digest(p).digest;
    stage.instrument={kInstrumentExecutionProfileSchemaVersion,VenueId{8},InstrumentId{4},InstrumentSpecVersion{6},MoneyMicros::from_atoms(10),PriceTicks::from_atoms(1),PriceTicks::from_atoms(100),QuantityLots::from_atoms(1),QuantityLots::from_atoms(10)};stage.venue={kVenueCapabilitiesSchemaVersion,VenueId{8},VenueCapabilitiesVersion{7},kVenueDirectivePlaceLimit,kVenueTifGtc,1};Command command{{CallerId{12},RequestSequence{11},digest()},stage};std::array<std::byte,kMaxKernelCommandBytes> out{};auto e=encode_kernel_command(command,out);require(e.error==KernelCodecError::none&&e.bytes_written==out.size(),"max command");return {out.begin(),out.end()};
}
struct ManySource{std::vector<std::byte> payload;std::uint32_t count{};static bool get(void*p,std::uint32_t n,ArchiveRecordView&out)noexcept{auto&s=*static_cast<ManySource*>(p);if(n>=s.count)return false;out={static_cast<std::uint64_t>(n+1),3,static_cast<std::int32_t>(n+1),0,s.payload};return true;}};
struct OffsetSource{std::vector<std::byte> payload;std::uint64_t index{};std::int64_t term{3};static bool get(void*p,std::uint32_t n,ArchiveRecordView&out)noexcept{if(n)return false;auto&s=*static_cast<OffsetSource*>(p);out={s.index,s.term,static_cast<std::int32_t>(s.index),0,s.payload};return true;}};
std::vector<std::filesystem::path> segments(const std::filesystem::path& root){std::vector<std::filesystem::path> out;for(const auto&e:std::filesystem::directory_iterator(root/"v2-archive")){const auto n=e.path().filename().string();if(n.starts_with("s-")&&n.ends_with(".bin"))out.push_back(e.path());}return out;}
using ArchiveImage=std::vector<std::pair<std::string,std::vector<char>>>;
ArchiveImage archive_image(const std::filesystem::path& root){ArchiveImage out;for(const auto&e:std::filesystem::directory_iterator(root/"v2-archive")){std::ifstream input(e.path(),std::ios::binary);out.emplace_back(e.path().filename().string(),std::vector<char>{std::istreambuf_iterator<char>{input},{}});}std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.first<b.first;});return out;}
}

int main(){
    Temp temp;
    const auto& catalog=catalog_test::standard_catalog();
    const auto row=catalog_test::row(AccountId{5},{kInstrumentExecutionProfileSchemaVersion,VenueId{9},InstrumentId{4},InstrumentSpecVersion{7},MoneyMicros::from_atoms(100),PriceTicks::from_atoms(1),PriceTicks::from_atoms(100),QuantityLots::from_atoms(1),QuantityLots::from_atoms(10)},{kVenueCapabilitiesSchemaVersion,VenueId{9},VenueCapabilitiesVersion{8},kVenueDirectivePlaceLimit|kVenueDirectiveCancel,kVenueTifGtc,1});
    std::array<std::byte,kMaxExecutionCatalogBytes> execution_bytes{};auto ec=encode_execution_catalog(std::span{&row,1},execution_bytes);require(ec.error==ExecutionCatalogError::none,"catalog encode");
    auto raw_identity=identity_test::identity(17,{17},catalog);auto identity=identity_test::verified_identity(raw_identity);
    std::array<std::byte,kMaxStaticIdentityBytes> identity_bytes{};auto ie=encode_static_identity(raw_identity,identity_bytes);require(ie.error==StaticIdentityCodecError::none,"identity encode");
    ArchiveStore store;require(ArchiveStore::open(temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},store)==ArchiveStoreError::none,"fresh open");
    Source source{hex("56324b43380000000900060001000000000000000100000000000000010000000000000000000000000000000000000000000000000000000000000001000000000000007ebfc094cb08c72268b6e92acc835d5b5bbee17c517e564750290c1a437a118b")};
    ArchiveRecordSource records{&source,1,&Source::get};
    auto published=ArchiveStoreTestAccess::publish(store,{1,1,1,1,3,0,0},records);if(published.error!=ArchiveStoreError::none)throw std::runtime_error("publish error "+std::to_string(static_cast<unsigned>(published.error)));require(store.covered_through()==1,"coverage");require(store.consume_for_reclamation(std::move(published.capability),1,1),"one-shot capability");require(!published.capability.local_publication_durable(),"move/consume invalidation");require(!store.consume_for_reclamation(std::move(published.capability),1,1),"consumed capability reuse must fail");
    store=ArchiveStore{};bool present=false;require(ArchiveStore::verify_existing(temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},present)==ArchiveStoreError::none&&present,"restart verification");
    std::ofstream(temp.path/"v2-archive"/"s-ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff.bin").put('x');
    require(ArchiveStore::verify_existing(temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},present)==ArchiveStoreError::unexpected_entry,"orphan refusal");

    Temp split_temp;ArchiveStore split;require(ArchiveStore::open(split_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},split)==ArchiveStoreError::none,"split open");
    ManySource many{maximum_command(),2264};ArchiveRecordSource many_records{&many,many.count,&ManySource::get};
    auto two=ArchiveStoreTestAccess::publish(split,{1,1,many.count,many.count,3,0,0},many_records);
    require(two.error==ArchiveStoreError::none,"two-segment publication must fit hard quota");
    require(split.covered_through()==many.count&&split.last_term()==3,"two-segment coverage boundary");

    Temp escrow_temp;ArchiveStore escrow;ArchiveStoreConfig one_object{};one_object.object_limit=1;require(ArchiveStore::open(escrow_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,one_object,escrow)==ArchiveStoreError::none,"escrow open");
    Source one{source.payload};ArchiveRecordSource one_record{&one,1,&Source::get};auto early=ArchiveStoreTestAccess::publish(escrow,{1,1,1,1,3,0,1},one_record);require(early.error==ArchiveStoreError::capacity_exceeded,"underfilled cut must reserve second object");require(segments(escrow_temp.path).empty(),"quota refusal must precede segment side effect");

    Temp full_temp;ArchiveStore full;require(ArchiveStore::open(full_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,one_object,full)==ArchiveStoreError::none,"full open");require(ArchiveStoreTestAccess::publish(full,{1,1,1,1,3,0,0},one_record).error==ArchiveStoreError::none,"fill sole catalog slot");OffsetSource full_second{source.payload,2,3};ArchiveRecordSource full_second_record{&full_second,1,&OffsetSource::get};const auto before_full_image=archive_image(full_temp.path);auto full_result=ArchiveStoreTestAccess::publish(full,{2,1,2,2,3,3,0},full_second_record);require(full_result.error==ArchiveStoreError::capacity_exceeded,"full catalog must classify as capacity");require(full.catalog_generation()==1&&archive_image(full_temp.path)==before_full_image,"full catalog refusal must leave namespace and every byte unchanged");

    Temp lane_temp;ArchiveStore lanes;ArchiveStoreConfig five{};five.object_limit=5;require(ArchiveStore::open(lane_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,five,lanes)==ArchiveStoreError::none,"lane open");auto first=ArchiveStoreTestAccess::publish(lanes,{1,1,1,1,3,0,0},one_record);require(first.error==ArchiveStoreError::none,"lane seed");require(!lanes.can_admit(1,false)&&lanes.can_admit(1,true),"ordinary high-water must preserve protected object margin");

    const auto split_files=segments(split_temp.path);require(split_files.size()==2,"two objects named");split=ArchiveStore{};std::filesystem::permissions(split_files.front(),std::filesystem::perms::owner_read|std::filesystem::perms::owner_write|std::filesystem::perms::group_read|std::filesystem::perms::others_read,std::filesystem::perm_options::replace);present=false;require(ArchiveStore::verify_existing(split_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},present)==ArchiveStoreError::segment_failed,"0644 named segment must refuse restart");

    Temp maximum_temp;ArchiveStore maximum;require(ArchiveStore::open(maximum_temp.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},maximum)==ArchiveStoreError::none,"maximum open");ManySource all{maximum_command(),8192};ArchiveRecordSource all_records{&all,all.count,&ManySource::get};auto packed=ArchiveStoreTestAccess::publish(maximum,{1,1,8192,8192,3,0,0},all_records);require(packed.error==ArchiveStoreError::none,"8192 records must pack into four segments");require(segments(maximum_temp.path).size()==4&&maximum.catalog_generation()==1&&maximum.covered_through()==8192,"maximum catalog must contain four objects at generation one");

    auto open_store=[&](Temp& t,ArchiveStore& s){require(ArchiveStore::open(t.path,identity,std::span{identity_bytes}.first(ie.bytes_written),std::span{execution_bytes}.first(ec.bytes_written),catalog,{},s)==ArchiveStoreError::none,"adversarial open");};
    OffsetSource second{source.payload,2,3};ArchiveRecordSource second_record{&second,1,&OffsetSource::get};
    Temp removed_temp;ArchiveStore removed;open_store(removed_temp,removed);require(ArchiveStoreTestAccess::publish(removed,{1,2,1,1,3,0,0},one_record).error==ArchiveStoreError::none,"removed seed");auto old=segments(removed_temp.path);require(old.size()==1,"removed seed segment");std::filesystem::remove(old.front());auto removed_result=ArchiveStoreTestAccess::publish(removed,{2,2,2,2,3,3,0},second_record);require(removed_result.error!=ArchiveStoreError::none&&removed.catalog_generation()==1&&segments(removed_temp.path).empty(),"missing prior segment must refuse before publication");

    Temp orphan_temp;ArchiveStore orphan;open_store(orphan_temp,orphan);require(ArchiveStoreTestAccess::publish(orphan,{1,2,1,1,3,0,0},one_record).error==ArchiveStoreError::none,"orphan seed");std::ofstream(orphan_temp.path/"v2-archive"/"v2-archive-segment.bin.tmp").put('x');auto orphan_result=ArchiveStoreTestAccess::publish(orphan,{2,2,2,2,3,3,0},second_record);require(orphan_result.error==ArchiveStoreError::unexpected_entry&&orphan.catalog_generation()==1&&segments(orphan_temp.path).size()==1,"open-store temp must refuse before mutation");

    Temp stale_temp;ArchiveStore stale;open_store(stale_temp,stale);require(ArchiveStoreTestAccess::publish(stale,{1,2,1,1,3,0,0},one_record).error==ArchiveStoreError::none,"stale seed");auto stale_result=ArchiveStoreTestAccess::publish(stale,{2,1,2,2,3,3,0},second_record);require(stale_result.error==ArchiveStoreError::stale_generation&&stale.catalog_generation()==1&&segments(stale_temp.path).size()==1,"older storage generation must refuse before writes");

    Temp bounds_temp;ArchiveStore bounds;open_store(bounds_temp,bounds);OffsetSource bad{source.payload,2,4};ArchiveRecordSource bad_record{&bad,1,&OffsetSource::get};auto bad_result=ArchiveStoreTestAccess::publish(bounds,{1,1,1,1,3,0,0},bad_record);require(bad_result.error!=ArchiveStoreError::none&&bounds.catalog_generation()==0&&segments(bounds_temp.path).empty(),"invalid source coordinates and term must refuse before writes");
    return 0;
}
