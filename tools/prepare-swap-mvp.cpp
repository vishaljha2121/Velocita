#include "velocita/v2/swap_runtime_production.hpp"
#include "velocita/v2/swap_transaction_review.hpp"
#include "velocita/v2/swap_codec.hpp"
#include "velocita/v2/swap_owner_accounting.hpp"
#include "velocita/v2/digest.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace velocita::mvp_builder {
using namespace v2;
namespace fs=std::filesystem;
using Fields=std::map<std::string,std::string>;
void require(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
Digest256 hash(std::string_view value){return sha256_digest(std::as_bytes(std::span(value)));}
std::string read(const fs::path& path,std::size_t limit){
    const auto fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK);require(fd>=0,"input unavailable or symlink");
    struct Close{int fd;~Close(){::close(fd);}} close{fd};struct stat st{};
    require(::fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size>0 && static_cast<std::uint64_t>(st.st_size)<=limit,"input is not a bounded regular file");
    std::string result(static_cast<std::size_t>(st.st_size),'\0');std::size_t offset{};
    while(offset<result.size()){auto n=::read(fd,result.data()+offset,result.size()-offset);require(n>0,"short input read");offset+=n;}
    char extra{};require(::read(fd,&extra,1)==0,"input changed size");return result;
}
Fields fields(std::string_view source){
    Fields result;
    while(!source.empty()){
        auto end=source.find('\n');auto line=source.substr(0,end);auto equal=line.find('=');
        require(equal!=std::string_view::npos && equal && equal+1<line.size(),"invalid parameter line");
        require(result.emplace(line.substr(0,equal),line.substr(equal+1)).second,"duplicate parameter");
        source=end==std::string_view::npos?std::string_view{}:source.substr(end+1);
    }
    return result;
}
std::string text(Fields& f,std::string_view name){auto it=f.find(std::string(name));require(it!=f.end(),"missing parameter");auto value=it->second;f.erase(it);return value;}
std::uint64_t number(Fields& f,std::string_view name){
    const auto s=text(f,name);std::uint64_t n{};auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);
    require(!s.empty() && s.front()>='0' && s.front()<='9' && (s.size()==1 || s.front()!='0') && parsed.ec==std::errc{} && parsed.ptr==s.data()+s.size(),"invalid unsigned parameter");return n;
}
Digest256 digest(Fields& f,std::string_view name){
    const auto s=text(f,name);require(s.size()==64,"digest width");Digest256 d;
    for(std::size_t i=0;i<32;++i){unsigned n{};auto p=std::from_chars(s.data()+2*i,s.data()+2*i+2,n,16);
        require(p.ec==std::errc{} && p.ptr==s.data()+2*i+2,"digest encoding");d.bytes[i]=static_cast<std::byte>(n);}
    require(!d.is_zero(),"zero evidence digest");return d;
}
std::string document(const Fields& f){std::string s;for(const auto& [k,v]:f)s+=k+'='+v+'\n';return s;}
std::string canonical(const swap::Record& record){
    std::array<std::byte,swap::max_record_bytes> bytes{};auto encoded=swap::encode(record,bytes);
    require(encoded.error==swap::CodecError::none,"record encoding refused");swap::Record decoded;
    require(swap::decode(std::span(bytes).first(encoded.size),decoded)==swap::CodecError::none && decoded==record,"canonical record roundtrip refused");
    return {reinterpret_cast<const char*>(bytes.data()),encoded.size};
}
void write_new(const fs::path& path,std::string_view body){
    const auto fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);require(fd>=0,"exclusive output refused");
    struct Close{int fd;~Close(){::close(fd);}} close{fd};std::size_t offset{};
    while(offset<body.size()){auto n=::write(fd,body.data()+offset,body.size()-offset);require(n>0,"output write failed");offset+=n;}
    require(::fsync(fd)==0,"output sync failed");
}
void emit(const fs::path& output,const Fields& files){
    ::umask(0077);require(fs::create_directory(output),"output already exists");
    Fields manifest{{"schema","1"},{"network_used","0"},{"funds_moved","0"}};
    for(const auto& [name,body]:files){write_new(output/name,body);manifest.emplace(name+".sha256",to_hex(hash(body)));}
    write_new(output/"manifest.txt",document(manifest));
    const auto fd=::open(output.c_str(),O_RDONLY|O_DIRECTORY);require(fd>=0,"output directory unavailable");
    const auto synced=::fsync(fd);::close(fd);require(synced==0,"output directory sync failed");
}
struct Context{SwapRuntimeProfile profile;SwapSignerVerificationContext transaction;SwapAccountHistoryCut history;};
Context context(std::string_view profile,std::string_view history,std::string_view transaction,std::uint64_t now){
    auto p=decode_swap_runtime_profile(profile);require(p.has_value() && swap_runtime_live_contract_admitted(*p,now),"runtime profile not admitted");
    require(validate_swap_gateway_startup(p->gateway,now)==SwapRuntimeCode::valid,"runtime credential review expired");
    auto tx=decode_swap_transaction_review(transaction,p->gateway.binding.transaction_review_digest,p->gateway.binding,p->organization.view(),p->signer_id.view());
    require(tx.has_value() && tx->policy.expires_at_ms>now,"transaction review invalid or expired");
    require(tx->policy.account_keys[0]==p->failure_policy.expected_external_fee_payer,"sponsor pin differs");
    auto h=decode_swap_account_history_cut(history,hash(history));require(h.has_value(),"account history refused");
    return {*p,*tx,*h};
}
Fields build_opening(std::string profile,std::string history,std::string transaction,std::string parameters){
    auto f=fields(parameters);require(number(f,"schema")==1,"unsupported parameters schema");
    const auto now=number(f,"now_ms"),expires=number(f,"review_expires_ms");auto c=context(profile,history,transaction,now);
    swap::Initialize opening;opening.binding=c.profile.gateway.binding.allocation;
    opening.base=swap::BaseAtoms{number(f,"base_atoms")};opening.quote=swap::QuoteAtoms{number(f,"quote_atoms")};
    opening.kill_epoch=number(f,"kill_epoch");opening.executor_generation=number(f,"executor_generation");
    auto& l=opening.limits;l.max_purchase_total=swap::QuoteAtoms{1'000'000};l.max_buy_debit=swap::QuoteAtoms{1'000'000};l.max_fee=swap::QuoteAtoms{2'000};
    l.max_realized_loss=swap::QuoteAtoms{number(f,"max_realized_loss_atoms")};l.max_sell_input=swap::BaseAtoms{number(f,"max_sell_input_atoms")};
    l.max_buy_price_per_sol=swap::QuoteAtoms{number(f,"max_buy_price_per_sol")};l.min_sell_price_per_sol=swap::QuoteAtoms{number(f,"min_sell_price_per_sol")};
    l.max_source_age=number(f,"max_source_age_ms");l.max_sign_lifetime=number(f,"max_sign_lifetime_ms");
    opening.base_cost=swap::QuoteAtoms{number(f,"valuation_net_quote_atoms")};const auto mark_time=number(f,"valuation_observed_ms");
    const auto mark_evidence=digest(f,"valuation_evidence_sha256");require(f.empty(),"unknown opening parameter");
    require(now && expires>now && expires-now<=900'000 && c.history.finished_ms<=now && now-c.history.finished_ms<900'000 &&
        expires-c.history.finished_ms<=900'000 && expires<=c.transaction.policy.expires_at_ms && expires<=c.profile.gateway.expires_at_ms,"review window exceeds current evidence");
    require(opening.base.value()==c.history.base_atoms && opening.quote.value()==c.history.quote_atoms && opening.quote.value()>=1'000'000,"observed balances do not match all-inventory history");
    require(opening.base.value()>0 && opening.base_cost.value()>0 && l.max_sell_input.value()>0 && l.max_sell_input.value()<=opening.base.value(),"positive full-position executable mark and sell bound required");
    require(l.max_source_age && l.max_source_age<=30'000 && l.max_sign_lifetime && l.max_sign_lifetime<=30'000 &&
        mark_time && mark_time<=now && now-mark_time<=l.max_source_age,"mark or intent timing bound invalid");
    const auto funding=document({{"schema","1"},{"account_history_sha256",to_hex(hash(history))},{"opening_parameters_sha256",to_hex(hash(parameters))},
        {"valuation_evidence_sha256",to_hex(mark_evidence)},{"valuation_base_atoms",std::to_string(opening.base.value())},
        {"valuation_net_quote_atoms",std::to_string(opening.base_cost.value())},{"valuation_observed_ms",std::to_string(mark_time)}});
    opening.funding_evidence=hash(funding);swap::State state;
    require(swap::apply(state,opening,swap::Role::operator_role,now)==swap::Code::applied && swap_mvp_buy_scope(state,c.transaction),"opening reducer or one-buy scope refused");
    const auto review=document({{"schema","1"},{"runtime_identity",to_hex(swap_runtime_identity_digest(c.profile.gateway.binding))},
        {"opening_digest",to_hex(swap_opening_review_digest(opening))},{"history_evidence",to_hex(hash(history))},
        {"user_id",std::string(c.profile.user_id.view())},{"reviewed_at_ms",std::to_string(now)},{"expires_at_ms",std::to_string(expires)},
        {"outside_base","0"},{"outside_quote","0"},{"activity_count","0"}});
    auto r=decode_swap_account_review(review,hash(review));require(r.has_value() && swap_account_history_funding(c.history,*r,c.profile.gateway.binding,state,now),"history identity or funding refused");
    auto pf=fields(profile);require(pf.count("account_review_digest")==1,"profile review field missing");pf["account_review_digest"]=to_hex(hash(review));profile=document(pf);
    const auto final=context(profile,history,transaction,now);require(final.profile.account_review_digest==r->document_digest &&
        swap_account_review_matches(*r,final.profile.gateway.binding,final.profile.user_id.view(),state),"final review/profile binding refused");
    const auto record=canonical({swap::Role::operator_role,now,opening});
    return {{"runtime.profile",profile},{"account.history",history},{"transaction.review",transaction},{"account.review",review},
        {"opening.parameters",parameters},{"funding.review",funding},{"opening.record",record},
        {"record-summary.txt",document({{"kind","Initialize"},{"record_sha256",to_hex(hash(record))},{"opening_digest",to_hex(swap_opening_review_digest(opening))},
            {"base_atoms",std::to_string(opening.base.value())},{"quote_atoms",std::to_string(opening.quote.value())},{"base_mark_quote_atoms",std::to_string(opening.base_cost.value())},
            {"max_purchase_atoms","1000000"},{"max_fee_atoms","2000"},{"kill_epoch",std::to_string(opening.kill_epoch)},{"executor_generation",std::to_string(opening.executor_generation)}})}};
}
Fields build_intent(const Fields& bundle,std::string parameters){
    auto f=fields(parameters);require(number(f,"schema")==1,"unsupported parameters schema");const auto now=number(f,"now_ms");
    const auto& profile=bundle.at("runtime.profile");auto c=context(profile,bundle.at("account.history"),bundle.at("transaction.review"),now);
    auto r=decode_swap_account_review(bundle.at("account.review"),c.profile.account_review_digest);require(r.has_value(),"opening review differs");
    const auto& bytes=bundle.at("opening.record");swap::Record decoded;require(swap::decode(std::as_bytes(std::span(bytes)),decoded)==swap::CodecError::none &&
        decoded.role==swap::Role::operator_role && std::holds_alternative<swap::Initialize>(decoded.command) && canonical(decoded)==bytes && decoded.event_time<=now,"opening record refused");
    swap::State state;const auto& opening=std::get<swap::Initialize>(decoded.command);
    require(swap::apply(state,opening,swap::Role::operator_role,decoded.event_time)==swap::Code::applied &&
        swap_mvp_buy_scope(state,c.transaction) && swap_account_history_funding(c.history,*r,c.profile.gateway.binding,state,now),"opening funding expired or differs");
    swap::Intent intent;intent.binding=opening.binding;intent.effect=EffectId{1};intent.side=Side::buy;
    intent.quote_input=swap::QuoteAtoms{998'000};intent.max_quote_debit=swap::QuoteAtoms{1'000'000};intent.fee_cap=swap::QuoteAtoms{2'000};
    intent.min_base_receive=swap::BaseAtoms{number(f,"minimum_base_atoms")};intent.source_time=number(f,"source_time_ms");intent.sign_by=number(f,"sign_by_ms");
    intent.kill_epoch=number(f,"kill_epoch");intent.executor_generation=number(f,"executor_generation");
    const auto quoted=number(f,"quote_gross_output_lamports"),slippage=number(f,"quote_slippage_bps"),repayment=number(f,"repayment_lamports"),quote_time=number(f,"quote_observed_ms");
    const auto quote_evidence=digest(f,"quote_evidence_sha256");require(f.empty(),"unknown intent parameter");
    require(quoted && quoted<=static_cast<std::uint64_t>(INT64_MAX) && slippage<=c.transaction.policy.max_slippage_bps &&
        repayment<=2'039'280 && quote_time && quote_time<=intent.source_time && intent.source_time<=now &&
        now-quote_time<c.transaction.policy.max_envelope_age_ms && intent.sign_by>now && intent.sign_by-now<=opening.limits.max_sign_lifetime &&
        intent.sign_by<=r->expires_at_ms && intent.sign_by<=c.transaction.policy.expires_at_ms && intent.sign_by<=c.profile.gateway.expires_at_ms,"fresh quote or intent window refused");
    const auto factor=10'000-slippage;const auto gross=(quoted/10'000)*factor+((quoted%10'000)*factor+9'999)/10'000;
    const auto net=bound_buy_net_cost(gross,repayment,0,intent.min_base_receive,c.transaction.policy.accepted_costs);
    require(net.code==BuyNetCostCode::within_arithmetic_bounds_only && intent.min_base_receive.value()>0,"conservative quote floor refused");
    require(swap::apply(state,intent,swap::Role::strategy,now)==swap::Code::applied && swap_mvp_buy_scope(state,c.transaction),"intent reducer or one-buy scope refused");
    const auto record=canonical({swap::Role::strategy,now,intent});
    return {{"intent.record",record},{"intent.parameters",parameters},{"record-summary.txt",document({{"kind","Intent"},{"record_sha256",to_hex(hash(record))},
        {"opening_record_sha256",to_hex(hash(bytes))},{"runtime_profile_sha256",to_hex(hash(profile))},{"quote_evidence_sha256",to_hex(quote_evidence)},
        {"effect","1"},{"principal_atoms","998000"},{"maximum_debit_atoms","1000000"},{"fee_atoms","2000"},
        {"minimum_base_atoms",std::to_string(intent.min_base_receive.value())},{"conservative_quote_floor",std::to_string(net.minimum_net_receipt)},
        {"repayment_lamports",std::to_string(repayment)},{"guaranteed_sponsor_credit","0"},{"sign_by_ms",std::to_string(intent.sign_by)}})}};
}
int run(int argc,char** argv){
    require(argc>=2,"mode required: opening or intent");const std::string mode=argv[1];Fields args;
    for(int n=2;n<argc;n+=2){require(n+1<argc && std::string_view(argv[n]).starts_with("--") && args.emplace(argv[n]+2,argv[n+1]).second,"invalid or duplicate CLI argument");}
    const auto output=text(args,"output");const auto parameters=read(text(args,"parameters"),8192);Fields result;
    if(mode=="opening"){
        auto profile=read(text(args,"runtime-profile"),8192),history=read(text(args,"account-history"),4096),transaction=read(text(args,"transaction-review"),32768);
        require(args.empty(),"unknown CLI argument");result=build_opening(profile,history,transaction,parameters);
    }else if(mode=="intent"){
        const fs::path bundle=text(args,"bundle");require(args.empty(),"unknown CLI argument");Fields inputs;
        for(const auto& [name,limit]:std::map<std::string,std::size_t>{{"runtime.profile",8192},{"account.history",4096},{"transaction.review",32768},{"account.review",8192},{"opening.record",512}})inputs[name]=read(bundle/name,limit);
        result=build_intent(inputs,parameters);
    }else throw std::runtime_error("mode must be opening or intent");
    emit(output,result);std::cout<<"PREPARED offline_only=1 network_used=0 funds_moved=0\n";return 0;
}
}
#ifndef VELOCITA_MVP_BUILDER_TEST
int main(int argc,char** argv){try{return velocita::mvp_builder::run(argc,argv);}catch(const std::exception& e){std::cerr<<"REFUSED "<<e.what()<<'\n';return 1;}}
#endif
