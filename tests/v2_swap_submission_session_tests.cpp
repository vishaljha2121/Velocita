#include "velocita/v2/swap_submission_session.hpp"
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
    bool bad_mac{}, bad_snapshot{}, silent{}, wrong_kind{};
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
        const auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/submission-session-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"fixture scratch created");base=name;
    }
    ~Scratch(){std::error_code ignored;std::filesystem::remove_all(base,ignored);}
    auto path() const {return base/"store";}
};
struct Port final:SwapExecutionPort {
    unsigned calls{};std::function<void()> during;
    SwapSubmissionIdentity received{};std::vector<std::byte> bytes;
    void execute(const SwapSubmissionIdentity& i,std::span<const std::byte> b) override {
        ++calls;received=i;bytes.assign(b.begin(),b.end());if(during)during();
    }
};
struct Fault {
    bool armed{};
    static bool proceed(SwapSubmissionStage stage,void* context) noexcept {
        return !(static_cast<Fault*>(context)->armed && stage==SwapSubmissionStage::temporary_sync);
    }
};
SwapSubmissionSessionConfig config() {return {swap_test::opening().binding,digest(30),20};}
struct Rig {
    Scratch scratch;Voter voter;GatewayLink link{1,process,routes(),voter};Fault fault;
    SwapSubmissionStore store{scratch.path(),{digest(1),digest(30)},{Fault::proceed,&fault}};
    Port port;std::unique_ptr<SwapSubmissionSession> session;
    Rig() {
        require(store.create({{digest(1),digest(30)},1,digest(51),EffectId{0}})==SwapSubmissionCode::none,"fixture submission journal created");
        ApplyScratch work;
        require(apply(voter.state,{{CallerId{9},RequestSequence{2},digest(99)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}},
            work,&shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture mode enabled");
        for(const auto& command : {swap::Command{swap_test::buy(swap_test::opening())},
            swap::Command{swap::Prepare{EffectId{1},identity().payload,identity().handle_digest,1}},
            swap::Command{swap::Attempt{EffectId{1},identity().payload}}}) {
            const bool intent=std::holds_alternative<swap::Intent>(command);
            static std::uint64_t seq=3;
            require(apply(voter.state,swap_test::command(intent?9:22,seq++,command),work,
                &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture attempt committed");
        }
        session=std::make_unique<SwapSubmissionSession>(link,store,port,config());
    }
    bool start(){return session->start(identity(),body_bytes(),10,1000);}
    auto finish(){(void)session->poll(1000,101);return session->poll(1001,102);}
};
void durable_before_send_and_no_repeat() {
    Rig r;bool durable=true;
    r.port.during=[&]{durable=durable && r.store.latest().phase==SwapSubmissionPhase::execute_intent &&
        r.store.latest().identity==identity() && r.store.latest().committed_index==10;};
    require(r.start(),"eligible request starts authenticated state read");
    const auto result=r.finish();require(durable,"exact execute identity is durable before port invocation");
    require(result==SwapSubmissionSessionStatus::needs_reconciliation && r.port.calls==1 &&
        r.port.received==identity() && r.port.bytes==std::vector<std::byte>(body_bytes().begin(),body_bytes().end()),
        "one exact request is sent and every response requires reconciliation");
    for(unsigned n=0;n<5;++n)(void)r.session->poll(1002+n,103+n);
    require(!r.start() && r.port.calls==1,"poll and start cannot repeat execution");
}
void payload_copy_and_input_binding() {
    Rig r;auto bytes=std::vector<std::byte>(body_bytes().begin(),body_bytes().end());
    require(r.session->start(identity(),bytes,10,1000),"copy fixture starts");
    std::fill(bytes.begin(),bytes.end(),std::byte{42});
    (void)r.finish();require(r.port.bytes==std::vector<std::byte>(body_bytes().begin(),body_bytes().end()),
        "request owns immutable bytes across asynchronous state read");
    Rig s;auto changed=identity();changed.execution_request=digest(99);
    require(!s.session->start(changed,body_bytes(),10,1000) && s.voter.requests==0 && s.port.calls==0,
        "request digest mismatch refuses before state or send");
    Rig t;auto malformed=identity();malformed.handle_size=kSwapSubmissionHandleBytes+1;
    require(!t.session->start(malformed,body_bytes(),10,1000) && t.voter.requests==0,
        "oversized handle refuses before state read");
}
void authenticated_state_fences() {
    for(unsigned mode=0;mode<5;++mode) {
        Rig r;
        if(mode==0)r.voter.domain=digest(98);
        if(mode==1)r.voter.index=9;
        if(mode==2)r.voter.bad_mac=true;
        if(mode==3)r.voter.bad_snapshot=true;
        if(mode==4)r.voter.silent=true;
        require(r.start(),"state fence fixture starts");(void)r.finish();(void)r.session->poll(1022,123);
        require(r.port.calls==0 && r.store.latest().phase==SwapSubmissionPhase::idle &&
            r.session->status()==SwapSubmissionSessionStatus::refused,
            "wrong domain stale state bad MAC digest or timeout never sends");
    }
}
void exact_pending_identity() {
    for(unsigned mode=0;mode<3;++mode) {
        Rig r;auto id=identity();
        if(mode==0)id.effect=EffectId{2};
        if(mode==1)id.payload=digest(99);
        if(mode==2){id.handle[0]^=std::byte{1};id.handle_digest=sha256_digest(std::span(id.handle).first(id.handle_size));}
        require(r.session->start(id,body_bytes(),10,1000),"pending mismatch fixture starts");(void)r.finish();
        require(r.port.calls==0 && r.store.latest().phase==SwapSubmissionPhase::idle,
            "effect payload and exact handle must match committed pending swap");
    }
}
void current_controls_deadline_and_phase() {
    for(unsigned mode=0;mode<4;++mode) {
        Rig r;ApplyScratch work;
        if(mode==0)require(apply(r.voter.state,{{CallerId{9},RequestSequence{9000},digest(77)},
            SetStrategyMode{StrategyMode::halted,KillEpoch{1},ExecutorGeneration{1}}},work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture halt committed");
        if(mode==1)require(apply(r.voter.state,swap_test::command(22,9001,swap::MarkUnknown{EffectId{1}},101),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture unknown committed");
        require(r.start(),"controls fixture starts");
        (void)r.session->poll(1000,mode==2?20100:mode==3?99:101);
        (void)r.session->poll(1001,mode==2?20101:mode==3?99:102);
        require(r.port.calls==0 && r.store.latest().phase==SwapSubmissionPhase::idle &&
            r.voter.state.spot_swap.reserved_quote.value()==1'002'000,
            "halt unknown expired or backwards event time refuses without releasing exposure");
    }
}
void disk_failure_and_ambiguous_send() {
    Rig r;r.fault.armed=true;require(r.start(),"disk failure fixture starts");
    require(r.finish()==SwapSubmissionSessionStatus::storage_failed && r.port.calls==0 &&
        r.voter.state.spot_swap.reserved_quote.value()==1'002'000,
        "journal sync failure prevents send and preserves reservation");
    Rig s;s.port.during=[]{throw std::runtime_error("synthetic lost response");};require(s.start(),"ambiguous fixture starts");
    require(s.finish()==SwapSubmissionSessionStatus::needs_reconciliation && s.port.calls==1 &&
        s.store.latest().phase==SwapSubmissionPhase::execute_intent,"throw after send remains durably uncertain");
    s.session.reset();s.session=std::make_unique<SwapSubmissionSession>(s.link,s.store,s.port,config());
    require(!s.start() && s.session->status()==SwapSubmissionSessionStatus::needs_reconciliation && s.port.calls==1,
        "reconstructed coordinator never resends uncertain execution");
}
void invalid_startup_and_backward_tick() {
    Rig r;auto c=config();c.binding.custody=digest(99);
    r.session=std::make_unique<SwapSubmissionSession>(r.link,r.store,r.port,c);
    require(r.start(),"wrong custody fixture starts");(void)r.finish();
    require(r.port.calls==0,"authenticated state must match configured custody and allocation");
    Rig s;require(s.start(),"backward tick fixture starts");
    require(s.session->poll(999,101)==SwapSubmissionSessionStatus::refused && s.port.calls==0,
        "backwards timeout clock cannot extend submission admission");
}
void late_queued_response_and_deadline_overflow() {
    Rig r;require(r.start(),"queued late reply fixture starts");
    (void)r.session->poll(1000,101);
    require(r.session->poll(1020,121)==SwapSubmissionSessionStatus::refused && r.port.calls==0 && !r.link.busy(),
        "queued valid reply at deadline cannot cause execution");
    Rig s;require(!s.session->start(identity(),body_bytes(),10,std::numeric_limits<Tick>::max()-5) &&
        s.voter.requests==0 && !s.link.busy(),"overflowing deadline refuses before state request");
}
void owned_state_request_cleanup() {
    Rig r;require(r.start(),"abandoned read fixture starts");
    require(r.link.busy(),"fixture state read in flight");
    r.session.reset();require(!r.link.busy(),"destroying session abandons only its owned state request");
    Rig s;require(s.start(),"refused read fixture starts");(void)s.session->poll(999,101);
    require(!s.link.busy(),"early refusal releases owned link request");
    Rig t;require(t.link.request_state(1000,20),"independent link request fixture starts");
    require(!t.start(),"session refuses already busy link");t.session.reset();
    require(t.link.busy(),"destructor does not abandon another owners request");
}
}
int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> cases[]{
        {"durable_before_send_and_no_repeat",durable_before_send_and_no_repeat},
        {"payload_copy_and_input_binding",payload_copy_and_input_binding},
        {"authenticated_state_fences",authenticated_state_fences},
        {"exact_pending_identity",exact_pending_identity},
        {"current_controls_deadline_and_phase",current_controls_deadline_and_phase},
        {"disk_failure_and_ambiguous_send",disk_failure_and_ambiguous_send},
        {"invalid_startup_and_backward_tick",invalid_startup_and_backward_tick},
        {"late_queued_response_and_deadline_overflow",late_queued_response_and_deadline_overflow},
        {"owned_state_request_cleanup",owned_state_request_cleanup}};
    unsigned ran{},failed{};
    for(const auto&[name,test]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
