#pragma once

namespace velocita::v2 {

// willemt/raft randomizes each voter's election timeout with the C library's
// rand(). Unseeded, every node PROCESS draws the same sequence, so after a
// leader dies the survivors time out together, split the vote and repeat in
// lock-step (seen: two survivors of three stuck as candidates for 10 terms).
// Call once per process, before any Raft server exists. False when the OS
// entropy source failed; the caller must not start a voter in that case.
[[nodiscard]] bool seed_election_jitter() noexcept;

}  // namespace velocita::v2
