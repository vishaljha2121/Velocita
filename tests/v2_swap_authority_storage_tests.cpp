#include "velocita/v2/archive_segment.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/bound_wal.hpp"
#include "velocita/v2/replication.hpp"
#include "velocita/v2/leader_read_barrier.hpp"
#include "v2_identity_test_support.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace velocita::v2;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
VerifiedStaticIdentity identity(int node=1){return identity_test::verified_identity(identity_test::identity(node,{1,2,3}));}
std::vector<std::byte> marker(bool wrong_domain=false){
    AuthorityBarrier request;request.incarnation[0]=std::byte{1};request.request_sequence=7;request.effect_id=EffectId{5};
    request.expected_effect_payload_digest.bytes[0]=std::byte{9};request.expected_kill_epoch=KillEpoch{2};request.expected_executor_generation=ExecutorGeneration{3};
    request.catalog_root=identity().value().catalog_root;request.authority_domain=identity().authority_domain();if(wrong_domain)request.authority_domain.bytes[0]^=std::byte{1};
    std::array<std::byte,kAuthorityBarrierBytes> bytes{};require(encode_authority_barrier(kAuthorityBarrierEntryType,request,bytes).error==AuthorityBarrierCodecError::none,"valid inner authority payload");return {bytes.begin(),bytes.end()};
}
velocita::WalRecord record(){return {1,1,7,kSwapAuthorityBarrierEntryType,marker()};}
void wal_roundtrip_and_refusal(){
    const auto value=record();std::vector<std::byte> framed;bool admitted=true;
    try{framed=encode_bound_wal_record(value,identity());}catch(const BoundWalError&){admitted=false;}
    require(admitted,"type102 WAL encoder accepts exact marker");const auto decoded=decode_bound_wal_record(framed,identity());
    require(decoded.index==1 && decoded.term==1 && decoded.entry_id==7 && decoded.entry_type==102 && decoded.payload==value.payload,"type102 WAL preserves exact coordinates type and marker");
    for(unsigned n=0;n<4;++n){auto bad=value;if(n==0)bad.entry_type=103;if(n==1)bad.payload=marker(true);if(n==2)bad.payload.back()^=std::byte{1};if(n==3)bad.payload.pop_back();
        bool encode_refused{},decode_refused{};try{(void)encode_bound_wal_record(bad,identity());}catch(const BoundWalError&){encode_refused=true;}
        const auto raw=identity_test::raw_record(bad,identity());try{(void)decode_bound_wal_record(raw,identity());}catch(const BoundWalError&){decode_refused=true;}
        require(encode_refused && decode_refused,"type102 WAL refuses wrong type domain checksum and truncated marker");}
}
void peer_roundtrip_and_refusal(){
    const auto payload=marker();velocita::PeerMessage message{1,2,velocita::AppendEntries{1,0,0,0,{{1,7,kSwapAuthorityBarrierEntryType,payload}}}};
    std::array<std::byte,kMaxBoundPeerBytes> bytes{};BoundFrameScratch scratch;const auto encoded=encode_bound_peer(message,identity(),bytes,scratch);
    require(encoded.error==BoundFrameError::none,"type102 owning peer encoder accepts exact marker");
    velocita::PeerMessage decoded;require(decode_bound_peer(std::span(bytes).first(encoded.bytes_written),identity(2),decoded,scratch)==BoundFrameError::none,"type102 peer decoder accepts exact marker");
    const auto& append=std::get<velocita::AppendEntries>(decoded.payload);require(append.entries.size()==1 && append.entries[0].type==102 && append.entries[0].payload==payload,"type102 peer roundtrip retains marker bytes");
    const std::array<BorrowedPeerLogEntry,1> entries{{{1,7,kSwapAuthorityBarrierEntryType,payload}}};const PeerMessageView borrowed{1,2,BorrowedAppendEntries{1,0,0,0,entries,0}};
    require(encode_bound_peer(borrowed,identity(),bytes,scratch).error==BoundFrameError::none,"type102 borrowed peer encoder accepts exact marker");
    for(unsigned n=0;n<4;++n){auto bad=message;auto& entry=std::get<velocita::AppendEntries>(bad.payload).entries[0];if(n==0)entry.type=103;if(n==1)entry.payload=marker(true);if(n==2)entry.payload.back()^=std::byte{1};if(n==3)entry.payload.pop_back();
        require(encode_bound_peer(bad,identity(),bytes,scratch).error!=BoundFrameError::none,"type102 peer encode refuses malformed marker");
        const auto raw=identity_test::frame_peer_body(bad,identity());require(decode_bound_peer(raw,identity(2),decoded,scratch)!=BoundFrameError::none,"type102 peer decode refuses malformed marker");}
}
struct ArchiveSource {ArchiveRecordView record;};
bool read_record(void* context,std::uint32_t n,ArchiveRecordView& out)noexcept{if(n)return false;out=static_cast<ArchiveSource*>(context)->record;return true;}
struct ArchiveCapture {bool called{};bool exact{};std::span<const std::byte> expected;};
bool capture(const ArchiveRecordView& value,void* context)noexcept{auto& c=*static_cast<ArchiveCapture*>(context);c.called=true;c.exact=value.index==1 && value.term==1 && value.entry_id==7 && value.entry_type==102 && value.payload.size()==c.expected.size() && std::equal(value.payload.begin(),value.payload.end(),c.expected.begin());return true;}
void archive_roundtrip_and_refusal(){
    const auto payload=marker();ArchiveSource source{{1,1,7,kSwapAuthorityBarrierEntryType,payload}};ArchiveSegmentScratch scratch;std::array<std::byte,512> bytes{};
    const auto encoded=encode_archive_segment({&source,1,read_record},0,identity(),bytes,scratch);require(encoded.error==ArchiveSegmentError::none,"type102 archive encoder accepts retained marker");
    ArchiveCapture seen{false,false,payload};ArchiveSegmentMetadata metadata;require(decode_archive_segment(std::span(bytes).first(encoded.bytes_written),identity(),capture,&seen,scratch,metadata).error==ArchiveSegmentError::none && seen.called && seen.exact && metadata.record_count==1,"type102 archive decoder preserves exact marker history");
    auto corrupted=bytes;corrupted[kArchiveSegmentHeaderBytes+kArchiveRecordHeaderBytes+10]^=std::byte{1};seen.called=false;
    require(decode_archive_segment(std::span(corrupted).first(encoded.bytes_written),identity(),capture,&seen,scratch,metadata).error!=ArchiveSegmentError::none && !seen.called,"corrupt type102 archive refuses before visitor");
    for(unsigned n=0;n<4;++n){auto bad=payload;source.record.entry_type=n==0?103:kSwapAuthorityBarrierEntryType;if(n==1)bad=marker(true);if(n==2)bad.back()^=std::byte{1};if(n==3)bad.pop_back();source.record.payload=bad;const auto before=bytes;
        require(encode_archive_segment({&source,1,read_record},0,identity(),bytes,scratch).error!=ArchiveSegmentError::none && bytes==before,"type102 archive refuses malformed payload without output mutation");}
}
void cold_replay_never_grants(){
    const auto* home=std::getenv("HOME");require(home!=nullptr,"durable scratch HOME exists");const auto root=std::filesystem::path(home)/".cache/velocita-build"/("swap-authority-storage-"+std::to_string(::getpid()));
    require(!std::filesystem::exists(root),"cold replay scratch is fresh");
    auto node=std::make_unique<ReplicatedKernel>(identity_test::bootstrap(root,2,{1,2,3}));
    identity_test::deliver_declared_peer(*node,velocita::PeerMessage{1,2,velocita::AppendEntries{1,0,0,0,{{1,7,kSwapAuthorityBarrierEntryType,marker()}}}});node->flush_durability();
    require(node->status().current_index==1 && node->authority_drain_index()==1 && !node->take_swap_authority_proof(),"uncommitted type102 is retained without proof");
    node.reset();node=std::make_unique<ReplicatedKernel>(identity_test::recover(root,2,{1,2,3}));
    require(node->status().current_index==1 && node->status().applied_index==0 && node->authority_drain_index()==1 && !node->take_swap_authority_proof(),"cold uncommitted type102 recovery retains drain without proof");
    identity_test::deliver_declared_peer(*node,velocita::PeerMessage{1,2,velocita::AppendEntries{1,1,1,1,{}}});node->flush_durability();node->tick(0);
    require(node->status().applied_index==1 && node->authority_drain_index()==0 && node->state().state_version==0 && !node->take_swap_authority_proof() && !node->take_committed_result(),"committed replayed marker advances log without economic state result or proof");
    node.reset();node=std::make_unique<ReplicatedKernel>(identity_test::recover(root,2,{1,2,3}));
    require(node->status().applied_index==1 && node->state().state_version==0 && !node->take_swap_authority_proof() && !node->take_committed_result(),"cold committed type102 recovery never resurrects signing grant");
    const auto records=identity_test::read_wal(root);require(records.size()==1 && records[0].entry_type==102 && records[0].payload==marker(),"cold WAL retains exact type102 payload");
}
std::vector<std::byte> leader_marker() {
    const auto domain = identity().authority_domain();
    return {domain.bytes.begin(), domain.bytes.end()};
}
velocita::WalRecord leader_record() {
    return {1, 1, 9, kLeaderReadBarrierEntryType, leader_marker()};
}
velocita::WalRecord invalid_leader_record(unsigned mutation) {
    auto value = leader_record();
    if (mutation == 0) value.entry_type = -1;
    if (mutation == 1) value.payload[0] ^= std::byte{1};
    if (mutation == 2) value.payload.pop_back();
    if (mutation == 3) value.payload.push_back(std::byte{0});
    if (mutation == 4) value.payload = marker();
    return value;
}
void leader_wal_roundtrip_and_refusal() {
    const auto value = leader_record();
    const auto framed = encode_bound_wal_record(value, identity());
    const auto decoded = decode_bound_wal_record(framed, identity());
    require(value.payload.size() == 32 && decoded.index == 1 && decoded.term == 1 &&
                decoded.entry_id == 9 && decoded.entry_type == 103 && decoded.payload == value.payload,
            "leader103 WAL preserves exact coordinates and 32-byte domain");
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        const auto bad = invalid_leader_record(mutation);
        bool encode_refused{}, decode_refused{};
        try { (void)encode_bound_wal_record(bad, identity()); } catch (const BoundWalError&) { encode_refused = true; }
        try { (void)decode_bound_wal_record(identity_test::raw_record(bad, identity()), identity()); }
        catch (const BoundWalError&) { decode_refused = true; }
        require(encode_refused && decode_refused,
                "leader103 WAL refuses wrong type domain size and authority-shaped payload");
    }
}
void leader_peer_roundtrip_and_refusal() {
    const auto payload = leader_marker();
    velocita::PeerMessage message{1, 2, velocita::AppendEntries{1, 0, 0, 0,
        {{1, 9, kLeaderReadBarrierEntryType, payload}}}};
    std::array<std::byte, kMaxBoundPeerBytes> bytes{};
    BoundFrameScratch scratch;
    const auto encoded = encode_bound_peer(message, identity(), bytes, scratch);
    require(encoded.error == BoundFrameError::none, "leader103 owning peer encode admits exact domain");
    velocita::PeerMessage decoded;
    require(decode_bound_peer(std::span(bytes).first(encoded.bytes_written), identity(2), decoded, scratch) ==
                BoundFrameError::none, "leader103 owning peer decode admits exact domain");
    const auto& append = std::get<velocita::AppendEntries>(decoded.payload);
    require(append.entries.size() == 1 && append.entries[0].type == 103 && append.entries[0].payload == payload,
            "leader103 peer roundtrip preserves domain");
    const std::array<BorrowedPeerLogEntry, 1> entries{{{1, 9, kLeaderReadBarrierEntryType, payload}}};
    const PeerMessageView borrowed{1, 2, BorrowedAppendEntries{1, 0, 0, 0, entries, 0}};
    const auto borrowed_encoded = encode_bound_peer(borrowed, identity(), bytes, scratch);
    require(borrowed_encoded.error == BoundFrameError::none &&
                decode_bound_peer(std::span(bytes).first(borrowed_encoded.bytes_written), identity(2), decoded, scratch) == BoundFrameError::none,
            "leader103 borrowed peer encoding roundtrips");
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        const auto bad = invalid_leader_record(mutation);
        auto malformed = message;
        std::get<velocita::AppendEntries>(malformed.payload).entries[0] =
            {bad.term, bad.entry_id, bad.entry_type, bad.payload};
        const auto before = bytes;
        require(encode_bound_peer(malformed, identity(), bytes, scratch).error != BoundFrameError::none && bytes == before,
                "leader103 owning peer refuses malformed without output mutation");
        const auto raw = identity_test::frame_peer_body(malformed, identity());
        require(decode_bound_peer(raw, identity(2), decoded, scratch) != BoundFrameError::none,
                "leader103 incoming peer refuses malformed payload");
        const std::array<BorrowedPeerLogEntry, 1> bad_entries{{{bad.term, bad.entry_id, bad.entry_type, bad.payload}}};
        const PeerMessageView bad_borrowed{1, 2, BorrowedAppendEntries{1, 0, 0, 0, bad_entries, 0}};
        require(encode_bound_peer(bad_borrowed, identity(), bytes, scratch).error != BoundFrameError::none && bytes == before,
                "leader103 borrowed peer refuses malformed without output mutation");
    }
}
bool capture_leader_marker(const ArchiveRecordView& value, void* context) noexcept {
    auto& c = *static_cast<ArchiveCapture*>(context);
    c.called = true;
    c.exact = value.index == 1 && value.term == 1 && value.entry_id == 9 && value.entry_type == 103 &&
        value.payload.size() == 32 && value.payload.size() == c.expected.size() &&
        std::equal(value.payload.begin(), value.payload.end(), c.expected.begin());
    return true;
}
std::vector<std::byte> independently_framed_leader_archive(const velocita::WalRecord& record) {
    const auto payload_size = record.payload.size();
    std::vector<std::byte> bytes(96 + 32 + payload_size + 32 + 32);
    const auto put = [&](std::size_t at, std::size_t width, std::uint64_t value) {
        for (std::size_t n = 0; n < width; ++n) bytes[at+n] = std::byte((value >> (8*n)) & 255);
    };
    bytes[0] = std::byte{'V'}; bytes[1] = std::byte{'2'}; bytes[2] = std::byte{'A'}; bytes[3] = std::byte{'S'};
    put(4,2,1); put(6,2,96); put(8,4,bytes.size()); put(12,4,1);
    put(16,8,1); put(24,8,1); put(40,8,1); put(48,8,1);
    const auto manifest = identity().manifest_digest();
    std::copy(manifest.bytes.begin(), manifest.bytes.end(), bytes.begin()+56);
    put(96,8,record.index); put(104,8,record.term); put(112,4,record.entry_id);
    put(116,4,static_cast<std::uint32_t>(record.entry_type)); put(120,4,payload_size);
    std::copy(record.payload.begin(), record.payload.end(), bytes.begin()+128);
    Sha256 record_hash;
    constexpr std::string_view record_domain = "velocita.v2.archive-record.v1";
    record_hash.update(std::as_bytes(std::span(record_domain)));
    record_hash.update(manifest.bytes);
    record_hash.update(std::span(bytes).subspan(96,32+payload_size));
    const auto record_digest = record_hash.finish();
    std::copy(record_digest.bytes.begin(), record_digest.bytes.end(), bytes.begin()+128+payload_size);
    Sha256 segment_hash;
    constexpr std::string_view segment_domain = "velocita.v2.archive-segment.v1";
    segment_hash.update(std::as_bytes(std::span(segment_domain)));
    segment_hash.update(std::span(bytes).first(bytes.size()-32));
    const auto segment_digest = segment_hash.finish();
    std::copy(segment_digest.bytes.begin(), segment_digest.bytes.end(), bytes.end()-32);
    return bytes;
}
void leader_archive_roundtrip_and_refusal() {
    const auto payload = leader_marker();
    ArchiveSource source{{1, 1, 9, kLeaderReadBarrierEntryType, payload}};
    ArchiveSegmentScratch scratch;
    std::array<std::byte, 512> bytes{};
    const auto encoded = encode_archive_segment({&source, 1, read_record}, 0, identity(), bytes, scratch);
    require(encoded.error == ArchiveSegmentError::none, "leader103 archive encode admits exact domain");
    const auto independent = independently_framed_leader_archive(leader_record());
    require(independent.size() == encoded.bytes_written && std::equal(independent.begin(), independent.end(), bytes.begin()),
            "leader103 archive bytes differ from independent byte-layout oracle");
    ArchiveCapture seen{false, false, payload};
    ArchiveSegmentMetadata metadata;
    require(decode_archive_segment(std::span(bytes).first(encoded.bytes_written), identity(), capture_leader_marker,
                &seen, scratch, metadata).error == ArchiveSegmentError::none && seen.called && seen.exact && metadata.record_count == 1,
            "leader103 archive decode preserves exact marker");
    auto corrupt = bytes;
    corrupt[kArchiveSegmentHeaderBytes + kArchiveRecordHeaderBytes] ^= std::byte{1};
    seen.called = false;
    require(decode_archive_segment(std::span(corrupt).first(encoded.bytes_written), identity(), capture_leader_marker,
                &seen, scratch, metadata).error != ArchiveSegmentError::none && !seen.called,
            "leader103 corrupt archive fails before visitor");
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        const auto bad = invalid_leader_record(mutation);
        source.record = {bad.index, bad.term, bad.entry_id, bad.entry_type, bad.payload};
        const auto before = bytes;
        require(encode_archive_segment({&source, 1, read_record}, 0, identity(), bytes, scratch).error != ArchiveSegmentError::none && bytes == before,
                "leader103 archive refuses wrong type domain size and authority-shaped payload");
        const auto raw = independently_framed_leader_archive(bad);
        seen.called = false;
        require(decode_archive_segment(raw, identity(), capture_leader_marker, &seen, scratch, metadata).error !=
                    ArchiveSegmentError::none && !seen.called,
                "leader103 sealed malformed archive refuses before visitor");
    }
}
void leader_cold_replay_never_grants() {
    const auto* home = std::getenv("HOME");
    require(home != nullptr, "leader103 durable scratch HOME exists");
    const auto root = std::filesystem::path(home)/".cache/velocita-build"/
        ("leader-read-storage-" + std::to_string(::getpid()));
    require(!std::filesystem::exists(root), "leader103 cold replay scratch is fresh");
    auto node = std::make_unique<ReplicatedKernel>(identity_test::bootstrap(root, 2, {1, 2, 3}));
    const auto clean = [&] {
        return node->state().state_version == 0 && node->reserved_result_count() == 0 &&
            node->ready_result_count() == 0 && !node->take_committed_result() &&
            !node->take_authority_proof() && !node->take_swap_authority_proof();
    };
    identity_test::deliver_declared_peer(*node, velocita::PeerMessage{1, 2,
        velocita::AppendEntries{1, 0, 0, 0, {{1, 9, kLeaderReadBarrierEntryType, leader_marker()}}}});
    node->flush_durability();
    require(node->status().current_index == 1 && node->status().applied_index == 0 &&
                node->retained_log_count() == 1 && node->retained_log_bytes() == 32 && clean(),
            "leader103 uncommitted marker retains bounded bytes without result or authority");
    node.reset();
    node = std::make_unique<ReplicatedKernel>(identity_test::recover(root, 2, {1, 2, 3}));
    require(node->status().current_index == 1 && node->status().applied_index == 0 && clean(),
            "leader103 uncommitted cold replay creates no result or authority");
    identity_test::deliver_declared_peer(*node, velocita::PeerMessage{1, 2,
        velocita::AppendEntries{1, 1, 1, 1, {}}});
    node->flush_durability();
    node->tick(0);
    require(node->status().applied_index == 1 && clean(),
            "leader103 committed marker changes log only without economic state or grant");
    node.reset();
    node = std::make_unique<ReplicatedKernel>(identity_test::recover(root, 2, {1, 2, 3}));
    require(node->status().applied_index == 1 && clean(),
            "leader103 committed cold replay never resurrects result or grant");
    const auto records = identity_test::read_wal(root);
    require(records.size() == 1 && records[0].entry_type == 103 && records[0].payload == leader_marker(),
            "leader103 cold WAL preserves exact retained marker");
}

}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> cases[]={{"wal_roundtrip_and_refusal",wal_roundtrip_and_refusal},{"peer_roundtrip_and_refusal",peer_roundtrip_and_refusal},{"archive_roundtrip_and_refusal",archive_roundtrip_and_refusal},{"cold_replay_never_grants",cold_replay_never_grants},{"leader_wal_roundtrip_and_refusal",leader_wal_roundtrip_and_refusal},{"leader_peer_roundtrip_and_refusal",leader_peer_roundtrip_and_refusal},{"leader_archive_roundtrip_and_refusal",leader_archive_roundtrip_and_refusal},{"leader_cold_replay_never_grants",leader_cold_replay_never_grants}};for(auto [name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';return 1;}}}
