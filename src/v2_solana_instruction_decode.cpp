#include "velocita/v2/solana_instruction_decode.hpp"

namespace velocita::v2::truemarkets {
namespace {
using Status = SolanaInstructionDecodeStatus;
using Accounts = std::span<const SolanaInstructionAccount>;

std::uint64_t little(std::span<const std::uint8_t> data) noexcept {
    std::uint64_t result{};
    for (std::size_t i = 0; i < data.size(); ++i)
        result |= static_cast<std::uint64_t>(data[i]) << (8U * i);
    return result;
}

bool consistent(Accounts accounts) noexcept {
    for (std::size_t i = 0; i < accounts.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (accounts[i].key == accounts[j].key && accounts[i] != accounts[j]) return false;
    return true;
}

SolanaInstructionDecodeResult invalid() noexcept { return {Status::invalid, {}}; }
}

SolanaInstructionDecodeResult decode_solana_instruction(
    const SolanaInstructionKey& program, std::span<const std::uint8_t> data,
    std::span<const SolanaInstructionAccount> accounts) noexcept {
    if (program == kSolanaComputeBudgetProgram) {
        if (data.empty()) return invalid();
        if (data[0] == 2) {
            if (data.size() != 5 || !accounts.empty()) return invalid();
            return {Status::decoded, ComputeUnitLimit{static_cast<std::uint32_t>(little(data.subspan(1)))}};
        }
        if (data[0] == 3) {
            if (data.size() != 9 || !accounts.empty()) return invalid();
            return {Status::decoded, ComputeUnitPrice{little(data.subspan(1))}};
        }
        return {};
    }
    if (program == kSolanaAssociatedTokenProgram) {
        if (data.empty()) return invalid();
        if (data[0] != 1) return {};
        if (data.size() != 1 || accounts.size() != 6) return invalid();
        if (accounts[5].key != kSolanaClassicTokenProgram) return {};
        if (accounts[4].key != kSolanaSystemProgram || !consistent(accounts) ||
            !accounts[0].signer || !accounts[0].writable || !accounts[1].writable) return invalid();
        return {Status::decoded, CreateAssociatedTokenIdempotent{
            accounts[0],accounts[1],accounts[2],accounts[3],accounts[4],accounts[5]}};
    }
    if (program == kSolanaClassicTokenProgram) {
        if (data.empty()) return invalid();
        if (data[0] == 17) {
            if (data.size() != 1 || accounts.size() != 1 || !accounts[0].writable) return invalid();
            return {Status::decoded, SyncNativeToken{accounts[0]}};
        }
        if (data[0] != 9) return {};
        if (data.size() != 1 || accounts.size() != 3) return invalid();
        if (!consistent(accounts) || !accounts[0].writable || !accounts[1].writable ||
            !accounts[2].signer) return invalid();
        return {Status::decoded, CloseTokenAccount{accounts[0],accounts[1],accounts[2]}};
    }
    if (program == kSolanaSystemProgram) {
        if (data.size() < 4) return invalid();
        if (little(data.first(4)) != 2) return {};
        if (data.size() != 12 || accounts.size() != 2) return invalid();
        if (!consistent(accounts) || !accounts[0].signer || !accounts[0].writable ||
            !accounts[1].writable) return invalid();
        return {Status::decoded, TransferLamports{accounts[0],accounts[1],little(data.subspan(4))}};
    }
    return {};
}
}
