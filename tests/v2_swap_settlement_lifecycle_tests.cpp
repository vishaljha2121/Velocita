#include "swap_settlement_test_fixture.hpp"
using namespace settlement_lifecycle_fixture;
namespace {
void captured_shape_arithmetic(){LifecycleFixture f;auto r=f.replay();require(r.code==SwapLifecycleCode::replayed_effects_only,"WSOL_create_transfer_close_replayed");
    require(r.owner_wallet_lamports==8860117 && r.owner_usdc_atoms==-1000000 && r.owner_new_refundable_deposits==0,"owner_receipt_includes_550840_repayment_cost");
    require(r.created_accounts==1 && r.closed_accounts==1 && r.instructions==10 && r.opaque_containers,"trace_counts_and_opaque_route_remain_explicit");}
void missing_or_unexplained(){for(unsigned mode=0;mode<10;++mode){LifecycleFixture f;
    if(mode==0)replace(f.json,"\"innerInstructions\":[","\"missingInnerInstructions\":[");
    if(mode==1)replace(f.json,"\"stackHeight\":2","\"stackHeight\":3");
    if(mode==2){f.route[1].data=amount(3,997999);f.rebuild();}
    if(mode==3){f.top[3].data.back()=1;f.rebuild();}
    if(mode==4){f.route[0].data={5};f.rebuild();}
    if(mode==5){f.create[2].data[1]=1;f.rebuild();}
    if(mode==6){f.route[0].accounts[2]=13;f.rebuild();}
    if(mode==7){f.route[0].program=10;f.rebuild();}
    if(mode==8){f.top[3].data[4]++;f.rebuild();}
    if(mode==9){f.route.push_back({10,{2}, {42}});f.rebuild();}
    require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"missing_trace_unknown_authority_or_unexplained_atoms_refused");}}
void existing_authority_change(){LifecycleFixture f;std::vector<std::uint8_t> change{6,3,1};change.insert(change.end(),f.keys[0].begin(),f.keys[0].end());f.route.insert(f.route.begin(),{8,{2,1},change});f.rebuild();require(f.replay().code==SwapLifecycleCode::unproven_control,"existing_close_authority_change_not_inferred_from_balances");}

void supported_checked_and_sync(){LifecycleFixture f;
    f.top[1].accounts.push_back(11);f.route[0]={8,{2,11,4,1},amount(12,2000)};f.route[0].data.push_back(6);
    f.create.push_back({8,{5},{17}});f.rebuild();
    require(f.replay().code==SwapLifecycleCode::replayed_effects_only,"checked_transfer_and_new_native_sync_supported");
    f.route[0].data.back()=9;f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"checked_transfer_wrong_decimals_refused");
}
void lifetime_and_program_edges(){for(unsigned mode=0;mode<7;++mode){LifecycleFixture f;
    if(mode==0)f.create[0].data[12]=164;
    if(mode==1)f.create[0].data[20]^=1;
    if(mode==2)f.top[2].accounts[2]=0;
    if(mode==3)f.top[2].accounts[1]=5;
    if(mode==4)f.route[2].accounts[0]=3;
    if(mode==5){f.create[1].data.push_back(0);}
    if(mode==6){f.top[3].data[0]=4;}
    f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"unsupported_lifetime_layout_mint_or_close_refused");}}
void trace_shape(){for(unsigned mode=0;mode<6;++mode){LifecycleFixture f;
    if(mode==0)replace(f.json,"\"index\":1","\"index\":0");
    if(mode==1)replace(f.json,"\"stackHeight\":2","\"stackHeight\":null");
    if(mode==2)replace(f.json,"\"programIdIndex\":7","\"programIdIndex\":256");
    if(mode==3)replace(f.json,"\"accounts\":[0,5]","\"accounts\":[0,256]");
    if(mode==4)replace(f.json,"\"data\":\"","\"data\":\"0");
    if(mode==5)replace(f.json,"\"stackHeight\":2","\"stackHeight\":3");
    require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"trace_index_height_and_encoding_must_be_exact");}}

void new_usdc_deposit_control(){for(unsigned external=0;external<2;++external){LifecycleFixture f;
    auto funding=f.create[0];funding.accounts={0,4};auto init=f.create[2];init.accounts={4,11};
    f.route.insert(f.route.begin(),init);f.route.insert(f.route.begin(),funding);
    if(external){std::vector<std::uint8_t> authority{6,3,1};authority.insert(authority.end(),f.keys[0].begin(),f.keys[0].end());f.route.insert(f.route.begin()+2,{8,{4,1},authority});}
    f.rebuild();replace(f.json,"\"preBalances\":[30000000,10000000,1488440,1488440,1488440,0","\"preBalances\":[30000000,10000000,1488440,1488440,0,0");
    replace(f.json,"\"postBalances\":[30512840","\"postBalances\":[29024400");replace(f.json,f.tb(4,9,0)+",","");replace(f.json,f.tb(4,9,2000),f.tb(4,2,2000));
    const auto r=f.replay();require(r.code==SwapLifecycleCode::replayed_effects_only,"created_USDC_close_control_replayed");
    require(r.owner_new_refundable_deposits==(external?0:1488440) && r.owner_usdc_atoms==-998000,"sponsor_close_control_excludes_owner_rent");
    OwnerPropertyMeasurement m;m.code=OwnerAccountingCode::measured_declared_scope_only;m.base_change_excluding_deposits=r.owner_wallet_lamports;
    m.deposit_change=r.owner_new_refundable_deposits;m.sol_change=m.base_change_excluding_deposits+m.deposit_change;m.usdc_change=r.owner_usdc_atoms;
    swap::Intent intent;intent.side=Side::buy;intent.quote_input=swap::QuoteAtoms{998000};intent.fee_cap=swap::QuoteAtoms{2000};intent.max_quote_debit=swap::QuoteAtoms{1000000};intent.min_base_receive=swap::BaseAtoms{9000000};
    require(compare_observed_swap_bounds(intent,m)==ObservedSwapBounds::receipt_shortfall,"new_deposit_cannot_hide_actual_base_shortfall");
}}
void observed_size_request(){LifecycleFixture f;f.create.insert(f.create.begin(),{8,{12},{21,7,0}});f.rebuild();require(f.replay().code==SwapLifecycleCode::replayed_effects_only,"observed_classic_size_request_supported");f.create[0].data[1]=8;f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"unknown_size_extension_refused");}
void readonly_event_leaf(){LifecycleFixture f;f.route.push_back({10,{13},{228,69,165,46,81,203,154,29}});f.rebuild();require(f.replay().code==SwapLifecycleCode::replayed_effects_only,"readonly_event_leaf_has_no_account_effect");f.route.back().accounts={2};f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"writable_unknown_leaf_still_refused");}
void cpi_account_scope(){LifecycleFixture f;f.route.insert(f.route.begin(),{10,{13,8},{42},2});f.route[1].height=3;f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"readonly_parent_cannot_hide_writable_descendant");}
void ata_identity(){LifecycleFixture f;f.top[0].accounts[2]=0;f.rebuild();require(f.replay().code!=SwapLifecycleCode::replayed_effects_only,"ATA_wallet_and_child_initialization_must_agree");}
void net_receipt_bound(){LifecycleFixture f;const auto replay=f.replay();
    OwnerPropertyMeasurement m;m.code=OwnerAccountingCode::measured_declared_scope_only;m.sol_change=replay.owner_wallet_lamports;m.base_change_excluding_deposits=m.sol_change;m.usdc_change=replay.owner_usdc_atoms;
    swap::Intent intent;intent.side=Side::buy;intent.quote_input=swap::QuoteAtoms{998000};intent.fee_cap=swap::QuoteAtoms{2000};intent.max_quote_debit=swap::QuoteAtoms{1000000};intent.min_base_receive=swap::BaseAtoms{9000000};
    require(compare_observed_swap_bounds(intent,m)==ObservedSwapBounds::receipt_shortfall,"gross_output_cannot_mask_reimbursement_shortfall");
}
}
int main(int argc,char** argv){const std::pair<const char*,void(*)()> tests[]={{"captured_shape_arithmetic",captured_shape_arithmetic},{"missing_or_unexplained",missing_or_unexplained},{"existing_authority_change",existing_authority_change},{"supported_checked_and_sync",supported_checked_and_sync},{"lifetime_and_program_edges",lifetime_and_program_edges},{"trace_shape",trace_shape},{"net_receipt_bound",net_receipt_bound},{"ata_identity",ata_identity},{"new_usdc_deposit_control",new_usdc_deposit_control},{"observed_size_request",observed_size_request},{"readonly_event_leaf",readonly_event_leaf},{"cpi_account_scope",cpi_account_scope}};
for(auto [name,test]:tests){if(argc==2 && std::string_view(argv[1])!=name)continue;try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}}
