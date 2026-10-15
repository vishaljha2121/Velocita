


#include <cstring>
#include <charconv>
#define main existing_gateway_suite_main
#include "v2_gateway_node_tests.cpp"
#undef main
#include "velocita/v2/swap_runtime_production.hpp"
#include "velocita/v2/swap_transaction_review.hpp"
#include "v2_swap_signer_test_support.hpp"
#include "v2_swap_transaction_verifier_test_support.hpp"
#include "swap_settlement_test_fixture.hpp"
#include <csignal>
#include <cerrno>
#include <fstream>
#include <sys/socket.h>
#include <sys/wait.h>

namespace continuous_drill {
using namespace velocita::v2;
using namespace truemarkets;
using namespace settlement_lifecycle_fixture;
using ::require;
constexpr std::uint64_t event_ms=2000, gross_buy=9'410'623, rent=1'488'440, repayment=2'039'280;
constexpr std::uint64_t net_buy=gross_buy+rent-repayment, sell_gross=1'002'000, sell_fee=2'000;
constexpr std::string_view organization="verifier-fixture", signer_id="11111111111111111111111111111113";
std::filesystem::path evidence;
void record(std::string_view line){
    const int fd=::open((evidence/"events.log").c_str(),O_WRONLY|O_CREAT|O_APPEND,0600);
    require(fd>=0,"event transcript opens");const auto b=std::string(line)+"\n";
    require(::write(fd,b.data(),b.size())==static_cast<ssize_t>(b.size()) && ::fsync(fd)==0,"event transcript durable");::close(fd);
}
unsigned count(std::string_view prefix){std::ifstream f(evidence/"events.log");std::string line;unsigned n{};while(std::getline(f,line))if(line.starts_with(prefix))++n;return n;}
std::string decimal(std::uint64_t n,unsigned places){auto s=std::to_string(n);if(s.size()<=places)s.insert(0,places+1-s.size(),'0');s.insert(s.size()-places,".");return s;}
std::string quote(std::string_view s){std::string out="\"";for(char c:s){if(c=='\"'||c=='\\')out+='\\';out+=c;}return out+'\"';}
std::string hex_digest(Digest256 d){std::string out;for(auto b:d.bytes){const auto n=std::to_integer<unsigned>(b);out+="0123456789abcdef"[n>>4];out+="0123456789abcdef"[n&15];}return out;}
swap::Initialize opening(){auto o=swap_test::opening();o.binding.incarnation=sha256_digest(kIncarnation);o.quote=swap::QuoteAtoms{25'000'000};
    o.limits.max_purchase_total=swap::QuoteAtoms{3'000'000'000};o.limits.max_buy_debit=swap::QuoteAtoms{1'000'000};
    o.limits.max_fee=swap::QuoteAtoms{50'000};o.limits.max_sell_input=swap::BaseAtoms{250'000'000};
    o.limits.max_buy_price_per_sol=swap::QuoteAtoms{146'635'156};return o;}
SwapSignerVerificationContext review(){test_support::RouteFixture tx;tx.policy.binding=opening().binding;tx.policy.max_envelope_age_ms=30'000;
    return {*tm_text(organization),*tm_text(signer_id),tx.policy};}
swap::Intent intent(unsigned effect){auto i=swap_test::buy(opening(),effect);i.source_time=1900;i.sign_by=21'000;
    i.quote_input=swap::QuoteAtoms{998'000};i.max_quote_debit=swap::QuoteAtoms{1'000'000};i.min_base_receive=swap::BaseAtoms{6'819'647};
    if(effect%2==0){i.side=Side::sell;i.quote_input={};i.max_quote_debit={};i.min_base_receive={};i.base_input=swap::BaseAtoms{net_buy};
        i.min_quote_receive=swap::QuoteAtoms{990'000};i.fee_cap=swap::QuoteAtoms{3'000};}return i;}
test_support::RouteFixture transaction(unsigned effect){test_support::RouteFixture tx;
    if(effect%2==0){tx.make_sell();tx.input=net_buy;tx.wrap=net_buy;tx.quoted=sell_gross;}
    tx.timestamp=1900+effect;tx.policy=review().policy;tx.pending.intent=intent(effect);return tx;}
std::array<std::byte,33> public_key(){std::array<std::byte,33> p{};std::ifstream f(evidence/"public-key",std::ios::binary);f.read(reinterpret_cast<char*>(p.data()),p.size());require(f.gcount()==33,"public fixture key read");return p;}
struct Clock:SwapFinalizedReadClock{std::uint64_t now_ms()noexcept override{return event_ms;}};
struct Wire {
    int fd;Bytes incoming,outgoing;bool observe_child_exit{},peer_closed{};
    explicit Wire(int f,bool observe_exit=false):fd(f),observe_child_exit(observe_exit){require(::fcntl(fd,F_SETFL,::fcntl(fd,F_GETFL)|O_NONBLOCK)==0,"bridge nonblocking");}
    void send(std::int32_t node,std::span<const std::byte> body){
        if(peer_closed)return;
        require(body.size()<=kMaxAuthEnvelopeBytes,"bridge frame bounded");
        const auto size=static_cast<std::uint32_t>(body.size());
        auto add=[&](const auto& v){const auto b=std::as_bytes(std::span(&v,1));outgoing.insert(outgoing.end(),b.begin(),b.end());};
        add(size);add(node);outgoing.insert(outgoing.end(),body.begin(),body.end());
        require(outgoing.size()<8*1024*1024,"bridge outbound bounded");flush();
    }
    void flush(){while(!outgoing.empty()){
        const auto n=::write(fd,outgoing.data(),outgoing.size());
        if(n>0)outgoing.erase(outgoing.begin(),outgoing.begin()+n);
        else if(n<0 && errno==EINTR)continue;
        else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))return;
        else if(n<0 && observe_child_exit && (errno==EPIPE || errno==ECONNRESET)){
            peer_closed=true;outgoing.clear();return;
        }else throw std::runtime_error("bridge write closed errno="+std::to_string(errno));
    }}
    std::optional<std::pair<std::int32_t,Bytes>> receive(){
        std::array<std::byte,16384> buf;
        for(;!peer_closed;){const auto n=::read(fd,buf.data(),buf.size());if(n>0){incoming.insert(incoming.end(),buf.begin(),buf.begin()+n);require(incoming.size()<8*1024*1024,"bridge inbound bounded");}
            else if(n<0 && errno==EINTR)continue;else if(n==0){if(observe_child_exit){peer_closed=true;outgoing.clear();}break;}
            else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))break;
            else if(n<0 && errno==ECONNRESET && observe_child_exit){peer_closed=true;outgoing.clear();break;}
            else throw std::runtime_error("bridge read errno="+std::to_string(errno));}
        if(incoming.size()<8)return {};std::uint32_t size;std::int32_t node;
        std::memcpy(&size,incoming.data(),4);std::memcpy(&node,incoming.data()+4,4);require(size<=kMaxAuthEnvelopeBytes,"bridge received bound");
        if(incoming.size()<8+size)return {};Bytes body(incoming.begin()+8,incoming.begin()+8+size);incoming.erase(incoming.begin(),incoming.begin()+8+size);return std::pair{node,std::move(body)};
    }
};
struct ProcessClient final:ClientTransport {
    Wire wire;std::uint64_t last_attempt_offered{};bool inject_state_outage{},outage_restored{};Tick outage_started{};unsigned dropped_reads{};
    explicit ProcessClient(int fd):wire(fd){}
    bool offer_ingress(std::int32_t node,std::span<const std::byte> frame)override{
        AuthRoute route;VerifiedAuthEnvelope verified;
        require(peek_auth_route(frame,route)==AuthEnvelopeError::none &&
            verify_auth_envelope(key(caller_seed(route.sender),AuthRole::ingress,route.sender,node),frame,verified)==AuthEnvelopeError::none,
            "diagnostic decodes only authenticated fixture ingress");
        IngressView ingress;Command command;KernelCommandCodecScratch scratch;
        if(decode_ingress(verified.body,ingress)==NodeProtocolError::none &&
            decode_kernel_command(ingress.command,command,scratch).error==KernelCodecError::none){
            if(const auto* swap=std::get_if<ApplySpotSwap>(&command.body))
                if(const auto* attempt=std::get_if<swap::Attempt>(&swap->record.command))last_attempt_offered=attempt->effect.value();
        }
        GatewayRequest request;
        if(inject_state_outage && decode_gateway_request(verified.body,request)==NodeProtocolError::none &&
           request.kind==GatewayRequestKind::state_read && count("create 4")){
            const auto now=steady_ms();
            if(!outage_started){outage_started=now;record("state-outage started");}
            if(now-outage_started<3500){++dropped_reads;return true;}
            if(!outage_restored){outage_restored=true;record("state-outage restored");}
        }
        wire.send(node,frame);return true;
    }
    std::optional<std::size_t> receive_response(std::span<std::byte> out)override{
        wire.flush();auto r=wire.receive();if(!r)return {};require(r->first==0 && r->second.size()<=out.size(),"worker response framing");
        std::copy(r->second.begin(),r->second.end(),out.begin());return r->second.size();
    }
};




struct Receipt {
    test_support::RouteFixture tx;
    std::string envelope, json, signature, lookup;
    std::array<SolanaAccountKey,17> keys{};
    explicit Receipt(unsigned effect):tx(transaction(effect)),envelope(tx.envelope()){
        DecodedSolanaPayload decoded;
        require(decode_solana_signing_envelope(envelope,organization,signer_id,decoded)==SolanaParseError::none,"real Solana envelope decoded");
        std::copy(tx.statics.begin(),tx.statics.end(),keys.begin());
        for(std::size_t n=0;n<tx.writable_lookup.size();++n)keys[11+n]=tx.table_entries[tx.writable_lookup[n]];
        for(std::size_t n=0;n<tx.readonly_lookup.size();++n)keys[14+n]=tx.table_entries[tx.readonly_lookup[n]];
        std::vector<std::uint8_t> signed_tx(decoded.transaction.begin(),decoded.transaction.begin()+decoded.transaction_size);
        std::array<std::uint8_t,64> sig{};
        for(unsigned n=0;n<64;++n){sig[n]=static_cast<std::uint8_t>(effect+n+1);signed_tx[1+n]=sig[n];signed_tx[65+n]=static_cast<std::uint8_t>(n+2);}
        signature=b58(sig);
        std::vector<std::uint8_t> alt{1,0,0,0};alt.insert(alt.end(),8,255);little(alt,1,8);alt.push_back(0);alt.push_back(1);
        alt.insert(alt.end(),test_support::kLookupAuthority.begin(),test_support::kLookupAuthority.end());alt.insert(alt.end(),2,0);
        for(const auto& key:tx.table_entries)alt.insert(alt.end(),key.begin(),key.end());
        lookup="{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"context\":{\"slot\":100},\"value\":{\"data\":["+quote(base64(alt))+",\"base64\"],\"executable\":false,\"lamports\":1,\"owner\":\"AddressLookupTab1e1111111111111111111111111\",\"rentEpoch\":0,\"space\":"+std::to_string(alt.size())+"}}}";
        std::array<std::uint64_t,17> before{},after{};before.fill(1);
        before[0]=30'000'000;before[1]=tx.sell?net_buy:0;before[2]=rent;before[3]=0;before[4]=rent;
        before[11]=rent;before[12]=100'000'000+rent;before[13]=1;after=before;after[0]-=500;
        std::vector<std::uint8_t> funding;little(funding,0,4);little(funding,rent,8);little(funding,165,8);
        funding.insert(funding.end(),kSolanaClassicTokenProgram.begin(),kSolanaClassicTokenProgram.end());
        std::vector<std::uint8_t> init{18};init.insert(init.end(),keys[1].begin(),keys[1].end());
        std::vector<Ix> create{{5,{0,3},funding},{8,{3},{22}},{8,{3,14},init}},route;
        const auto pre_quote=tx.sell?24'000'000U:25'000'000U;
        const auto post_quote=tx.sell?25'000'000U:24'000'000U;
        std::uint64_t pool_quote_before=10'000'000,pool_quote_after=pool_quote_before;
        std::uint64_t pool_base_before=100'000'000,pool_base_after=pool_base_before;
        if(tx.sell){
            route={{8,{3,12,1},amount(3,net_buy)},{8,{11,2,13},amount(3,sell_gross)},{8,{2,4,1},amount(3,sell_fee)}};
            after[1]-=net_buy;after[12]+=net_buy;pool_base_after+=net_buy;pool_quote_after-=sell_gross;

        }else{
            route={{8,{2,4,1},amount(3,2'000)},{8,{2,11,1},amount(3,998'000)},{8,{12,3,13},amount(3,gross_buy)}};
            after[0]=after[0]-rent+repayment;after[1]+=net_buy;after[12]-=gross_buy;
            pool_base_after-=gross_buy;pool_quote_after+=998'000;
        }
        const auto token=[&](unsigned index,const SolanaAccountKey& owner,std::uint64_t atoms,bool native=false){
            return "{\"accountIndex\":"+std::to_string(index)+",\"mint\":"+quote(native?kSwapSolMint:kSwapUsdcMint)+
                ",\"owner\":"+quote(b58(owner))+",\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\",\"uiTokenAmount\":{\"amount\":\""+
                std::to_string(atoms)+"\",\"decimals\":"+(native?"9":"6")+"}}";
        };
        const auto tokens=[&](bool post){return token(2,keys[1],post?post_quote:pre_quote)+","+
            token(4,keys[13],post?2'000:0)+","+token(11,keys[13],post?pool_quote_after:pool_quote_before)+","+
            token(12,keys[13],post?pool_base_after:pool_base_before,true);};
        const auto numbers=[](const auto& a){std::string out;for(auto n:a){if(!out.empty())out+=',';out+=std::to_string(n);}return out;};
        const auto key_list=[&](const auto& indices){std::string out;for(auto n:indices){if(!out.empty())out+=',';out+=quote(b58(tx.table_entries[n]));}return out;};
        const auto group=[](unsigned index,const auto& rows){std::string out="{\"index\":"+std::to_string(index)+",\"instructions\":[";
            bool first=true;for(const auto& ix:rows){if(!first)out+=',';first=false;out+=inner(ix);}return out+"]}";};
        json="{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"slot\":100,\"version\":0,\"transaction\":["+quote(base64(signed_tx))+
            ",\"base64\"],\"meta\":{\"err\":null,\"fee\":500,\"preBalances\":["+numbers(before)+"],\"postBalances\":["+numbers(after)+
            "],\"loadedAddresses\":{\"writable\":["+key_list(tx.writable_lookup)+"],\"readonly\":["+key_list(tx.readonly_lookup)+
            "]},\"preTokenBalances\":["+tokens(false)+"],\"postTokenBalances\":["+tokens(true)+"],\"innerInstructions\":["+
            group(2,create)+","+group(tx.sell?5:3,route)+"]}}}";
    }
};

void verify_execute_stamp(std::string_view body,std::string_view payload){
    const auto raw=json_member_raw(body,"signatures");std::array<std::string_view,2> signatures{};
    const auto count=raw?json_array_elements(*raw,signatures):std::nullopt;
    require(count && *count==1 && signatures[0].size()>2 && signatures[0].front()=='"' && signatures[0].back()=='"',"one encoded execution stamp");
    std::string encoded(signatures[0].substr(1,signatures[0].size()-2));
    require(encoded.size()<=swap_signing::kMaxApiKeyStampBytes,"execution stamp bounded");
    std::replace(encoded.begin(),encoded.end(),'-','+');std::replace(encoded.begin(),encoded.end(),'_','/');
    const auto unpadded=encoded.size();while(encoded.size()%4)encoded+='=';
    std::array<unsigned char,512> decoded{};const auto size=EVP_DecodeBlock(decoded.data(),reinterpret_cast<const unsigned char*>(encoded.data()),static_cast<int>(encoded.size()));
    require(size>=0 && static_cast<std::size_t>(size)>=encoded.size()-unpadded,"execution stamp base64 decodes");
    const std::string_view json(reinterpret_cast<const char*>(decoded.data()),size-(encoded.size()-unpadded));
    const auto pub=json_member_string(json,"publicKey"),sig=json_member_string(json,"signature");
    require(pub && sig && json_member_string(json,"scheme")=="SIGNATURE_SCHEME_TK_API_P256","execution stamp fields pinned");
    const auto decode_hex=[](std::string_view text){require(text.size()%2==0,"hex length even");std::vector<unsigned char> bytes;
        for(std::size_t n=0;n<text.size();n+=2){unsigned value{};const auto parsed=std::from_chars(text.data()+n,text.data()+n+2,value,16);
            require(parsed.ec==std::errc{} && parsed.ptr==text.data()+n+2,"hex character valid");bytes.push_back(value);}return bytes;};
    auto pubbytes=decode_hex(*pub);const auto signature=decode_hex(*sig);const auto expected=public_key();
    require(pubbytes.size()==expected.size() && std::equal(pubbytes.begin(),pubbytes.end(),expected.begin(),[](auto a,auto b){return a==std::to_integer<unsigned char>(b);}),"execution public key exactly signer fixture");
    char group[]="prime256v1";OSSL_PARAM parameters[]={OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,group,0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,pubbytes.data(),pubbytes.size()),OSSL_PARAM_construct_end()};
    EVP_PKEY_CTX* importer=EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr);EVP_PKEY* key{};
    const bool loaded=importer && EVP_PKEY_fromdata_init(importer)==1 && EVP_PKEY_fromdata(importer,&key,EVP_PKEY_PUBLIC_KEY,parameters)==1;EVP_PKEY_CTX_free(importer);
    require(loaded && key,"public-only verification key loaded");
    EVP_MD_CTX* verifier=EVP_MD_CTX_new();const bool valid=verifier && EVP_DigestVerifyInit(verifier,nullptr,EVP_sha256(),nullptr,key)==1 &&
        EVP_DigestVerify(verifier,signature.data(),signature.size(),reinterpret_cast<const unsigned char*>(payload.data()),payload.size())==1;
    EVP_MD_CTX_free(verifier);EVP_PKEY_free(key);require(valid,"execution P256 signature verifies exact retained Solana envelope");
}

struct FakeHttps {
    SwapPrepareStore& prepared;SwapSubmissionStore& submitted;
    bool crash_after_accept{};
    transport::SwapHttpsResult exchange(const transport::SwapHttpsConfig& cfg,const transport::SwapHttpsRequest& request){
        using namespace transport;
        require(cfg.route_count==1 && cfg.routes[0].method==request.method && cfg.routes[0].path==request.path,"fake leaf exact configured route");
        require(cfg.timeout_ms>0 && cfg.timeout_ms<=60'000,"fake leaf finite timeout");
        require(cfg.origin=="https://api.truemarkets.co" || cfg.origin=="https://api.mainnet-beta.solana.com","no unreviewed origin");
        const auto effect=static_cast<unsigned>(prepared.latest().operation.effect.value());
        require(effect>=1 && effect<=6,"bounded six effect fixture");Receipt receipt(effect);
        SwapHttpsResult out;out.status=SwapHttpsStatus::complete;out.exposure=SwapHttpsExposure::response_received;out.http_status=200;
        std::string body;
        if(cfg.origin=="https://api.truemarkets.co"){
            require(request.bearer=="fixture-bearer","fake TM requires fixture-only credential");
            if(request.method==SwapHttpsMethod::get && request.path=="/v1/gateway/balances"){
                const auto row=[](std::string_view symbol,std::uint64_t value,unsigned places){const auto n=decimal(value,places);
                    return "{\"symbol\":"+quote(symbol)+",\"chain\":\"solana\",\"venue\":\"defi\",\"total\":"+quote(n)+",\"available\":"+quote(n)+",\"held\":\"0\"}";};
                body="{\"data\":["+row("SOL",effect%2?0:net_buy,9)+","+row("USDC",effect%2?25'000'000:24'000'000,6)+"]}";
            }else if(request.method==SwapHttpsMethod::post && request.path=="/v1/gateway/orders"){
                require(prepared.latest().phase==SwapPreparePhase::create_intent,"create durable fence");
                SwapRestRequest exact;require(encode_swap_rest_create({effect%2?Side::buy:Side::sell,effect%2?1'000'000:net_buy},exact)==SwapRestCode::encoded,"expected create encodes");
                require(request.body==exact.body_view(),"exact create side and atoms");
                require(count("create "+std::to_string(effect))==0,"no repeated create POST per effect");record("create "+std::to_string(effect));
                body="{\"order_id\":\"order-"+std::to_string(effect)+"\",\"status\":\"initialized\",\"payloads\":[{\"digest\":"+
                    quote(hex_digest(sha256_digest(std::as_bytes(std::span(receipt.envelope)))))+",\"payload\":"+quote(receipt.envelope)+"}],\"quote\":{\"base_asset\":"+quote(kSwapSolMint)+
                    ",\"quote_asset\":"+quote(kSwapUsdcMint)+",\"fee_asset\":"+quote(kSwapUsdcMint)+",\"qty\":"+quote(decimal(receipt.tx.input,effect%2?6:9))+
                    ",\"qty_out\":"+quote(decimal(effect%2?gross_buy:1'000'000,effect%2?9:6))+",\"fee\":\"0.002\"}}";out.http_status=201;
            }else if(request.method==SwapHttpsMethod::post && request.path=="/v1/gateway/orders/order-"+std::to_string(effect)+"/execute"){
                require(submitted.latest().phase==SwapSubmissionPhase::execute_intent && submitted.latest().identity.effect.value()==effect &&
                    sha256_digest(std::as_bytes(std::span(request.body)))==submitted.latest().identity.execution_request,"execute durable exact-byte fence");
                verify_execute_stamp(request.body,receipt.envelope);
                require(count("execute "+std::to_string(effect))==0,"no repeated execution POST per effect");record("execute "+std::to_string(effect));
                if(crash_after_accept && effect==3){::kill(::getpid(),SIGKILL);::_exit(99);}


                out.status=SwapHttpsStatus::timeout;out.exposure=SwapHttpsExposure::possibly_sent;return out;
            }else if(request.method==SwapHttpsMethod::get && request.path=="/v1/gateway/orders/order-"+std::to_string(effect)){
                require(count("execute "+std::to_string(effect))==1,"status read only after acceptance");
                body="{\"order_id\":\"order-"+std::to_string(effect)+"\",\"asset_id\":"+quote(kSwapSolAssetId)+
                    ",\"base_asset\":\"SOL\",\"quote_asset\":\"USDC\",\"asset_type\":\"spot\",\"venue\":\"defi\",\"status\":\"complete\",\"type\":\"market\",\"side\":"+
                    quote(effect%2?"buy":"sell")+",\"qty_unit\":"+quote(effect%2?"quote":"base")+",\"qty\":"+quote(decimal(receipt.tx.input,effect%2?6:9))+
                    ",\"executed_qty\":"+quote(decimal(net_buy,9))+",\"leaves_qty\":\"0\",\"executed_vwap\":\"112\",\"fee\":\"0.002\",\"tx_hash\":"+quote(receipt.signature)+"}";
            }else throw std::runtime_error("unexpected fake TM request");
        }else{
            require(request.method==SwapHttpsMethod::post && request.path=="/" && request.bearer.empty(),"exact keyless RPC boundary");
            if(request.body.find("\"method\":\"getAccountInfo\"")!=std::string_view::npos){
                require(request.body.find(quote(b58(receipt.tx.table)))!=std::string_view::npos,"exact ALT account");body=receipt.lookup;
            }else{
                require(request.body.find("\"method\":\"getTransaction\"")!=std::string_view::npos && request.body.find(quote(receipt.signature))!=std::string_view::npos &&
                    request.body.find("\"commitment\":\"finalized\"")!=std::string_view::npos,"exact finalized signature read");
                body=receipt.json;record("receipt "+std::to_string(effect));
            }
        }
        require(body.size()<=out.body.size(),"fake response bounded");out.body_size=body.size();std::copy(body.begin(),body.end(),out.body.begin());return out;
    }
};
FakeHttps* fake_https{};
}


namespace velocita::v2::transport {
SwapHttpsClient::SwapHttpsClient(SwapHttpsConfig c):config_(std::move(c)),valid_(true){}
SwapHttpsResult SwapHttpsClient::exchange(const SwapHttpsRequest& r){
    if(!valid_ || !continuous_drill::fake_https)throw std::runtime_error("fake HTTPS authority unavailable");
    return continuous_drill::fake_https->exchange(config_,r);
}
SwapHttpsFingerprint SwapHttpsClient::fingerprint(){return {"fake","fake","none","offline",false,false};}
}
namespace continuous_drill {
LinkOutcome bootstrap(GatewayLink& link){
    require(link.request_state(steady_ms(),5000),"worker independent authenticated bootstrap");
    const auto deadline=steady_ms()+10'000;LinkOutcome out;
    do{out=link.pump(steady_ms());if(out.status==LinkStatus::done)break;::usleep(500);}while(steady_ms()<deadline);
    require(out.status==LinkStatus::done,"bootstrap committed read succeeds");return out;
}
int signer_worker(unsigned epoch,int fd){
    ProcessClient transport(fd);GatewayLink link(epoch,process_incarnation(static_cast<std::uint8_t>(0xA0+epoch)),gateway_routes({1,2,3},caller_seed(kSignerKey),kSignerKey),transport);
    const auto out=bootstrap(link);const auto pub=public_key();
    const int keyfd=::open((evidence/"fixture-key.pem").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);require(keyfd>=0,"fixture signer key opened");
    auto key=SwapSignerKey::load_inherited_fd(keyfd,pub);::close(keyfd);require(bool(key),"protected signer fixture key loads");
    SwapSignerConfig cfg{opening().binding,out.reading.authority_domain,::digest(221),CallerId{kSigner},1,5000};
    const auto reviewed=review();cfg.transaction_policy_digest=swap_signer_verification_digest(reviewed);cfg.account_review_expires_at_ms=100'000;
    const auto binding=swap_signer_journal_binding(cfg,pub);SwapSubmissionStore store(evidence/"signer-result",binding);
    if(epoch==1)require(store.create({binding,out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"signer result journal initialized once");
    else require(store.recover()==SwapSubmissionCode::none,"signer existing journal reopened");
    const SwapSignerClock clock{[](void*)noexcept{return steady_ms();},[](void*)noexcept{return event_ms;},nullptr};
    SwapSignerSession session(link,store,*key,cfg,clock,reviewed);swap_signer::Server server;
    require(server.listen(evidence/"s.sock",::geteuid()),"real protected signer UDS listens");
    while(!std::filesystem::exists(evidence/("stop-signer-"+std::to_string(epoch)))){
        const auto before=key->signature_attempts();(void)server.serve_once(session,2000);
        if(key->signature_attempts()!=before){require(key->signature_attempts()==before+1,"one signing attempt per request");record("signature "+std::to_string(store.latest().identity.effect.value()));}
    }
    return 0;
}
SwapAccountStartupAdmission admission(const SwapRuntimeBinding& binding,const TradingState& current,const StateReading& reading){
    SwapAccountStartupAdmission a;a.state=current.spot_swap;
    require(swap::apply(a.funding_state,current.spot_swap.opening,swap::Role::operator_role,100)==swap::Code::applied,"funding opening reconstructed from authenticated current read");
    a.review.document_digest=::digest(210);a.review.runtime_identity=swap_runtime_identity_digest(binding);a.review.opening_digest=swap_opening_review_digest(opening());
    a.review.history_evidence=::digest(211);a.review.user_id=*tm_text("12345678-1234-1234-1234-123456789abc");
    a.review.reviewed_at_ms=1100;a.review.expires_at_ms=100'000;
    a.history.document=a.review.history_evidence;a.history.owner=sha256_digest(binding.owner_wallet);a.history.user=sha256_digest(std::as_bytes(std::span(a.review.user_id.view())));
    a.history.finished_ms=1050;a.history.started_ms=1000;a.history.quote_atoms=25'000'000;
    a.barrier={binding.allocation.incarnation,a.review.document_digest,reading.state_digest,static_cast<std::uint64_t>(reading.applied_index),event_ms,event_ms};
    require(swap_admission_funded(a,binding,event_ms),"production account admission validates managed funding");return a;
}
struct TransitionSigning final:SwapGatewaySigningPort {
    SwapDurableSigningPort& delegate;SwapSubmissionStore& requests;std::string_view crash;Tick last_sign_ms{};
    TransitionSigning(SwapDurableSigningPort& d,SwapSubmissionStore& r,std::string_view c):delegate(d),requests(r),crash(c){}
    std::optional<SwapSignedExecution> sign_or_retrieve(std::uint64_t sequence,std::int64_t index,std::uint64_t expiry)override{
        const auto started=steady_ms();auto result=delegate.sign_or_retrieve(sequence,index,expiry);last_sign_ms=steady_ms()-started;return result;
    }
    bool settled(const swap::State& state,const StateReading& reading)override{
        if(crash=="before-advance" && state.pending.phase==swap::Phase::none && state.last_effect==2){
            require(count("receipt 2")>=1 && count("execute 2")==1,"effect2 finalized receipt precedes retirement crash");record("crash before-advance 2");::kill(::getpid(),SIGKILL);::_exit(99);}
        const bool result=delegate.settled(state,reading);
        if(result)require(requests.latest().phase==SwapSubmissionPhase::resolved,"settled effect retires durable signing request before next trade");
        if(result && crash=="after-advance" && state.pending.phase==swap::Phase::none && state.last_effect==4){
            require(count("receipt 4")>=1 && count("execute 4")==1,"effect4 finalized receipt precedes retirement crash");record("crash after-advance 4");::kill(::getpid(),SIGKILL);::_exit(99);}
        return result;
    }
};
int gateway_worker(unsigned epoch,int fd,std::string_view crash){
    ProcessClient transport(fd);transport.inject_state_outage=crash=="state-outage";GatewayLink link(epoch,process_incarnation(static_cast<std::uint8_t>(0x90+epoch)),gateway_routes({1,2,3},caller_seed(kGatewayKey)),transport);
    const auto out=bootstrap(link);const auto allocation=opening().binding;
    const SwapPrepareBinding binding{allocation,out.reading.authority_domain,fee_inclusive_prepare_mapping_identity()};
    SwapPrepareStore prepared(evidence/"prepared",binding);SwapSubmissionStore submitted(evidence/"submission",{allocation.incarnation,binding.authority_domain});
    SwapResultWatermark watermark(allocation.incarnation);SwapReconcileStore recovery(evidence/"recovery",{allocation.incarnation,binding.authority_domain},watermark);
    require(prepared.recover()==SwapPrepareStoreCode::none && submitted.recover()==SwapSubmissionCode::none && recovery.recover()==SwapReconcileStoreCode::none,"gateway journals recovered without reset");
    SwapSigningConnectionConfig signcfg{binding,public_key()};SwapSubmissionStore requests(evidence/"sign-requests",swap_signing_connection_binding(signcfg));
    if(epoch==1)require(requests.create({swap_signing_connection_binding(signcfg),out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"request journal initialized once");
    else require(requests.recover()==SwapSubmissionCode::none,"request journal reopened without reset");
    SwapRuntimeBinding runtime_binding;runtime_binding.allocation=allocation;runtime_binding.authority_domain=binding.authority_domain;
    runtime_binding.account_session={::digest(201),::digest(202)};runtime_binding.signer_public_key=public_key();
    const auto owner=review().policy.owner;std::transform(owner.begin(),owner.end(),runtime_binding.owner_wallet.begin(),[](auto c){return std::byte{c};});
    const auto socket=(evidence/"s.sock").string();require(socket.size()<runtime_binding.socket.size(),"fixture socket length bounded");
    runtime_binding.socket_size=socket.size();std::copy(socket.begin(),socket.end(),runtime_binding.socket.begin());
    runtime_binding.gateway_uid=::geteuid();runtime_binding.signer_uid=::geteuid()+1;
    runtime_binding.gateway_caller=CallerId{kGateway};runtime_binding.signer_caller=CallerId{kSigner};runtime_binding.ipc_timeout_ms=10'000;
    runtime_binding.max_buy_output_deduction_lamports=repayment;runtime_binding.transaction_review_digest=swap_signer_verification_digest(review());
    constexpr std::string_view token="fixture-bearer";
    SwapGatewayStartupConfig credentials_config{runtime_binding,sha256_digest(std::as_bytes(std::span(token))),100'000};
    SwapRuntimeCode code;const int tokenfd=::open((evidence/"fixture-bearer").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    auto credentials=SwapGatewayCredentials::load(credentials_config,SwapInheritedSecretFd(tokenfd),event_ms,code);
    require(bool(credentials),"production credential descriptor load code="+std::to_string(int(code)));
    Clock clock;auto admit=admission(runtime_binding,link.state(),out.reading);
    admit.recovery_only=swap_startup_recovery_only(link.state().spot_swap,prepared.latest().phase,prepared.latest().operation.effect,submitted.latest().phase);
    record(std::string("startup ")+(admit.recovery_only?"recovery-only ":"normal ")+std::to_string(epoch));
    FakeHttps fake{prepared,submitted,crash=="accepted"};fake_https=&fake;
    SwapProductionPorts ports(*credentials,prepared,submitted,binding,clock,*tm_text(organization),*tm_text(signer_id),5000,admit);
    const auto until=steady_ms()+5000;while(!std::filesystem::exists(evidence/"s.sock") && steady_ms()<until)::usleep(500);
    swap_signer::Client client(evidence/"s.sock",::geteuid(),public_key(),10'000);
    SwapDurableSigningPort signing(prepared,requests,client,signcfg,ports);TransitionSigning guarded(signing,requests,crash);
    SwapGatewayRuntimeConfig cfg{binding,CallerId{kGateway},epoch*1000U,epoch*1000U+900U,3000,250};
    cfg.continuous=true;cfg.recovery_only=admit.recovery_only;cfg.read_outage=120'000;
    SwapGatewayRuntime runtime(link,recovery,prepared,submitted,ports,guarded,ports,ports,cfg);
    unsigned announced{};Tick max_step_ms{};const auto deadline=steady_ms()+60'000;
    while(!runtime.stopped() && steady_ms()<deadline){const auto started=steady_ms();runtime.step(started,event_ms);max_step_ms=std::max(max_step_ms,steady_ms()-started);
        if(runtime.observation()){
            const auto& state=runtime.observation()->state;
            if(state.pending.phase==swap::Phase::none && state.last_effect>announced && submitted.latest().phase==SwapSubmissionPhase::resolved && requests.latest().phase==SwapSubmissionPhase::resolved && runtime.status()==SwapGatewayRuntimeStatus::running){
                announced=static_cast<unsigned>(state.last_effect);record("ready "+std::to_string(announced));
                if(announced==6){require(!transport.inject_state_outage || (transport.dropped_reads && transport.outage_restored),"controlled state outage was exercised and restored");require(runtime.status()==SwapGatewayRuntimeStatus::running,"continuous runtime remains operational after six effects");record("timing max_step_ms="+std::to_string(max_step_ms)+" last_sign_ms="+std::to_string(guarded.last_sign_ms)+" last_attempt_offered="+std::to_string(transport.last_attempt_offered));return 0;}
            }
        }
        ::usleep(500);
    }
    if(admit.recovery_only){require(runtime.status()==SwapGatewayRuntimeStatus::complete,"recovery-only completes reconciliation without new submission");record("recovery-complete");return 0;}
    if(transport.inject_state_outage)require(!runtime.stopped(),"continuous runtime survives injected regular-state outage status="+std::to_string(int(runtime.status()))+" cause="+std::to_string(int(runtime.cause())));
    throw std::runtime_error("gateway continuous lifecycle stopped status="+std::to_string(int(runtime.status()))+" cause="+std::to_string(int(runtime.cause()))+" trace="+std::to_string(int(ports.create_trace().stage))+" last_sign_ms="+std::to_string(guarded.last_sign_ms)+" max_step_ms="+std::to_string(max_step_ms)+" last_attempt_offered="+std::to_string(transport.last_attempt_offered));
}
struct ChildGuard{pid_t pid;~ChildGuard(){if(pid>0){::kill(pid,SIGKILL);::waitpid(pid,nullptr,0);}}};
int run_workers(Cluster& cluster,Caller& strategy,const std::filesystem::path& exe,unsigned epoch,std::string_view crash,unsigned& next){
    int signer_pair[2],gateway_pair[2];require(::socketpair(AF_UNIX,SOCK_STREAM,0,signer_pair)==0 && ::socketpair(AF_UNIX,SOCK_STREAM,0,gateway_pair)==0,"worker frame bridges");
    const auto epoch_text=std::to_string(epoch),sfd=std::to_string(signer_pair[1]),gfd=std::to_string(gateway_pair[1]);
    const auto signer_pid=::fork();require(signer_pid>=0,"signer fork");
    if(signer_pid==0){::close(signer_pair[0]);::close(gateway_pair[0]);::close(gateway_pair[1]);::execl(exe.c_str(),exe.c_str(),"--signer",evidence.c_str(),epoch_text.c_str(),sfd.c_str(),nullptr);::_exit(126);}
    ::close(signer_pair[1]);ChildGuard signer_guard{signer_pid};Wire signer_wire(signer_pair[0],true);
    const auto gateway_pid=::fork();require(gateway_pid>=0,"gateway fork");
    if(gateway_pid==0){::close(gateway_pair[0]);::close(signer_pair[0]);::execl(exe.c_str(),exe.c_str(),"--gateway",evidence.c_str(),epoch_text.c_str(),gfd.c_str(),std::string(crash).c_str(),nullptr);::_exit(126);}
    ::close(gateway_pair[1]);ChildGuard gateway_guard{gateway_pid};Wire gateway_wire(gateway_pair[0],true);
    const auto pump=[&](Wire& wire,std::int32_t key){
        while(auto packet=wire.receive()){require(cluster.nodes.contains(packet->first),"configured live replica bridge");cluster.network.at(packet->first).ingress_inbox.push_back(std::move(packet->second));}
        auto& responses=cluster.network.responses[key];while(!responses.empty()){wire.send(0,responses.front());responses.pop_front();}wire.flush();
    };
    int status{};const auto deadline=steady_ms()+70'000;
    for(;;){const auto reaped=::waitpid(gateway_pid,&status,WNOHANG);if(reaped==gateway_pid){gateway_guard.pid=0;break;}
        require(reaped==0 && steady_ms()<deadline,"bounded gateway process lifetime");
        pump(gateway_wire,kGatewayKey);pump(signer_wire,kSignerKey);cluster.run(1);
        if(next<=6 && count("ready "+std::to_string(next-1))){
            const auto& state=cluster.state_of(cluster.elect()).spot_swap;
            require(state.pending.phase==swap::Phase::none && state.last_effect==next-1,"next effect only after previous Raft settlement");
            require(cluster.commit(strategy,ApplySpotSwap{{swap::Role::strategy,event_ms,intent(next)}}).code==ApplyCode::applied,"next alternating strategy intent commits");++next;
        }
    }
    {std::ofstream stop(evidence/("stop-signer-"+std::to_string(epoch)));stop<<"stop";}
    int signer_status{};const auto finish=steady_ms()+10'000;
    while(::waitpid(signer_pid,&signer_status,WNOHANG)==0){require(steady_ms()<finish,"bounded signer shutdown");pump(signer_wire,kSignerKey);cluster.run(1);}
    signer_guard.pid=0;::close(signer_pair[0]);::close(gateway_pair[0]);cluster.network.responses[kGatewayKey].clear();cluster.network.responses[kSignerKey].clear();
    require(WIFEXITED(signer_status) && WEXITSTATUS(signer_status)==0,"independent signer stopped cleanly");
    record("gateway-exit "+std::to_string(epoch)+" signal="+std::to_string(WIFSIGNALED(status)?WTERMSIG(status):0)+" code="+std::to_string(WIFEXITED(status)?WEXITSTATUS(status):-1));
    return status;
}
void fixture_oracles(){
    for(unsigned n:{1U,2U}){
        Receipt receipt(n);const auto checked=receipt.tx.run(event_ms);
        require(bool(checked.verified),"real route verifier accepts fixture code="+std::to_string(int(checked.code)));
        settlement_lifecycle_fixture::Clock clock;clock.now=event_ms;settlement_lifecycle_fixture::Port port;port.clock=&clock;port.body(receipt.json);
        SwapFinalizedReader reader(port,clock,port.identity,5000);const auto sig=decode_solana_signature(receipt.signature);require(sig.has_value(),"fixture Solana signature decodes");
        const auto read=reader.read(*sig,1,100);require(read.evidence.has_value(),"finalized RPC receipt attributed");
        const auto lookup=jupiter_lookup_evidence(receipt.tx.prefix());
        const auto replay=replay_swap_settlement_lifecycle(*read.evidence,receipt.envelope,organization,signer_id,std::span(&lookup,1),receipt.tx.policy.owner);
        require(replay.code==SwapLifecycleCode::replayed_effects_only,"real receipt lifecycle oracle code="+std::to_string(int(replay.code)));
        require(replay.owner_wallet_lamports==(n==1?static_cast<std::int64_t>(net_buy):-static_cast<std::int64_t>(net_buy)) &&
            replay.owner_usdc_atoms==(n==1?-1'000'000:1'000'000) && replay.fee_atoms==2'000 && replay.fee_attribution==SwapFeeAttributionCode::observed_abi_bound_usdc_fee,
            "independent exact receipt economics oracle");
    }
}
void scenario(const std::filesystem::path& exe,unsigned mode){
    std::filesystem::create_directories(evidence);std::filesystem::permissions(evidence,std::filesystem::perms::owner_all);
    {swap_signer_test::TestKey key(evidence/"fixture-key.pem");std::ofstream f(evidence/"public-key",std::ios::binary);f.write(reinterpret_cast<const char*>(key.public_key.data()),key.public_key.size());}
    {const int fd=::open((evidence/"fixture-bearer").c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);constexpr char token[]="fixture-bearer";
        require(fd>=0 && ::write(fd,token,sizeof(token)-1)==sizeof(token)-1,"fixture-only bearer created");::close(fd);}
    Cluster c(evidence/"voters",{1,2,3});Caller op(kOperatorKey,kOperator),strategy(kStrategyKey,kStrategy);c.elect();
    require(c.commit(op,AdvanceKillEpoch{KillEpoch{1}},ProposeLane::control).code==ApplyCode::applied,"kill epoch initialized");
    require(c.commit(op,SetExecutorGeneration{ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"executor initialized");
    require(c.commit(op,InstallExecutionCatalog{fixture_catalog().root(),KillEpoch{1},ExecutorGeneration{1}}).code==ApplyCode::applied,"real SOL catalog installed");
    require(c.commit(op,AssignCallerRole{CallerId{kGateway},CallerRole::gateway}).code==ApplyCode::applied,"gateway caller role installed");
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,100,opening()}}).code==ApplyCode::applied,"25 dollar virtual funding committed");
    require(c.commit(op,AssignCallerRole{CallerId{kStrategy},CallerRole::strategy}).code==ApplyCode::applied,"strategy caller role installed");
    const swap::GrantStrategy grant{swap::opening_digest(opening()),swap_test::digest(97),CallerId{kStrategy},RequestSequence{1},1,1};
    require(c.commit(op,ApplySpotSwap{{swap::Role::operator_role,101,grant}}).code==ApplyCode::applied,"operator grants only strategy intent authority");
    require(c.commit(op,SetStrategyMode{StrategyMode::normal,KillEpoch{1},ExecutorGeneration{1}},ProposeLane::control).code==ApplyCode::applied,"normal mode committed");
    {
        GatewayClient wire;wire.network=&c.network;GatewayLink link(1,process_incarnation(0x80),gateway_routes(c.voters,caller_seed(kGatewayKey)),wire);
        require(link.request_state(steady_ms(),5000),"initial authenticated journal bootstrap requested");const auto out=settle(c,link);require(out.status==LinkStatus::done,"initial authenticated journal bootstrap read");
        const auto allocation=opening().binding;const SwapPrepareBinding binding{allocation,out.reading.authority_domain,fee_inclusive_prepare_mapping_identity()};
        SwapPrepareStore prepared(evidence/"prepared",binding);SwapSubmissionStore submitted(evidence/"submission",{allocation.incarnation,binding.authority_domain});
        SwapResultWatermark watermark(allocation.incarnation);SwapReconcileStore recovery(evidence/"recovery",{allocation.incarnation,binding.authority_domain},watermark);
        require(prepared.create({binding,out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapPrepareStoreCode::none,"preparation initialized once");
        require(submitted.create({{allocation.incarnation,binding.authority_domain},out.reading.applied_index,out.reading.state_digest,EffectId{0}})==SwapSubmissionCode::none,"submission initialized once");
        require(recovery.create({{allocation.incarnation,binding.authority_domain},out.reading.applied_index,out.reading.state_digest})==SwapReconcileStoreCode::none,"reconciliation initialized once");
    }
    require(c.commit(strategy,ApplySpotSwap{{swap::Role::strategy,event_ms,intent(1)}}).code==ApplyCode::applied,"first buy reserved");
    unsigned next=2,epoch=1;
    const auto success=[](int s){require(WIFEXITED(s) && WEXITSTATUS(s)==0,"worker exits successfully");};
    const auto killed=[](int s){require(WIFSIGNALED(s) && WTERMSIG(s)==SIGKILL,"actual gateway SIGKILL observed");};
    if(mode==1){killed(run_workers(c,strategy,exe,epoch++,"before-advance",next));killed(run_workers(c,strategy,exe,epoch++,"after-advance",next));}
    if(mode==2){killed(run_workers(c,strategy,exe,epoch++,"accepted",next));
        const auto& s=c.state_of(c.elect()).spot_swap;
        require(s.settlement_count==2 && s.reserved_quote.value()==1'000'000 && count("execute ")==3,"accepted unconfirmed buy keeps reservation");
        success(run_workers(c,strategy,exe,epoch++,"none",next));
        require(count("recovery-complete")==1 && count("execute ")==3,"recovery-only resolves without another POST");
    }
    success(run_workers(c,strategy,exe,epoch++,mode==0?"state-outage":"none",next));
    c.run_until([&]{return std::all_of(c.nodes.begin(),c.nodes.end(),[](auto& node){return node.second->applied_state().spot_swap.settlement_count==6;});},"six settlements replicated on all voters");
    for(auto& [id,node]:c.nodes){const auto& s=node->applied_state().spot_swap;
        require(s.quote.value()==25'000'000 && s.base.value()==0 && s.cost.value()==0 && s.reserved_quote.value()==0 && s.reserved_base.value()==0 &&
            s.pending.phase==swap::Phase::none && s.settlement_count==6 && s.fees.value()==12'000 && s.purchases.value()==3'000'000,"all replicas exact six-effect economics and flat inventory");}
    require(count("create ")==6 && count("signature ")==6 && count("execute ")==6,"six unique creates signatures and sends despite lost responses and process restarts");
    require(mode!=0 || (count("state-outage started")==1 && count("state-outage restored")==1),"exactly one controlled read outage exercised");
    std::ofstream result(evidence/"result.json");result<<"{\"case\":"<<mode<<",\"passed\":true,\"production_ports\":true,\"strategy_caller\":21,\"operator_granted_intents\":true,\"real_transaction_verifier\":true,\"real_finalized_receipt_parser\":true,\"synthetic_verified_settlement_factory\":false,\"tls_qualified\":false,\"production_uid_isolation_qualified\":false,\"voters\":3,\"settlements\":6,\"creates\":6,\"signatures\":6,\"executes\":6,\"cash_atoms\":25000000,\"base_atoms\":0,\"fees_atoms\":12000,\"purchase_atoms\":3000000,\"gateway_processes\":"<<epoch-1<<"}\n";
    std::cout<<"PASS continuous case "<<mode<<": six real-format verified swaps, six sends, flat inventory\n";
}
}
int main(int argc,char** argv){try{
    ::signal(SIGPIPE,SIG_IGN);g_swap=true;
    if(argc==5 && std::string_view(argv[1])=="--signer"){continuous_drill::evidence=argv[2];return continuous_drill::signer_worker(std::stoul(argv[3]),std::stoi(argv[4]));}
    if(argc==6 && std::string_view(argv[1])=="--gateway"){continuous_drill::evidence=argv[2];return continuous_drill::gateway_worker(std::stoul(argv[3]),std::stoi(argv[4]),argv[5]);}
    continuous_drill::fixture_oracles();
    const auto root=argc==4 && std::string_view(argv[1])=="--case"?std::filesystem::path(argv[3]):
        std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build"/("continuous-"+std::to_string(::getpid()));
    require(!std::filesystem::exists(root),"refuse existing continuous-drill evidence");
    const auto exe=std::filesystem::canonical(argv[0]);
    for(unsigned mode=0;mode<3;++mode){if(argc==4 && mode!=std::stoul(argv[2]))continue;
        continuous_drill::evidence=root/("c"+std::to_string(mode));continuous_drill::scenario(exe,mode);}
    std::cout<<"Evidence: "<<root<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL continuous drill: "<<error.what()<<'\n';return 1;}}
