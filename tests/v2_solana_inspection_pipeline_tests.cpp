#include "velocita/v2/solana_instruction_decode.hpp"
#include "velocita/v2/solana_resolved_accounts.hpp"

#include <algorithm>
#include <array>
#include <iostream>
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

SolanaInstructionDecodeResult inspect(const DecodedSolanaPayload& decoded,
                                     const ResolvedSolanaAccounts& resolved, std::size_t n) {
    const auto& ix = decoded.instructions.at(n);
    std::array<SolanaInstructionAccount, 256> accounts{};
    require(ix.accounts_count <= accounts.size(), "pipeline account capacity");
    for (std::size_t j = 0; j < ix.accounts_count; ++j) {
        const auto index = decoded.transaction.at(ix.accounts_offset + j);
        require(index < resolved.account_count, "pipeline index bound");
        const auto& account = resolved.accounts[index];
        accounts[j] = {account.key, account.signer, account.writable};
    }
    return decode_solana_instruction(resolved.accounts.at(ix.program_index).key,
        std::span(decoded.transaction).subspan(ix.data_offset, ix.data_size),
        std::span(accounts).first(ix.accounts_count));
}

void six_instruction_topology() {
    const auto f = fixture();
    require(f.bytes.size() == 878, "hand-counted synthetic transaction is 878 bytes");
    DecodedSolanaPayload decoded;
    ResolvedSolanaAccounts resolved;
    decode(f, decoded, resolved);
    require(resolved.account_count == 33 && resolved.lookup_count == 2,
            "pipeline complete 33-key map");
    require(resolved.accounts[25].key == key(117) && resolved.accounts[25].writable &&
            resolved.accounts[26].key == key(104) && !resolved.accounts[26].writable &&
            resolved.accounts[32].key == key(118), "pipeline cross-table loaded order");
    require(std::get<ComputeUnitLimit>(inspect(decoded, resolved, 0).body).units == 1'400'000,
            "pipeline exact compute limit");
    require(std::get<ComputeUnitPrice>(inspect(decoded, resolved, 1).body).micro_lamports == 71'428,
            "pipeline exact compute price");
    const auto ata = std::get<CreateAssociatedTokenIdempotent>(inspect(decoded, resolved, 2).body);
    require(ata.funder.key == key(1) && ata.associated_account.key == key(6) &&
            ata.wallet.key == key(2) && ata.mint.key == key(104), "pipeline ATA roles and loaded mint");
    const auto route = inspect(decoded, resolved, 3);
    require(route.status == SolanaInstructionDecodeStatus::unsupported &&
            std::holds_alternative<std::monostate>(route.body), "pipeline unknown route remains unsupported");
    const auto close = std::get<CloseTokenAccount>(inspect(decoded, resolved, 4).body);
    require(close.account.key == key(6) && close.destination.key == key(2) &&
            close.authority == close.destination && close.authority.signer,
            "pipeline real-shaped close destination-authority alias");
    const auto payment = std::get<TransferLamports>(inspect(decoded, resolved, 5).body);
    require(payment.source.key == key(2) && payment.destination.key == key(1) &&
            payment.lamports == 2'039'280, "pipeline explicit owner-to-sponsor transfer");
}

void message_privileges_reach_decoder() {
    auto f = fixture();
    f.bytes[131] = 1;
    DecodedSolanaPayload decoded;
    ResolvedSolanaAccounts resolved;
    decode(f, decoded, resolved);
    require(resolved.accounts[1].signer && !resolved.accounts[1].writable,
            "pipeline readonly owner header decoded");
    require(inspect(decoded, resolved, 5).status == SolanaInstructionDecodeStatus::invalid,
            "pipeline readonly transfer source refused");
    require(inspect(decoded, resolved, 4).status == SolanaInstructionDecodeStatus::invalid,
            "pipeline readonly close destination refused");
}

void changed_lookup_cannot_be_hidden_by_offsets() {
    auto f = fixture();
    DecodedSolanaPayload decoded;
    ResolvedSolanaAccounts resolved;
    decode(f, decoded, resolved);
    f.tables[1].addresses[0] = key(1);
    require(resolve_solana_accounts(decoded, "pipeline-fixture", kSigner, f.tables, resolved) ==
                SolanaResolutionError::account_alias && resolved == ResolvedSolanaAccounts{},
            "pipeline loaded alias to sponsor refused and result cleared");
    f = fixture();
    decoded.instructions[5].accounts_offset = decoded.transaction_size;
    require(resolve_solana_accounts(decoded, "pipeline-fixture", kSigner, f.tables, resolved) ==
                SolanaResolutionError::decoded_mismatch && resolved == ResolvedSolanaAccounts{},
            "pipeline forged transfer view refused before decoder");
}

}

int main() {
    try {
        six_instruction_topology();
        std::cout << "PASS six_instruction_topology\n";
        message_privileges_reach_decoder();
        std::cout << "PASS message_privileges_reach_decoder\n";
        changed_lookup_cannot_be_hidden_by_offsets();
        std::cout << "PASS changed_lookup_cannot_be_hidden_by_offsets\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
