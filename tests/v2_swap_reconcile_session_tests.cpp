#include "velocita/v2/swap_reconcile_session.hpp"
#include "v2_swap_test_support.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace velocita::v2;
using namespace velocita::v2::transport;
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
struct Clock final : SwapReconcileClock {
    std::uint64_t now{1000};
    std::uint64_t now_ms() noexcept override { return now; }
};
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
struct Http final : SwapBalanceGet {
    SwapBalanceSessionIdentity bound{digest(40),digest(41)};
    unsigned calls{};
    SwapHttpsResult response;
    std::function<void()> during;
    Http() {
        constexpr std::string_view body=R"({"data":[{"symbol":"SOL","chain":"solana","venue":"defi","total":"0","available":"0","held":"0"},{"symbol":"USDC","chain":"solana","venue":"defi","total":"5","available":"5","held":"0"}]})";
        response.status=SwapHttpsStatus::complete;response.exposure=SwapHttpsExposure::response_received;response.http_status=200;
        response.body_size=body.size();std::copy(body.begin(),body.end(),response.body.begin());
    }
    SwapBalanceSessionIdentity identity() const noexcept override { return bound; }
    SwapHttpsResult get() override { ++calls;if(during)during();return response; }
};
SwapReconcileSessionConfig config() {
    SwapReconcileSessionConfig c;
    c.binding={swap_test::opening().binding,digest(40),10'000'000};
    c.outside={digest(40),digest(42),digest(43),900,0,0};
    c.http_identity={digest(40),digest(41)};c.authority_domain=digest(30);
    c.clock_epoch=digest(42);c.session_nonce=digest(44);c.max_age_ms=100;c.state_timeout_ms=20;return c;
}
struct Rig {
    Voter voter;
    GatewayLink link{1,process,routes(),voter};
    Clock clock;
    Http http;
    SwapResultWatermark watermark{digest(1)};
    SwapReconcileSession session{link,http,clock,watermark,config()};
    void seed() { require(watermark.restore({digest(1),digest(50),10}),"explicit fixture durable seed accepted"); }
    SwapReconcileSessionStatus finish() { (void)session.poll();++clock.now;return session.poll(); }
};
void authenticated_state_and_exact_http() {
    Rig r;r.seed();
    require(r.session.start(),"seeded coordinator requests state");
    require(r.finish()==SwapReconcileSessionStatus::complete,"authenticated state followed by exact HTTP snapshot compares");
    const auto& result=r.session.result();
    std::array<std::byte,kMaxStateSnapshotBytes> bytes{};
    const auto snapshot=encode_state_snapshot(r.voter.state,bytes);
    require(r.http.calls==1 && result.reading.applied_index==10 && result.reading.authority_domain==digest(30) &&
            result.captured==r.voter.state.spot_swap && result.reading.state_digest==snapshot.state_digest &&
            result.comparison.comparison.verdict==SpotBalanceVerdict::matching_snapshot_only,
            "owned state and authenticated full digest retained without trading authority");
    (void)r.session.poll();require(r.http.calls==1,"completed poll never repeats GET");
}
void unexplained_delta_is_not_complete() {
    Rig r;r.seed();
    std::string body(r.http.response.body.data(),r.http.response.body_size);
    for(auto field:{std::string("\"total\":\"5\""),std::string("\"available\":\"5\"")}){
        const auto at=body.find(field);require(at!=body.npos,"fixture quote amount found");
        body.replace(at,field.size(),field.substr(0,field.size()-2)+"4\"");
    }
    r.http.response.body_size=body.size();std::copy(body.begin(),body.end(),r.http.response.body.begin());
    require(r.session.start(),"changed cash fixture starts");
    require(r.finish()==SwapReconcileSessionStatus::comparison_refused &&
        r.session.result().comparison.comparison.quote_delta_atoms==-1'000'000,
        "unexplained wallet withdrawal refuses completion and retains exact delta");
}
void unseeded_and_bad_identity_never_read_http() {
    Rig r;require(!r.session.start() && r.http.calls==0 && r.voter.requests==0,"restart starts closed without durable seed");
    require(!r.watermark.restore({digest(99),digest(50),10}),"other incarnation seed refused");
    Rig s;s.seed();s.http.bound.session_evidence_digest=digest(99);
    require(!s.session.start() && s.http.calls==0,"changed session provenance cannot select another account");
    Rig t;t.seed();t.voter.domain=digest(99);require(t.session.start(),"wrong domain fixture starts");
    require(t.finish()==SwapReconcileSessionStatus::identity_refused && t.http.calls==0,"authenticated other authority domain cannot authorize balance read");
}
void bad_authentication_and_digest_never_read_http() {
    for(unsigned mode=0;mode<3;++mode) {
        Rig r;r.seed();r.voter.bad_mac=mode==0;r.voter.bad_snapshot=mode==1;r.voter.wrong_kind=mode==2;
        require(r.session.start(),"rejected response fixture starts");(void)r.finish();
        r.clock.now+=21;(void)r.session.poll();
        require(r.session.result().status==SwapReconcileSessionStatus::state_refused && r.http.calls==0,
                "MAC digest or wrong response kind cannot bypass GatewayLink");
    }
}
void watermark_during_http_and_monotonicity() {
    Rig r;r.seed();r.http.during=[&]{std::thread worker([&]{require(r.watermark.publish(11),"concurrent committed result published");});worker.join();};
    require(r.session.start(),"in flight result fixture starts");
    require(r.finish()==SwapReconcileSessionStatus::comparison_refused && r.session.result().end_result_watermark==11 &&
            r.session.result().comparison.code==SwapReconcileCode::state_watermark,
            "commit learned during synchronous GET invalidates captured old state");
    require(!r.watermark.publish(10) && r.watermark.index()==11,"committed watermark cannot decrease");
    require(!r.watermark.publish(-1) && r.watermark.index()<0,"invalid committed index permanently closes watermark");
    require(!r.watermark.restore({digest(1),digest(50),12}),"invalid watermark cannot be silently reseeded");
}
void http_failures_and_stale_completion() {
    for(unsigned mode=0;mode<4;++mode) {
        Rig r;r.seed();
        if(mode==0)r.http.response.http_status=401;
        if(mode==1)r.http.response.status=SwapHttpsStatus::tls_failure;
        if(mode==2)r.http.response.exposure=SwapHttpsExposure::possibly_sent;
        if(mode==3)r.http.response.body_size=kSwapHttpsBodyCapacity+1;
        require(r.session.start(),"bad HTTP fixture starts");
        require(r.finish()==SwapReconcileSessionStatus::http_refused && r.http.calls==1,"bad HTTP result refused without body span or retry");
    }
    Rig r;r.seed();r.http.during=[&]{r.clock.now+=101;};require(r.session.start(),"late HTTP fixture starts");
    require(r.finish()==SwapReconcileSessionStatus::comparison_refused && r.http.calls==1,"late HTTP response fails request age gate");
}
void concrete_adapter_refuses_unbound_credentials() {
    SwapHttpsBalanceGet unbound({},"fixture-token");
    auto result=unbound.get();require(result.status==SwapHttpsStatus::local_refusal && result.exposure==SwapHttpsExposure::not_attempted,
                                   "unbound concrete credential refused before transport");
    SwapHttpsBalanceGet empty({digest(40),digest(41)},"");result=empty.get();
    require(result.status==SwapHttpsStatus::local_refusal,"empty bearer cannot become anonymous balance GET");
    SwapHttpsBalanceGet bounded({digest(40),digest(41)},"fixture-token",0);result=bounded.get();
    require(result.status==SwapHttpsStatus::local_refusal && result.exposure==SwapHttpsExposure::not_attempted,
            "concrete adapter preserves exact configured transport refusal without network");
}
void pending_state_and_late_identity_remain_closed() {
    Rig r;r.seed();ApplyScratch scratch;
    const auto& catalog=shadow_catalog::truemarkets_two_row_catalog();
    require(apply(r.voter.state,{{CallerId{9},RequestSequence{2},digest(70)},
        SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}},scratch,&catalog).code==ApplyCode::applied,
        "fixture enables explicit swap");
    require(apply(r.voter.state,swap_test::command(9,3,swap_test::buy(swap_test::opening())),scratch,&catalog).code==ApplyCode::applied,
            "fixture pending reservation committed");
    const auto before=r.voter.state.spot_swap;
    require(r.session.start(),"pending state fixture starts");
    require(r.finish()==SwapReconcileSessionStatus::comparison_refused && r.http.calls==0 &&
            r.session.result().captured==before && before.reserved_quote==swap::QuoteAtoms{1'002'000},
            "authenticated pending state keeps reservation and never issues GET");
    Rig s;s.seed();s.http.during=[&]{s.http.bound.session_evidence_digest=digest(99);};
    require(s.session.start(),"late identity fixture starts");
    require(s.finish()==SwapReconcileSessionStatus::identity_refused && s.http.calls==1,
            "identity changed during GET never produces comparison");
    Rig t;t.seed();require(t.watermark.publish(11),"new result before read known");
    require(t.session.start(),"older state fixture starts");
    require(t.finish()==SwapReconcileSessionStatus::comparison_refused && t.http.calls==0 &&
            t.session.result().comparison.code==SwapReconcileCode::state_watermark,
            "state behind known result refuses HTTP before read");
}
}
int main(int argc,char** argv) {
    const std::pair<const char*,void(*)()> cases[]{
        {"authenticated_state_and_exact_http",authenticated_state_and_exact_http},
        {"unexplained_delta_is_not_complete",unexplained_delta_is_not_complete},
        {"unseeded_and_bad_identity_never_read_http",unseeded_and_bad_identity_never_read_http},
        {"bad_authentication_and_digest_never_read_http",bad_authentication_and_digest_never_read_http},
        {"watermark_during_http_and_monotonicity",watermark_during_http_and_monotonicity},
        {"http_failures_and_stale_completion",http_failures_and_stale_completion},
        {"concrete_adapter_refuses_unbound_credentials",concrete_adapter_refuses_unbound_credentials},
        {"pending_state_and_late_identity_remain_closed",pending_state_and_late_identity_remain_closed}};
    unsigned ran{},failed{};
    for(const auto&[name,test]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;++ran;
        try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception&e){++failed;std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
    return ran && !failed?0:1;
}
