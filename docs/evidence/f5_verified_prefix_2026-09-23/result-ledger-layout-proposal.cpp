#include "velocita/v2/replication.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace proposal {
using namespace velocita::v2;

enum class SlotState : std::uint8_t { free, reserved_uncommitted, committed_ready };
enum class SlotPool : std::uint8_t { ordinary, control };

struct LedgerSlot {
    CommittedResult result{};
    Digest256 canonical_command_digest{};
    std::uint64_t generation{};
    std::uint16_t next{};
    SlotState state{SlotState::free};
    SlotPool pool{SlotPool::ordinary};
};

struct LedgerHandle {
    std::uint64_t generation{};
    std::uint16_t slot{};
};

struct LedgerHeader {
    std::uint64_t next_generation{1};
    std::uint16_t ordinary_free_head{};
    std::uint16_t control_free_head{};
    std::uint16_t ready_head{};
    std::uint16_t ready_tail{};
    std::uint16_t ordinary_free_count{};
    std::uint16_t control_free_count{};
    std::uint16_t ready_count{};
};

struct ResultLedger {
    LedgerHeader header{};
    std::array<LedgerSlot, 1024> slots{};
};
} // namespace proposal

#define FIELD(T, F) std::cout << #T "." #F "=" << offsetof(T, F) << '\n'
int main() {
    using namespace proposal;
    std::cout << "CommandIdentity=" << sizeof(CommandIdentity) << '\n';
    std::cout << "ApplyResult=" << sizeof(ApplyResult) << '\n';
    std::cout << "CommittedResult=" << sizeof(CommittedResult) << '\n';
    FIELD(CommittedResult, identity); FIELD(CommittedResult, result);
    FIELD(CommittedResult, term); FIELD(CommittedResult, index);
    std::cout << "LedgerSlot=" << sizeof(LedgerSlot) << " align=" << alignof(LedgerSlot) << '\n';
    FIELD(LedgerSlot, result); FIELD(LedgerSlot, canonical_command_digest);
    FIELD(LedgerSlot, generation); FIELD(LedgerSlot, next);
    FIELD(LedgerSlot, state); FIELD(LedgerSlot, pool);
    std::cout << "LedgerHandle=" << sizeof(LedgerHandle) << " align=" << alignof(LedgerHandle) << '\n';
    FIELD(LedgerHandle, generation); FIELD(LedgerHandle, slot);
    std::cout << "LedgerHeader=" << sizeof(LedgerHeader) << " align=" << alignof(LedgerHeader) << '\n';
    FIELD(LedgerHeader, next_generation); FIELD(LedgerHeader, ordinary_free_head);
    FIELD(LedgerHeader, control_free_head); FIELD(LedgerHeader, ready_head);
    FIELD(LedgerHeader, ready_tail); FIELD(LedgerHeader, ordinary_free_count);
    FIELD(LedgerHeader, control_free_count); FIELD(LedgerHeader, ready_count);
    std::cout << "slots1024=" << sizeof(std::array<LedgerSlot, 1024>) << '\n';
    std::cout << "ordinary960=" << sizeof(std::array<LedgerSlot, 960>) << '\n';
    std::cout << "control64=" << sizeof(std::array<LedgerSlot, 64>) << '\n';
    std::cout << "ResultLedger=" << sizeof(ResultLedger) << " align=" << alignof(ResultLedger) << '\n';
    FIELD(ResultLedger, header); FIELD(ResultLedger, slots);
}
