// Proposed storage shapes only: not production declarations or an accepted ABI.
#include "velocita/v2/result_ledger.hpp"
#include "raft.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

template<std::size_t Bytes> struct Mailbox {
    std::array<std::byte, Bytes> bytes{};
    std::int64_t term{};
    std::uint32_t length{};
};
struct PeerMailboxes {
    Mailbox<32768> append_request;
    std::array<Mailbox<160>, 3> small;
    std::uint64_t replacements{};
    std::uint64_t stale_term_refusals{};
    std::uint8_t fair_cursor{};
};
struct LogDescriptor {
    std::uint64_t index{};
    std::optional<velocita::v2::ResultHandle> result;
    std::uint16_t length{};
    std::uint8_t pool{};
};
int main() {
    std::cout << "proposal_only=1\n"
              << "peer_mailboxes=" << sizeof(PeerMailboxes) << '\n'
              << "log_descriptor=" << sizeof(LogDescriptor) << '\n'
              << "raft_entry=" << sizeof(raft_entry_t) << '\n'
              << "payload_cells=" << 8192ULL * 2048 << '\n'
              << "log_descriptors=" << 8192ULL * sizeof(LogDescriptor) << '\n'
              << "raft_ring_steady=" << 10240ULL * sizeof(raft_entry_t) << '\n'
              << "raft_ring_resize_peak=" << 15360ULL * sizeof(raft_entry_t) << '\n';
}
