// Seeds each voter's election timeout randomness.
#pragma once

namespace velocita::v2 {

[[nodiscard]] bool seed_election_jitter() noexcept;

}
