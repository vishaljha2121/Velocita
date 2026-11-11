// Byte encoding of Raft peer messages between tape voters, including the pre-vote flag.
#pragma once

#include "velocita/peer_wire.hpp"

#include <optional>
#include <span>
#include <vector>

namespace velocita::tape {

[[nodiscard]] std::vector<std::byte> encode_peer(const PeerMessage& message);
[[nodiscard]] std::optional<PeerMessage> decode_peer(std::span<const std::byte> bytes);

}
