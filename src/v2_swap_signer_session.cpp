#include "velocita/v2/swap_signer_session.hpp"
#include "velocita/v2/swap_kernel.hpp"
#include "velocita/v2/swap_authority.hpp"
#include "velocita/v2/swap_transaction_review.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ecdsa.h>

namespace velocita::v2 {
namespace {
struct WipedBytes {
    std::array<unsigned char,4096> bytes{};
    ~WipedBytes(){OPENSSL_cleanse(bytes.data(),bytes.size());}
};
void put64(std::span<std::byte> out,std::uint64_t value)noexcept{
    for(unsigned n=0;n<8;++n)out[n]=static_cast<std::byte>(value>>(8*n));
}
std::uint64_t get64(std::span<const std::byte> bytes)noexcept{
    std::uint64_t value{};for(unsigned n=0;n<8;++n)value|=std::uint64_t(std::to_integer<unsigned>(bytes[n]))<<(8*n);return value;
}
bool request_identity(EffectId effect,std::uint64_t sequence,std::string_view handle,std::span<const std::byte> payload,
    std::int64_t index,std::uint64_t expiry,std::uint64_t event_floor,SwapSubmissionIdentity& out)noexcept{
    if(!effect.value() || !sequence || index<=0 || !expiry || !event_floor || event_floor>=expiry || handle.empty() || handle.size()>96 ||
        payload.empty() || payload.size()>8192 || !std::all_of(handle.begin(),handle.end(),[](unsigned char c){
            return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_';}))return false;
    out={};out.effect=effect;out.payload=sha256_digest(payload);out.handle_size=handle.size()+8;
    put64(std::span(out.handle),event_floor);std::copy(handle.begin(),handle.end(),reinterpret_cast<char*>(out.handle.data()+8));
    out.handle_digest=sha256_digest(std::span(out.handle).first(out.handle_size));
    const auto original=sha256_digest(std::as_bytes(std::span(handle.data(),handle.size())));
    std::array<std::byte,96> request{};put64(std::span(request),sequence);put64(std::span(request).subspan(8),effect.value());
    put64(std::span(request).subspan(16),static_cast<std::uint64_t>(index));put64(std::span(request).subspan(24),expiry);
    std::copy(out.payload.bytes.begin(),out.payload.bytes.end(),request.begin()+32);
    std::copy(original.bytes.begin(),original.bytes.end(),request.begin()+64);out.execution_request=sha256_digest(request);return true;
}

std::uint64_t verification_deadline(std::span<const std::byte> payload,
    const SwapSignerVerificationContext& v,std::uint64_t now,std::uint64_t expiry)noexcept{
    truemarkets::DecodedSolanaPayload decoded;
    const auto text=std::string_view(reinterpret_cast<const char*>(payload.data()),payload.size());
    if(truemarkets::decode_solana_signing_envelope(text,v.organization.view(),v.signer.view(),decoded)!=truemarkets::SolanaParseError::none ||
        decoded.timestamp_ms<=0 || static_cast<std::uint64_t>(decoded.timestamp_ms)>now)return 0;
    const auto deadline=std::min(expiry,static_cast<std::uint64_t>(decoded.timestamp_ms)+v.policy.max_envelope_age_ms);
    return now<deadline?deadline:0;
}

}
SwapSignerKey::SwapSignerKey(void* key,std::array<std::byte,33> pub)noexcept:key_(key),public_(pub){}
SwapSignerKey::~SwapSignerKey(){EVP_PKEY_free(static_cast<EVP_PKEY*>(key_));}
std::unique_ptr<SwapSignerKey> SwapSignerKey::load_inherited_fd(int fd,std::span<const std::byte,33> expected){
    struct stat info{};const auto flags=::fcntl(fd,F_GETFL);
    if(flags<0 || (flags&O_ACCMODE)!=O_RDONLY || ::fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) ||
        info.st_uid!=::geteuid() || (info.st_mode&07777)!=0600 || info.st_nlink!=1 || info.st_size<=0 || info.st_size>4096)return {};
    WipedBytes buffer;std::size_t offset{};
    while(offset<static_cast<std::size_t>(info.st_size)){
        const auto n=::pread(fd,buffer.bytes.data()+offset,static_cast<std::size_t>(info.st_size)-offset,static_cast<off_t>(offset));
        if(n<0 && errno==EINTR)continue;if(n<=0)return {};offset+=static_cast<std::size_t>(n);
    }
    BIO* input=BIO_new_mem_buf(buffer.bytes.data(),static_cast<int>(offset));if(!input)return {};
    auto no_password=[](char*,int,int,void*)->int{return 0;};
    EVP_PKEY* key=PEM_read_bio_PrivateKey(input,nullptr,no_password,nullptr);
    char trailing{};bool clean=true;while(BIO_read(input,&trailing,1)==1)if(trailing!='\n' && trailing!='\r' && trailing!=' ' && trailing!='\t')clean=false;
    BIO_free(input);if(!key)return {};
    auto discard=[&](){EVP_PKEY_free(key);return std::unique_ptr<SwapSignerKey>{};};
    if(!clean || EVP_PKEY_is_a(key,"EC")!=1)return discard();
    std::array<char,32> group{};std::size_t length{};
    if(EVP_PKEY_get_utf8_string_param(key,OSSL_PKEY_PARAM_GROUP_NAME,group.data(),group.size(),&length)!=1 ||
        std::string_view(group.data(),length)!="prime256v1")return discard();
    EVP_PKEY_CTX* check=EVP_PKEY_CTX_new(key,nullptr);if(!check)return discard();
    const bool valid=EVP_PKEY_private_check(check)==1 && EVP_PKEY_pairwise_check(check)==1;EVP_PKEY_CTX_free(check);
    if(!valid || EVP_PKEY_set_utf8_string_param(key,OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,"compressed")!=1)return discard();
    std::array<std::byte,33> pub{};
    if(EVP_PKEY_get_octet_string_param(key,OSSL_PKEY_PARAM_PUB_KEY,reinterpret_cast<unsigned char*>(pub.data()),pub.size(),&length)!=1 ||
        length!=pub.size() || !std::equal(pub.begin(),pub.end(),expected.begin()))return discard();
    return std::unique_ptr<SwapSignerKey>(new SwapSignerKey(key,pub));
}
bool SwapSignerKey::sign(std::span<const std::byte> bytes,std::span<std::byte> der,std::size_t& size)noexcept{
    ++attempts_;
    EVP_MD_CTX* context=EVP_MD_CTX_new();if(!context)return false;size=der.size();
    const bool ok=EVP_DigestSignInit(context,nullptr,EVP_sha256(),nullptr,static_cast<EVP_PKEY*>(key_))==1 &&
        EVP_DigestSign(context,reinterpret_cast<unsigned char*>(der.data()),&size,
            reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size())==1;
    EVP_MD_CTX_free(context);if(!ok)size=0;return ok;
}
bool SwapSignerKey::verify(std::span<const std::byte> bytes,std::span<const std::byte> signature)noexcept{
    EVP_MD_CTX* context=EVP_MD_CTX_new();if(!context)return false;
    const bool valid=EVP_DigestVerifyInit(context,nullptr,EVP_sha256(),nullptr,static_cast<EVP_PKEY*>(key_))==1 &&
        EVP_DigestVerify(context,reinterpret_cast<const unsigned char*>(signature.data()),signature.size(),
            reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size())==1;
    EVP_MD_CTX_free(context);return valid;
}
namespace {
bool raw_signature(std::span<const std::byte> der,SwapSubmissionTransaction& raw)noexcept{
    const auto* p=reinterpret_cast<const unsigned char*>(der.data());const auto* end=p+der.size();
    ECDSA_SIG* sig=d2i_ECDSA_SIG(nullptr,&p,static_cast<long>(der.size()));if(!sig)return false;
    const BIGNUM *r{},*s{};ECDSA_SIG_get0(sig,&r,&s);
    const bool valid=p==end && BN_bn2binpad(r,reinterpret_cast<unsigned char*>(raw.data()),32)==32 &&
        BN_bn2binpad(s,reinterpret_cast<unsigned char*>(raw.data()+32),32)==32;
    ECDSA_SIG_free(sig);return valid;
}
bool der_signature(const SwapSubmissionTransaction& raw,std::span<std::byte> der,std::size_t& size)noexcept{
    ECDSA_SIG* sig=ECDSA_SIG_new();if(!sig)return false;
    BIGNUM* r=BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()),32,nullptr);
    BIGNUM* s=BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()+32),32,nullptr);
    if(!r || !s || ECDSA_SIG_set0(sig,r,s)!=1){BN_free(r);BN_free(s);ECDSA_SIG_free(sig);return false;}
    const int n=i2d_ECDSA_SIG(sig,nullptr);bool valid=n>0 && static_cast<std::size_t>(n)<=der.size();
    if(valid){auto* p=reinterpret_cast<unsigned char*>(der.data());valid=i2d_ECDSA_SIG(sig,&p)==n;size=static_cast<std::size_t>(n);}
    ECDSA_SIG_free(sig);return valid;
}
}
SwapSubmissionBinding swap_signer_journal_binding(const SwapSignerConfig& c,std::span<const std::byte,33> pub)noexcept{
    std::array<std::byte,265> bytes{};constexpr std::string_view label="swap-signer-v2";
    std::copy(label.begin(),label.end(),reinterpret_cast<char*>(bytes.data()));
    put64(std::span(bytes).subspan(16),c.caller.value());put64(std::span(bytes).subspan(24),c.key_version);
    std::copy(c.authority_domain.bytes.begin(),c.authority_domain.bytes.end(),bytes.begin()+32);
    std::copy(c.signer_instance.bytes.begin(),c.signer_instance.bytes.end(),bytes.begin()+64);
    std::copy(pub.begin(),pub.end(),bytes.begin()+96);
    std::copy(c.binding.catalog.bytes.begin(),c.binding.catalog.bytes.end(),bytes.begin()+129);
    std::copy(c.binding.custody.bytes.begin(),c.binding.custody.bytes.end(),bytes.begin()+161);
    put64(std::span(bytes).subspan(193),c.binding.account.value());put64(std::span(bytes).subspan(201),c.binding.instrument.value());
    put64(std::span(bytes).subspan(209),c.binding.policy.value());put64(std::span(bytes).subspan(217),c.binding.policy_version.value());
    put64(std::span(bytes).subspan(225),c.binding.risk_policy_version.value());
    std::copy(c.transaction_policy_digest.bytes.begin(),c.transaction_policy_digest.bytes.end(),bytes.begin()+233);
    return {c.binding.incarnation,sha256_digest(std::span(bytes).first(c.transaction_policy_digest.is_zero()?233:265))};
}
SwapSignerSession::SwapSignerSession(GatewayLink& link,SwapSubmissionStore& store,SwapSignerKey& key,SwapSignerConfig config,SwapSignerClock clock,
    std::optional<SwapSignerVerificationContext> verification)
    :link_(link),store_(store),key_(key),config_(config),clock_(clock),verification_(std::move(verification)){
    if(!config_.state_timeout || config_.authority_domain.is_zero() || config_.binding.incarnation.is_zero() ||
        config_.signer_instance.is_zero() || !config_.caller.value() || !config_.key_version || !clock_.monotonic || !clock_.event ||
        store_.status()!=SwapSubmissionCode::none || store_.latest().binding!=swap_signer_journal_binding(config_,key_.public_key()))return;
    if(verification_){
        if(config_.transaction_policy_digest.is_zero() || verification_->policy.binding!=config_.binding ||
            swap_signer_verification_digest(*verification_)!=config_.transaction_policy_digest)return;
    }else if(!config_.transaction_policy_digest.is_zero())return;
    status_=store_.latest().phase==SwapSubmissionPhase::idle?SwapSignerStatus::idle:SwapSignerStatus::fenced;
}
SwapSignerSession::~SwapSignerSession(){if((status_==SwapSignerStatus::reading_state || status_==SwapSignerStatus::reading_authority))link_.abandon();erase();}
void SwapSignerSession::erase()noexcept{OPENSSL_cleanse(payload_.data(),payload_.size());size_=0;lookup_={};}
SwapSignerStatus SwapSignerSession::refuse(SwapSignerStatus status)noexcept{
    if((status_==SwapSignerStatus::reading_state || status_==SwapSignerStatus::reading_authority))link_.abandon();erase();der_size_=0;stamp_={};status_=status;return status;
}
bool SwapSignerSession::start(EffectId effect,std::uint64_t sequence,std::string_view handle,std::span<const std::byte> payload,std::int64_t minimum_index,std::uint64_t expected_sign_by,
    const truemarkets::JupiterLookupPrefix& lookup){
    if(status_!=SwapSignerStatus::idle || link_.busy())return false;
    const auto now=clock_.monotonic(clock_.context);last_event_=clock_.event(clock_.context);
    if((config_.account_review_expires_at_ms && last_event_>=config_.account_review_expires_at_ms) || !sequence || !expected_sign_by || effect.value()<=store_.latest().identity.effect.value() || minimum_index<=store_.latest().committed_index ||
        handle.empty() || handle.size()>96 || payload.empty() || payload.size()>payload_.size() ||
        now>std::numeric_limits<Tick>::max()-config_.state_timeout ||
        !std::all_of(handle.begin(),handle.end(),[](unsigned char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_';})){
        (void)refuse(SwapSignerStatus::refused);return false;
    }
    if(!request_identity(effect,sequence,handle,payload,minimum_index,expected_sign_by,last_event_,identity_)){
        (void)refuse(SwapSignerStatus::refused);return false;
    }
    original_handle_digest_=sha256_digest(std::as_bytes(std::span(handle.data(),handle.size())));
    std::copy(payload.begin(),payload.end(),payload_.begin());size_=payload.size();minimum_index_=minimum_index;expected_sign_by_=expected_sign_by;sequence_=sequence;
    lookup_=lookup;
    last_tick_=now;deadline_=now+config_.state_timeout;
    if(!link_.request_state(now,config_.state_timeout)){(void)refuse(SwapSignerStatus::refused);return false;}
    status_=SwapSignerStatus::reading_state;return true;
}
bool SwapSignerSession::clock_valid(const swap::Intent& intent,std::uint64_t state_event)noexcept{
    const auto now=clock_.monotonic(clock_.context);const auto event=clock_.event(clock_.context);
    if(now<last_tick_ || now>=deadline_ || event<last_event_ || event<state_event || event<intent.source_time || event>=intent.sign_by ||
       (config_.account_review_expires_at_ms && event>=config_.account_review_expires_at_ms) ||
       (verification_expires_at_ms_ && event>=verification_expires_at_ms_))return false;
    last_tick_=now;last_event_=event;return true;
}
SwapSignerStatus SwapSignerSession::poll(){
    if(status_!=SwapSignerStatus::reading_state && status_!=SwapSignerStatus::reading_authority)return status_;
    const auto now=clock_.monotonic(clock_.context);
    if(now<last_tick_ || now>=deadline_)return refuse(SwapSignerStatus::refused);
    last_tick_=now;
    if(store_.status()!=SwapSubmissionCode::none)return refuse(SwapSignerStatus::storage_failed);
    const auto outcome=link_.pump(now);
    if(outcome.status==LinkStatus::pending || outcome.status==LinkStatus::idle)return status_;
    if(status_==SwapSignerStatus::reading_authority){
        if(outcome.status!=LinkStatus::done || outcome.request!=LinkRequest::swap_authority || !outcome.swap_proof)return refuse(SwapSignerStatus::refused);
        const auto& proof=outcome.swap_proof->view();
        if(proof.request!=authority_request_ || proof.issuer_id<=0 || proof.term<=0 || proof.barrier_index<=state_read_index_ ||
            proof.barrier_index<minimum_index_ || proof.applied_index<proof.barrier_index ||
            !clock_valid(authority_pending_.intent,last_event_))return refuse(SwapSignerStatus::refused);
        return finish_authorized(proof.barrier_index);
    }
    if(outcome.status!=LinkStatus::done || outcome.request!=LinkRequest::state ||
        outcome.reading.authority_domain!=config_.authority_domain || outcome.reading.applied_index<minimum_index_ ||
        outcome.reading.state_digest.is_zero())return refuse(SwapSignerStatus::refused);
    const auto& state=link_.state();const auto& swap=state.spot_swap;const auto& pending=swap.pending;
    if(!swap_state_bindings_valid(state) || !swap.initialized || swap.conflicted ||
        swap.opening.binding!=config_.binding || pending.phase!=swap::Phase::prepared ||
        pending.intent.effect!=identity_.effect || pending.payload!=identity_.payload || pending.handle!=original_handle_digest_ ||
        swap.mode==swap::Mode::halted || (pending.intent.side==Side::buy && swap.mode!=swap::Mode::normal) ||
        pending.intent.kill_epoch!=swap.kill_epoch || pending.intent.executor_generation!=swap.executor_generation ||
        pending.intent.sign_by!=expected_sign_by_ || !clock_valid(pending.intent,swap.event_time))return refuse(SwapSignerStatus::refused);
    if(verification_){
        const auto& v=*verification_;
        if(!swap_live_scope(swap,v))return refuse(SwapSignerStatus::refused);
        const auto checked=truemarkets::verify_swap_transaction(
            std::string_view(reinterpret_cast<const char*>(payload_.data()),size_),v.organization.view(),v.signer.view(),
            pending,v.policy,lookup_,last_event_);
        if(!checked.verified || checked.verified->payload_digest()!=identity_.payload ||
            checked.verified->intent_digest()!=truemarkets::verified_swap_intent_digest(pending.intent) ||
            checked.verified->policy_digest()!=truemarkets::swap_execution_policy_digest(v.policy) ||
            checked.verified->lookup_digest()!=truemarkets::jupiter_lookup_prefix_digest(lookup_))return refuse(SwapSignerStatus::refused);
        verification_expires_at_ms_=checked.verified->expires_at_ms();
    }else{
#ifdef VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE
    constexpr std::string_view fixture="VELOCITA SYNTHETIC EXACT INPUT SWAP\n";
    constexpr std::string_view inclusive_fixture="VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n";
    const auto& intent=pending.intent;
    const auto text=std::string_view(reinterpret_cast<const char*>(payload_.data()),size_);
    // Two literal non-transaction fixtures, each bound to its own exact economics.
    const bool fixture_economics=(text==fixture && intent.quote_input.value()==1'000'000 && intent.max_quote_debit.value()==1'002'000) ||
        (text==inclusive_fixture && intent.quote_input.value()==998'000 && intent.max_quote_debit.value()==1'000'000);
    if(!fixture_economics || intent.side!=Side::buy ||
        intent.fee_cap.value()!=2'000 || intent.min_base_receive.value()!=9'000'000 || intent.base_input.value()!=0 ||
        intent.min_quote_receive.value()!=0 || intent.transient_base_hold.value()!=0)
        return refuse(SwapSignerStatus::refused);
#else
    return refuse(SwapSignerStatus::refused);
#endif
    }
    authority_pending_=pending;state_read_index_=outcome.reading.applied_index;
    authority_request_={link_.incarnation(),sequence_,identity_.effect,swap_signing_authority_digest(pending),
        KillEpoch{pending.intent.kill_epoch},ExecutorGeneration{pending.intent.executor_generation},config_.binding.catalog,config_.authority_domain};
    if(!link_.request_swap_authority(authority_request_,last_tick_,deadline_-last_tick_))return refuse(SwapSignerStatus::refused);
    status_=SwapSignerStatus::reading_authority;return status_;
}
SwapSignerStatus SwapSignerSession::finish_authorized(std::int64_t committed_index){
    const auto& pending=authority_pending_;
    if(store_.begin(identity_,committed_index)!=SwapSubmissionCode::none)return refuse(SwapSignerStatus::storage_failed);
    status_=SwapSignerStatus::fenced;
    if(!clock_valid(pending.intent,last_event_))return refuse(SwapSignerStatus::fenced);
    if(!key_.sign(std::span(payload_).first(size_),der_,der_size_))return refuse(SwapSignerStatus::fenced);
    if(!clock_valid(pending.intent,last_event_))return refuse(SwapSignerStatus::fenced);
    if(swap_signing::encode_api_key_stamp(key_.public_key(),std::span(der_).first(der_size_),stamp_)!=swap_signing::StampCode::encoded)
        return refuse(SwapSignerStatus::fenced);
    SwapSubmissionTransaction raw{};
    if(!raw_signature(std::span(der_).first(der_size_),raw) || store_.observe_transaction(identity_,raw)!=SwapSubmissionCode::none)
        return refuse(SwapSignerStatus::storage_failed);
    if(!clock_valid(pending.intent,last_event_))return refuse(SwapSignerStatus::fenced);
    erase();status_=SwapSignerStatus::signed_once;return status_;
}
void SwapSignerSession::cancel_pending()noexcept{
    if(status_==SwapSignerStatus::reading_state || status_==SwapSignerStatus::reading_authority)(void)refuse(SwapSignerStatus::refused);
}
bool SwapSignerSession::retrieve(EffectId effect,std::uint64_t sequence,std::string_view handle,
    std::span<const std::byte> payload,std::int64_t minimum_index,std::uint64_t expiry){
    if(status_!=SwapSignerStatus::fenced && status_!=SwapSignerStatus::signed_once)return false;
    der_size_=0;stamp_={};
    const auto now=clock_.monotonic(clock_.context);const auto event=clock_.event(clock_.context);
    if((config_.account_review_expires_at_ms && event>=config_.account_review_expires_at_ms) || !expiry || now>std::numeric_limits<Tick>::max()-config_.state_timeout || now<last_tick_ || event<last_event_ || event>=expiry ||
        store_.status()!=SwapSubmissionCode::none || store_.latest().phase!=SwapSubmissionPhase::execute_intent ||
        store_.latest().binding!=swap_signer_journal_binding(config_,key_.public_key()))return false;
    const auto record=store_.latest();if(record.identity.handle_size<=8 || record.identity.handle_size>104)return false;
    const auto floor=get64(std::span(record.identity.handle).first(8));SwapSubmissionIdentity expected;
    if(event<floor || !request_identity(effect,sequence,handle,payload,minimum_index,expiry,floor,expected) || expected!=record.identity)return false;
    std::uint64_t verified_expiry{};
    if(verification_){
        verified_expiry=verification_deadline(payload,*verification_,event,expiry);
        if(!verified_expiry)return false;
    }else{
#ifdef VELOCITA_SWAP_SIGNER_SYNTHETIC_FIXTURE
    constexpr std::string_view fixture="VELOCITA SYNTHETIC EXACT INPUT SWAP\n";
    constexpr std::string_view inclusive_fixture="VELOCITA SYNTHETIC FEE INCLUSIVE SWAP\n";
    const auto text=std::string_view(reinterpret_cast<const char*>(payload.data()),payload.size());
    if(text!=fixture && text!=inclusive_fixture)return false;
#else
    return false;
#endif
    }
    if(!der_signature(record.transaction,der_,der_size_) || !key_.verify(payload,std::span(der_).first(der_size_))){der_size_=0;return false;}
    const auto after=clock_.monotonic(clock_.context);const auto after_event=clock_.event(clock_.context);
    if(after<now || after>=now+config_.state_timeout || after_event<event || after_event>=expiry ||
        (verified_expiry && after_event>=verified_expiry) || (config_.account_review_expires_at_ms && after_event>=config_.account_review_expires_at_ms) ||
        store_.status()!=SwapSubmissionCode::none || store_.latest().checksum!=record.checksum ||
        swap_signing::encode_api_key_stamp(key_.public_key(),std::span(der_).first(der_size_),stamp_)!=swap_signing::StampCode::encoded){
        der_size_=0;stamp_={};return false;
    }
    last_tick_=after;last_event_=after_event;status_=SwapSignerStatus::signed_once;return true;
}
}
