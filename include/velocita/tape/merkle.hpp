// RFC 9162 Merkle tree hashing: a bounded frontier for appending, and proofs over full leaf lists.
#pragma once

#include "velocita/tape/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace velocita::tape {

[[nodiscard]] Digest merkle_leaf_hash(std::span<const std::byte> leaf) noexcept;
[[nodiscard]] Digest merkle_node_hash(const Digest& left, const Digest& right) noexcept;
[[nodiscard]] Digest merkle_empty_root() noexcept;

struct MerkleFrontier {
    std::uint64_t size{};
    std::array<Digest, kMerkleFrontier> levels{};

    void append(const Digest& leaf_hash) noexcept;
    [[nodiscard]] Digest root() const noexcept;
};

[[nodiscard]] Digest merkle_root(std::span<const Digest> leaves);
[[nodiscard]] std::vector<Digest> inclusion_proof(std::span<const Digest> leaves, std::uint64_t index);
[[nodiscard]] std::vector<Digest> consistency_proof(std::span<const Digest> leaves, std::uint64_t old_size);
[[nodiscard]] bool verify_inclusion(const Digest& leaf_hash, std::uint64_t index, std::uint64_t tree_size,
                                    std::span<const Digest> proof, const Digest& root) noexcept;
[[nodiscard]] bool verify_consistency(std::uint64_t old_size, std::uint64_t new_size, const Digest& old_root,
                                      const Digest& new_root, std::span<const Digest> proof) noexcept;

}
