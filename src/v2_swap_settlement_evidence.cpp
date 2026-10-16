#include "velocita/v2/swap_settlement_evidence.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/digest.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <string>

namespace velocita::v2 {
namespace {
using namespace truemarkets;
constexpr std::string_view base58_chars="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr std::uint64_t maximum=std::numeric_limits<std::int64_t>::max();
std::optional<std::uint64_t> integer(std::string_view text)noexcept{
    if(text.empty() || (text.size()>1 && text.front()=='0'))return {};
    std::uint64_t value{};const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size() || value>maximum)return {};return value;
}
std::optional<std::uint64_t> number(std::string_view object,std::string_view member)noexcept{
    const auto raw=json_member_raw(object,member);return raw?integer(*raw):std::nullopt;
}
bool key_from_text(std::string_view text,SolanaAccountKey& out)noexcept{
    if(text.empty() || text.size()>44)return false;out={};
    std::size_t zeroes{};while(zeroes<text.size() && text[zeroes]=='1')++zeroes;
    for(char c:text){const auto n=base58_chars.find(c);if(n==std::string_view::npos)return false;unsigned carry=static_cast<unsigned>(n);
        for(std::size_t i=32;i-->0;){carry+=unsigned(out[i])*58U;out[i]=carry&255U;carry>>=8;}if(carry)return false;}
    std::size_t first{};while(first<32 && out[first]==0)++first;return first==zeroes;
}
std::string base58_text(std::span<const std::byte> bytes){
    std::array<unsigned,88> digits{};std::size_t used=1,zeroes{};
    while(zeroes<bytes.size() && bytes[zeroes]==std::byte{})++zeroes;
    for(auto b:bytes){unsigned carry=std::to_integer<unsigned>(b);for(std::size_t i=0;i<used;++i){carry+=digits[i]*256;digits[i]=carry%58;carry/=58;}while(carry){digits[used++]=carry%58;carry/=58;}}
    std::string text(zeroes,'1');for(std::size_t i=used;i-->0;)text+=base58_chars[digits[i]];return text;
}
std::string signature_text(const SwapSubmissionTransaction& signature){return base58_text(signature);}
bool base64_bytes(std::string_view text,std::span<std::uint8_t> target,std::size_t& size)noexcept{
    constexpr std::string_view chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size=0;if(text.empty() || text.size()%4)return false;
    for(std::size_t i=0;i<text.size();i+=4){unsigned value{};unsigned pad{};
        for(unsigned j=0;j<4;++j){const auto c=text[i+j];if(c=='='){if(j<2 || i+4!=text.size())return false;++pad;value<<=6;}
            else{const auto n=chars.find(c);if(n==std::string_view::npos || pad)return false;value=(value<<6)|static_cast<unsigned>(n);}}
        if(pad>2 || (pad==1 && (value&255U)) || (pad==2 && (value&65535U)))return false;
        if(size+3-pad>target.size())return false;for(unsigned j=0;j<3-pad;++j)target[size++]=static_cast<std::uint8_t>(value>>(16-8*j));
    }return true;
}
bool add(std::uint64_t& a,std::uint64_t b)noexcept{if(b>maximum || a>maximum-b)return false;a+=b;return true;}
bool balances(std::string_view meta,std::string_view name,std::size_t count,std::array<SettlementAccountBalance,256>& out,bool after)noexcept{
    const auto raw=json_member_raw(meta,name);std::array<std::string_view,256> rows{};const auto n=raw?json_array_elements(*raw,rows):std::nullopt;
    if(!n || *n!=count)return false;for(std::size_t i=0;i<count;++i){const auto amount=integer(rows[i]);if(!amount)return false;(after?out[i].after_lamports:out[i].before_lamports)=*amount;}return true;
}
bool tokens(std::string_view meta,std::string_view name,std::size_t count,std::array<SettlementAccountBalance,256>& out,bool after)noexcept{
    const auto raw=json_member_raw(meta,name);std::array<std::string_view,256> rows{};const auto n=raw?json_array_elements(*raw,rows):std::nullopt;if(!n)return false;
    for(std::size_t i=0;i<*n;++i){const auto index=number(rows[i],"accountIndex");if(!index || *index>=count)return false;
        auto& token=after?out[*index].after_token:out[*index].before_token;if(token.present)return false;
        const auto mint=json_member_string(rows[i],"mint"),owner=json_member_string(rows[i],"owner"),program=json_member_string(rows[i],"programId");
        if(!mint || !owner || !program || !key_from_text(*mint,token.mint) || !key_from_text(*owner,token.owner) || !key_from_text(*program,token.program) || token.program!=kSolanaClassicTokenProgram)return false;
        const auto ui=json_member_raw(rows[i],"uiTokenAmount");if(!ui)return false;const auto amount=json_member_string(*ui,"amount");const auto decimals=number(*ui,"decimals");const auto atoms=amount?integer(*amount):std::nullopt;
        if(!atoms || !decimals || *decimals>18)return false;token.present=true;token.atoms=*atoms;token.decimals=static_cast<std::uint8_t>(*decimals);
    }return true;
}
bool loaded(std::string_view meta,const DecodedSolanaPayload& decoded,const ResolvedSolanaAccounts& resolved)noexcept{
    const auto raw=json_member_raw(meta,"loadedAddresses");if(!raw)return decoded.loaded_key_count==0;
    std::size_t offset=decoded.static_key_count,writable{};for(std::size_t i=0;i<decoded.lookup_count;++i)writable+=decoded.lookups[i].writable_count;
    for(unsigned side=0;side<2;++side){const auto array=json_member_raw(*raw,side?"readonly":"writable");std::array<std::string_view,256> rows{};const auto n=array?json_array_elements(*array,rows):std::nullopt;
        if(!n || *n!=(side?decoded.loaded_key_count-writable:writable))return false;
        for(std::size_t i=0;i<*n;++i){if(rows[i].size()<2 || rows[i].front()!='"' || rows[i].back()!='"')return false;SolanaAccountKey key{};
            if(!key_from_text(rows[i].substr(1,rows[i].size()-2),key) || key!=resolved.accounts[offset++].key)return false;}}
    return offset==resolved.account_count;
}
bool conserved(const SettlementEvidence& result)noexcept{
    std::uint64_t before{},after{};for(std::size_t i=0;i<result.account_count;++i)if(!add(before,result.accounts[i].before_lamports) || !add(after,result.accounts[i].after_lamports))return false;
    if(!add(after,result.network_fee) || before!=after)return false;
    for(std::size_t i=0;i<result.account_count;++i)for(unsigned side=0;side<2;++side){const auto& basis=side?result.accounts[i].after_token:result.accounts[i].before_token;if(!basis.present)continue;
        std::uint64_t pre{},post{};for(std::size_t j=0;j<result.account_count;++j){const auto& a=result.accounts[j];
            if(a.before_token.present && a.before_token.mint==basis.mint){if(a.before_token.decimals!=basis.decimals || !add(pre,a.before_token.atoms))return false;}
            if(a.after_token.present && a.after_token.mint==basis.mint){if(a.after_token.decimals!=basis.decimals || !add(post,a.after_token.atoms))return false;}}
        SolanaAccountKey wrapped{};if(!key_from_text(kSwapSolMint,wrapped))return false;
        if(basis.mint!=wrapped && pre!=post)return false;
    }
    return true;
}
}
SwapFinalizedReader::SwapFinalizedReader(SwapFinalizedRpcPort& p,SwapFinalizedReadClock& c,Digest256 d,std::uint64_t t):port_(p),clock_(c),provider_(d),timeout_(t){}
FinalizedReadResult SwapFinalizedReader::read(const SwapSubmissionTransaction& signature,std::uint64_t request_id,std::uint64_t minimum_slot){
    if(provider_.is_zero() || !timeout_ || !request_id || request_id>maximum || !minimum_slot || minimum_slot>maximum ||
        signature==SwapSubmissionTransaction{} || port_.provider_identity()!=provider_)return {};
    const auto start=clock_.now_ms();if(start>std::numeric_limits<std::uint64_t>::max()-timeout_)return {};
    const auto body=std::string("{\"jsonrpc\":\"2.0\",\"id\":")+std::to_string(request_id)+",\"method\":\"getTransaction\",\"params\":[\""+signature_text(signature)+"\",{\"encoding\":\"base64\",\"commitment\":\"finalized\",\"maxSupportedTransactionVersion\":0}]}";
    SettlementRpcResponse response;try{response=port_.request(body,timeout_);}catch(...){return {FinalizedReadCode::transport_refused,{}};}
    const auto end=clock_.now_ms();if(end<start || end-start>=timeout_ || port_.provider_identity()!=provider_)return {};
    if(!response.transport_complete || response.http_status!=200 || response.size>response.body.size())return {FinalizedReadCode::transport_refused,{}};
    const std::string_view document(response.body.data(),response.size);
    if(validate_json_object(document)!=JsonError::none || json_member_string(document,"jsonrpc")!="2.0" ||
        number(document,"id")!=request_id || json_member_raw(document,"error"))return {FinalizedReadCode::malformed,{}};
    const auto result=json_member_raw(document,"result");if(!result)return {FinalizedReadCode::malformed,{}};if(*result=="null")return {FinalizedReadCode::not_found,{}};
    const auto slot=number(*result,"slot");if(!slot || !*slot)return {FinalizedReadCode::malformed,{}};if(*slot<minimum_slot)return {FinalizedReadCode::stale,{}};
    VerifiedFinalizedRead evidence;evidence.body_=response.body;evidence.size_=response.size;evidence.provider_=provider_;evidence.signature_=signature;evidence.slot_=*slot;
    evidence.response_digest_=sha256_digest(std::as_bytes(std::span(document.data(),document.size())));
    return {FinalizedReadCode::attributed_finalized_response,evidence};
}
std::optional<JupiterLookupPrefix> read_jupiter_lookup_prefix(SwapFinalizedRpcPort& port,const Digest256& provider,
    std::uint64_t timeout,std::uint64_t request_id,const JupiterLookupRequirement& need){
    if(provider.is_zero() || !timeout || !request_id || request_id>maximum || !need.count || need.count>kMaxLookupPrefix ||
        port.provider_identity()!=provider)return {};
    const auto body=std::string("{\"jsonrpc\":\"2.0\",\"id\":")+std::to_string(request_id)+",\"method\":\"getAccountInfo\",\"params\":[\""+
        base58_text(std::as_bytes(std::span(need.table)))+"\",{\"encoding\":\"base64\",\"commitment\":\"finalized\"}]}";
    SettlementRpcResponse response;try{response=port.request(body,timeout);}catch(...){return {};}
    if(port.provider_identity()!=provider || !response.transport_complete || response.http_status!=200 || response.size>response.body.size())return {};
    const std::string_view document(response.body.data(),response.size);
    if(validate_json_object(document)!=JsonError::none || json_member_string(document,"jsonrpc")!="2.0" ||
        number(document,"id")!=request_id || json_member_raw(document,"error"))return {};
    const auto result=json_member_raw(document,"result");const auto value=result?json_member_raw(*result,"value"):std::nullopt;
    if(!value || *value=="null" || json_member_string(*value,"owner")!="AddressLookupTab1e1111111111111111111111111")return {};
    const auto data=json_member_raw(*value,"data");std::array<std::string_view,2> items{};
    const auto count=data?json_array_elements(*data,items):std::nullopt;
    if(!count || *count!=2 || items[1]!="\"base64\"" || items[0].size()<2 || items[0].front()!='"' || items[0].back()!='"')return {};
    std::array<std::uint8_t,kLookupTableMetaBytes+32*kMaxLookupPrefix> raw{};std::size_t size{};
    if(!base64_bytes(items[0].substr(1,items[0].size()-2),raw,size))return {};
    return jupiter_lookup_prefix_from_account(need,std::span(raw).first(size));
}
namespace {
std::optional<SwapSubmissionTransaction> signature_from_text(std::string_view text)noexcept{
    if(text.empty() || text.size()>88)return {};SwapSubmissionTransaction out{};std::size_t zeroes{};
    while(zeroes<text.size() && text[zeroes]=='1')++zeroes;
    for(char c:text){const auto n=base58_chars.find(c);if(n==std::string_view::npos)return {};unsigned carry=static_cast<unsigned>(n);
        for(std::size_t i=out.size();i-->0;){carry+=std::to_integer<unsigned>(out[i])*58U;out[i]=static_cast<std::byte>(carry&255U);carry>>=8;}if(carry)return {};}
    std::size_t first{};while(first<out.size() && out[first]==std::byte{})++first;
    if(first==out.size() || first!=zeroes)return {};return out;
}
struct RpcDocument{SettlementRpcResponse response{};std::string_view document{};std::string_view result{};};
bool rpc_call(SwapFinalizedRpcPort& port,const Digest256& provider,std::uint64_t timeout,std::uint64_t id,const std::string& body,RpcDocument& out,Sha256& transcript)noexcept{
    try{out.response=port.request(body,timeout);}catch(...){return false;}
    if(port.provider_identity()!=provider || !out.response.transport_complete || out.response.http_status!=200 || out.response.size>out.response.body.size())return false;
    out.document=std::string_view(out.response.body.data(),out.response.size);
    if(validate_json_object(out.document)!=JsonError::none || json_member_string(out.document,"jsonrpc")!="2.0" ||
        number(out.document,"id")!=id || json_member_raw(out.document,"error"))return false;
    const auto result=json_member_raw(out.document,"result");if(!result)return false;out.result=*result;
    const auto digest=sha256_digest(std::as_bytes(std::span(out.document.data(),out.document.size())));transcript.update(digest.bytes);return true;
}
std::string rpc_head(std::uint64_t id,std::string_view method){return std::string("{\"jsonrpc\":\"2.0\",\"id\":")+std::to_string(id)+",\"method\":\""+std::string(method)+"\",\"params\":[";}
}
UnexecutedChainProof prove_unexecuted_on_chain(SwapFinalizedRpcPort& port,const Digest256& provider,std::uint64_t timeout,
    std::uint64_t id,std::string_view original,std::string_view organization,std::string_view signer,const SolanaAccountKey& owner){
    UnexecutedChainProof out;using Code=UnexecutedProofCode;
    const auto finish=[&](Code code){UnexecutedChainProof r;r.code=code;r.slot=out.slot;r.block_time_s=out.block_time_s;r.checked_transactions=out.checked_transactions;return r;};
    if(provider.is_zero() || !timeout || !id || id>maximum-64 || owner==SolanaAccountKey{} || port.provider_identity()!=provider)return out;
    DecodedSolanaPayload d;
    if(decode_solana_signing_envelope(original,organization,signer,d)!=SolanaParseError::none || !d.version_zero || d.nonzero_signatures ||
        d.timestamp_ms<=0 || !d.signature_count || 1U+64U*d.signature_count>=d.transaction_size)return finish(Code::envelope_refused);
    const auto stamp=static_cast<std::uint64_t>(d.timestamp_ms);
    const auto message=1U+64U*static_cast<std::size_t>(d.signature_count);
    const auto blockhash=base58_text(std::as_bytes(std::span(d.transaction).subspan(d.blockhash_offset,32)));
    Sha256 transcript;constexpr std::string_view domain="velocita.swap.unexecuted-chain.v1";transcript.update(std::as_bytes(std::span(domain.data(),domain.size())));
    RpcDocument valid;
    if(!rpc_call(port,provider,timeout,id,rpc_head(id,"isBlockhashValid")+"\""+blockhash+"\",{\"commitment\":\"finalized\"}]}",valid,transcript))return finish(Code::transport_refused);
    const auto context=json_member_raw(valid.result,"context");const auto value=json_member_raw(valid.result,"value");
    const auto slot=context?number(*context,"slot"):std::nullopt;
    if(!slot || !*slot || !value || (*value!="true" && *value!="false"))return finish(Code::malformed);
    out.slot=*slot;
    if(*value!="false")return finish(Code::blockhash_still_valid);
    RpcDocument time;
    if(!rpc_call(port,provider,timeout,id+1,rpc_head(id+1,"getBlockTime")+std::to_string(*slot)+"]}",time,transcript))return finish(Code::transport_refused);
    const auto block_time=integer(time.result);if(!block_time || *block_time>maximum/1000)return finish(Code::malformed);
    out.block_time_s=*block_time;
    if(stamp>maximum-kUnexecutedFinalityMarginMs || *block_time*1000<stamp+kUnexecutedFinalityMarginMs)return finish(Code::not_final_long_enough);
    const auto earliest=stamp>kUnexecutedHistoryLeadMs?stamp-kUnexecutedHistoryLeadMs:0;
    std::array<SwapSubmissionTransaction,kUnexecutedMaxCandidates> candidates{};std::size_t candidate_count{};
    std::string before;bool complete=false;std::uint64_t next=id+2;
    const auto address=base58_text(std::as_bytes(std::span(owner)));
    for(std::size_t page=0;page<kUnexecutedHistoryPages && !complete;++page){
        RpcDocument history;
        auto body=rpc_head(next,"getSignaturesForAddress")+"\""+address+"\",{\"commitment\":\"finalized\",\"limit\":"+std::to_string(kUnexecutedHistoryPage)+
            ",\"minContextSlot\":"+std::to_string(*slot);
        if(!before.empty())body+=",\"before\":\""+before+"\"";
        body+="}]}";
        if(!rpc_call(port,provider,timeout,next,body,history,transcript))return finish(Code::transport_refused);
        ++next;
        std::array<std::string_view,kUnexecutedHistoryPage+1> rows{};const auto n=json_array_elements(history.result,rows);
        if(!n || *n>kUnexecutedHistoryPage)return finish(Code::malformed);
        for(std::size_t i=0;i<*n;++i){
            const auto text=json_member_string(rows[i],"signature");const auto signature=text?signature_from_text(*text):std::nullopt;
            const auto entry_slot=number(rows[i],"slot");const auto entry_time=number(rows[i],"blockTime");
            if(!signature || !entry_slot || !entry_time || *entry_time>maximum/1000)return finish(Code::malformed);
            if(*entry_time*1000<earliest){complete=true;break;}
            if(*entry_slot>*slot)continue;
            if(candidate_count==candidates.size())return finish(Code::history_too_large);
            candidates[candidate_count++]=*signature;
        }
        if(!complete && *n<kUnexecutedHistoryPage)complete=true;
        if(!complete){const auto last=json_member_string(rows[*n-1],"signature");if(!last)return finish(Code::malformed);before=std::string(*last);}
    }
    if(!complete)return finish(Code::history_incomplete);
    for(std::size_t c=0;c<candidate_count;++c){
        RpcDocument tx;
        const auto body=rpc_head(next,"getTransaction")+"\""+signature_text(candidates[c])+"\",{\"encoding\":\"base64\",\"commitment\":\"finalized\",\"maxSupportedTransactionVersion\":0}]}";
        if(!rpc_call(port,provider,timeout,next,body,tx,transcript))return finish(Code::transport_refused);
        ++next;
        if(tx.result=="null")return finish(Code::malformed);
        const auto encoded=json_member_raw(tx.result,"transaction");std::array<std::string_view,2> items{};const auto count=encoded?json_array_elements(*encoded,items):std::nullopt;
        if(!count || *count!=2 || items[1]!="\"base64\"" || items[0].size()<2 || items[0].front()!='"' || items[0].back()!='"')return finish(Code::malformed);
        std::array<std::uint8_t,kMaxSolanaTransaction> raw{};std::size_t size{};
        if(!base64_bytes(items[0].substr(1,items[0].size()-2),raw,size))return finish(Code::malformed);
        out.checked_transactions=c+1;
        if(size>=1 && raw[0]==d.signature_count && size==d.transaction_size &&
            std::equal(raw.begin()+static_cast<std::ptrdiff_t>(message),raw.begin()+static_cast<std::ptrdiff_t>(size),d.transaction.begin()+static_cast<std::ptrdiff_t>(message)))
            return finish(Code::landed);
    }
    std::array<std::byte,16> tail{};for(unsigned n=0;n<8;++n){tail[n]=static_cast<std::byte>(*slot>>(8*n));tail[8+n]=static_cast<std::byte>(*block_time>>(8*n));}
    transcript.update(tail);
    out.evidence=transcript.finish();out.code=Code::proven;return out;
}
std::optional<SolanaAccountKey> decode_solana_account_key(std::string_view text) noexcept {
    SolanaAccountKey key{};if(!key_from_text(text,key) || key==SolanaAccountKey{})return {};return key;
}
SettlementEvidence decode_settlement_evidence(const VerifiedFinalizedRead& read,std::string_view original,std::string_view organization,std::string_view signer,
    std::span<const SolanaLookupEvidence> lookups,const SolanaAccountKey& owner)noexcept{
    SettlementEvidence result;DecodedSolanaPayload decoded;ResolvedSolanaAccounts resolved;
    if(owner==SolanaAccountKey{} || decode_solana_signing_envelope(original,organization,signer,decoded)!=SolanaParseError::none ||
        resolve_solana_accounts(decoded,organization,signer,lookups,resolved)!=SolanaResolutionError::none)return result;
    const auto fail=[&](SettlementEvidenceCode code){SettlementEvidence empty;empty.code=code;return empty;};
    const auto root=json_member_raw(read.body(),"result");if(!root)return fail(SettlementEvidenceCode::malformed);
    const auto version=json_member_raw(*root,"version");if(!version || (decoded.version_zero?*version!="0":*version!="\"legacy\""))return fail(SettlementEvidenceCode::transaction_mismatch);
    const auto tx=json_member_raw(*root,"transaction"),meta=json_member_raw(*root,"meta");std::array<std::string_view,2> items{};const auto count=tx?json_array_elements(*tx,items):std::nullopt;
    if(!meta || !count || *count!=2 || items[1]!="\"base64\"" || items[0].size()<2 || items[0].front()!='"' || items[0].back()!='"')return fail(SettlementEvidenceCode::malformed);
    std::array<std::uint8_t,kMaxSolanaTransaction> raw{};std::size_t size{};
    if(!base64_bytes(items[0].substr(1,items[0].size()-2),raw,size))return fail(SettlementEvidenceCode::malformed);
    const std::size_t message=1U+64U*decoded.signature_count;
    if(size!=decoded.transaction_size || size<=message || raw[0]!=decoded.signature_count ||
        !std::equal(raw.begin()+message,raw.begin()+size,decoded.transaction.begin()+message))return fail(SettlementEvidenceCode::transaction_mismatch);
    for(std::size_t i=0;i<64;++i)if(raw[1+i]!=std::to_integer<unsigned>(read.signature()[i]))return fail(SettlementEvidenceCode::transaction_mismatch);
    for(std::size_t i=0;i<decoded.signature_count;++i)if(std::all_of(raw.begin()+1+64*i,raw.begin()+1+64*(i+1),[](auto b){return b==0;}))return fail(SettlementEvidenceCode::transaction_mismatch);
    const auto err=json_member_raw(*meta,"err");if(!err)return fail(SettlementEvidenceCode::malformed);const bool failed=*err!="null";
    if(failed){const auto instruction=json_member_raw(*err,"InstructionError");std::array<std::string_view,2> parts{};
        const auto length=instruction?json_array_elements(*instruction,parts):std::nullopt;
        if(!length || *length!=2)return fail(SettlementEvidenceCode::failed_transaction);
        const auto index=integer(parts[0]);const auto custom=number(parts[1],"Custom");
        if(!index || *index>=decoded.instruction_count || !custom || *custom>std::numeric_limits<std::uint32_t>::max())return fail(SettlementEvidenceCode::failed_transaction);
    }
    const auto fee=number(*meta,"fee");if(!fee || !loaded(*meta,decoded,resolved))return fail(SettlementEvidenceCode::account_mismatch);
    result.account_count=resolved.account_count;result.owner=owner;result.network_fee=*fee;
    bool found{};for(std::size_t i=0;i<resolved.account_count;++i){result.accounts[i].key=resolved.accounts[i].key;if(resolved.accounts[i].key==owner){if(!resolved.accounts[i].signer || !resolved.accounts[i].writable)return fail(SettlementEvidenceCode::account_mismatch);result.owner_index=i;found=true;}}
    if(!found || !balances(*meta,"preBalances",result.account_count,result.accounts,false) || !balances(*meta,"postBalances",result.account_count,result.accounts,true) ||
        !tokens(*meta,"preTokenBalances",result.account_count,result.accounts,false) || !tokens(*meta,"postTokenBalances",result.account_count,result.accounts,true))return fail(SettlementEvidenceCode::account_mismatch);
    SolanaAccountKey wrapped{},usdc{};if(!key_from_text(kSwapSolMint,wrapped) || !key_from_text(kSwapUsdcMint,usdc))return fail(SettlementEvidenceCode::malformed);
    for(std::size_t i=0;i<result.account_count;++i)for(unsigned side=0;side<2;++side){const auto& token=side?result.accounts[i].after_token:result.accounts[i].before_token;if(!token.present)continue;
        if((token.mint==usdc && token.decimals!=6) || (token.mint==wrapped && token.decimals!=9))return fail(SettlementEvidenceCode::account_mismatch);
        if(token.owner==owner && token.mint!=usdc && token.mint!=wrapped)return fail(SettlementEvidenceCode::unsupported_owner_asset);}
    if(result.accounts[result.owner_index].before_token.present || result.accounts[result.owner_index].after_token.present || !conserved(result))return fail(SettlementEvidenceCode::balance_mismatch);
    result.code=failed?SettlementEvidenceCode::observed_failed_transaction:SettlementEvidenceCode::observed_transaction_needs_account_lifecycle;result.response_digest=read.response_digest();result.provider=read.provider_identity();
    result.original_payload_digest=sha256_digest(std::as_bytes(std::span(original.data(),original.size())));result.signature=read.signature();result.slot=read.slot();return result;
}
SettlementPropertyResult corroborate_settlement_properties(const SettlementEvidence& evidence,std::span<const DeclaredOwnedAccount> scope,std::span<const OwnerAccountChange> changes)noexcept{
    if(evidence.code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle || !evidence.account_count || evidence.account_count>256 || evidence.owner_index>=evidence.account_count)return {};
    if(scope.empty() || scope.size()>kOwnerPropertyAccounts || scope.size()!=changes.size())return {SettlementPropertyCode::scope_mismatch,{}};
    SolanaAccountKey wrapped{},usdc{};if(!key_from_text(kSwapSolMint,wrapped) || !key_from_text(kSwapUsdcMint,usdc))return {};
    std::size_t expected{};for(std::size_t i=0;i<evidence.account_count;++i){const auto& a=evidence.accounts[i];const bool owned=i==evidence.owner_index || (a.before_token.present && a.before_token.owner==evidence.owner) || (a.after_token.present && a.after_token.owner==evidence.owner);if(!owned)continue;++expected;
        const auto at=std::find_if(scope.begin(),scope.end(),[&](const auto& s){return s.key==a.key;});if(at==scope.end())return {SettlementPropertyCode::scope_mismatch,{}};
        const auto n=static_cast<std::size_t>(at-scope.begin());if(changes[n].key!=a.key)return {SettlementPropertyCode::scope_mismatch,{}};
        if((i==evidence.owner_index)!=(at->kind==OwnedAccountKind::native_wallet))return {SettlementPropertyCode::property_mismatch,{}};
        for(unsigned side=0;side<2;++side){const auto& p=side?changes[n].after:changes[n].before;const auto& t=side?a.after_token:a.before_token;const auto lamports=side?a.after_lamports:a.before_lamports;
            if(p.lamports!=lamports || p.exists!=(lamports!=0))return {SettlementPropertyCode::property_mismatch,{}};
            if(at->kind==OwnedAccountKind::native_wallet){if(t.present || p.token_atoms || p.native_reserve)return {SettlementPropertyCode::property_mismatch,{}};}
            else if(p.exists){if(!t.present || t.owner!=evidence.owner || t.atoms!=p.token_atoms ||
                (at->kind==OwnedAccountKind::wrapped_sol?t.mint!=wrapped:t.mint!=usdc))return {SettlementPropertyCode::property_mismatch,{}};}
            else if(t.present)return {SettlementPropertyCode::property_mismatch,{}};
        }
    }
    if(expected!=scope.size())return {SettlementPropertyCode::scope_mismatch,{}};
    const auto measurement=measure_owner_property(scope,changes);if(measurement.code!=OwnerAccountingCode::measured_declared_scope_only)return {SettlementPropertyCode::property_mismatch,{}};
    return {SettlementPropertyCode::corroborated_declarations_only,measurement};
}
}
