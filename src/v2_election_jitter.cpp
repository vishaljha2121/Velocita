#include "velocita/v2/election_jitter.hpp"

#include <cstdlib>
#include <sys/random.h>
#include <unistd.h>

namespace velocita::v2 {

bool seed_election_jitter() noexcept {
    unsigned seed = 0;
    if (::getentropy(&seed, sizeof(seed)) != 0) return false;
    std::srand(seed);
    return true;
}

}
