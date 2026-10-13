#include "velocita/v2/swap_settlement_lifecycle.hpp"
#include "velocita/v2/swap_rest_codec.hpp"
#include "velocita/v2/truemarkets_wire.hpp"
#include "velocita/v2/jupiter_route_decode.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace velocita::v2 {
namespace {
using namespace truemarkets;
using Code=SwapLifecycleCode;
constexpr std::uint64_t maximum=std::numeric_limits<std::int64_t>::max();
constexpr std::size_t instruction_capacity=128;
struct Instruction {
    std::uint8_t program{},height{};
    std::uint16_t account_count{},data_size{};
    std::array<std::uint8_t,64> accounts{};
    std::array<std::uint8_t,256> data{};
};
struct Trace { std::array<Instruction,instruction_capacity> instructions{};std::size_t count{}; };
std::optional<std::uint64_t> integer(std::string_view s) noexcept {
    if(s.empty() || (s.size()>1 && s.front()=='0'))return {};
    std::uint64_t n{};const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(ec!=std::errc{} || end!=s.data()+s.size() || n>maximum)return {};return n;
}
std::optional<std::uint64_t> member(std::string_view s,std::string_view name) noexcept {
    const auto value=json_member_raw(s,name);return value?integer(*value):std::nullopt;
}
bool base58(std::string_view text,std::span<std::uint8_t> out,std::size_t& size) noexcept {
    constexpr std::string_view alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if(text.empty() || text.size()>350)return false;
    std::array<std::uint8_t,256> bytes{};std::size_t zeros{};
    while(zeros<text.size() && text[zeros]=='1')++zeros;
    if(zeros>out.size())return false;
    for(char c:text){const auto n=alphabet.find(c);if(n==std::string_view::npos)return false;unsigned carry=n;
        for(std::size_t i=bytes.size();i-->0;){carry+=unsigned(bytes[i])*58;bytes[i]=carry&255;carry>>=8;}if(carry)return false;}
    std::size_t first{};while(first<bytes.size() && !bytes[first])++first;
    size=zeros+bytes.size()-first;if(size>out.size())return false;
    std::fill_n(out.begin(),zeros,0);std::copy(bytes.begin()+first,bytes.end(),out.begin()+zeros);return true;
}
SolanaAccountKey mint(std::string_view text) noexcept {
    SolanaAccountKey key{};std::size_t size{};if(!base58(text,key,size) || size!=key.size())return {};return key;
}
std::uint64_t little(const Instruction& ix,std::size_t offset,unsigned count) noexcept {
    std::uint64_t n{};for(unsigned i=0;i<count;++i)n|=std::uint64_t(ix.data[offset+i])<<(8*i);return n;
}
Code trace(const VerifiedFinalizedRead& read,const DecodedSolanaPayload& decoded,std::size_t accounts,Trace& out) noexcept {
    const auto result=json_member_raw(read.body(),"result");const auto meta=result?json_member_raw(*result,"meta"):std::nullopt;
    const auto inner=meta?json_member_raw(*meta,"innerInstructions"):std::nullopt;
    if(!inner || *inner=="null")return Code::missing_trace;
    std::array<std::string_view,64> groups{};const auto count=json_array_elements(*inner,groups);if(!count)return Code::malformed_trace;
    std::array<std::string_view,64> by_index{};std::uint64_t previous{};
    for(std::size_t i=0;i<*count;++i){const auto index=member(groups[i],"index");const auto rows=json_member_raw(groups[i],"instructions");
        if(!index || *index>=decoded.instruction_count || (i && *index<=previous) || !rows)return Code::malformed_trace;
        by_index[*index]=*rows;previous=*index;}
    for(std::size_t n=0;n<decoded.instruction_count;++n){if(out.count==out.instructions.size())return Code::capacity;
        const auto& view=decoded.instructions[n];auto& top=out.instructions[out.count++];top.program=view.program_index;top.height=1;
        if(view.accounts_count>top.accounts.size() || view.data_size>top.data.size())return Code::capacity;
        top.account_count=view.accounts_count;top.data_size=view.data_size;
        std::copy_n(decoded.transaction.begin()+view.accounts_offset,view.accounts_count,top.accounts.begin());
        std::copy_n(decoded.transaction.begin()+view.data_offset,view.data_size,top.data.begin());
        if(by_index[n].empty())continue;
        std::array<std::string_view,instruction_capacity> rows{};const auto count_rows=json_array_elements(by_index[n],rows);if(!count_rows || !*count_rows)return Code::malformed_trace;
        unsigned height=1;
        for(std::size_t j=0;j<*count_rows;++j){if(out.count==out.instructions.size())return Code::capacity;
            auto& ix=out.instructions[out.count++];const auto p=member(rows[j],"programIdIndex"),h=member(rows[j],"stackHeight");
            const auto a=json_member_raw(rows[j],"accounts"),data=json_member_string(rows[j],"data");
            if(!p || *p>=accounts || !h || *h<2 || *h>5 || *h>height+1 || !a || !data)return Code::malformed_trace;
            height=*h;ix.height=*h;ix.program=*p;
            std::array<std::string_view,64> indices{};const auto num=json_array_elements(*a,indices);if(!num)return Code::capacity;ix.account_count=*num;
            for(std::size_t k=0;k<*num;++k){const auto index=integer(indices[k]);if(!index || *index>=accounts)return Code::malformed_trace;ix.accounts[k]=*index;}
            std::size_t bytes{};if(!base58(*data,ix.data,bytes))return Code::malformed_trace;ix.data_size=bytes;
        }
    }
    std::array<std::size_t,6> parents{};
    for(std::size_t n=0;n<out.count;++n){const auto& ix=out.instructions[n];
        if(ix.height>1){const auto& parent=out.instructions[parents[ix.height-1]];
            const auto includes=[&](std::uint8_t index){return std::find(parent.accounts.begin(),parent.accounts.begin()+parent.account_count,index)!=parent.accounts.begin()+parent.account_count;};
            if(!includes(ix.program))return Code::malformed_trace;
            for(std::size_t a=0;a<ix.account_count;++a)if(!includes(ix.accounts[a]))return Code::malformed_trace;
        }
        parents[ix.height]=n;
    }
    return Code::replayed_effects_only;
}
struct Account {
    std::uint64_t lamports{},atoms{};
    SolanaAccountKey owner{},mint{},close{};
    bool token{},created{},closed{},native{},unknown_atoms{},close_known{};
};
bool move(std::uint64_t& source,std::uint64_t& destination,std::uint64_t n) noexcept {
    if(n>maximum || source<n || destination>maximum-n)return false;
    if(&source!=&destination){source-=n;destination+=n;}return true;
}
struct FeeRoute {
    bool supported{},invalid{};
    std::size_t count{},end{},source{},destination{},fee{};
    Side side{};
    std::uint64_t input{},fee_before{},source_debit{},destination_credit{},principal{},paid_fee{};
};
bool add(std::uint64_t& total,std::uint64_t value) noexcept {
    if(value>maximum || total>maximum-value)return false;total+=value;return true;
}
FeeRoute fee_route(const Instruction& ix,const ResolvedSolanaAccounts& resolved,
    const std::array<Account,256>& state,const SolanaAccountKey& owner) noexcept {
    FeeRoute route;
    const auto shared=decode_jupiter_shared_route_data(std::span(ix.data).first(ix.data_size));
    const auto direct=decode_jupiter_direct_route_data(std::span(ix.data).first(ix.data_size));
    const bool is_shared=shared.status==JupiterRouteStatus::decoded_data_only;
    if(!is_shared && direct.status!=JupiterRouteStatus::decoded_data_only)return route;
    const auto jupiter=mint("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4"),sol=mint(kSwapSolMint),usdc=mint(kSwapUsdcMint);
    if(ix.account_count<(is_shared?13:9))return route;
    const auto key=[&](std::size_t role)->const SolanaAccountKey&{return resolved.accounts[ix.accounts[role]].key;};
    const auto authority=is_shared?2:1,source=is_shared?3:2,destination=is_shared?6:3;
    const auto fee=is_shared?9:6,event=is_shared?11:7,program=is_shared?12:8,destination_mint=is_shared?8:5;
    if(key(0)!=kSolanaClassicTokenProgram || key(authority)!=owner || !resolved.accounts[ix.accounts[authority]].signer ||
        key(program)!=jupiter || key(event)!=mint("D8cy77BBepLMngZx6ZukaTff5hCt1HrWyKk3Hnd9oitf") ||
        resolved.accounts[ix.accounts[event]].writable || (is_shared?key(10)!=jupiter:key(4)!=jupiter))return route;
    const auto& from=state[ix.accounts[source]];const auto& to=state[ix.accounts[destination]];const auto& fee_account=state[ix.accounts[fee]];
    if(!from.token || !to.token || from.owner!=owner || to.owner!=owner || !fee_account.token || fee_account.mint!=usdc ||
        fee_account.owner==owner || ix.accounts[fee]==ix.accounts[source] || ix.accounts[fee]==ix.accounts[destination] ||
        !resolved.accounts[ix.accounts[source]].writable || !resolved.accounts[ix.accounts[destination]].writable || !resolved.accounts[ix.accounts[fee]].writable ||
        key(destination_mint)!=to.mint || (is_shared && key(7)!=from.mint))return route;
    if(from.mint==usdc && to.mint==sol)route.side=Side::buy;
    else if(from.mint==sol && to.mint==usdc)route.side=Side::sell;
    else return route;
    const auto input=is_shared?shared.data.input_atoms:direct.data.input_atoms;
    const auto output=is_shared?shared.data.quoted_output_atoms:direct.data.quoted_output_atoms;
    const auto bps=is_shared?shared.data.platform_fee_bps:direct.data.platform_fee_bps;
    const auto slippage=is_shared?shared.data.slippage_bps:direct.data.slippage_bps;
    if(!input || input>maximum || !output || output>maximum || bps!=20 || slippage>10000)return route;
    route.supported=true;route.source=ix.accounts[source];route.destination=ix.accounts[destination];route.fee=ix.accounts[fee];route.input=input;route.fee_before=fee_account.atoms;return route;
}
Code replay(const Trace& trace,const ResolvedSolanaAccounts& resolved,const SettlementEvidence& evidence,
    SwapLifecycleReplay& result) noexcept {
    const auto sol=mint(kSwapSolMint),usdc=mint(kSwapUsdcMint);
    std::array<Account,256> state{};
    for(std::size_t i=0;i<evidence.account_count;++i){const auto& before=evidence.accounts[i].before_token;auto& a=state[i];a.lamports=evidence.accounts[i].before_lamports;
        if(before.present){a.token=true;a.owner=before.owner;a.mint=before.mint;a.atoms=before.atoms;a.native=a.mint==sol;
            if(a.mint!=sol && a.mint!=usdc)return Code::unsupported_instruction;
            if(a.owner==evidence.owner && a.native)return Code::unproven_control;}}
    FeeRoute fees;bool owner_flow_outside_route{};
    if(state[0].lamports<evidence.network_fee)return Code::replay_mismatch;state[0].lamports-=evidence.network_fee;
    for(std::size_t n=0;n<trace.count;++n){const auto& ix=trace.instructions[n];const auto& program=resolved.accounts[ix.program].key;
        const bool children=n+1<trace.count && trace.instructions[n+1].height>ix.height;
        for(std::size_t k=0;k<ix.account_count;++k)if(ix.accounts[k]>=evidence.account_count)return Code::malformed_trace;
        const auto writable=[&](std::size_t k){return resolved.accounts[ix.accounts[k]].writable;};
        const auto key=[&](std::size_t k)->const SolanaAccountKey&{return resolved.accounts[ix.accounts[k]].key;};
        const auto account=[&](std::size_t k)->Account&{return state[ix.accounts[k]];};
        if(program==kSolanaComputeBudgetProgram){if(children)return Code::unsupported_instruction;
            const auto decoded=decode_solana_instruction(program,std::span(ix.data).first(ix.data_size),{});
            if(decoded.status!=SolanaInstructionDecodeStatus::decoded)return Code::unsupported_instruction;continue;}
        if(program==kSolanaAssociatedTokenProgram){if(ix.data_size!=1 || ix.data[0]!=1 || ix.account_count!=6 ||
            key(4)!=kSolanaSystemProgram || key(5)!=kSolanaClassicTokenProgram || !writable(0) || !writable(1))return Code::unsupported_instruction;
            if(!children && (!account(1).token || account(1).owner!=key(2) || account(1).mint!=key(3)))return Code::replay_mismatch;
            if(children) {
                unsigned matching_initializations{};
                for(std::size_t j=n+1;j<trace.count && trace.instructions[j].height>ix.height;++j) {
                    const auto& child=trace.instructions[j];
                    if(resolved.accounts[child.program].key==kSolanaClassicTokenProgram && child.data_size && child.data[0]==18) {
                        if(child.data_size!=33 || child.account_count!=2 || child.accounts[0]!=ix.accounts[1] ||
                            child.accounts[1]!=ix.accounts[3] || !std::equal(key(2).begin(),key(2).end(),child.data.begin()+1))return Code::replay_mismatch;
                        ++matching_initializations;
                    }
                }
                if(matching_initializations!=1)return Code::replay_mismatch;
            }
            continue;}
        if(ix.height==1 && program==mint("JUP6LkbZbjS1jKKwapdHNy74zcZ3tLUZoi5QNyVTaV4")) {
            const auto previous_count=fees.count;fees=fee_route(ix,resolved,state,evidence.owner);fees.count=previous_count+1;
            fees.end=n+1;while(fees.end<trace.count && trace.instructions[fees.end].height>1)++fees.end;
        }
        if(program!=kSolanaSystemProgram && program!=kSolanaClassicTokenProgram){
            if(!children)for(std::size_t k=0;k<ix.account_count;++k)if(writable(k))return Code::unsupported_instruction;
            result.opaque_containers=true;continue;}
        if(children)return Code::unsupported_instruction;
        if(program==kSolanaSystemProgram){if(ix.data_size<4)return Code::unsupported_instruction;const auto tag=little(ix,0,4);
            if(tag==2){if(ix.data_size!=12 || ix.account_count!=2 || !writable(0) || !writable(1) || account(0).token ||
                !move(account(0).lamports,account(1).lamports,little(ix,4,8)))return Code::replay_mismatch;
                continue;}
            if(tag==0){if(ix.data_size!=52 || ix.account_count!=2 || !writable(0) || !writable(1) || ix.accounts[0]==ix.accounts[1] ||
                account(0).token || account(1).lamports || account(1).token || account(1).created || little(ix,12,8)!=165 ||
                !std::equal(kSolanaClassicTokenProgram.begin(),kSolanaClassicTokenProgram.end(),ix.data.begin()+20) ||
                !move(account(0).lamports,account(1).lamports,little(ix,4,8)))return Code::replay_mismatch;
                account(1).created=true;++result.created_accounts;continue;}
            return Code::unsupported_instruction;
        }
        if(!ix.data_size)return Code::unsupported_instruction;
        const auto tag=ix.data[0];
        if(tag==21){if((ix.data_size!=1 && !(ix.data_size==3 && ix.data[1]==7 && ix.data[2]==0)) || ix.account_count!=1 || (key(0)!=sol && key(0)!=usdc))return Code::unsupported_instruction;continue;}
        if(tag==22){if(ix.data_size!=1 || ix.account_count!=1 || !writable(0) || !account(0).created || account(0).token)return Code::unsupported_instruction;continue;}
        if(tag==18){if(ix.data_size!=33 || ix.account_count!=2 || !writable(0) || !account(0).created || account(0).token || account(0).closed ||
            (key(1)!=sol && key(1)!=usdc))return Code::unsupported_instruction;
            auto& a=account(0);a.token=true;a.mint=key(1);std::copy_n(ix.data.begin()+1,32,a.owner.begin());a.close=a.owner;a.close_known=true;
            a.native=a.mint==sol;a.unknown_atoms=a.native;a.atoms=0;continue;}
        if(tag==17){if(ix.data_size!=1 || ix.account_count!=1 || !writable(0) || !account(0).token || !account(0).native || !account(0).created)return Code::unsupported_instruction;
            account(0).unknown_atoms=true;continue;}
        if(tag==3 || tag==12){const bool checked=tag==12;const auto dest=checked?2:1,authority=checked?3:2;
            if(ix.data_size!=(checked?10:9) || ix.account_count!=(checked?4:3) || !writable(0) || !writable(dest))return Code::unsupported_instruction;
            auto& from=account(0);auto& to=account(dest);const auto value=little(ix,1,8);
            if(!from.token || !to.token || from.mint!=to.mint || from.owner!=key(authority) ||
                (checked && (key(1)!=from.mint || ix.data[9]!=(from.native?9:6))))return Code::replay_mismatch;
            if(value && ix.accounts[0]!=ix.accounts[dest] && (from.owner==evidence.owner || to.owner==evidence.owner) &&
                (!fees.supported || n>=fees.end))owner_flow_outside_route=true;
            if(fees.supported && ix.accounts[0]!=ix.accounts[dest]) {
                const auto source=ix.accounts[0],destination=ix.accounts[dest];
                if(source==fees.fee)fees.invalid=true;
                if(destination==fees.fee) {
                    if(n>=fees.end || source!=(fees.side==Side::buy?fees.source:fees.destination) || from.mint!=usdc || !add(fees.paid_fee,value))fees.invalid=true;
                }
                if(source==fees.source) {
                    if(n>=fees.end || !add(fees.source_debit,value) || (destination!=fees.fee && !add(fees.principal,value)))fees.invalid=true;
                }
                if(destination==fees.source)fees.invalid=true;
                if(destination==fees.destination) {
                    if(n>=fees.end || !add(fees.destination_credit,value))fees.invalid=true;
                }
                if(source==fees.destination && (fees.side!=Side::sell || destination!=fees.fee))fees.invalid=true;
            }
            if(ix.accounts[0]==ix.accounts[dest])continue;
            if(!from.unknown_atoms && from.atoms<value)return Code::replay_mismatch;
            if(!to.unknown_atoms && (value>maximum || to.atoms>maximum-value))return Code::replay_mismatch;
            if(!from.unknown_atoms)from.atoms-=value;if(!to.unknown_atoms)to.atoms+=value;
            if(from.native && !move(from.lamports,to.lamports,value))return Code::replay_mismatch;continue;}
        if(tag==6){if(ix.account_count!=2 || !writable(0) || !account(0).token || !account(0).created)return Code::unproven_control;
            if(ix.data_size!=35 || ix.data[1]!=3 || ix.data[2]!=1 || !account(0).close_known || account(0).close!=key(1))return Code::unsupported_instruction;
            std::copy_n(ix.data.begin()+3,32,account(0).close.begin());continue;}
        if(tag==9){if(ix.data_size!=1 || ix.account_count!=3 || !writable(0) || !writable(1) || !account(0).token ||
            !account(0).created || !account(0).close_known)return Code::unproven_control;
            if(ix.accounts[0]==ix.accounts[1] || account(0).close!=key(2) || (!account(0).native && account(0).atoms) ||
                !move(account(0).lamports,account(1).lamports,account(0).lamports))return Code::replay_mismatch;
            account(0).token=false;account(0).closed=true;++result.closed_accounts;continue;}
        return Code::unsupported_instruction;
    }
    std::uint64_t before_usdc{},after_usdc{},new_deposits{};
    for(std::size_t i=0;i<evidence.account_count;++i){const auto& expected=evidence.accounts[i];const auto& a=state[i];const auto& after=expected.after_token;
        if(a.lamports!=expected.after_lamports || a.token!=after.present || (a.created && !a.token && !a.closed))return Code::replay_mismatch;
        if(a.token && (a.owner!=after.owner || a.mint!=after.mint || a.unknown_atoms || a.atoms!=after.atoms))return Code::replay_mismatch;
        if(expected.before_token.present && expected.before_token.owner==evidence.owner){if(expected.before_lamports!=expected.after_lamports)return Code::unproven_control;
            if(expected.before_token.mint!=usdc || before_usdc>maximum-expected.before_token.atoms)return Code::unproven_control;
            before_usdc+=expected.before_token.atoms;}
        if(after.present && after.owner==evidence.owner){if(after.mint!=usdc || after_usdc>maximum-after.atoms)return Code::unproven_control;after_usdc+=after.atoms;
            if(a.created){if(!a.close_known)return Code::unproven_control;if(a.close==evidence.owner){if(new_deposits>maximum-a.lamports)return Code::replay_mismatch;new_deposits+=a.lamports;}}}
    }
    const auto& wallet=evidence.accounts[evidence.owner_index];
    result.owner_wallet_lamports=static_cast<std::int64_t>(wallet.after_lamports)-static_cast<std::int64_t>(wallet.before_lamports);
    result.owner_usdc_atoms=static_cast<std::int64_t>(after_usdc)-static_cast<std::int64_t>(before_usdc);
    result.owner_new_refundable_deposits=static_cast<std::int64_t>(new_deposits);
    if(fees.count==1 && fees.supported && !fees.invalid && !owner_flow_outside_route && fees.paid_fee && fees.source_debit==fees.input &&
        state[fees.fee].token && state[fees.fee].atoms>=fees.fee_before && state[fees.fee].atoms-fees.fee_before==fees.paid_fee) {
        result.fee_attribution=SwapFeeAttributionCode::observed_abi_bound_usdc_fee;
        result.route_side=fees.side;result.route_input=fees.input;result.route_source_debit=fees.source_debit;
        result.route_destination_credit=fees.destination_credit;result.route_principal_debit=fees.principal;result.fee_atoms=fees.paid_fee;
    }
    return Code::replayed_effects_only;
}
}
SwapLifecycleReplay replay_swap_settlement_lifecycle(const VerifiedFinalizedRead& read,std::string_view original,
    std::string_view organization,std::string_view signer,std::span<const SolanaLookupEvidence> lookups,
    const SolanaAccountKey& owner) noexcept {
    const auto evidence=decode_settlement_evidence(read,original,organization,signer,lookups,owner);
    if(evidence.code!=SettlementEvidenceCode::observed_transaction_needs_account_lifecycle)return {};
    DecodedSolanaPayload decoded;ResolvedSolanaAccounts resolved;
    if(decode_solana_signing_envelope(original,organization,signer,decoded)!=SolanaParseError::none ||
        resolve_solana_accounts(decoded,organization,signer,lookups,resolved)!=SolanaResolutionError::none)return {};
    Trace instructions;SwapLifecycleReplay result;result.code=trace(read,decoded,resolved.account_count,instructions);
    if(result.code!=Code::replayed_effects_only)return result;
    result.instructions=instructions.count;result.code=replay(instructions,resolved,evidence,result);
    result.response_digest=read.response_digest();result.payload_digest=evidence.original_payload_digest;return result;
}
FailedSwapAccounting account_failed_swap(const VerifiedFinalizedRead& read, std::string_view original,
    std::string_view organization, std::string_view signer, std::span<const SolanaLookupEvidence> lookups,
    const SolanaAccountKey& owner, SwapFailureAccountingPolicy policy) noexcept {
    const auto evidence=decode_settlement_evidence(read,original,organization,signer,lookups,owner);
    if(evidence.code!=SettlementEvidenceCode::observed_failed_transaction)return {};
    if(policy.expected_external_fee_payer==SolanaAccountKey{} || policy.expected_external_fee_payer==owner ||
        !evidence.account_count || evidence.accounts[0].key!=policy.expected_external_fee_payer || evidence.owner_index==0)
        return {FailedSwapAccountingCode::sponsor_unproven,0};
    DecodedSolanaPayload decoded;ResolvedSolanaAccounts resolved;
    if(decode_solana_signing_envelope(original,organization,signer,decoded)!=SolanaParseError::none ||
        resolve_solana_accounts(decoded,organization,signer,lookups,resolved)!=SolanaResolutionError::none ||
        !resolved.accounts[0].signer || !resolved.accounts[0].writable)return {};
    for(std::size_t n=0;n<decoded.instruction_count;++n){const auto& ix=decoded.instructions[n];
        if(resolved.accounts[ix.program_index].key==kSolanaSystemProgram){
            const auto bytes=std::span(decoded.transaction).subspan(ix.data_offset,ix.data_size);
            if(bytes.size()!=12 || bytes[0]!=2 || bytes[1] || bytes[2] || bytes[3])return {FailedSwapAccountingCode::unsupported_transaction,0};
        }
    }
    for(std::size_t n=0;n<evidence.account_count;++n){const auto& a=evidence.accounts[n];const auto& before=a.before_token;const auto& after=a.after_token;
        if(n==0){if(before.present || after.present || a.before_lamports<evidence.network_fee || a.after_lamports!=a.before_lamports-evidence.network_fee)return {FailedSwapAccountingCode::rollback_mismatch,0};}
        else if(a.before_lamports!=a.after_lamports)return {FailedSwapAccountingCode::rollback_mismatch,0};
        if(before.present!=after.present || before.mint!=after.mint || before.owner!=after.owner || before.program!=after.program ||
            before.atoms!=after.atoms || before.decimals!=after.decimals)return {FailedSwapAccountingCode::rollback_mismatch,0};
    }
    return {FailedSwapAccountingCode::observed_external_fee_only,evidence.network_fee};
}
}
