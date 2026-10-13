#include "velocita/v2/swap_submission_resolution.hpp"
#include "v2_swap_test_support.hpp"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

namespace {
using namespace velocita::v2;
void require(bool yes, const char* name) { if (!yes) throw std::runtime_error(name); }
Digest256 digest(unsigned n) { return swap_test::digest(n); }
const std::array<std::byte,16> cluster{std::byte{1}}, process{std::byte{2}};
AuthKey key(AuthRole role, int sender, int recipient) {
    std::array<std::byte,32> bytes{}; bytes.fill(std::byte{42});
    auto secret = AuthSecret::from_bytes(bytes);
    return std::move(*AuthKey::derive(*secret,{role,sender,recipient},cluster));
}
std::vector<ClientRoute> routes() {
    std::vector<AuthKey> responses; responses.push_back(key(AuthRole::response,1,22));
    std::vector<ClientRoute> result;
    result.push_back({1,key(AuthRole::ingress,22,1),std::move(responses)}); return result;
}
struct Voter final : ClientTransport {
    TradingState state{catalog_test::state(shadow_catalog::truemarkets_two_row_catalog())};
    std::deque<std::vector<std::byte>> replies;
    Digest256 domain{digest(30)};
    std::uint64_t sequence{};
    std::int64_t index{10};
    bool bad_mac{}, bad_snapshot{}, silent{}, wrong_kind{}, bad_arithmetic{};
    unsigned requests{};
    Voter() {
        state.kill_epoch = KillEpoch{1}; state.executor_generation = ExecutorGeneration{1};
        ApplyScratch scratch;
        require(apply(state,swap_test::command(9,1,swap_test::opening()),scratch,
                      &shadow_catalog::truemarkets_two_row_catalog()).code == ApplyCode::applied,"fixture funding committed");
    }
    bool offer_ingress(std::int32_t node, std::span<const std::byte> envelope) override {
        require(node==1,"request sent to configured voter"); ++requests;
        if(silent) return true;
        auto ingress=key(AuthRole::ingress,22,1); VerifiedAuthEnvelope verified;
        require(verify_auth_envelope(ingress,envelope,verified)==AuthEnvelopeError::none,"actual state request authenticated");
        GatewayRequest request;
        require(decode_gateway_request(verified.body,request)==NodeProtocolError::none &&
                request.kind==GatewayRequestKind::state_read,"coordinator issues actual state read only");
        const auto correlation=gateway_correlation(verified.body);
        std::array<std::byte,kMaxStateSnapshotBytes> snapshot{};
        auto encoded=encode_state_snapshot(state,snapshot);
        require(encoded.error==StateSnapshotError::none,"fixture full snapshot encodes");
        if(bad_arithmetic) {
            std::array<std::byte,8> expected{};for(unsigned n=0;n<8;++n)expected[n]=static_cast<std::byte>(std::uint64_t{3'998'000}>>(8*n));
            auto begin=snapshot.begin();auto end=begin+static_cast<std::ptrdiff_t>(encoded.bytes_written-32);
            auto where=std::search(begin,end,expected.begin(),expected.end());
            require(where!=end && std::search(where+1,end,expected.begin(),expected.end())==end,"independent corrupted quote field uniquely located");
            *where^=std::byte{1};
            const auto hash=sha256_digest(std::span(snapshot).subspan(kStateSnapshotDigestInputOffset,encoded.bytes_written-32-kStateSnapshotDigestInputOffset));
            std::copy(hash.bytes.begin(),hash.bytes.end(),snapshot.begin()+static_cast<std::ptrdiff_t>(encoded.bytes_written-32));
        }
        const auto snapshot_bytes=std::span(snapshot).first(encoded.bytes_written);
        const auto count=(encoded.bytes_written+kStateChunkPayloadBytes-1)/kStateChunkPayloadBytes;
        for(std::size_t n=0;n<count;++n) {
            std::array<std::byte,kMaxStateChunkResponseBytes> body{};
            StateChunkHeader h;
            h.correlation=correlation;h.responder_id=1;h.leader_id=1;h.term=1;h.applied_index=index;
            h.authority_domain=domain;h.snapshot_bytes=static_cast<std::uint32_t>(snapshot_bytes.size());
            h.snapshot_digest=bad_snapshot?digest(99):sha256_digest(snapshot_bytes);
            h.chunk_index=static_cast<std::uint16_t>(n);h.chunk_count=static_cast<std::uint16_t>(count);
            const auto offset=n*kStateChunkPayloadBytes;
            auto framed=encode_state_chunk(h,snapshot_bytes.subspan(offset,std::min(kStateChunkPayloadBytes,snapshot_bytes.size()-offset)),body);
            require(framed.error==NodeProtocolError::none,"fixture state chunk encodes");
            if(wrong_kind) body[0]=static_cast<std::byte>(ResponseKind::committed);
            std::vector<std::byte> sealed(kMaxAuthEnvelopeBytes);
            auto response=key(AuthRole::response,1,22);
            const auto auth=encode_auth_envelope(response,1,++sequence,std::span(body).first(framed.bytes_written),sealed);
            require(auth.error==AuthEnvelopeError::none,"fixture state chunk authenticated");
            sealed.resize(auth.bytes_written);if(bad_mac) sealed.back()^=std::byte{1};
            replies.push_back(std::move(sealed));
        }
        return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> out) override {
        if(replies.empty()) return std::nullopt;
        const auto n=replies.front().size(); require(n<=out.size(),"response capacity");
        std::copy(replies.front().begin(),replies.front().end(),out.begin()); replies.pop_front(); return n;
    }
};

const std::string body="synthetic-execute-request";
auto body_bytes() { return std::as_bytes(std::span(body.data(),body.size())); }
SwapSubmissionIdentity identity() {
    SwapSubmissionIdentity i;i.effect=EffectId{1};i.payload=digest(7);
    constexpr std::string_view handle="fixture-order-1";
    i.handle_size=handle.size();std::copy(std::as_bytes(std::span(handle.data(),handle.size())).begin(),
        std::as_bytes(std::span(handle.data(),handle.size())).end(),i.handle.begin());
    i.handle_digest=sha256_digest(std::span(i.handle).first(i.handle_size));
    i.execution_request=sha256_digest(body_bytes());return i;
}
struct Scratch {
    std::filesystem::path base;
    Scratch() {
        const auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/submission-resolution-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture scratch created");base=name;
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(base,ignored);}
    auto path() const {return base/"store";}
};
struct Fault {
    bool armed{};
    static bool proceed(SwapSubmissionStage stage,void* context) noexcept {
        return !(static_cast<Fault*>(context)->armed && stage==SwapSubmissionStage::temporary_sync);
    }
};
SwapSubmissionResolutionConfig config(){return {swap_test::opening().binding,digest(30),20};}
struct Rig {
    Scratch scratch;Voter voter;GatewayLink link{1,process,routes(),voter};Fault fault;
    std::unique_ptr<SwapSubmissionStore> store;
    std::unique_ptr<SwapSubmissionResolutionSession> session;
    std::uint64_t sequence{3};
    Rig() {
        store=std::make_unique<SwapSubmissionStore>(scratch.path(),SwapSubmissionBinding{digest(1),digest(30)},SwapSubmissionFaultHook{Fault::proceed,&fault});
        require(store->create({{digest(1),digest(30)},1,digest(51),EffectId{0}})==SwapSubmissionCode::none,"fixture journal initialized");
        require(store->begin(identity(),10)==SwapSubmissionCode::none,"fixture execute intent persisted");
        ApplyScratch work;
        require(apply(voter.state,{{CallerId{9},RequestSequence{2},digest(99)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}},
            work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture mode enabled");
        for(const auto& command:{swap::Command{swap_test::buy(swap_test::opening())},
            swap::Command{swap::Prepare{EffectId{1},identity().payload,identity().handle_digest,1}},
            swap::Command{swap::Attempt{EffectId{1},identity().payload}}}) {
            const auto caller=std::holds_alternative<swap::Intent>(command)?9:22;
            require(apply(voter.state,swap_test::command(caller,sequence++,command),work,
                &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture attempt committed");
        }
        restart();
    }
    swap::Settlement settlement() const {
        swap::Settlement x;x.effect=EffectId{1};x.custody=swap_test::opening().binding.custody;x.payload=identity().payload;
        x.transaction=digest(70);x.evidence=digest(71);x.outcome=swap::Outcome::succeeded;
        x.base_credit=swap::BaseAtoms{10'000'000};x.quote_debit=swap::QuoteAtoms{1'002'000};x.fee=swap::QuoteAtoms{2'000};x.finalized=true;return x;
    }
    void settle(bool failed=false){
        auto x=settlement();if(failed){x.outcome=swap::Outcome::failed_without_charge;x.base_credit={};x.quote_debit={};x.fee={};}
        ApplyScratch work;require(apply(voter.state,swap_test::command(22,sequence++,x,101),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture terminal committed");voter.index=11;
    }
    void restart(){
        session.reset();store.reset();
        store=std::make_unique<SwapSubmissionStore>(scratch.path(),SwapSubmissionBinding{digest(1),digest(30)},SwapSubmissionFaultHook{Fault::proceed,&fault});
        require(store->recover()==SwapSubmissionCode::none,"fixture real journal recovery");
        session=std::make_unique<SwapSubmissionResolutionSession>(link,*store,config());
    }
    bool start(std::int64_t floor=10){return session->start(floor,1000);}
    auto finish(){(void)session->poll(1000);return session->poll(1001);}
};
void committed_resolution_and_restart(){
    for(bool failed:{false,true}){
        Rig r;r.settle(failed);const auto before=r.voter.state.spot_swap;
        require(r.start(),"recovered outstanding journal starts authenticated resolution read");
        require(r.finish()==SwapSubmissionResolutionStatus::resolved && r.store->latest().phase==SwapSubmissionPhase::resolved,
            "exact committed terminal settlement durably resolves journal");
        require(r.store->latest().committed_index==11 && r.store->latest().identity==identity() &&
            r.store->latest().evidence==r.session->result().reading.state_digest &&
            r.session->result().settlement==r.voter.state.spot_swap.settlements[0],
            "durable resolution binds authenticated state digest index and exact settlement");
        require(r.voter.state.spot_swap==before && r.voter.requests==1,"resolution issues one read and never changes kernel balances");
        require(r.voter.state.spot_swap.quote.value()==(failed?5'000'000:3'998'000) &&
            r.voter.state.spot_swap.base.value()==(failed?0:10'000'000) && r.voter.state.spot_swap.reserved_quote.value()==0,
            "settled balance arithmetic matches independently calculated outcome");
        r.restart();require(r.session->status()==SwapSubmissionResolutionStatus::resolved && !r.start() && r.voter.requests==1,
            "durably resolved restart performs no new resolution or execution");
    }
}
void absent_or_mismatched_settlement(){
    for(unsigned mode=0;mode<5;++mode){
        Rig r;
        if(mode!=0)r.settle();else r.voter.index=11;
        if(mode==1)r.voter.state.spot_swap.settlements[0].payload=digest(99);
        if(mode==2){r.voter.state.spot_swap.settlements[0].effect=EffectId{2};r.voter.state.spot_swap.last_effect=2;}
        if(mode==3){r.voter.state.spot_swap.opening.binding.custody=digest(99);r.voter.state.spot_swap.settlements[0].custody=digest(99);}
        if(mode==4){ApplyScratch work;auto conflicting=r.settlement();conflicting.transaction=digest(99);
            require(apply(r.voter.state,swap_test::command(22,r.sequence++,conflicting,102),work,
                &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::reconciliation_required,"fixture conflict committed");}
        require(r.start(),"mismatch resolution fixture starts");(void)r.finish();
        require(r.session->status()!=SwapSubmissionResolutionStatus::resolved && r.store->latest().phase==SwapSubmissionPhase::execute_intent,
            "absent wrong effect payload custody or conflicted settlement cannot resolve journal");
        require(r.voter.requests==1,"nonterminal reconciliation never sends a trade or release");
    }
}
void authenticated_state_and_arithmetic(){
    for(unsigned mode=0;mode<7;++mode){
        Rig r;r.settle();
        if(mode==0)r.voter.domain=digest(90);
        if(mode==1)r.voter.index=9;
        if(mode==2)r.voter.bad_mac=true;
        if(mode==3)r.voter.bad_snapshot=true;
        if(mode==4)r.voter.index=10;
        if(mode==5)r.voter.bad_arithmetic=true;
        if(mode==6)r.voter.wrong_kind=true;
        require(r.start(),"state integrity fixture starts");(void)r.finish();(void)r.session->poll(1020);
        require(r.session->status()==SwapSubmissionResolutionStatus::refused && r.store->latest().phase==SwapSubmissionPhase::execute_intent,
            "wrong domain stale index bad MAC digest arithmetic or response kind leaves journal unresolved");
    }
    Rig floor;floor.settle();require(floor.start(12),"higher observed floor fixture starts");(void)floor.finish();
    require(floor.session->status()==SwapSubmissionResolutionStatus::refused && floor.store->latest().phase==SwapSubmissionPhase::execute_intent,
        "snapshot must cover caller committed floor");
}
void canonical_signature_identity_oracle(){
    SwapSubmissionTransaction zero{};
    require(!solana_signature_transaction_digest(zero),"zero signature cannot create canonical transaction identity");
    SwapSubmissionTransaction signature{};for(unsigned n=0;n<64;++n)signature[n]=static_cast<std::byte>(n);
    const auto expected=*parse_digest256("c1e2d2fd696b8ad1228b9d8eb2036995e7d742c839d466bca478decfb0ab4969");
    const auto actual=solana_signature_transaction_digest(signature);
    require(actual && *actual==expected,"independent Python domain and raw signature hash oracle");
    signature[63]^=std::byte{1};
    require(solana_signature_transaction_digest(signature)!=actual,"last signature byte contributes to canonical identity");
}
void known_transaction_mapping_and_journal_change(){
    {
        Rig r;r.settle();auto c=config();c.binding.policy=PolicyId{902};
        r.session=std::make_unique<SwapSubmissionResolutionSession>(r.link,*r.store,c);
        require(r.start(),"different policy fixture starts");(void)r.finish();
        require(r.session->status()==SwapSubmissionResolutionStatus::refused && r.store->latest().phase==SwapSubmissionPhase::execute_intent,
            "exact configured policy binding required for terminal resolution");
    }

    {
        Rig r;r.settle();SwapSubmissionTransaction tx{};tx[0]=std::byte{7};
        require(r.store->observe_transaction(identity(),tx)==SwapSubmissionCode::none,"fixture known signature persisted");
        require(r.start(),"known mismatch fixture starts authenticated read");(void)r.finish();
        require(r.session->status()==SwapSubmissionResolutionStatus::refused && r.voter.requests==1 &&
            r.store->latest().phase==SwapSubmissionPhase::execute_intent,"known signature must match canonical committed transaction identity");
    }
    {
        Rig r;r.settle();SwapSubmissionTransaction tx{};for(unsigned n=0;n<64;++n)tx[n]=static_cast<std::byte>(n);
        require(r.store->observe_transaction(identity(),tx)==SwapSubmissionCode::none,"fixture exact known signature persisted");
        r.voter.state.spot_swap.settlements[0].transaction=*parse_digest256("c1e2d2fd696b8ad1228b9d8eb2036995e7d742c839d466bca478decfb0ab4969");
        require(r.start(),"known exact match fixture starts authenticated read");
        require(r.finish()==SwapSubmissionResolutionStatus::resolved && r.store->latest().phase==SwapSubmissionPhase::resolved &&
            r.store->latest().transaction==tx,"canonical matching known signature resolves durable journal");
        r.restart();require(r.session->status()==SwapSubmissionResolutionStatus::resolved,"known signature resolved identity survives restart");
    }
    {
        Rig r;r.settle();require(r.start(),"journal mutation fixture starts");(void)r.session->poll(1000);
        SwapSubmissionTransaction tx{};tx[0]=std::byte{7};require(r.store->observe_transaction(identity(),tx)==SwapSubmissionCode::none,"fixture signature changes during read");
        require(r.session->poll(1001)==SwapSubmissionResolutionStatus::refused && !r.link.busy() &&
            r.store->latest().phase==SwapSubmissionPhase::execute_intent,"journal identity ordinal or phase change invalidates outstanding read");
    }
    {
        Rig r;r.settle();require(r.start(),"phase mutation fixture starts");(void)r.session->poll(1000);
        require(r.store->resolve({identity(),12,digest(99)})==SwapSubmissionCode::none,"fixture other owner path resolves during read");
        require(r.session->poll(1001)==SwapSubmissionResolutionStatus::refused && r.store->latest().committed_index==12,
            "changed journal phase cannot be overwritten by stale resolution read");
    }
}
void timeout_ownership_and_limits(){
    for(bool backward:{false,true}){
        Rig r;r.settle();require(r.start(),"deadline fixture starts");(void)r.session->poll(1000);
        require(r.session->poll(backward?999:1020)==SwapSubmissionResolutionStatus::refused && !r.link.busy() &&
            r.store->latest().phase==SwapSubmissionPhase::execute_intent,"late queued response or backward clock refuses and releases owned read");
    }
    {
        Rig r;require(r.start(),"destruction fixture starts");r.session.reset();
        require(!r.link.busy(),"destroyed resolution session releases its pending read");
    }
    {
        Rig r;require(r.link.request_state(2000,20),"fixture unrelated read pending");
        require(!r.start(),"busy shared link is not acquired");r.session.reset();
        require(r.link.busy(),"idle resolution destructor preserves unrelated pending request");r.link.abandon();
    }
    {
        Rig r;require(!r.session->start(10,UINT64_MAX-19) && !r.link.busy(),"deadline overflow refuses before state request");
    }
    {
        Rig r;require(!r.start(9) && r.voter.requests==0,"minimum floor below retained attempt refuses");
    }
}
void failed_durability_keeps_resolution_closed(){
    Rig r;r.settle();r.fault.armed=true;require(r.start(),"sync fault fixture starts");
    require(r.finish()==SwapSubmissionResolutionStatus::storage_failed && r.store->latest().phase==SwapSubmissionPhase::execute_intent &&
        r.voter.requests==1,"resolution sync failure never publishes resolved journal or sends command");
    require(!r.start(),"storage failure never retries resolution automatically");
    r.session.reset();r.store.reset();SwapSubmissionStore recovered(r.scratch.path(),{digest(1),digest(30)});
    require(recovered.recover()==SwapSubmissionCode::temporary_present,"partial resolution recovery remains closed with evidence");
}
}
int main(int argc,char** argv){
    const std::pair<const char*,void(*)()> cases[]{
        {"committed_resolution_and_restart",committed_resolution_and_restart},
        {"absent_or_mismatched_settlement",absent_or_mismatched_settlement},
        {"authenticated_state_and_arithmetic",authenticated_state_and_arithmetic},
        {"canonical_signature_identity_oracle",canonical_signature_identity_oracle},
        {"known_transaction_mapping_and_journal_change",known_transaction_mapping_and_journal_change},
        {"timeout_ownership_and_limits",timeout_ownership_and_limits},
        {"failed_durability_keeps_resolution_closed",failed_durability_keeps_resolution_closed}};
    unsigned ran{},failed{};
    for(const auto& [name,run]:cases)if(argc==1 || std::string(argv[1])==name){++ran;try{run();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
