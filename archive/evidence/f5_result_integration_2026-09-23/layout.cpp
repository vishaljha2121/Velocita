#include "velocita/v2/replication.hpp"
#include <iostream>
int main() {
    std::cout << "ReplicatedKernel=" << sizeof(velocita::v2::ReplicatedKernel)
              << " ResultLedger=" << sizeof(velocita::v2::ResultLedger)
              << " ResultHandle=" << sizeof(velocita::v2::ResultHandle)
              << " optional_handle=" << sizeof(std::optional<velocita::v2::ResultHandle>)
              << " CommittedResult=" << sizeof(velocita::v2::CommittedResult) << '\n';
}
