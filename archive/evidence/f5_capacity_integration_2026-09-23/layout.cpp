#include "velocita/v2/replication.hpp"
#include <cstddef>
#include <iostream>

int main() {
    using namespace velocita::v2;
    std::cout << "ReplicatedKernel.inline=" << sizeof(ReplicatedKernel) << '\n'
              << "OutboundMailboxes.owner=" << sizeof(OutboundMailboxes) << '\n'
              << "RetainedLog.owner=" << sizeof(RetainedLog) << '\n'
              << "RetainedLogDescriptor=" << sizeof(RetainedLogDescriptor) << '\n'
              << "descriptor.index=" << offsetof(RetainedLogDescriptor, index) << '\n'
              << "descriptor.length=" << offsetof(RetainedLogDescriptor, length) << '\n'
              << "descriptor.pool=" << offsetof(RetainedLogDescriptor, pool) << '\n'
              << "descriptor.result=" << offsetof(RetainedLogDescriptor, result) << '\n'
              << "payload_backing=" << kRetainedLogEntries * kRetainedLogCellBytes << '\n'
              << "descriptor_backing=" << kRetainedLogEntries * sizeof(RetainedLogDescriptor) << '\n'
              << "ResultLedger.owner=" << sizeof(ResultLedger) << '\n'
              << "raft_entry_t=" << sizeof(raft_entry_t) << '\n';
}
