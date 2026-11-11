#include "velocita/tape/merkle.hpp"

#include "velocita/v2/digest.hpp"

namespace velocita::tape {
namespace {

std::uint64_t largest_power_of_two_below(std::uint64_t n) noexcept {
    std::uint64_t k = 1;
    while (k << 1 < n) k <<= 1;
    return k;
}

Digest subtree_root(std::span<const Digest> leaves) {
    if (leaves.empty()) return merkle_empty_root();
    if (leaves.size() == 1) return leaves[0];
    const auto k = largest_power_of_two_below(leaves.size());
    return merkle_node_hash(subtree_root(leaves.first(k)), subtree_root(leaves.subspan(k)));
}

void path(std::span<const Digest> leaves, std::uint64_t m, std::vector<Digest>& out) {
    if (leaves.size() <= 1) return;
    const auto k = largest_power_of_two_below(leaves.size());
    if (m < k) {
        path(leaves.first(k), m, out);
        out.push_back(subtree_root(leaves.subspan(k)));
    } else {
        path(leaves.subspan(k), m - k, out);
        out.push_back(subtree_root(leaves.first(k)));
    }
}

void subproof(std::span<const Digest> leaves, std::uint64_t m, bool complete, std::vector<Digest>& out) {
    const auto n = leaves.size();
    if (m == n) {
        if (!complete) out.push_back(subtree_root(leaves));
        return;
    }
    const auto k = largest_power_of_two_below(n);
    if (m <= k) {
        subproof(leaves.first(k), m, complete, out);
        out.push_back(subtree_root(leaves.subspan(k)));
    } else {
        subproof(leaves.subspan(k), m - k, false, out);
        out.push_back(subtree_root(leaves.first(k)));
    }
}

}

Digest merkle_leaf_hash(std::span<const std::byte> leaf) noexcept {
    v2::Sha256 h;
    const std::byte prefix{0x00};
    h.update({&prefix, 1});
    h.update(leaf);
    return h.finish();
}

Digest merkle_node_hash(const Digest& left, const Digest& right) noexcept {
    v2::Sha256 h;
    const std::byte prefix{0x01};
    h.update({&prefix, 1});
    h.update(left.bytes);
    h.update(right.bytes);
    return h.finish();
}

Digest merkle_empty_root() noexcept {
    return v2::sha256_digest({});
}

void MerkleFrontier::append(const Digest& leaf_hash) noexcept {
    Digest carry = leaf_hash;
    std::size_t level = 0;
    while (level < kMerkleFrontier && (size >> level) & 1u) {
        carry = merkle_node_hash(levels[level], carry);
        levels[level] = Digest{};
        ++level;
    }
    if (level < kMerkleFrontier) levels[level] = carry;
    ++size;
}

Digest MerkleFrontier::root() const noexcept {
    if (size == 0) return merkle_empty_root();
    bool have = false;
    Digest acc;
    for (std::size_t level = 0; level < kMerkleFrontier; ++level) {
        if (!((size >> level) & 1u)) continue;
        acc = have ? merkle_node_hash(levels[level], acc) : levels[level];
        have = true;
    }
    return acc;
}

Digest merkle_root(std::span<const Digest> leaves) { return subtree_root(leaves); }

std::vector<Digest> inclusion_proof(std::span<const Digest> leaves, std::uint64_t index) {
    std::vector<Digest> out;
    if (index < leaves.size()) path(leaves, index, out);
    return out;
}

std::vector<Digest> consistency_proof(std::span<const Digest> leaves, std::uint64_t old_size) {
    std::vector<Digest> out;
    if (old_size > 0 && old_size < leaves.size()) subproof(leaves, old_size, true, out);
    return out;
}

bool verify_inclusion(const Digest& leaf_hash, std::uint64_t index, std::uint64_t tree_size,
                      std::span<const Digest> proof, const Digest& root) noexcept {
    if (index >= tree_size) return false;
    std::uint64_t fn = index, sn = tree_size - 1;
    Digest r = leaf_hash;
    for (const auto& p : proof) {
        if (sn == 0) return false;
        if ((fn & 1u) || fn == sn) {
            r = merkle_node_hash(p, r);
            if (!(fn & 1u)) {
                while (fn != 0 && !(fn & 1u)) {
                    fn >>= 1;
                    sn >>= 1;
                }
            }
        } else {
            r = merkle_node_hash(r, p);
        }
        fn >>= 1;
        sn >>= 1;
    }
    return sn == 0 && r == root;
}

bool verify_consistency(std::uint64_t old_size, std::uint64_t new_size, const Digest& old_root,
                        const Digest& new_root, std::span<const Digest> proof) noexcept {
    if (old_size == 0 || old_size > new_size) return false;
    if (old_size == new_size) return proof.empty() && old_root == new_root;
    std::size_t at = 0;
    std::vector<Digest> path_nodes(proof.begin(), proof.end());
    if ((old_size & (old_size - 1)) == 0) path_nodes.insert(path_nodes.begin(), old_root);
    if (path_nodes.empty()) return false;
    std::uint64_t fn = old_size - 1, sn = new_size - 1;
    while (fn & 1u) {
        fn >>= 1;
        sn >>= 1;
    }
    Digest fr = path_nodes[at], sr = path_nodes[at];
    ++at;
    for (; at < path_nodes.size(); ++at) {
        const auto& c = path_nodes[at];
        if (sn == 0) return false;
        if ((fn & 1u) || fn == sn) {
            fr = merkle_node_hash(c, fr);
            sr = merkle_node_hash(c, sr);
            if (!(fn & 1u)) {
                while (fn != 0 && !(fn & 1u)) {
                    fn >>= 1;
                    sn >>= 1;
                }
            }
        } else {
            sr = merkle_node_hash(sr, c);
        }
        fn >>= 1;
        sn >>= 1;
    }
    return sn == 0 && fr == old_root && sr == new_root;
}

}
