#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>

namespace velocita::v2::truemarkets {

using SolanaInstructionKey = std::array<std::uint8_t, 32>;

struct SolanaInstructionAccount {
    SolanaInstructionKey key{};
    bool signer{};
    bool writable{};
    bool operator==(const SolanaInstructionAccount&) const = default;
};

inline constexpr SolanaInstructionKey kSolanaSystemProgram{};
inline constexpr SolanaInstructionKey kSolanaComputeBudgetProgram{
    3,6,70,111,229,33,23,50,255,236,173,186,114,195,155,231,
    188,140,229,187,197,247,18,107,44,67,155,58,64,0,0,0};
inline constexpr SolanaInstructionKey kSolanaAssociatedTokenProgram{
    140,151,37,143,78,36,137,241,187,61,16,41,20,142,13,131,
    11,90,19,153,218,255,16,132,4,142,123,216,219,233,248,89};
inline constexpr SolanaInstructionKey kSolanaClassicTokenProgram{
    6,221,246,225,215,101,161,147,217,203,225,70,206,235,121,172,
    28,180,133,237,95,91,55,145,58,140,245,133,126,255,0,169};

struct ComputeUnitLimit {
    std::uint32_t units{};
    bool operator==(const ComputeUnitLimit&) const = default;
};

struct ComputeUnitPrice {
    std::uint64_t micro_lamports{};
    bool operator==(const ComputeUnitPrice&) const = default;
};

struct CreateAssociatedTokenIdempotent {
    SolanaInstructionAccount funder{}, associated_account{}, wallet{}, mint{};
    SolanaInstructionAccount system_program{}, token_program{};
    bool operator==(const CreateAssociatedTokenIdempotent&) const = default;
};

struct CloseTokenAccount {
    SolanaInstructionAccount account{}, destination{}, authority{};
    bool operator==(const CloseTokenAccount&) const = default;
};

struct SyncNativeToken {
    SolanaInstructionAccount account{};
    bool operator==(const SyncNativeToken&) const = default;
};

struct TransferLamports {
    SolanaInstructionAccount source{}, destination{};
    std::uint64_t lamports{};
    bool operator==(const TransferLamports&) const = default;
};

enum class SolanaInstructionDecodeStatus : std::uint8_t { unsupported, invalid, decoded };
using SolanaInstructionBody = std::variant<std::monostate, ComputeUnitLimit, ComputeUnitPrice,
    CreateAssociatedTokenIdempotent, CloseTokenAccount, TransferLamports, SyncNativeToken>;

struct SolanaInstructionDecodeResult {
    SolanaInstructionDecodeStatus status{SolanaInstructionDecodeStatus::unsupported};
    SolanaInstructionBody body{};
    bool operator==(const SolanaInstructionDecodeResult&) const = default;
};

[[nodiscard]] SolanaInstructionDecodeResult decode_solana_instruction(
    const SolanaInstructionKey& program, std::span<const std::uint8_t> data,
    std::span<const SolanaInstructionAccount> accounts) noexcept;

static_assert(std::is_trivially_copyable_v<SolanaInstructionDecodeResult>);
static_assert(sizeof(SolanaInstructionDecodeResult) <= 224);

}
