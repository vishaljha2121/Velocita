#include "velocita/v2/swap_settlement_evidence.hpp"
#include "velocita/v2/digest.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
#include "velocita/v2/swap_settlement_connection.hpp"
#include <filesystem>
#include <functional>
#include <cstdlib>
#include <unistd.h>
#endif
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace velocita::v2;
namespace {
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
truemarkets::SolanaAccountKey key(unsigned n){truemarkets::SolanaAccountKey k{};k[31]=static_cast<std::uint8_t>(n);return k;}
std::string address(unsigned n){return std::string(31,'1')+"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"[n];}
Digest256 digest(unsigned n){Digest256 d;d.bytes[0]=static_cast<std::byte>(n);return d;}
std::string base64(const std::vector<std::uint8_t>& data){
    constexpr char digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string s;
    for(std::size_t i=0;i<data.size();i+=3){const unsigned n=(unsigned(data[i])<<16)|(i+1<data.size()?unsigned(data[i+1])<<8:0)|(i+2<data.size()?data[i+2]:0);
        s+=digits[n>>18];s+=digits[(n>>12)&63];s+=i+1<data.size()?digits[(n>>6)&63]:'=';s+=i+2<data.size()?digits[n&63]:'=';}
    return s;
}
std::string wrap(const std::vector<std::uint8_t>& b){std::string h;for(auto c:b){h+="0123456789abcdef"[c>>4];h+="0123456789abcdef"[c&15];}
    return "{\"organizationId\":\"org\",\"parameters\":{\"signWith\":\""+address(2)+"\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\""+h+"\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";}
std::string token(unsigned index,unsigned owner,std::uint64_t amount){return "{\"accountIndex\":"+std::to_string(index)+",\"mint\":\""+std::string(truemarkets::kSwapUsdcMint)+"\",\"owner\":\""+address(owner)+"\",\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\",\"uiTokenAmount\":{\"amount\":\""+std::to_string(amount)+"\",\"decimals\":6}}";}
void replace(std::string& s,std::string_view a,std::string_view b){auto p=s.find(a);require(p!=std::string::npos,"fixture replacement exists");s.replace(p,a.size(),b);}
struct Clock:SwapFinalizedReadClock{std::uint64_t now{10};std::uint64_t now_ms()noexcept override{return now;}};
struct Port:SwapFinalizedRpcPort{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
    std::function<void()> during;
#endif
Digest256 identity{digest(1)};SettlementRpcResponse reply{};std::string requested;std::uint64_t requested_timeout{};unsigned calls{};Clock* clock{};bool change_identity{},late{},backward{},throws{};
    Digest256 provider_identity()const noexcept override{return identity;}
    SettlementRpcResponse request(std::string_view b,std::uint64_t timeout_ms)override{
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
        if(during)during();
#endif
        ++calls;requested=b;requested_timeout=timeout_ms;if(change_identity)identity=digest(2);if(late)clock->now=30;if(backward)clock->now=9;if(throws)throw std::runtime_error("lost");return reply;}
    void body(const std::string& s){reply={};reply.transport_complete=true;reply.http_status=200;reply.size=s.size();std::copy(s.begin(),s.end(),reply.body.begin());}
};
struct Fixture {
    Clock clock;Port port;std::vector<std::uint8_t> unsigned_tx,signed_tx;SwapSubmissionTransaction signature{};std::string original,json;
    Fixture(){port.clock=&clock;unsigned_tx={2};unsigned_tx.resize(129,0);unsigned_tx.insert(unsigned_tx.end(),{128,2,0,1,5});
        for(unsigned i=1;i<=5;++i){unsigned_tx.insert(unsigned_tx.end(),31,0);unsigned_tx.push_back(i);}
        unsigned_tx.insert(unsigned_tx.end(),32,3);unsigned_tx.insert(unsigned_tx.end(),{1,4,1,1,1,42,0});
        signed_tx=unsigned_tx;for(unsigned i=0;i<64;++i){signature[i]=static_cast<std::byte>(i+1);signed_tx[i+1]=i+1;signed_tx[65+i]=i+2;}original=wrap(unsigned_tx);rebuild();}
    void rebuild(){json="{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"slot\":100,\"version\":0,\"transaction\":[\""+base64(signed_tx)+"\",\"base64\"],\"meta\":{\"err\":null,\"fee\":500,\"preBalances\":[30000000,10000000,2039280,2039280,1],\"postBalances\":[20999500,19000000,2039280,2039280,1],\"loadedAddresses\":{\"writable\":[],\"readonly\":[]},\"preTokenBalances\":["+token(2,2,2000000)+","+token(3,9,1000000)+"],\"postTokenBalances\":["+token(2,2,998000)+","+token(3,9,2002000)+"]}}}";port.body(json);}
    FinalizedReadResult read(){SwapFinalizedReader reader(port,clock,digest(1),20);return reader.read(signature,7,100);}
    SettlementEvidence decode(){port.body(json);const auto r=read();require(r.evidence.has_value(),"fixture receives attributed response");return decode_settlement_evidence(*r.evidence,original,"org",address(2),{},key(2));}
};
void finalized_request_and_ownership(){Fixture f;auto r=f.read();require(r.code==FinalizedReadCode::attributed_finalized_response && r.evidence.has_value(),"finalized reader owns exact attributed response");
    require(f.port.requested.find("2Ana1pUpv2ZbMVkwF5FXapYeBEjdxDatLn7nvJkhgTSXbs59SyZSx866bXirPgj8QQVB57uxHJBG1YFvkRbFj4T")!=std::string::npos,"independent base58 signature request oracle");
    require(f.port.calls==1 && f.port.requested_timeout==20 && f.port.requested.find("\"commitment\":\"finalized\"")!=std::string::npos && f.port.requested.find("\"encoding\":\"base64\"")!=std::string::npos && f.port.requested.find("\"id\":7")!=std::string::npos,"reader constructs pinned finalized request");
    const auto saved=std::string(r.evidence->body());f.port.reply={};require(r.evidence->body()==saved && r.evidence->slot()==100 && r.evidence->signature()==f.signature,"returned evidence owns bytes and identities");}
void read_identity_and_time(){for(unsigned n=0;n<7;++n){Fixture f;if(n==0)f.port.identity=digest(2);if(n==1)f.port.change_identity=true;if(n==2)f.port.late=true;if(n==3)f.port.backward=true;if(n==4)f.port.throws=true;if(n==5)f.port.reply.transport_complete=false;if(n==6)f.port.reply.http_status=503;auto r=f.read();require(!r.evidence,"unattributed late or failed read grants no evidence");require(f.port.calls<2,"reader never retries");}}
void rpc_shape_and_watermark(){for(auto pair:std::vector<std::pair<std::string,std::string>>{{"\"id\":7","\"id\":8"},{"\"slot\":100","\"slot\":99"},{"\"jsonrpc\":\"2.0\"","\"jsonrpc\":\"1.0\""},{"\"id\":7","\"id\":7,\"id\":7"}}){Fixture f;replace(f.json,pair.first,pair.second);f.port.body(f.json);require(!f.read().evidence,"wrong RPC identity duplicate or old slot grants no evidence");}Fixture f;f.port.body("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":null}");require(f.read().code==FinalizedReadCode::not_found,"null result remains unknown");}
void exact_transaction_and_conservation(){Fixture f;const auto e=f.decode();require(e.code==SettlementEvidenceCode::observed_transaction_needs_account_lifecycle && e.account_count==5 && e.owner_index==1 && e.network_fee==500 && e.accounts[1].after_lamports-e.accounts[1].before_lamports==9000000 && e.accounts[2].before_token.atoms-e.accounts[2].after_token.atoms==1002000,"hand computed owner debit receipt and network fee preserved");
    for(unsigned n=0;n<5;++n){Fixture g;if(n==0){g.signed_tx.back()=1;g.rebuild();}if(n==1){g.signed_tx[1]^=1;g.rebuild();}if(n==2)replace(g.json,"20999500","20999501");if(n==3)replace(g.json,"\"amount\":\"2002000\"","\"amount\":\"2002001\"");if(n==4)replace(g.json,"\"err\":null","\"err\":{\"InstructionError\":[0,1]}");require(g.decode().code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle,"changed message signature failed execution or conservation refused");}}
void token_attribution_and_coverage(){for(unsigned n=0;n<5;++n){Fixture f;if(n==0)replace(f.json,"\"accountIndex\":2","\"accountIndex\":9");if(n==1)replace(f.json,"\"decimals\":6","\"decimals\":9");if(n==2)replace(f.json,"\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\"","\"programId\":\"11111111111111111111111111111111\"");if(n==3)replace(f.json,"\"preTokenBalances\":[","\"preTokenBalances\":["+token(2,2,2000000)+",");if(n==4)replace(f.json,"\"writable\":[]","\"writable\":[\"11111111111111111111111111111111\"]");require(f.decode().code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle,"token index decimals program duplicates and loaded coverage refused");}}
void property_corroboration(){Fixture f;const auto e=f.decode();std::array<DeclaredOwnedAccount,2> scope{{{key(2),OwnedAccountKind::native_wallet},{key(3),OwnedAccountKind::usdc_token}}};
    std::array<OwnerAccountChange,2> properties{{{key(2),{true,10000000,0,0},{true,19000000,0,0}},{key(3),{true,2039280,2000000,0,DepositControl::owner},{true,2039280,998000,0,DepositControl::owner}}}};
    auto r=corroborate_settlement_properties(e,scope,properties);require(r.code==SettlementPropertyCode::corroborated_declarations_only && r.measurement.sol_change==9000000 && r.measurement.usdc_change==-1002000,"corroborated owner property remains declaration only");
    properties[1].after.token_atoms++;require(corroborate_settlement_properties(e,scope,properties).code==SettlementPropertyCode::property_mismatch,"declared amounts must equal transaction evidence");properties[1].after.token_atoms--;
    require(corroborate_settlement_properties(e,std::span(scope).first(1),std::span(properties).first(1)).code==SettlementPropertyCode::scope_mismatch,"omitted owner token account refused");
    properties[1].after.deposit_control=DepositControl::unverified;require(corroborate_settlement_properties(e,scope,properties).code==SettlementPropertyCode::property_mismatch,"unverified close control cannot measure owner property");}

void rpc_capacity_and_base64(){
    for(unsigned n=0;n<5;++n){Fixture f;if(n==0)f.port.reply.size=kSettlementRpcBytes+1;if(n==1)f.port.body("{\"jsonrpc\":\"2.0\",\"id\":7,\"error\":null,\"result\":{\"slot\":100}}");if(n==2)f.signature={};if(n==3)f.clock.now=UINT64_MAX;if(n==4)f.port.body("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"slot\":100.0}}");require(!f.read().evidence,"capacity zero signature clock overflow RPC error and fractional slot refuse");}
    for(unsigned n=0;n<5;++n){Fixture f;if(n==0)replace(f.json,"\"version\":0","\"version\":1");if(n==1)replace(f.json,"\"base64\"","\"base58\"");if(n==2){auto p=f.json.find("\"transaction\":[\"")+16;f.json[p]='!';}if(n==3){std::fill(f.signed_tx.begin()+65,f.signed_tx.begin()+129,0);f.rebuild();}if(n==4)replace(f.json,"\"preBalances\":[30000000","\"preBalances\":[9223372036854775808");require(f.decode().code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle,"unknown version encoding malformed bytes unsigned signer and overflow refuse");}
}
void rent_oracle_and_owner_coverage(){
    Fixture f;replace(f.json,"2039280,2039280,1],\"loadedAddresses", "1488440,2039280,1],\"loadedAddresses");replace(f.json,"20999500","21550340");const auto evidence=f.decode();
    require(evidence.code==SettlementEvidenceCode::observed_transaction_needs_account_lifecycle,"changed rent amount is explicit account evidence");
    std::array<DeclaredOwnedAccount,2> scope{{{key(2),OwnedAccountKind::native_wallet},{key(3),OwnedAccountKind::usdc_token}}};
    std::array<OwnerAccountChange,2> accounts{{{key(2),{true,10000000,0,0},{true,19000000,0,0}},{key(3),{true,2039280,2000000,0,DepositControl::owner},{true,1488440,998000,0,DepositControl::external}}}};
    const auto result=corroborate_settlement_properties(evidence,scope,accounts);
    require(result.code==SettlementPropertyCode::corroborated_declarations_only && result.measurement.sol_change==6960720 && result.measurement.deposit_change==-2039280 && result.measurement.base_change_excluding_deposits==9000000,"lost owner rent is included and sponsor rent is excluded");
    accounts[0].after.lamports++;require(corroborate_settlement_properties(evidence,scope,accounts).code==SettlementPropertyCode::property_mismatch,"native wallet declaration must match exact chain delta");
    Fixture g;replace(g.json,"\"owner\":\""+address(2)+"\"","\"owner\":\""+address(8)+"\"");const auto changed=g.decode();accounts[0].after.lamports--;
    require(corroborate_settlement_properties(changed,scope,accounts).code==SettlementPropertyCode::property_mismatch,"token ownership transition cannot be hidden by declarations");
}

#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
void retained_connection(){
    for(unsigned mode=0;mode<9;++mode){
        Fixture f;
        const auto root=std::filesystem::path(std::getenv("HOME"))/".cache/velocita-build/settlement-connection";
        std::filesystem::create_directories(root);auto directory=(root/"fixture-XXXXXX").string();require(::mkdtemp(directory.data()),"settlement scratch");
        struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{directory};
        const SwapPrepareBinding binding{{digest(1),digest(2),digest(3),AccountId{2},InstrumentId{2},PolicyId{901},PolicyVersion{1},RiskPolicyVersion{1}},digest(4),digest(5)};
        SwapPrepareStore preparation(std::filesystem::path(directory)/"prepare",binding);
        SwapSubmissionStore submission(std::filesystem::path(directory)/"submit",{binding.allocation.incarnation,binding.authority_domain});
        require(preparation.create({binding,1,digest(6),EffectId{0}})==SwapPrepareStoreCode::none,"retained preparation bootstrap");
        truemarkets::SwapRestRequest request;require(truemarkets::encode_swap_rest_create({Side::buy,1'000'000},request)==truemarkets::SwapRestCode::encoded,"retained create");
        require(preparation.begin({EffectId{1},digest(7),sha256_digest(std::as_bytes(std::span(request.body.data(),request.body_size))),{Side::buy,1'000'000}},2,digest(8))==SwapPrepareStoreCode::none,"retained request");
        truemarkets::SwapRestPrepared p;p.handle=*truemarkets::tm_text("order-1");p.payload_size=f.original.size();
        std::copy(f.original.begin(),f.original.end(),p.payload.begin());p.payload_digest=sha256_digest(std::as_bytes(std::span(f.original.data(),f.original.size())));
        p.quoted_input=1'000'000;p.quoted_output=9'000'000;p.quoted_fee=2'000;
        require(preparation.complete(p,digest(9))==SwapPrepareStoreCode::none,"original payload retained");
        require(submission.create({{binding.allocation.incarnation,binding.authority_domain},1,digest(10),EffectId{0}})==SwapSubmissionCode::none,"retained submission bootstrap");
        SwapSubmissionIdentity identity;identity.effect=EffectId{mode==8?2U:1U};identity.payload=mode==1?digest(99):p.payload_digest;identity.execution_request=digest(11);
        identity.handle_size=p.handle.length;std::copy_n(reinterpret_cast<const std::byte*>(p.handle.bytes.data()),p.handle.length,identity.handle.begin());
        if(mode==6)identity.handle[0]=std::byte{'x'};
        identity.handle_digest=sha256_digest(std::span(identity.handle).first(identity.handle_size));
        require(submission.begin(identity,mode==7?2:3)==SwapSubmissionCode::none,"retained execute intent");
        if(mode!=2)require(submission.observe_transaction(identity,f.signature)==SwapSubmissionCode::none,"retained authenticated transaction identity");
        if(mode==3){replace(f.json,"\"err\":null","\"err\":{\"InstructionError\":[0,1]}");f.port.body(f.json);}
        const auto before=submission.latest().checksum;
        if(mode==4)f.port.during=[&]{require(submission.resolve({identity,4,digest(88)})==SwapSubmissionCode::none,"fixture advances journal during RPC");};
        auto expected=binding;if(mode==5)expected.allocation.account=AccountId{3};
        SwapFinalizedReader reader(f.port,f.clock,digest(1),20);
        const auto result=collect_retained_swap_settlement(preparation,submission,expected,reader,7,100,"org",address(2),{},key(2));
        if(mode==0)require(result.code==SwapSettlementConnectionCode::observed_needs_account_lifecycle && result.evidence.signature==f.signature &&
            result.evidence.original_payload_digest==p.payload_digest && result.evidence.accounts[1].after_lamports==19'000'000,
            "retained exact transaction connects to finalized evidence without inventing lifecycle proof");
        if(mode==1 || mode==2 || mode==5 || mode==6 || mode==7 || mode==8)require(result.code==SwapSettlementConnectionCode::identity_refused && !f.port.calls,"wrong retained identity or missing transaction never reaches RPC");
        if(mode==3)require(result.code==SwapSettlementConnectionCode::evidence_refused,"failed finalized transaction never becomes successful settlement");
        if(mode==4)require(result.code==SwapSettlementConnectionCode::identity_refused && f.port.calls==1,
            "journal changing during RPC invalidates evidence handoff");
        else require(submission.latest().checksum==before && submission.latest().phase==SwapSubmissionPhase::execute_intent,
            "finalized observation alone never releases funds or resolves journal");
    }
}
#endif

}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> cases[]={
#ifdef VELOCITA_SWAP_SETTLEMENT_CONNECTION_TEST_ONLY
{"retained_connection",retained_connection},
#endif
{"finalized_request_and_ownership",finalized_request_and_ownership},{"read_identity_and_time",read_identity_and_time},{"rpc_shape_and_watermark",rpc_shape_and_watermark},{"exact_transaction_and_conservation",exact_transaction_and_conservation},{"token_attribution_and_coverage",token_attribution_and_coverage},{"property_corroboration",property_corroboration},{"rpc_capacity_and_base64",rpc_capacity_and_base64},{"rent_oracle_and_owner_coverage",rent_oracle_and_owner_coverage}};for(auto [name,fn]:cases){if(argc==2 && std::string_view(argv[1])!=name)continue;try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}}
