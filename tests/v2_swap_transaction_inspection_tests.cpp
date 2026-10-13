#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/swap_transaction_inspection.hpp"
#include "velocita/v2/solana_resolved_accounts.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace velocita::v2::truemarkets;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

SolanaAccountKey key(std::uint8_t last) {
    SolanaAccountKey result{};
    result.back() = last;
    return result;
}

std::vector<std::uint8_t> unhex(std::string_view hex) {
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    return result;
}

struct Fixture {
    std::vector<std::uint8_t> bytes;
    std::array<SolanaLookupEvidence, 2> tables{};
};

constexpr SolanaAccountKey kJupiterProgram{
    4,121,213,91,242,49,192,110,238,116,197,110,206,104,21,7,
    253,177,178,222,163,244,142,81,2,177,205,162,86,188,19,143};

Fixture fixture() {
    Fixture f;
    f.bytes.push_back(2);
    f.bytes.resize(129, 0);
    f.bytes.insert(f.bytes.end(), {128, 2, 0, 7, 15});
    const std::array<SolanaAccountKey, 15> keys{
        key(1), key(2), key(3), key(4), key(5), key(6), key(7), key(8),
        kSolanaSystemProgram, kSolanaComputeBudgetProgram, kJupiterProgram,
        kSolanaClassicTokenProgram, key(12), kSolanaAssociatedTokenProgram, key(14)};
    for (const auto& value : keys) f.bytes.insert(f.bytes.end(), value.begin(), value.end());
    f.bytes.insert(f.bytes.end(), 32, 42);
    f.bytes.push_back(6);
    const auto instruction = [&](std::uint8_t program, std::initializer_list<std::uint8_t> accounts,
                                 std::string_view hex) {
        f.bytes.push_back(program);
        f.bytes.push_back(static_cast<std::uint8_t>(accounts.size()));
        f.bytes.insert(f.bytes.end(), accounts);
        const auto data = unhex(hex);
        f.bytes.push_back(static_cast<std::uint8_t>(data.size()));
        f.bytes.insert(f.bytes.end(), data.begin(), data.end());
    };
    instruction(9, {}, "02c05c1500");
    instruction(9, {}, "030417010000000000");
    instruction(13, {0,5,1,26,8,11}, "01");
    instruction(10, {11,12,1,3,4,2,5,29,26,6,10,14,10,32,12,30,29,18,21,25,
                      20,23,24,19,22,7,4,11,11,28,12,17,15,16,2,7,11,11,27,31,10},
                "c1209b3341d69c810602000000a100010000006400018d0064010240420f0000000000e8418c0000000000f40114");
    instruction(11, {5,1,1}, "09");
    instruction(8, {1,0}, "02000000f01d1f0000000000");
    f.bytes.push_back(2);
    for (std::size_t i = 0; i < f.tables.size(); ++i) {
        auto& table = f.tables[i];
        table.table = key(static_cast<std::uint8_t>(240 + i));
        table.observed_slot = 777;
        table.evidence_digest.bytes[0] = static_cast<std::byte>(i + 1);
        table.address_count = 9;
        for (std::uint8_t j = 0; j < 9; ++j)
            table.addresses[j] = key(static_cast<std::uint8_t>(101 + i * 9 + j));
        f.bytes.insert(f.bytes.end(), table.table.begin(), table.table.end());
        const std::uint8_t writable = i == 0 ? 3 : 8;
        f.bytes.push_back(writable);
        for (std::uint8_t j = 0; j < writable; ++j) f.bytes.push_back(j);
        f.bytes.push_back(static_cast<std::uint8_t>(9 - writable));
        for (std::uint8_t j = writable; j < 9; ++j) f.bytes.push_back(j);
    }
    f.tables[0].addresses[3]=SolanaAccountKey{6,155,136,87,254,171,129,132,251,104,127,99,70,24,192,53,218,196,57,220,26,235,59,85,152,160,240,0,0,0,0,1};
    f.tables[0].addresses[6]=SolanaAccountKey{198,250,122,243,190,219,173,58,61,101,243,106,171,201,116,49,177,187,228,194,210,246,224,228,124,166,2,3,69,47,93,97};
    return f;
}

const std::string kSigner = std::string(31, '1') + '3';

std::string envelope(const Fixture& f) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex;
    for (const auto b : f.bytes) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    return "{\"organizationId\":\"pipeline-fixture\",\"parameters\":{\"signWith\":\"" + kSigner +
        "\",\"type\":\"TRANSACTION_TYPE_SOLANA\",\"unsignedTransaction\":\"" + hex +
        "\"},\"timestampMs\":\"1000\",\"type\":\"ACTIVITY_TYPE_SIGN_TRANSACTION_V2\"}";
}

void decode(const Fixture& f, DecodedSolanaPayload& decoded, ResolvedSolanaAccounts& resolved) {
    require(decode_solana_signing_envelope(envelope(f), "pipeline-fixture", kSigner, decoded) ==
                SolanaParseError::none, "pipeline envelope decode");
    require(resolve_solana_accounts(decoded, "pipeline-fixture", kSigner, f.tables, resolved) ==
                SolanaResolutionError::none, "pipeline account resolution");
}


CapturedBuyProfile profile(const Fixture& f) {
    DecodedSolanaPayload d;ResolvedSolanaAccounts r;decode(f,d,r);
    CapturedBuyProfile p;
    for(std::size_t i=0;i<33;++i)p.account_keys[i]=r.accounts[i].key;
    p.intent.side=velocita::v2::Side::buy;p.intent.effect=velocita::v2::EffectId{1};
    p.intent.quote_input=velocita::v2::swap::QuoteAtoms{1'000'000};
    p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{1'002'000};
    p.intent.fee_cap=velocita::v2::swap::QuoteAtoms{2'000};
    p.intent.min_base_receive=velocita::v2::swap::BaseAtoms{9'000'000};
    p.now_ms=1001;p.max_envelope_age_ms=100;p.max_compute_units=1'400'000;
    p.max_compute_price=71'428;p.max_slippage_bps=500;p.max_platform_fee_bps=20;
    return p;
}
CapturedBuyInspection inspect(const Fixture& f,const CapturedBuyProfile& p) {
    return inspect_captured_buy_transaction(envelope(f),"pipeline-fixture",kSigner,p,f.tables);
}
void oracle() {
    auto f=fixture();const auto p=profile(f);auto r=inspect(f,p);
    require(r.code==TransactionInspectionCode::matched_capture_shape_only,"capture_shape_inspected");
    require(r.explicit_owner_payment==2'039'280 && r.nominal_output_after_slippage==8'732'317,
        "hand_worked_payment_and_nominal_ceiling");
    require(r.priority_fee_upper_lamports==100'000 && r.compute_price==71'428 && r.compute_units==1'400'000,
        "checked_compute_fee_ceiling");
    require(r.blockers==(route_economics_unproven|account_lifecycle_unproven|cpi_semantics_unproven|
        committed_authority_unproven|chain_validity_unproven|lookup_provenance_unproven|auxiliary_sol_payment_conflict|nominal_slippage_shortfall),
        "structure_never_implies_signing_authority");
    require(!r.envelope_digest.is_zero() && !r.accounts.transaction_digest.is_zero() &&
        r.route.input_atoms==1'000'000 && r.accounts.account_count==33,"owning_report_retains_payload_binding");
}
void profile_bounds() {
    const auto f=fixture();auto p=profile(f);
    p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{1'002'001};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"one_atom_unaccounted_debit_refused");
    p=profile(f);p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{1'001'999};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"one_atom_underreserved_fee_refused");
    p=profile(f);p.intent.base_input=velocita::v2::swap::BaseAtoms{1};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"buy_with_sell_base_input_refused");
    p=profile(f);p.intent.min_quote_receive=velocita::v2::swap::QuoteAtoms{1};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"buy_with_sell_quote_output_refused");
    p=profile(f);
    p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{std::uint64_t{1}<<63};
    p.intent.fee_cap=velocita::v2::swap::QuoteAtoms{(std::uint64_t{1}<<63)-1'000'000};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"debit_above_signed_money_range_refused");
    p=profile(f);
    p.intent.fee_cap=velocita::v2::swap::QuoteAtoms{std::numeric_limits<std::uint64_t>::max()};
    p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{999'999};
    require(inspect(f,p).code==TransactionInspectionCode::invalid_profile,"wrapped_quote_plus_fee_refused");
    p=profile(f);
    p.intent.max_quote_debit=velocita::v2::swap::QuoteAtoms{std::numeric_limits<std::int64_t>::max()};
    p.intent.fee_cap=velocita::v2::swap::QuoteAtoms{std::numeric_limits<std::int64_t>::max()-1'000'000};
    require(inspect(f,p).code==TransactionInspectionCode::matched_capture_shape_only,
        "largest_consistent_profile_remains_shape_only");
}
void identity_and_time() {
    auto wrong=fixture();wrong.tables[0].addresses[3]=key(104);
    require(inspect(wrong,profile(wrong)).code==TransactionInspectionCode::invalid_profile,"wrong_base_mint_refused");
    wrong=fixture();wrong.tables[0].addresses[6]=key(199);
    require(inspect(wrong,profile(wrong)).code==TransactionInspectionCode::invalid_profile,"wrong_quote_mint_refused");
    auto f=fixture();auto p=profile(f);p.account_keys[1]=key(199);
    require(inspect(f,p).code==TransactionInspectionCode::identity_mismatch,"owner_identity_pinned");
    p=profile(f);p.account_keys[6]=key(199);
    require(inspect(f,p).code==TransactionInspectionCode::identity_mismatch,"fee_recipient_pinned");
    p=profile(f);p.now_ms=1101;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_envelope,"expired_envelope_refused");
    p.now_ms=999;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_envelope,"future_envelope_refused");
    p=profile(f);f.bytes[1]=1;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_envelope,"preexisting_signature_not_blindly_trusted");
}
void topology_and_privileges() {
    auto f=fixture();const auto p=profile(f);DecodedSolanaPayload d;ResolvedSolanaAccounts r;decode(f,d,r);
    f.bytes[d.instructions[5].accounts_offset+1]=7;
    require(inspect(f,p).code==TransactionInspectionCode::unsupported_topology,"reimbursement_recipient_pinned");
    f=fixture();f.bytes[d.instructions[4].accounts_offset+1]=0;
    require(inspect(f,p).code==TransactionInspectionCode::unsupported_topology,"close_destination_pinned");
    f=fixture();f.bytes[d.instructions[3].accounts_offset+10]=11;
    require(inspect(f,p).code==TransactionInspectionCode::unsupported_topology,"optional_slot_not_shifted");
    f=fixture();f.bytes[d.instructions[3].accounts_offset+35]=6;
    require(inspect(f,p).code==TransactionInspectionCode::unsupported_topology,"remaining_accounts_exact");
    f=fixture();f.bytes[131]=1;
    require(inspect(f,p).code==TransactionInspectionCode::identity_mismatch,"readonly_owner_refused");
}
void route_bounds() {
    auto f=fixture();auto p=profile(f);DecodedSolanaPayload d;ResolvedSolanaAccounts r;decode(f,d,r);
    const auto at=d.instructions[3].data_offset;
    f.bytes[at+13]=200;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_route,"unknown_variant_refused");
    f=fixture();f.bytes[at+21]=2;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_route,"route_connectivity_exact");
    f=fixture();f.bytes[at+27]=0x41;
    require(inspect(f,p).code==TransactionInspectionCode::route_bound_mismatch,"one_input_atom_mismatch");
    f=fixture();p.max_slippage_bps=499;
    require(inspect(f,p).code==TransactionInspectionCode::route_bound_mismatch,"slippage_cap_enforced");
    p=profile(f);p.max_platform_fee_bps=19;
    require(inspect(f,p).code==TransactionInspectionCode::route_bound_mismatch,"platform_fee_field_cap_enforced");
    p=profile(f);p.max_compute_price=71'427;
    require(inspect(f,p).code==TransactionInspectionCode::invalid_instruction,"compute_price_cap_enforced");
}
void shape_only() {
    auto f=fixture();auto p=profile(f);DecodedSolanaPayload d;ResolvedSolanaAccounts r;decode(f,d,r);
    const auto pay=d.instructions[5].data_offset;
    for(unsigned i=4;i<12;++i)f.bytes[pay+i]=0;
    p.intent.min_base_receive=velocita::v2::swap::BaseAtoms{8'732'317};
    auto result=inspect(f,p);
    require(result.code==TransactionInspectionCode::matched_capture_shape_only &&
        result.blockers==(route_economics_unproven|account_lifecycle_unproven|cpi_semantics_unproven|committed_authority_unproven|chain_validity_unproven|lookup_provenance_unproven),
        "zero_payment_still_has_no_economic_or_signing_authority");
    f=fixture();f.tables[1].addresses[0]=key(1);
    require(inspect(f,p).code==TransactionInspectionCode::invalid_accounts,"lookup_alias_refused");
}
}
int main(int argc,char** argv) {
    int failures{},ran{};
    for(auto [name,fn]:std::array<std::pair<const char*,void(*)()>,6>{{{"oracle",oracle},{"profile",profile_bounds},{"identity",identity_and_time},
            {"topology",topology_and_privileges},{"bounds",route_bounds},{"shape",shape_only}}}) {
        if(argc>1 && std::string_view(argv[1])!=name)continue;
        ++ran;try {fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    return !ran ? 2 : failures ? 1 : 0;
}
