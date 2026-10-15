#include "velocita/v2/solana_instruction_decode.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace velocita::v2::truemarkets;
namespace {
using Status = SolanaInstructionDecodeStatus;
using Account = SolanaInstructionAccount;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Account account(std::uint8_t id, bool signer = false, bool writable = false) {
    Account a;
    a.key[31] = id;
    a.signer = signer;
    a.writable = writable;
    return a;
}
std::array<Account, 6> ata_accounts() {
    return {account(1,true,true), account(2,false,true), account(3), account(4),
            Account{kSolanaSystemProgram}, Account{kSolanaClassicTokenProgram}};
}
std::array<Account, 3> close_accounts() {
    return {account(5,false,true), account(1,true,true), account(1,true,true)};
}
std::array<Account, 2> transfer_accounts() {
    return {account(1,true,true), account(2,false,true)};
}
constexpr std::array<std::uint8_t, 12> rent_bytes{2,0,0,0,240,29,31,0,0,0,0,0};
std::string base58(SolanaInstructionKey bytes) {
    constexpr std::string_view alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::size_t leading = 0;
    while (leading < bytes.size() && bytes[leading] == 0) ++leading;
    std::string digits;
    for (std::size_t start = leading; start < bytes.size();) {
        unsigned remainder = 0;
        for (std::size_t i = start; i < bytes.size(); ++i) {
            const unsigned value = remainder * 256 + bytes[i];
            bytes[i] = static_cast<std::uint8_t>(value / 58);
            remainder = value % 58;
        }
        digits.insert(digits.begin(), alphabet[remainder]);
        while (start < bytes.size() && bytes[start] == 0) ++start;
    }
    return std::string(leading, '1') + digits;
}
void official_program_ids() {
    require(base58(kSolanaSystemProgram) == "11111111111111111111111111111111", "system_program_id");
    require(base58(kSolanaComputeBudgetProgram) == "ComputeBudget111111111111111111111111111111", "compute_program_id");
    require(base58(kSolanaAssociatedTokenProgram) == "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL", "ata_program_id");
    require(base58(kSolanaClassicTokenProgram) == "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA", "token_program_id");
}
void refusal(const SolanaInstructionDecodeResult& r, Status status, const char* name) {
    require(r.status == status && std::holds_alternative<std::monostate>(r.body), name);
}
void compute_oracles() {
    const std::array<std::uint8_t, 5> limit{2,1,2,3,4};
    auto r = decode_solana_instruction(kSolanaComputeBudgetProgram, limit, {});
    require(r.status == Status::decoded && std::get_if<ComputeUnitLimit>(&r.body) &&
            std::get<ComputeUnitLimit>(r.body).units == 67305985U, "compute_limit_little_endian");
    const std::array<std::uint8_t, 9> price{3,8,7,6,5,4,3,2,1};
    r = decode_solana_instruction(kSolanaComputeBudgetProgram, price, {});
    require(r.status == Status::decoded && std::get_if<ComputeUnitPrice>(&r.body) &&
            std::get<ComputeUnitPrice>(r.body).micro_lamports == 72623859790382856ULL,
            "compute_price_little_endian");
    auto maximum = price;
    maximum.fill(255); maximum[0] = 3;
    r = decode_solana_instruction(kSolanaComputeBudgetProgram, maximum, {});
    require(r.status == Status::decoded && std::get<ComputeUnitPrice>(r.body).micro_lamports ==
            std::numeric_limits<std::uint64_t>::max(), "compute_price_no_signed_narrowing");
    const std::array<std::uint8_t, 5> zero{2,0,0,0,0};
    r = decode_solana_instruction(kSolanaComputeBudgetProgram, zero, {});
    require(r.status == Status::decoded && std::get<ComputeUnitLimit>(r.body).units == 0,
            "zero_is_structural_not_economic_policy");
}
void typed_roles_and_ownership() {
    const std::array<std::uint8_t, 1> ata{1}, close{9};
    auto accounts = ata_accounts();
    auto r = decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, accounts);
    require(r.status == Status::decoded && std::get_if<CreateAssociatedTokenIdempotent>(&r.body),
            "ata_decodes_typed_operation");
    const auto expected = CreateAssociatedTokenIdempotent{
        accounts[0],accounts[1],accounts[2],accounts[3],accounts[4],accounts[5]};
    accounts = {};
    require(std::get<CreateAssociatedTokenIdempotent>(r.body) == expected,
            "ata_roles_owned_in_original_order");
    auto closes = close_accounts();
    r = decode_solana_instruction(kSolanaClassicTokenProgram, close, closes);
    require(r.status == Status::decoded && std::get_if<CloseTokenAccount>(&r.body) &&
            std::get<CloseTokenAccount>(r.body) == CloseTokenAccount{closes[0],closes[1],closes[2]},
            "close_allows_destination_authority_alias");
    auto transfers = transfer_accounts();
    r = decode_solana_instruction(kSolanaSystemProgram, rent_bytes, transfers);
    require(r.status == Status::decoded && std::get_if<TransferLamports>(&r.body) &&
            std::get<TransferLamports>(r.body) == TransferLamports{transfers[0],transfers[1],2039280},
            "system_rent_transfer_exact_amount_and_roles");
    transfers[1] = transfers[0];
    r = decode_solana_instruction(kSolanaSystemProgram, rent_bytes, transfers);
    require(r.status == Status::decoded, "self_transfer_is_decoded_not_authorized");
    auto maximum = rent_bytes;
    for (std::size_t i = 4; i < maximum.size(); ++i) maximum[i] = 255;
    r = decode_solana_instruction(kSolanaSystemProgram, maximum, transfers);
    require(r.status == Status::decoded && std::get<TransferLamports>(r.body).lamports ==
            std::numeric_limits<std::uint64_t>::max(), "transfer_no_signed_narrowing");
    accounts = ata_accounts();
    for (auto& a : accounts) { a.signer = true; a.writable = true; }
    require(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, accounts).status == Status::decoded,
            "message_privilege_union_does_not_require_readonly");
}
void exact_shapes() {
    struct Case { SolanaInstructionKey program; std::vector<std::uint8_t> bytes; std::vector<Account> accounts; };
    const auto a = ata_accounts(); const auto c = close_accounts(); const auto t = transfer_accounts();
    const std::array<Case, 5> cases{{
        {kSolanaComputeBudgetProgram,{2,1,2,3,4},{}},
        {kSolanaComputeBudgetProgram,{3,8,7,6,5,4,3,2,1},{}},
        {kSolanaAssociatedTokenProgram,{1},{a.begin(),a.end()}},
        {kSolanaClassicTokenProgram,{9},{c.begin(),c.end()}},
        {kSolanaSystemProgram,{rent_bytes.begin(),rent_bytes.end()},{t.begin(),t.end()}}
    }};
    for (const auto& item : cases) {
        for (std::size_t n = 0; n < item.bytes.size(); ++n)
            refusal(decode_solana_instruction(item.program, {item.bytes.data(),n}, item.accounts),
                    Status::invalid, "every_truncation_refused");
        auto bytes = item.bytes; bytes.push_back(0);
        refusal(decode_solana_instruction(item.program, bytes, item.accounts), Status::invalid,
                "trailing_instruction_data_refused");
        auto accounts = item.accounts; accounts.push_back(account(99,true,true));
        refusal(decode_solana_instruction(item.program, item.bytes, accounts), Status::invalid,
                "extra_accounts_including_multisig_refused");
        if (!item.accounts.empty()) {
            accounts = item.accounts; accounts.pop_back();
            refusal(decode_solana_instruction(item.program, item.bytes, accounts), Status::invalid,
                    "missing_instruction_accounts_refused");
        }
    }
}
void privileges_and_fixed_programs() {
    const std::array<std::uint8_t, 1> ata{1}, close{9};
    auto a = ata_accounts();
    a[0].signer = false;
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, a), Status::invalid, "ata_funder_signer_required");
    a = ata_accounts(); a[0].writable = false;
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, a), Status::invalid, "ata_funder_writable_required");
    a = ata_accounts(); a[1].writable = false;
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, a), Status::invalid, "ata_account_writable_required");
    a = ata_accounts(); a[4].key[31] = 99;
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, a), Status::invalid, "ata_system_program_identity_required");
    a = ata_accounts(); a[5].key[31] ^= 1;
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, ata, a), Status::unsupported, "ata_classic_token_program_only");
    auto c = close_accounts(); c[2] = account(3);
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, close, c), Status::invalid, "close_authority_signature_required");
    c = close_accounts(); c[0].writable = false;
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, close, c), Status::invalid, "close_source_writable_required");
    c = close_accounts(); c[1] = account(4);
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, close, c), Status::invalid, "close_destination_writable_required");
    c = close_accounts(); c[2].writable = false;
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, close, c), Status::invalid, "alias_privileges_must_be_consistent");
    auto t = transfer_accounts(); t[0].signer = false;
    refusal(decode_solana_instruction(kSolanaSystemProgram, rent_bytes, t), Status::invalid, "transfer_source_signature_required");
    t = transfer_accounts(); t[0].writable = false;
    refusal(decode_solana_instruction(kSolanaSystemProgram, rent_bytes, t), Status::invalid, "transfer_source_writable_required");
    t = transfer_accounts(); t[1].writable = false;
    refusal(decode_solana_instruction(kSolanaSystemProgram, rent_bytes, t), Status::invalid, "transfer_destination_writable_required");
}
void sync_native_wraps_one_writable_account() {
    const std::array<std::uint8_t, 1> sync{17};
    const std::array<Account, 1> wsol{account(7, false, true)};
    auto r = decode_solana_instruction(kSolanaClassicTokenProgram, sync, wsol);
    require(r.status == Status::decoded && std::get_if<SyncNativeToken>(&r.body) &&
            std::get<SyncNativeToken>(r.body) == SyncNativeToken{wsol[0]}, "sync_native_decodes_its_account");
    const std::array<std::uint8_t, 2> longer{17,0};
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, longer, wsol), Status::invalid, "sync_native_extra_data_refused");
    const std::array<Account, 1> readonly{account(7, false, false)};
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, sync, readonly), Status::invalid, "sync_native_needs_writable_account");
    const std::array<Account, 2> two{account(7, false, true), account(8, false, true)};
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, sync, two), Status::invalid, "sync_native_takes_one_account");
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, sync, std::span<const Account>{}), Status::invalid, "sync_native_needs_an_account");
}
void unsupported_never_returns_a_body() {
    const std::array<std::uint8_t, 1> close{9}, authority{6}, create{0};
    const auto c = close_accounts(); const auto a = ata_accounts();
    refusal(decode_solana_instruction(account(99).key, close, c), Status::unsupported, "unknown_program_refused");
    const SolanaInstructionKey token2022{6,221,246,225,238,117,143,222,24,66,93,188,228,108,205,218,
        182,26,252,77,131,185,13,39,254,189,249,40,216,161,139,252};
    refusal(decode_solana_instruction(token2022, close, c), Status::unsupported, "token2022_program_refused");
    refusal(decode_solana_instruction(kSolanaClassicTokenProgram, authority, c), Status::unsupported, "authority_change_refused");
    refusal(decode_solana_instruction(kSolanaAssociatedTokenProgram, create, a), Status::unsupported, "non_idempotent_create_refused");
    auto nonce = rent_bytes; nonce[0] = 4;
    refusal(decode_solana_instruction(kSolanaSystemProgram, nonce, transfer_accounts()), Status::unsupported, "nonce_instruction_refused");
    nonce = rent_bytes; nonce[1] = 1;
    refusal(decode_solana_instruction(kSolanaSystemProgram, nonce, transfer_accounts()), Status::unsupported, "system_tag_uses_all_four_bytes");
    const std::array<std::uint8_t, 5> heap{1,0,4,0,0};
    refusal(decode_solana_instruction(kSolanaComputeBudgetProgram, heap, {}), Status::unsupported, "unknown_compute_operation_refused");
}
}
int main(int argc, char** argv) {
    const std::array cases{
        std::pair{"official_program_ids",official_program_ids},
        std::pair{"compute_oracles",compute_oracles},
        std::pair{"typed_roles_and_ownership",typed_roles_and_ownership},
        std::pair{"exact_shapes",exact_shapes},
        std::pair{"privileges_and_fixed_programs",privileges_and_fixed_programs},
        std::pair{"sync_native_wraps_one_writable_account",sync_native_wraps_one_writable_account},
        std::pair{"unsupported_never_returns_a_body",unsupported_never_returns_a_body}};
    try {
        bool selected = false;
        for (const auto& [name, test] : cases) {
            if (argc == 1 || std::string_view(argv[1]) == name) {
                selected = true; test(); std::cout << "PASS " << name << '\n';
            }
        }
        require(selected, "unknown_test_name");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}
