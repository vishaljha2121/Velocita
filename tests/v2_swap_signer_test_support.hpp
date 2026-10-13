#pragma once
#include "velocita/v2/swap_signer_session.hpp"
#include "v2_swap_test_support.hpp"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/core_names.h>

namespace swap_signer_test {
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
    bool authority_silent{},authority_bad_mac{};std::int64_t authority_delta{1};unsigned authority_requests{};
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
                (request.kind==GatewayRequestKind::state_read || request.kind==GatewayRequestKind::swap_authority),"coordinator issues actual state read only");
        const auto correlation=gateway_correlation(verified.body);
        if(request.kind==GatewayRequestKind::swap_authority){
            ++authority_requests;if(authority_silent)return true;
            std::array<std::byte,kResponseSwapAuthorityGrantedBytes> body{};
            SwapAuthorityProofView proof{request.barrier,1,1,index+authority_delta,index+authority_delta};
            const auto encoded=encode_swap_authority_granted(correlation,proof,body);
            require(encoded.error==NodeProtocolError::none,"fixture fresh swap authority encodes");
            std::vector<std::byte> sealed(kMaxAuthEnvelopeBytes);auto response=key(AuthRole::response,1,22);
            const auto auth=encode_auth_envelope(response,1,++sequence,std::span(body).first(encoded.bytes_written),sealed);
            require(auth.error==AuthEnvelopeError::none,"fixture swap authority authenticated");
            sealed.resize(auth.bytes_written);if(authority_bad_mac)sealed.back()^=std::byte{1};replies.push_back(std::move(sealed));return true;
        }
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

const std::string body="VELOCITA SYNTHETIC EXACT INPUT SWAP\n";
auto body_bytes(){return std::as_bytes(std::span(body.data(),body.size()));}
const std::string handle="synthetic-order-1";
struct Scratch {
    std::filesystem::path base;
    Scratch(){auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/swap-signer-tests";
        std::filesystem::create_directories(root);auto name=(root/"case-XXXXXX").string();
        require(::mkdtemp(name.data())!=nullptr,"scratch created");base=name;}
    ~Scratch(){std::error_code ec;std::filesystem::remove_all(base,ec);}
};
struct TestKey {
    EVP_PKEY* original{};int fd{-1};std::array<std::byte,33> public_key{};
    std::unique_ptr<SwapSignerKey> protected_key;
    explicit TestKey(const std::filesystem::path& path){
        original=EVP_EC_gen("prime256v1");require(original,"synthetic P256 generated");
        require(EVP_PKEY_set_utf8_string_param(original,OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,"compressed")==1,"compressed key fixture");
        std::size_t n{};require(EVP_PKEY_get_octet_string_param(original,OSSL_PKEY_PARAM_PUB_KEY,
            reinterpret_cast<unsigned char*>(public_key.data()),public_key.size(),&n)==1 && n==33,"public key exported");
        BIO* pem=BIO_new(BIO_s_mem());require(pem && PEM_write_bio_PrivateKey(pem,original,nullptr,nullptr,0,nullptr,nullptr)==1,"synthetic PEM encoded");
        char* bytes{};const auto len=BIO_get_mem_data(pem,&bytes);
        int out=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);require(out>=0,"synthetic private file created");
        require(::write(out,bytes,static_cast<std::size_t>(len))==len,"synthetic key written");::close(out);BIO_free(pem);
        fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(fd>=0,"inherited fd fixture opened");
        protected_key=SwapSignerKey::load_inherited_fd(fd,public_key);
    }
    ~TestKey(){if(fd>=0)::close(fd);EVP_PKEY_free(original);}
    bool verifies(std::span<const std::byte> payload,std::span<const std::byte> signature){
        EVP_MD_CTX* context=EVP_MD_CTX_new();require(context,"verification context");
        const bool valid=EVP_DigestVerifyInit(context,nullptr,EVP_sha256(),nullptr,original)==1 &&
            EVP_DigestVerify(context,reinterpret_cast<const unsigned char*>(signature.data()),signature.size(),
                reinterpret_cast<const unsigned char*>(payload.data()),payload.size())==1;
        EVP_MD_CTX_free(context);return valid;
    }
};
struct Clock {
    Tick tick{1000};std::uint64_t event{101};
    static Tick monotonic(void* p)noexcept{return static_cast<Clock*>(p)->tick;}
    static std::uint64_t events(void* p)noexcept{return static_cast<Clock*>(p)->event;}
    SwapSignerClock interface(){return {monotonic,events,this};}
};
struct Fault {
    bool armed{},advance{};Clock* clock{};
    bool fail_result{};unsigned writes{};
    bool crash{};SwapSubmissionStage crash_stage{SwapSubmissionStage::temporary_sync};
    static bool proceed(SwapSubmissionStage stage,void* p)noexcept{
        auto& f=*static_cast<Fault*>(p);
        if(f.crash && stage==f.crash_stage)::_exit(73);
        if(stage==SwapSubmissionStage::partial_write)++f.writes;
        if(f.fail_result && stage==SwapSubmissionStage::temporary_sync && f.writes>=2)return false;
        if(f.advance && stage==SwapSubmissionStage::before_publish)f.clock->tick=1020;
        return !(f.armed && stage==SwapSubmissionStage::temporary_sync);
    }
};
SwapSignerConfig config(){return {swap_test::opening().binding,digest(30),digest(44),CallerId{22},1,20};}
struct Rig {
    Scratch scratch;TestKey key{scratch.base/"key.pem"};Voter voter;
    GatewayLink link{1,process,routes(),voter};Clock clock;Fault fault{false,false,&clock};
    SwapSubmissionBinding binding{swap_signer_journal_binding(config(),key.public_key)};
    std::unique_ptr<SwapSubmissionStore> store;std::unique_ptr<SwapSignerSession> session;
    Rig(){
        require(bool(key.protected_key),"protected synthetic key loaded");
        store=std::make_unique<SwapSubmissionStore>(scratch.base/"signer-replay",binding,SwapSubmissionFaultHook{Fault::proceed,&fault});
        require(store->create({binding,1,digest(51),EffectId{0}})==SwapSubmissionCode::none,"signer journal created");
        ApplyScratch work;
        require(apply(voter.state,{{CallerId{9},RequestSequence{2},digest(99)},SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}}},work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture mode enabled");
        require(apply(voter.state,swap_test::command(9,3,swap_test::buy(swap_test::opening())),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture exact input committed");
        require(apply(voter.state,swap_test::command(22,4,swap::Prepare{EffectId{1},sha256_digest(body_bytes()),
            sha256_digest(std::as_bytes(std::span(handle.data(),handle.size()))),1}),work,
            &shadow_catalog::truemarkets_two_row_catalog()).code==ApplyCode::applied,"fixture prepared committed");
        session=std::make_unique<SwapSignerSession>(link,*store,*key.protected_key,config(),clock.interface());
    }
    bool start(){return session->start(EffectId{1},1,handle,body_bytes(),10,20100);}
    auto finish(){for(unsigned n=0;n<10 && (session->status()==SwapSignerStatus::reading_state || session->status()==SwapSignerStatus::reading_authority);++n){(void)session->poll();++clock.tick;}return session->status();}
};
}
